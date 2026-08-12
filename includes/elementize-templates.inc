<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Templates {
    private const CACHE_TTL = 300;
    private const MAX_ITEMS = 500;
    private const MAX_CONTENT_FIELDS = 800;
    private const PIXFORT_SOURCE_ID = 'pixfort';
    private const PIXFORT_LIBRARY_OPTION = 'custom_remote_info_library';
    private const MAX_REMOTE_BYTES = 33554432; // 32 MiB. Request one extra byte to detect truncation cleanly.

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 27 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/templates', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'search' ],
            'permission_callback' => [ Elementize_Core::class, 'can_access' ],
            'args' => [
                'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'type' => [ 'type' => 'string', 'default' => 'all', 'sanitize_callback' => 'sanitize_key' ],
                'provider' => [ 'type' => 'string', 'default' => 'all', 'sanitize_callback' => 'sanitize_key' ],
                'page' => [ 'type' => 'integer', 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'per_page' => [ 'type' => 'integer', 'default' => 30, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
            ],
        ] );

        register_rest_route( Elementize_Core::NS, '/templates/(?P<source_id>[A-Za-z0-9._-]+)/(?P<template_id>[A-Za-z0-9._:-]+)', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'inspect' ],
            'permission_callback' => [ Elementize_Core::class, 'can_access' ],
            'args' => [
                'source_id' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_key' ],
                'template_id' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_text_field' ],
            ],
        ] );
    }

    public static function search( WP_REST_Request $request ) {
        $q = self::lower( trim( (string) $request->get_param( 'q' ) ) );
        $type = sanitize_key( (string) $request->get_param( 'type' ) );
        $provider = sanitize_key( (string) $request->get_param( 'provider' ) );
        if ( ! in_array( $type, [ 'all', 'page', 'section', 'container' ], true ) ) {
            return new WP_Error( 'elementize_template_type_invalid', 'type must be all, page, section, or container.', [ 'status' => 400 ] );
        }
        if ( ! in_array( $provider, [ 'all', 'pixfort', 'local' ], true ) ) {
            return new WP_Error( 'elementize_template_provider_invalid', 'provider must be all, pixfort, or local.', [ 'status' => 400 ] );
        }

        $items = self::index();
        if ( is_wp_error( $items ) ) return $items;

        $filtered = [];
        foreach ( $items as $item ) {
            if ( 'all' !== $provider && $provider !== (string) $item['provider'] ) continue;
            if ( 'all' !== $type && $type !== strtolower( (string) ( $item['type'] ?? '' ) ) ) continue;
            if ( '' !== $q ) {
                $haystack = self::lower( implode( ' ', [
                    (string) $item['title'],
                    (string) $item['type'],
                    (string) $item['provider'],
                    (string) ( $item['subtype'] ?? '' ),
                    implode( ' ', (array) $item['keywords'] ),
                ] ) );
                if ( false === strpos( $haystack, $q ) ) continue;
            }
            $filtered[] = $item;
        }

        usort( $filtered, static function( array $a, array $b ): int {
            if ( $a['provider'] !== $b['provider'] ) return 'pixfort' === $a['provider'] ? -1 : 1;
            return strcasecmp( (string) $a['title'], (string) $b['title'] );
        } );

        $page = max( 1, absint( $request->get_param( 'page' ) ) );
        $per_page = min( 100, max( 1, absint( $request->get_param( 'per_page' ) ) ) );
        $total = count( $filtered );
        $sources = self::source_descriptors();

        return new WP_REST_Response( [
            'q' => $q,
            'type' => $type,
            'provider' => $provider,
            'page' => $page,
            'per_page' => $per_page,
            'total' => $total,
            'total_pages' => $total > 0 ? (int) ceil( $total / $per_page ) : 0,
            'pixfort_source_detected' => (bool) array_filter( $sources, static fn( array $s ): bool => 'pixfort' === $s['provider'] ),
            'sources' => $sources,
            'items' => array_slice( $filtered, ( $page - 1 ) * $per_page, $per_page ),
        ], 200 );
    }

    public static function inspect( WP_REST_Request $request ) {
        $source_id = sanitize_key( (string) $request['source_id'] );
        $template_id = sanitize_text_field( (string) $request['template_id'] );
        $loaded = self::load_template( $source_id, $template_id );
        if ( is_wp_error( $loaded ) ) return $loaded;

        $elements = $loaded['elements'];
        $summary = self::structure_summary( $elements );

        return new WP_REST_Response( [
            'source_id' => $source_id,
            'template_id' => $template_id,
            'provider' => $loaded['provider'],
            'title' => $loaded['title'],
            'type' => $loaded['type'],
            'insertable' => empty( $summary['embedded_template_references'] ),
            'warnings' => self::warnings( $summary ),
            'structure' => $summary,
            'editable_content_fields' => array_slice( Elementize_Content::discover_items( $elements ), 0, self::MAX_CONTENT_FIELDS ),
            'design_controls' => Elementize_Design::controls_for_template( $elements, self::MAX_CONTENT_FIELDS ),
            'creative_guidelines' => Elementize_Design::guidelines(),
        ], 200 );
    }

    public static function load_template( string $source_id, string $template_id ) {
        if ( self::PIXFORT_SOURCE_ID === $source_id ) return self::load_pixfort_template( $template_id );

        $sources = self::sources();
        if ( is_wp_error( $sources ) ) return $sources;
        $source = $sources[ $source_id ] ?? null;
        if ( ! is_object( $source ) ) return new WP_Error( 'elementize_template_source_not_found', 'The requested Elementor template source is not available.', [ 'status' => 404 ] );

        $provider = self::source_provider( $source_id, $source );
        if ( ! in_array( $provider, [ 'pixfort', 'local' ], true ) ) {
            return new WP_Error( 'elementize_template_source_forbidden', 'Elementize only imports detected Pixfort or local Elementor template sources.', [ 'status' => 400 ] );
        }
        if ( ! method_exists( $source, 'get_data' ) ) {
            return new WP_Error( 'elementize_template_source_unsupported', 'The template source cannot provide Elementor template data.', [ 'status' => 503 ] );
        }

        $data = null;
        $attempts = [
            [ 'template_id' => $template_id, 'display' => true ],
            [ 'template_id' => $template_id ],
            [ 'id' => $template_id, 'display' => true ],
        ];
        foreach ( $attempts as $args ) {
            try { $candidate = $source->get_data( $args ); } catch ( Throwable $e ) { $candidate = null; }
            if ( is_wp_error( $candidate ) || ! is_array( $candidate ) ) continue;
            $elements = self::extract_elements( $candidate );
            if ( is_array( $elements ) ) { $data = $candidate; break; }
        }

        if ( ! is_array( $data ) ) return new WP_Error( 'elementize_template_data_unavailable', 'The selected template source did not return usable Elementor content.', [ 'status' => 503 ] );
        $elements = self::extract_elements( $data );
        if ( ! is_array( $elements ) || ! $elements ) return new WP_Error( 'elementize_template_content_empty', 'The selected template contains no insertable Elementor elements.', [ 'status' => 400 ] );

        $summary = self::structure_summary( $elements );
        if ( ! empty( $summary['embedded_template_references'] ) ) {
            return new WP_Error( 'elementize_template_embedded_reference', 'This template contains an embedded/global template reference. Elementize will not insert shared template dependencies into a managed local section.', [ 'status' => 400, 'references' => $summary['embedded_template_references'] ] );
        }

        return [
            'source_id' => $source_id,
            'template_id' => $template_id,
            'provider' => $provider,
            'title' => self::template_title_from_data( $data, $template_id ),
            'type' => self::template_type_from_data( $data ),
            'elements' => $elements,
        ];
    }

    public static function structure_summary( array $elements ): array {
        $summary = [
            'root_count' => count( $elements ),
            'element_count' => 0,
            'widget_count' => 0,
            'max_depth' => 0,
            'widgets' => [],
            'dynamic_values' => 0,
            'global_style_values' => 0,
            'embedded_template_references' => [],
        ];
        self::summarize_elements( $elements, $summary, 0 );
        arsort( $summary['widgets'] );
        $summary['widgets'] = array_slice( $summary['widgets'], 0, 40, true );
        $summary['embedded_template_references'] = array_values( array_unique( $summary['embedded_template_references'] ) );
        return $summary;
    }

    private static function index() {
        $fingerprint = defined( 'ELEMENTIZE_VERSION' ) ? ELEMENTIZE_VERSION : '0';
        $key = 'elementize_template_index_' . md5( $fingerprint . '|' . ( defined( 'ELEMENTOR_VERSION' ) ? ELEMENTOR_VERSION : '' ) );
        $cached = get_transient( $key );
        if ( is_array( $cached ) ) return $cached;

        $items = [];
        $seen = [];

        foreach ( self::pixfort_items() as $raw ) {
            if ( ! is_array( $raw ) ) continue;
            $item = self::normalize_item( self::PIXFORT_SOURCE_ID, 'pixfort', $raw );
            if ( ! $item ) continue;
            $dedupe = $item['source_id'] . ':' . $item['template_id'];
            if ( isset( $seen[ $dedupe ] ) ) continue;
            $seen[ $dedupe ] = true;
            $items[] = $item;
        }

        $sources = self::sources();
        if ( is_wp_error( $sources ) ) return $sources;
        foreach ( $sources as $source_id => $source ) {
            $provider = self::source_provider( (string) $source_id, $source );
            if ( ! in_array( $provider, [ 'pixfort', 'local' ], true ) || ! method_exists( $source, 'get_items' ) ) continue;
            try { $source_items = $source->get_items(); } catch ( Throwable $e ) { $source_items = []; }
            if ( is_wp_error( $source_items ) || ! is_array( $source_items ) ) continue;
            foreach ( array_slice( $source_items, 0, self::MAX_ITEMS ) as $raw ) {
                if ( ! is_array( $raw ) ) continue;
                $item = self::normalize_item( (string) $source_id, $provider, $raw );
                if ( ! $item ) continue;
                $dedupe = $item['source_id'] . ':' . $item['template_id'];
                if ( isset( $seen[ $dedupe ] ) ) continue;
                $seen[ $dedupe ] = true;
                $items[] = $item;
            }
        }

        set_transient( $key, $items, self::CACHE_TTL );
        return $items;
    }

    private static function sources() {
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->templates_manager ) ) {
            return new WP_Error( 'elementize_elementor_required', 'Elementor template manager is unavailable.', [ 'status' => 503 ] );
        }

        $manager = \Elementor\Plugin::$instance->templates_manager;
        $sources = [];
        try {
            if ( method_exists( $manager, 'get_source' ) ) {
                $local = $manager->get_source( 'local' );
                if ( is_object( $local ) ) $sources['local'] = $local;
            }
        } catch ( Throwable $ignored ) {}

        foreach ( [ '_registered_sources', 'registered_sources' ] as $property_name ) {
            try {
                $reflection = new ReflectionObject( $manager );
                while ( $reflection ) {
                    if ( $reflection->hasProperty( $property_name ) ) {
                        $property = $reflection->getProperty( $property_name );
                        $property->setAccessible( true );
                        $registered = $property->getValue( $manager );
                        if ( is_array( $registered ) ) {
                            foreach ( $registered as $id => $source ) {
                                if ( is_object( $source ) ) $sources[ sanitize_key( (string) $id ) ] = $source;
                            }
                        }
                        break 2;
                    }
                    $reflection = $reflection->getParentClass();
                }
            } catch ( Throwable $ignored ) {}
        }
        return $sources;
    }

    private static function source_descriptors(): array {
        $out = [];
        $pixfort = self::pixfort_descriptor();
        if ( $pixfort ) $out[] = $pixfort;

        $sources = self::sources();
        if ( is_wp_error( $sources ) ) return $out;
        foreach ( $sources as $id => $source ) {
            $provider = self::source_provider( (string) $id, $source );
            if ( ! in_array( $provider, [ 'pixfort', 'local' ], true ) ) continue;
            if ( self::PIXFORT_SOURCE_ID === (string) $id ) continue;
            $out[] = [ 'source_id' => (string) $id, 'provider' => $provider, 'class' => get_class( $source ) ];
        }
        return $out;
    }

    private static function source_provider( string $id, object $source ): string {
        $haystack = strtolower( $id . ' ' . get_class( $source ) );
        if ( false !== strpos( $haystack, 'pixfort' ) ) return 'pixfort';
        if ( 'local' === $id || false !== strpos( $haystack, 'source_local' ) ) return 'local';
        return 'other';
    }

    private static function pixfort_descriptor(): ?array {
        $class_ready = self::ensure_pixfort_source_class();
        $library = self::pixfort_library();
        $has_library = ! empty( $library['templates'] ) && is_array( $library['templates'] );
        if ( ! $class_ready && ! function_exists( 'pixfort_elementor_library_data' ) && ! $has_library ) return null;
        return [
            'source_id' => self::PIXFORT_SOURCE_ID,
            'provider' => 'pixfort',
            'class' => $class_ready ? '\\Elementor\\TemplateLibrary\\Source_Pixfort' : 'pixfort_elementor_library_data',
            'adapter' => $class_ready ? 'pixfort_native_source' : 'pixfort_remote_fallback',
        ];
    }

    private static function ensure_pixfort_source_class(): bool {
        $class = '\\Elementor\\TemplateLibrary\\Source_Pixfort';
        if ( class_exists( $class ) ) return true;
        if ( ! class_exists( '\\Elementor\\TemplateLibrary\\Source_Base' ) ) return false;

        $candidates = [];
        if ( defined( 'PIX_CORE_PLUGIN_DIR' ) ) $candidates[] = rtrim( (string) PIX_CORE_PLUGIN_DIR, '/\\' ) . '/includes/import/elementor/source.php';
        if ( defined( 'PIXFORT_PLUGIN_DIR' ) ) $candidates[] = rtrim( (string) PIXFORT_PLUGIN_DIR, '/\\' ) . '/includes/import/elementor/source.php';
        if ( defined( 'WP_PLUGIN_DIR' ) ) $candidates[] = rtrim( (string) WP_PLUGIN_DIR, '/\\' ) . '/pixfort-core/includes/import/elementor/source.php';

        $plugins_root = defined( 'WP_PLUGIN_DIR' ) ? realpath( (string) WP_PLUGIN_DIR ) : false;
        $plugins_root_norm = is_string( $plugins_root ) ? strtolower( rtrim( wp_normalize_path( $plugins_root ), '/' ) . '/' ) : '';

        foreach ( array_unique( $candidates ) as $candidate ) {
            $real = realpath( $candidate );
            if ( ! is_string( $real ) || ! is_file( $real ) ) continue;
            $real_norm = strtolower( wp_normalize_path( $real ) );
            if ( '' !== $plugins_root_norm && ! str_starts_with( $real_norm, $plugins_root_norm ) ) continue;
            try { require_once $real; } catch ( Throwable $ignored ) {}
            if ( class_exists( $class ) ) return true;
        }
        return false;
    }

    private static function pixfort_library(): array {
        $library = [];

        if ( function_exists( 'pixfort_elementor_library_data' ) ) {
            try { $payload = pixfort_elementor_library_data(); } catch ( Throwable $e ) { $payload = null; }
            if ( is_array( $payload ) ) {
                if ( isset( $payload['library'] ) && is_array( $payload['library'] ) ) $library = $payload['library'];
                elseif ( isset( $payload['templates'] ) && is_array( $payload['templates'] ) ) $library = $payload;
            }
        }

        if ( empty( $library['templates'] ) && self::ensure_pixfort_source_class() && method_exists( '\\Elementor\\TemplateLibrary\\Source_Pixfort', 'get_library_data' ) ) {
            try { $candidate = \Elementor\TemplateLibrary\Source_Pixfort::get_library_data(); } catch ( Throwable $e ) { $candidate = null; }
            if ( is_array( $candidate ) && isset( $candidate['templates'] ) && is_array( $candidate['templates'] ) ) $library = $candidate;
        }

        if ( empty( $library['templates'] ) ) {
            $candidate = get_option( self::PIXFORT_LIBRARY_OPTION, [] );
            if ( is_array( $candidate ) && isset( $candidate['templates'] ) && is_array( $candidate['templates'] ) ) $library = $candidate;
        }

        return is_array( $library ) ? $library : [];
    }

    private static function pixfort_items(): array {
        if ( self::ensure_pixfort_source_class() ) {
            try {
                $source = new \Elementor\TemplateLibrary\Source_Pixfort();
                $items = $source->get_items();
            } catch ( Throwable $e ) {
                $items = [];
            }
            if ( is_array( $items ) && $items ) return array_slice( $items, 0, self::MAX_ITEMS );
        }

        $library = self::pixfort_library();
        return isset( $library['templates'] ) && is_array( $library['templates'] )
            ? array_slice( $library['templates'], 0, self::MAX_ITEMS )
            : [];
    }

    private static function load_pixfort_template( string $template_id ) {
        $record = self::pixfort_template_record( $template_id );
        if ( ! is_array( $record ) ) {
            return new WP_Error( 'elementize_pixfort_template_not_found', 'The requested Pixfort template is not present in the installed Pixfort library catalogue.', [ 'status' => 404 ] );
        }

        $payload = self::load_pixfort_native_payload( $template_id );
        if ( null === $payload ) $payload = self::fetch_pixfort_template_payload( $record );
        if ( is_wp_error( $payload ) ) return $payload;

        if ( ! empty( $payload['pixIsNested'] ) || ! empty( $payload['pixNestedTemplates'] ) ) {
            return new WP_Error(
                'elementize_pixfort_nested_template_unsupported',
                'This Pixfort template depends on nested Pixfort templates. Elementize will not import those hidden dependencies outside the guarded page transaction.',
                [ 'status' => 400 ]
            );
        }

        $elements = self::extract_elements( $payload );
        if ( ! is_array( $elements ) || ! $elements ) {
            return new WP_Error( 'elementize_template_content_empty', 'The selected Pixfort template contains no insertable Elementor elements.', [ 'status' => 400 ] );
        }

        $summary = self::structure_summary( $elements );
        if ( ! empty( $summary['embedded_template_references'] ) ) {
            return new WP_Error( 'elementize_template_embedded_reference', 'This Pixfort template contains an embedded/global template reference. Elementize will not insert shared template dependencies into a managed local section.', [ 'status' => 400, 'references' => $summary['embedded_template_references'] ] );
        }

        return [
            'source_id' => self::PIXFORT_SOURCE_ID,
            'template_id' => $template_id,
            'provider' => 'pixfort',
            'title' => wp_strip_all_tags( (string) ( $record['title'] ?? ( 'Template ' . $template_id ) ) ),
            'type' => self::normalized_type( (string) ( $record['type'] ?? '' ), (string) ( $record['subtype'] ?? '' ), (string) ( $record['title'] ?? '' ), true ),
            'elements' => $elements,
        ];
    }

    private static function load_pixfort_native_payload( string $template_id ) {
        if ( ! self::ensure_pixfort_source_class() ) return null;

        $state = class_exists( 'Elementize_Capabilities' ) ? Elementize_Capabilities::state() : [];
        $scope_page_id = ! empty( $state['creative_enabled'] ) ? absint( $state['scope_page_id'] ?? 0 ) : 0;
        $args = [ 'template_id' => $template_id ];
        if ( $scope_page_id > 0 ) $args['editor_post_id'] = $scope_page_id;

        try {
            $source = new \Elementor\TemplateLibrary\Source_Pixfort();
            $candidate = $source->get_data( $args );
        } catch ( Throwable $e ) {
            return new WP_Error(
                'elementize_pixfort_native_template_failed',
                'Pixfort native template loading failed: ' . $e->getMessage(),
                [ 'status' => 503, 'adapter' => 'pixfort_native_source', 'scope_page_id' => $scope_page_id ]
            );
        }

        if ( is_wp_error( $candidate ) ) {
            return new WP_Error(
                'elementize_pixfort_native_template_failed',
                'Pixfort native template loading failed: ' . $candidate->get_error_message(),
                [ 'status' => 503, 'adapter' => 'pixfort_native_source', 'scope_page_id' => $scope_page_id ]
            );
        }
        if ( ! is_array( $candidate ) ) {
            return new WP_Error(
                'elementize_pixfort_native_template_invalid',
                'Pixfort native template loading returned an invalid response.',
                [ 'status' => 503, 'adapter' => 'pixfort_native_source', 'scope_page_id' => $scope_page_id ]
            );
        }

        $elements = self::extract_elements( $candidate );
        if ( ! is_array( $elements ) || ! $elements ) {
            return new WP_Error(
                'elementize_pixfort_native_template_empty',
                'Pixfort native template loading returned no Elementor content.',
                [ 'status' => 503, 'adapter' => 'pixfort_native_source', 'scope_page_id' => $scope_page_id ]
            );
        }
        return $candidate;
    }

    private static function pixfort_template_record( string $template_id ): ?array {
        $library = self::pixfort_library();
        foreach ( (array) ( $library['templates'] ?? [] ) as $record ) {
            if ( ! is_array( $record ) ) continue;
            $id = $record['id'] ?? $record['template_id'] ?? null;
            if ( is_scalar( $id ) && (string) $id === $template_id ) return $record;
        }

        foreach ( self::pixfort_items() as $item ) {
            if ( ! is_array( $item ) ) continue;
            $id = $item['template_id'] ?? $item['id'] ?? null;
            if ( is_scalar( $id ) && (string) $id === $template_id ) return $item;
        }
        return null;
    }

    private static function fetch_pixfort_template_payload( array $record ) {
        $file = trim( (string) ( $record['file'] ?? '' ) );
        if ( '' === $file ) return new WP_Error( 'elementize_pixfort_template_file_missing', 'The Pixfort catalogue entry does not contain a template file.', [ 'status' => 503 ] );
        if ( str_contains( $file, '..' ) || str_contains( $file, '\\' ) || ! preg_match( '#^[A-Za-z0-9._/-]+$#', $file ) ) {
            return new WP_Error( 'elementize_pixfort_template_file_invalid', 'The Pixfort catalogue returned an unsafe template file path.', [ 'status' => 503 ] );
        }

        $url = self::pixfort_import_base_url() . ltrim( $file, '/' );
        $parts = wp_parse_url( $url );
        if ( ! is_array( $parts ) || 'https' !== strtolower( (string) ( $parts['scheme'] ?? '' ) ) || 'import.pixfort.com' !== strtolower( (string) ( $parts['host'] ?? '' ) ) ) {
            return new WP_Error( 'elementize_pixfort_template_url_invalid', 'The resolved Pixfort template URL is outside the trusted Pixfort import host.', [ 'status' => 503 ] );
        }

        // Remote fallback is used only when Pixfort's native source class cannot be loaded.
        $args = [
            'timeout' => 40,
            'redirection' => 3,
            'sslverify' => true,
            'reject_unsafe_urls' => true,
            'limit_response_size' => self::MAX_REMOTE_BYTES + 1,
        ];
        $filtered = apply_filters( 'pixfort_el_remote_get_args', $args );
        if ( is_array( $filtered ) ) $args = array_merge( $args, $filtered );
        $args['timeout'] = min( 40, max( 5, absint( $args['timeout'] ?? 40 ) ) );
        $args['redirection'] = min( 3, max( 0, absint( $args['redirection'] ?? 3 ) ) );
        $args['sslverify'] = true;
        $args['reject_unsafe_urls'] = true;
        $args['limit_response_size'] = self::MAX_REMOTE_BYTES + 1;

        $response = wp_remote_get( $url, $args );
        if ( is_wp_error( $response ) ) {
            return new WP_Error( 'elementize_pixfort_template_fetch_failed', 'Pixfort template download failed: ' . $response->get_error_message(), [ 'status' => 503 ] );
        }

        $status = (int) wp_remote_retrieve_response_code( $response );
        if ( 200 !== $status ) {
            return new WP_Error( 'elementize_pixfort_template_fetch_status', 'Pixfort template download returned HTTP ' . $status . '.', [ 'status' => 503 ] );
        }

        $body = wp_remote_retrieve_body( $response );
        if ( ! is_string( $body ) || '' === $body ) return new WP_Error( 'elementize_pixfort_template_empty_response', 'Pixfort returned an empty template response.', [ 'status' => 503 ] );

        $bytes = strlen( $body );
        if ( $bytes > self::MAX_REMOTE_BYTES ) {
            return new WP_Error( 'elementize_pixfort_template_too_large', 'The Pixfort template response exceeds Elementize limits.', [ 'status' => 413, 'response_bytes' => $bytes ] );
        }

        $payload = self::decode_pixfort_json( $body );
        if ( is_wp_error( $payload ) ) {
            $data = $payload->get_error_data();
            $data = is_array( $data ) ? $data : [];
            $data['status'] = 503;
            $data['response_bytes'] = $bytes;
            $data['content_type'] = sanitize_text_field( (string) wp_remote_retrieve_header( $response, 'content-type' ) );
            $data['content_encoding'] = sanitize_text_field( (string) wp_remote_retrieve_header( $response, 'content-encoding' ) );
            $data['template_file'] = sanitize_text_field( $file );
            return new WP_Error( 'elementize_pixfort_template_invalid_json', 'Pixfort returned invalid template JSON.', $data );
        }

        if ( isset( $payload['error'] ) ) {
            return new WP_Error( 'elementize_pixfort_template_remote_error', sanitize_text_field( (string) $payload['error'] ), [ 'status' => 503 ] );
        }
        if ( empty( $payload['data'] ) && empty( $payload['content'] ) && empty( $payload['elements'] ) ) {
            return new WP_Error( 'elementize_pixfort_template_data_missing', 'Pixfort returned a template response without Elementor content.', [ 'status' => 503 ] );
        }
        return $payload;
    }

    private static function decode_pixfort_json( string $body ) {
        // Some JSON exporters prepend a UTF-8 BOM. PHP json_decode treats that as a syntax error.
        if ( str_starts_with( $body, "\xEF\xBB\xBF" ) ) $body = substr( $body, 3 );
        $body = trim( $body );
        if ( '' === $body ) return new WP_Error( 'elementize_pixfort_template_empty_json', 'Pixfort returned an empty JSON body.', [ 'json_error' => 'Empty body' ] );

        // Defensive fallback for a server/proxy that returns raw gzip bytes without a usable Content-Encoding header.
        if ( strlen( $body ) >= 2 && "\x1f\x8b" === substr( $body, 0, 2 ) && function_exists( 'gzdecode' ) ) {
            $decoded = @gzdecode( $body );
            if ( is_string( $decoded ) && '' !== $decoded && strlen( $decoded ) <= self::MAX_REMOTE_BYTES ) $body = $decoded;
        }

        $payload = json_decode( $body, true, 512, JSON_BIGINT_AS_STRING );
        if ( is_array( $payload ) ) return $payload;

        $kind = preg_match( '/^\s*</', $body ) ? 'html_or_xml' : 'non_json';
        return new WP_Error( 'elementize_pixfort_template_json_decode_failed', 'Pixfort template JSON could not be decoded.', [
            'json_error' => function_exists( 'json_last_error_msg' ) ? json_last_error_msg() : (string) json_last_error(),
            'response_kind' => $kind,
        ] );
    }

    private static function pixfort_import_base_url(): string {
        $slug = defined( 'PIXFORT_THEME_SLUG' ) ? sanitize_key( (string) PIXFORT_THEME_SLUG ) : '';
        if ( 'acquire' === $slug ) return 'https://import.pixfort.com/acquire/elementor/';
        if ( 'conseil' === $slug ) return 'https://import.pixfort.com/conseil/elementor/';
        if ( 'pixdocs' === $slug ) return 'https://import.pixfort.com/pixdocs/elementor/';
        if ( 'pixfort' === $slug ) return 'https://import.pixfort.com/pixfort-theme/elementor/';
        return 'https://import.pixfort.com/essentials/elementor/';
    }

    private static function normalize_item( string $source_id, string $provider, array $raw ): ?array {
        $id = $raw['template_id'] ?? $raw['id'] ?? null;
        if ( ! is_scalar( $id ) || '' === trim( (string) $id ) ) return null;

        $title = (string) ( $raw['title'] ?? $raw['name'] ?? ( 'Template ' . $id ) );
        $subtype = (string) ( $raw['subtype'] ?? $raw['subType'] ?? $raw['sub_type'] ?? '' );
        $type = self::normalized_type( (string) ( $raw['type'] ?? $raw['template_type'] ?? '' ), $subtype, $title, 'pixfort' === $provider );
        $keywords = [];
        foreach ( [ 'tags', 'keywords', 'categories', 'subtype', 'subType', 'sub_type' ] as $key ) {
            foreach ( self::keyword_values( $raw[ $key ] ?? null ) as $part ) $keywords[] = $part;
        }
        $thumbnail = $raw['thumbnail'] ?? $raw['thumbnail_url'] ?? $raw['preview'] ?? null;

        return [
            'source_id' => $source_id,
            'template_id' => (string) $id,
            'provider' => $provider,
            'pixfort_native' => 'pixfort' === $provider,
            'title' => wp_strip_all_tags( $title ),
            'type' => $type,
            'subtype' => sanitize_text_field( $subtype ),
            'keywords' => array_values( array_unique( array_filter( array_map( 'sanitize_text_field', $keywords ) ) ) ),
            'thumbnail' => is_string( $thumbnail ) ? esc_url_raw( $thumbnail ) : null,
        ];
    }

    private static function keyword_values( $value ): array {
        if ( is_array( $value ) ) {
            $out = [];
            foreach ( $value as $part ) if ( is_scalar( $part ) && '' !== trim( (string) $part ) ) $out[] = (string) $part;
            return $out;
        }
        if ( ! is_string( $value ) || '' === trim( $value ) ) return [];

        $trimmed = trim( $value );
        if ( str_starts_with( $trimmed, '[' ) || str_starts_with( $trimmed, '{' ) ) {
            $decoded = json_decode( $trimmed, true );
            if ( is_array( $decoded ) ) {
                $out = [];
                array_walk_recursive( $decoded, static function( $part ) use ( &$out ): void {
                    if ( is_scalar( $part ) && '' !== trim( (string) $part ) ) $out[] = (string) $part;
                } );
                if ( $out ) return $out;
            }
        }
        return [ $trimmed ];
    }

    private static function normalized_type( string $type, string $subtype, string $title, bool $pixfort = false ): string {
        $type = strtolower( trim( $type ) );
        $subtype = strtolower( trim( $subtype ) );
        if ( 'page' === $type || false !== strpos( $subtype, 'page' ) ) return 'page';
        if ( in_array( $type, [ 'section', 'block' ], true ) || false !== strpos( $subtype, 'section' ) || false !== strpos( $subtype, 'block' ) ) return 'section';
        if ( 'container' === $type ) return 'container';
        if ( false !== stripos( $title, 'page' ) ) return 'page';
        if ( false !== stripos( $title, 'section' ) ) return 'section';
        return $pixfort ? 'section' : 'container';
    }

    private static function extract_elements( array $data ): ?array {
        foreach ( [ 'content', 'elements' ] as $key ) {
            if ( isset( $data[ $key ] ) && is_array( $data[ $key ] ) ) return array_values( $data[ $key ] );
        }
        if ( isset( $data['data'] ) && is_array( $data['data'] ) ) return self::extract_elements( $data['data'] );
        return null;
    }

    private static function template_title_from_data( array $data, string $fallback ): string {
        foreach ( [ 'title', 'name' ] as $key ) {
            if ( is_string( $data[ $key ] ?? null ) && '' !== trim( $data[ $key ] ) ) return wp_strip_all_tags( $data[ $key ] );
        }
        return 'Template ' . $fallback;
    }

    private static function template_type_from_data( array $data ): string {
        $type = strtolower( (string) ( $data['type'] ?? $data['template_type'] ?? 'container' ) );
        if ( 'block' === $type ) return 'section';
        return in_array( $type, [ 'page', 'section', 'container' ], true ) ? $type : 'container';
    }

    private static function summarize_elements( array $elements, array &$summary, int $depth ): void {
        if ( $depth > 100 ) return;
        $summary['max_depth'] = max( (int) $summary['max_depth'], $depth );
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $summary['element_count']++;
            $widget = strtolower( (string) ( $element['widgetType'] ?? '' ) );
            if ( '' !== $widget ) {
                $summary['widget_count']++;
                $summary['widgets'][ $widget ] = ( $summary['widgets'][ $widget ] ?? 0 ) + 1;
                if ( false !== strpos( $widget, 'template' ) || false !== strpos( $widget, 'global' ) ) {
                    $summary['embedded_template_references'][] = (string) ( $element['id'] ?? $widget );
                }
            }
            self::scan_values( $element['settings'] ?? [], $summary, [] );
            if ( is_array( $element['elements'] ?? null ) ) self::summarize_elements( $element['elements'], $summary, $depth + 1 );
        }
    }

    private static function scan_values( $value, array &$summary, array $path ): void {
        if ( count( $path ) > 20 ) return;
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $child ) {
                if ( '__globals__' === (string) $key ) $summary['global_style_values']++;
                self::scan_values( $child, $summary, array_merge( $path, [ $key ] ) );
            }
            return;
        }
        if ( is_string( $value ) ) {
            $trimmed = ltrim( $value );
            if ( str_starts_with( $trimmed, '[elementor-tag' ) || false !== strpos( $value, '{{' ) ) $summary['dynamic_values']++;
        }
    }

    private static function warnings( array $summary ): array {
        $warnings = [];
        if ( ! empty( $summary['dynamic_values'] ) ) $warnings[] = 'Template contains dynamic Elementor values. They will be preserved but remain read-only.';
        if ( ! empty( $summary['global_style_values'] ) ) $warnings[] = 'Template references Elementor global styles. Elementize will not rewrite those global references.';
        if ( ! empty( $summary['embedded_template_references'] ) ) $warnings[] = 'Template contains embedded/global template widgets and is not eligible for managed local insertion.';
        if ( (int) $summary['max_depth'] > 60 ) $warnings[] = 'Template is unusually deeply nested; choose a simpler template when possible.';
        return $warnings;
    }

    private static function lower( string $value ): string {
        return function_exists( 'mb_strtolower' ) ? mb_strtolower( $value, 'UTF-8' ) : strtolower( $value );
    }
}

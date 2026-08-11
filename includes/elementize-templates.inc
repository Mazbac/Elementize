<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Templates {
    private const CACHE_TTL = 300;
    private const MAX_ITEMS = 500;
    private const MAX_CONTENT_FIELDS = 800;

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
        if ( ! in_array( $type, [ 'all', 'page', 'section', 'container' ], true ) ) return new WP_Error( 'elementize_template_type_invalid', 'type must be all, page, section, or container.', [ 'status' => 400 ] );
        if ( ! in_array( $provider, [ 'all', 'pixfort', 'local' ], true ) ) return new WP_Error( 'elementize_template_provider_invalid', 'provider must be all, pixfort, or local.', [ 'status' => 400 ] );

        $items = self::index();
        if ( is_wp_error( $items ) ) return $items;
        $filtered = [];
        foreach ( $items as $item ) {
            if ( 'all' !== $provider && $provider !== (string) $item['provider'] ) continue;
            $item_type = strtolower( (string) ( $item['type'] ?? '' ) );
            if ( 'all' !== $type && $type !== $item_type ) continue;
            if ( '' !== $q ) {
                $haystack = self::lower( implode( ' ', [ (string) $item['title'], (string) $item['type'], (string) $item['provider'], implode( ' ', (array) $item['keywords'] ) ] ) );
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
        $content = array_slice( Elementize_Content::discover_items( $elements ), 0, self::MAX_CONTENT_FIELDS );
        $design = Elementize_Design::controls_for_template( $elements, self::MAX_CONTENT_FIELDS );
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
            'editable_content_fields' => $content,
            'design_controls' => $design,
            'creative_guidelines' => Elementize_Design::guidelines(),
        ], 200 );
    }

    public static function load_template( string $source_id, string $template_id ) {
        $sources = self::sources();
        if ( is_wp_error( $sources ) ) return $sources;
        $source = $sources[ $source_id ] ?? null;
        if ( ! is_object( $source ) ) return new WP_Error( 'elementize_template_source_not_found', 'The requested Elementor template source is not available.', [ 'status' => 404 ] );
        $provider = self::source_provider( $source_id, $source );
        if ( ! in_array( $provider, [ 'pixfort', 'local' ], true ) ) return new WP_Error( 'elementize_template_source_forbidden', 'Elementize only imports detected Pixfort or local Elementor template sources.', [ 'status' => 400 ] );
        if ( ! method_exists( $source, 'get_data' ) ) return new WP_Error( 'elementize_template_source_unsupported', 'The template source cannot provide Elementor template data.', [ 'status' => 503 ] );

        $data = null;
        $attempts = [
            [ 'template_id' => $template_id, 'display' => true ],
            [ 'template_id' => $template_id ],
            [ 'id' => $template_id, 'display' => true ],
        ];
        foreach ( $attempts as $args ) {
            try { $candidate = $source->get_data( $args ); }
            catch ( Throwable $e ) { $candidate = null; }
            if ( is_wp_error( $candidate ) ) continue;
            if ( is_array( $candidate ) ) {
                $elements = self::extract_elements( $candidate );
                if ( is_array( $elements ) ) { $data = $candidate; break; }
            }
        }
        if ( ! is_array( $data ) ) return new WP_Error( 'elementize_template_data_unavailable', 'The selected template source did not return usable Elementor content.', [ 'status' => 503 ] );
        $elements = self::extract_elements( $data );
        if ( ! is_array( $elements ) || ! $elements ) return new WP_Error( 'elementize_template_content_empty', 'The selected template contains no insertable Elementor elements.', [ 'status' => 400 ] );
        $summary = self::structure_summary( $elements );
        if ( ! empty( $summary['embedded_template_references'] ) ) return new WP_Error( 'elementize_template_embedded_reference', 'This template contains an embedded/global template reference. Elementize will not insert shared template dependencies into a managed local section.', [ 'status' => 400, 'references' => $summary['embedded_template_references'] ] );
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
        $sources = self::sources();
        if ( is_wp_error( $sources ) ) return $sources;
        $items = [];
        foreach ( $sources as $source_id => $source ) {
            $provider = self::source_provider( $source_id, $source );
            if ( ! in_array( $provider, [ 'pixfort', 'local' ], true ) || ! method_exists( $source, 'get_items' ) ) continue;
            try { $source_items = $source->get_items(); }
            catch ( Throwable $e ) { $source_items = []; }
            if ( is_wp_error( $source_items ) || ! is_array( $source_items ) ) continue;
            foreach ( array_slice( $source_items, 0, self::MAX_ITEMS ) as $raw ) {
                if ( ! is_array( $raw ) ) continue;
                $item = self::normalize_item( (string) $source_id, $provider, $raw );
                if ( $item ) $items[] = $item;
            }
        }
        set_transient( $key, $items, self::CACHE_TTL );
        return $items;
    }

    private static function sources() {
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->templates_manager ) ) return new WP_Error( 'elementize_elementor_required', 'Elementor template manager is unavailable.', [ 'status' => 503 ] );
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
                        if ( is_array( $registered ) ) foreach ( $registered as $id => $source ) if ( is_object( $source ) ) $sources[ sanitize_key( (string) $id ) ] = $source;
                        break 2;
                    }
                    $reflection = $reflection->getParentClass();
                }
            } catch ( Throwable $ignored ) {}
        }
        return $sources;
    }

    private static function source_descriptors(): array {
        $sources = self::sources();
        if ( is_wp_error( $sources ) ) return [];
        $out = [];
        foreach ( $sources as $id => $source ) {
            $provider = self::source_provider( (string) $id, $source );
            if ( ! in_array( $provider, [ 'pixfort', 'local' ], true ) ) continue;
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

    private static function normalize_item( string $source_id, string $provider, array $raw ): ?array {
        $id = $raw['template_id'] ?? $raw['id'] ?? null;
        if ( ! is_scalar( $id ) || '' === trim( (string) $id ) ) return null;
        $title = (string) ( $raw['title'] ?? $raw['name'] ?? ( 'Template ' . $id ) );
        $type = strtolower( (string) ( $raw['type'] ?? $raw['template_type'] ?? '' ) );
        if ( ! in_array( $type, [ 'page', 'section', 'container' ], true ) ) $type = false !== stripos( $title, 'section' ) ? 'section' : 'container';
        $keywords = [];
        foreach ( [ 'tags', 'keywords', 'categories', 'subType', 'sub_type' ] as $key ) {
            $value = $raw[ $key ] ?? null;
            if ( is_string( $value ) ) $keywords[] = $value;
            if ( is_array( $value ) ) foreach ( $value as $part ) if ( is_scalar( $part ) ) $keywords[] = (string) $part;
        }
        $thumbnail = $raw['thumbnail'] ?? $raw['thumbnail_url'] ?? $raw['preview'] ?? null;
        return [
            'source_id' => $source_id,
            'template_id' => (string) $id,
            'provider' => $provider,
            'pixfort_native' => 'pixfort' === $provider,
            'title' => wp_strip_all_tags( $title ),
            'type' => $type,
            'keywords' => array_values( array_unique( array_filter( array_map( 'sanitize_text_field', $keywords ) ) ) ),
            'thumbnail' => is_string( $thumbnail ) ? esc_url_raw( $thumbnail ) : null,
        ];
    }

    private static function extract_elements( array $data ): ?array {
        foreach ( [ 'content', 'elements' ] as $key ) if ( isset( $data[ $key ] ) && is_array( $data[ $key ] ) ) return array_values( $data[ $key ] );
        if ( isset( $data['data'] ) && is_array( $data['data'] ) ) return self::extract_elements( $data['data'] );
        return null;
    }

    private static function template_title_from_data( array $data, string $fallback ): string {
        foreach ( [ 'title', 'name' ] as $key ) if ( is_string( $data[ $key ] ?? null ) && '' !== trim( $data[ $key ] ) ) return wp_strip_all_tags( $data[ $key ] );
        return 'Template ' . $fallback;
    }

    private static function template_type_from_data( array $data ): string {
        $type = strtolower( (string) ( $data['type'] ?? $data['template_type'] ?? 'container' ) );
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
                if ( false !== strpos( $widget, 'template' ) || false !== strpos( $widget, 'global' ) ) $summary['embedded_template_references'][] = (string) ( $element['id'] ?? $widget );
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
        if ( is_string( $value ) && str_starts_with( ltrim( $value ), '[elementor-tag' ) ) $summary['dynamic_values']++;
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

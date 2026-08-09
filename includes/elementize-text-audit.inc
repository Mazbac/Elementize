<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Text_Audit {
    private const VERSION = '0.5.5';
    private const NS = 'elementize/v1';
    private const MAX_MATCHES = 50;
    private const MAX_DEPTH = 3;
    private const MAX_ERRORS = 20;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 65 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 85, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/text-audit', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'audit' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'q' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_text_field' ],
            ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'manage_options' ) || ! current_user_can( 'edit_post', $id ) ) {
            return new WP_Error( 'elementize_text_audit_forbidden', 'Text-source audit requires an administrator who can edit the page.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function audit( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $q = trim( (string) $request->get_param( 'q' ) );
        if ( mb_strlen( $q ) < 2 ) return new WP_Error( 'elementize_text_audit_query_short', 'q must contain at least 2 characters.', [ 'status' => 400 ] );

        $elements = self::elements_for_post( $id );
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor page data could not be read.', [ 'status' => 500 ] );

        $page_matches = [];
        self::scan_strings( $elements, $q, [], $page_matches, self::MAX_MATCHES );

        $named_template_refs = [];
        self::collect_named_template_refs( $elements, $named_template_refs );

        $all_template_refs = [];
        self::collect_any_elementor_library_refs( $elements, $all_template_refs );

        $template_matches = [];
        $visited = [];
        foreach ( array_keys( $all_template_refs ) as $template_id ) {
            self::scan_template_chain( absint( $template_id ), $q, 1, $visited, $template_matches );
            if ( count( $template_matches ) >= self::MAX_MATCHES ) break;
        }

        $effective_matches = [];
        $effective_errors = [];
        self::scan_effective_widget_settings( $elements, $q, $effective_matches, $effective_errors );

        return new WP_REST_Response( [
            'page_id' => $id,
            'query' => $q,
            'page_match_count' => count( $page_matches ),
            'page_matches' => $page_matches,
            'named_template_ref_count' => count( $named_template_refs ),
            'named_template_ref_ids' => array_map( 'absint', array_keys( $named_template_refs ) ),
            'all_elementor_library_ref_count' => count( $all_template_refs ),
            'all_elementor_library_ref_ids' => array_map( 'absint', array_keys( $all_template_refs ) ),
            'elementor_library_ref_sources' => self::ref_sources( $all_template_refs ),
            'template_match_count' => count( $template_matches ),
            'template_matches' => $template_matches,
            'effective_widget_match_count' => count( $effective_matches ),
            'effective_widget_matches' => $effective_matches,
            'effective_widget_error_count' => count( $effective_errors ),
            'effective_widget_errors' => $effective_errors,
            'diagnosis' => self::diagnosis( $page_matches, $template_matches, $all_template_refs, $effective_matches ),
            'read_only' => true,
        ], 200 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['text_source_audit'] = true;
        $data['text_source_audit_read_only'] = true;
        $data['text_source_audit_broad_template_refs'] = true;
        $data['text_source_audit_effective_widget_settings'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function elements_for_post( int $post_id ): ?array {
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $document = \Elementor\Plugin::$instance->documents->get( $post_id );
                if ( $document ) {
                    $elements = $document->get_elements_data();
                    if ( is_array( $elements ) ) return $elements;
                }
            } catch ( Throwable $exception ) {}
        }
        $raw = get_post_meta( $post_id, '_elementor_data', true );
        if ( is_array( $raw ) ) return $raw;
        if ( is_string( $raw ) && '' !== $raw ) {
            $decoded = json_decode( $raw, true );
            if ( is_array( $decoded ) ) return $decoded;
        }
        return null;
    }

    private static function scan_strings( $value, string $q, array $path, array &$matches, int $limit ): void {
        if ( count( $matches ) >= $limit ) return;
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $next ) {
                self::scan_strings( $next, $q, array_merge( $path, [ $key ] ), $matches, $limit );
                if ( count( $matches ) >= $limit ) return;
            }
            return;
        }
        if ( ! is_string( $value ) || false === mb_stripos( $value, $q ) ) return;
        $matches[] = [
            'path' => array_values( $path ),
            'setting_key' => self::last_string_key( $path ),
            'value' => self::trim_value( $value ),
        ];
    }

    private static function scan_effective_widget_settings( array $elements, string $q, array &$matches, array &$errors ): void {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return;

        foreach ( $elements as $element_data ) {
            if ( ! is_array( $element_data ) ) continue;

            $element_id = isset( $element_data['id'] ) && is_scalar( $element_data['id'] ) ? (string) $element_data['id'] : '';
            $el_type = isset( $element_data['elType'] ) && is_scalar( $element_data['elType'] ) ? (string) $element_data['elType'] : '';
            $widget_type = isset( $element_data['widgetType'] ) && is_scalar( $element_data['widgetType'] ) ? (string) $element_data['widgetType'] : '';

            if ( 'widget' === $el_type && count( $matches ) < self::MAX_MATCHES ) {
                try {
                    $instance = \Elementor\Plugin::$instance->elements_manager->create_element_instance( $element_data );
                    if ( $instance && method_exists( $instance, 'get_settings_for_display' ) ) {
                        $effective = $instance->get_settings_for_display();
                        if ( is_array( $effective ) ) {
                            $raw_settings = isset( $element_data['settings'] ) && is_array( $element_data['settings'] ) ? $element_data['settings'] : [];
                            self::scan_effective_strings(
                                $effective,
                                $raw_settings,
                                $q,
                                [],
                                $matches,
                                $element_id,
                                $widget_type
                            );
                        }
                    } elseif ( count( $errors ) < self::MAX_ERRORS ) {
                        $errors[] = [
                            'element_id' => $element_id,
                            'widget_type' => $widget_type,
                            'reason' => 'element_instance_unavailable',
                        ];
                    }
                } catch ( Throwable $exception ) {
                    if ( count( $errors ) < self::MAX_ERRORS ) {
                        $errors[] = [
                            'element_id' => $element_id,
                            'widget_type' => $widget_type,
                            'reason' => 'effective_settings_exception',
                            'exception_class' => get_class( $exception ),
                        ];
                    }
                }
            }

            if ( ! empty( $element_data['elements'] ) && is_array( $element_data['elements'] ) ) {
                self::scan_effective_widget_settings( $element_data['elements'], $q, $matches, $errors );
            }

            if ( count( $matches ) >= self::MAX_MATCHES ) return;
        }
    }

    private static function scan_effective_strings(
        $value,
        array $raw_settings,
        string $q,
        array $path,
        array &$matches,
        string $element_id,
        string $widget_type
    ): void {
        if ( count( $matches ) >= self::MAX_MATCHES ) return;

        if ( is_array( $value ) ) {
            foreach ( $value as $key => $next ) {
                self::scan_effective_strings(
                    $next,
                    $raw_settings,
                    $q,
                    array_merge( $path, [ $key ] ),
                    $matches,
                    $element_id,
                    $widget_type
                );
                if ( count( $matches ) >= self::MAX_MATCHES ) return;
            }
            return;
        }

        if ( ! is_string( $value ) || false === mb_stripos( $value, $q ) ) return;

        $raw_found = false;
        $raw_value = self::path_value( $raw_settings, $path, $raw_found );
        $raw_matches_query = $raw_found && is_string( $raw_value ) && false !== mb_stripos( $raw_value, $q );
        $source = $raw_matches_query
            ? 'stored_setting'
            : ( $raw_found ? 'derived_from_stored_setting' : 'control_default_or_runtime_value' );

        $matches[] = [
            'element_id' => $element_id,
            'widget_type' => $widget_type,
            'setting_path' => array_values( $path ),
            'setting_key' => self::last_string_key( $path ),
            'effective_value' => self::trim_value( $value ),
            'raw_present' => $raw_found,
            'raw_matches_query' => $raw_matches_query,
            'source' => $source,
        ];
    }

    private static function path_value( array $root, array $path, bool &$found ) {
        $cursor = $root;
        foreach ( $path as $segment ) {
            if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) {
                $found = false;
                return null;
            }
            $cursor = $cursor[ $segment ];
        }
        $found = true;
        return $cursor;
    }

    private static function collect_named_template_refs( $value, array &$refs, array $path = [] ): void {
        if ( ! is_array( $value ) ) return;
        foreach ( $value as $key => $next ) {
            $next_path = array_merge( $path, [ $key ] );
            if ( is_string( $key ) && str_contains( strtolower( $key ), 'template' ) ) {
                self::add_template_ids_from_value( $next, $refs, $next_path, 'template_key' );
            }
            if ( is_array( $next ) ) self::collect_named_template_refs( $next, $refs, $next_path );
        }
    }

    private static function collect_any_elementor_library_refs( $value, array &$refs, array $path = [] ): void {
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $next ) {
                self::collect_any_elementor_library_refs( $next, $refs, array_merge( $path, [ $key ] ) );
            }
            return;
        }

        if ( is_numeric( $value ) ) {
            self::add_candidate_id( absint( $value ), $refs, $path, 'numeric_scalar' );
            return;
        }

        if ( ! is_string( $value ) || '' === trim( $value ) ) return;
        if ( preg_match_all( '/(?<!\\d)([1-9][0-9]{1,9})(?!\\d)/', $value, $matches ) ) {
            foreach ( array_unique( $matches[1] ) as $candidate ) {
                self::add_candidate_id( absint( $candidate ), $refs, $path, 'embedded_numeric_string' );
            }
        }
    }

    private static function add_template_ids_from_value( $value, array &$refs, array $path, string $source ): void {
        if ( is_numeric( $value ) ) {
            self::add_candidate_id( absint( $value ), $refs, $path, $source );
            return;
        }
        if ( ! is_array( $value ) ) return;
        foreach ( $value as $key => $next ) {
            self::add_template_ids_from_value( $next, $refs, array_merge( $path, [ $key ] ), $source );
        }
    }

    private static function add_candidate_id( int $id, array &$refs, array $path, string $source ): void {
        if ( $id < 1 || 'elementor_library' !== get_post_type( $id ) ) return;
        if ( ! isset( $refs[ $id ] ) ) $refs[ $id ] = [];
        $signature = wp_json_encode( [ $source, array_values( $path ) ] );
        foreach ( $refs[ $id ] as $existing ) {
            if ( wp_json_encode( [ $existing['source'] ?? '', $existing['path'] ?? [] ] ) === $signature ) return;
        }
        $refs[ $id ][] = [ 'source' => $source, 'path' => array_values( $path ) ];
    }

    private static function ref_sources( array $refs ): array {
        $out = [];
        foreach ( $refs as $id => $sources ) {
            $out[] = [
                'template_id' => absint( $id ),
                'template_title' => get_the_title( absint( $id ) ),
                'sources' => array_values( is_array( $sources ) ? $sources : [] ),
            ];
        }
        return $out;
    }

    private static function scan_template_chain( int $template_id, string $q, int $depth, array &$visited, array &$matches ): void {
        if ( $template_id < 1 || $depth > self::MAX_DEPTH || isset( $visited[ $template_id ] ) || count( $matches ) >= self::MAX_MATCHES ) return;
        if ( 'elementor_library' !== get_post_type( $template_id ) ) return;
        $visited[ $template_id ] = true;

        $elements = self::elements_for_post( $template_id );
        if ( ! is_array( $elements ) ) return;
        $local = [];
        self::scan_strings( $elements, $q, [], $local, self::MAX_MATCHES - count( $matches ) );
        foreach ( $local as $match ) {
            $match['template_id'] = $template_id;
            $match['template_title'] = get_the_title( $template_id );
            $match['depth'] = $depth;
            $matches[] = $match;
            if ( count( $matches ) >= self::MAX_MATCHES ) return;
        }

        $refs = [];
        self::collect_any_elementor_library_refs( $elements, $refs );
        foreach ( array_keys( $refs ) as $child_id ) {
            self::scan_template_chain( absint( $child_id ), $q, $depth + 1, $visited, $matches );
            if ( count( $matches ) >= self::MAX_MATCHES ) return;
        }
    }

    private static function diagnosis( array $page_matches, array $template_matches, array $template_refs, array $effective_matches ): string {
        if ( $page_matches ) return 'query_found_in_page_elementor_data';
        if ( $template_matches ) return 'query_found_in_referenced_elementor_template';
        if ( $effective_matches ) return 'query_found_in_effective_widget_settings_only';
        if ( $template_refs ) return 'query_not_found_but_elementor_library_refs_detected';
        return 'query_not_found_in_page_or_effective_widget_settings_and_no_elementor_library_refs_detected';
    }

    private static function trim_value( string $value ): string {
        return mb_strlen( $value ) > 1200 ? mb_substr( $value, 0, 1200 ) . '…' : $value;
    }

    private static function last_string_key( array $path ): string {
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) if ( is_string( $path[ $i ] ) ) return (string) $path[ $i ];
        return '';
    }
}

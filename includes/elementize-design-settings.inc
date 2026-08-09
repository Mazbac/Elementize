<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Settings {
    private const VERSION = '0.9.0';
    private const NS = 'elementize/v1';
    private const DEFAULT_LIMIT = 100;
    private const MAX_LIMIT = 300;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 28 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 225, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/design-settings', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'read' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'category' => [ 'type' => 'string', 'required' => false, 'sanitize_callback' => 'sanitize_key' ],
                'element_id' => [ 'type' => 'string', 'required' => false, 'sanitize_callback' => 'sanitize_text_field' ],
                'top_level_id' => [ 'type' => 'string', 'required' => false, 'sanitize_callback' => 'sanitize_text_field' ],
                'responsive_scope' => [ 'type' => 'string', 'required' => false, 'sanitize_callback' => 'sanitize_key' ],
                'offset' => [ 'type' => 'integer', 'required' => false, 'default' => 0, 'sanitize_callback' => 'absint' ],
                'limit' => [ 'type' => 'integer', 'required' => false, 'default' => self::DEFAULT_LIMIT, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        $id = absint( $request['id'] );
        $page = get_post( $id );
        if ( ! $page || 'page' !== $page->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot inspect this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function read( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $document = self::document( $page_id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );

        $controls = [];
        foreach ( $elements as $index => $node ) {
            if ( ! is_array( $node ) ) continue;
            $top_id = (string) ( $node['id'] ?? 'top-' . $index );
            self::walk( [ $node ], $top_id, $controls );
        }

        $category_counts = [];
        $source_counts = [ 'explicit' => 0, 'global' => 0, 'dynamic' => 0 ];
        $responsive_counts = [];
        $writer_candidates = 0;
        foreach ( $controls as $control ) {
            $category = (string) ( $control['category'] ?? 'other' );
            $source = (string) ( $control['source'] ?? 'explicit' );
            $scope = (string) ( $control['responsive_scope'] ?? 'base' );
            $category_counts[ $category ] = ( $category_counts[ $category ] ?? 0 ) + 1;
            if ( isset( $source_counts[ $source ] ) ) $source_counts[ $source ]++;
            $responsive_counts[ $scope ] = ( $responsive_counts[ $scope ] ?? 0 ) + 1;
            if ( ! empty( $control['writer_candidate'] ) ) $writer_candidates++;
        }
        ksort( $category_counts );
        ksort( $responsive_counts );

        $category = sanitize_key( (string) $request->get_param( 'category' ) );
        $element_id = trim( (string) $request->get_param( 'element_id' ) );
        $top_level_id = trim( (string) $request->get_param( 'top_level_id' ) );
        $responsive_scope = sanitize_key( (string) $request->get_param( 'responsive_scope' ) );

        $filtered = array_values( array_filter( $controls, static function( $control ) use ( $category, $element_id, $top_level_id, $responsive_scope ) {
            if ( '' !== $category && $category !== (string) ( $control['category'] ?? '' ) ) return false;
            if ( '' !== $element_id && $element_id !== (string) ( $control['element_id'] ?? '' ) ) return false;
            if ( '' !== $top_level_id && $top_level_id !== (string) ( $control['top_level_id'] ?? '' ) ) return false;
            if ( '' !== $responsive_scope && $responsive_scope !== (string) ( $control['responsive_scope'] ?? '' ) ) return false;
            return true;
        } ) );

        $offset = max( 0, absint( $request->get_param( 'offset' ) ) );
        $limit = absint( $request->get_param( 'limit' ) );
        if ( $limit < 1 ) $limit = self::DEFAULT_LIMIT;
        $limit = min( self::MAX_LIMIT, $limit );
        $page = get_post( $page_id );

        return new WP_REST_Response( [
            'design_settings_version' => self::VERSION,
            'read_only' => true,
            'write_endpoint_available' => false,
            'page' => [
                'id' => $page_id,
                'title' => get_the_title( $page_id ),
                'status' => (string) $page->post_status,
                'modified_gmt' => (string) $page->post_modified_gmt,
            ],
            'content_hash' => self::hash( $elements ),
            'discovery_policy' => [
                'purpose' => 'Observe real Elementor/Pixfort design controls before any broader guarded design writer is introduced.',
                'writer_candidate_is_advisory' => true,
                'global_and_dynamic_controls_read_only' => true,
                'note' => 'A writer_candidate means the explicit control shape looks suitable for a future typed writer. It is not writable through this endpoint.',
            ],
            'summary' => [
                'total_discovered_controls' => count( $controls ),
                'filtered_control_count' => count( $filtered ),
                'returned_control_count' => count( array_slice( $filtered, $offset, $limit ) ),
                'writer_candidate_count' => $writer_candidates,
                'category_counts' => $category_counts,
                'source_counts' => $source_counts,
                'responsive_scope_counts' => $responsive_counts,
            ],
            'filters' => [
                'category' => $category ?: null,
                'element_id' => $element_id ?: null,
                'top_level_id' => $top_level_id ?: null,
                'responsive_scope' => $responsive_scope ?: null,
                'offset' => $offset,
                'limit' => $limit,
            ],
            'controls' => array_slice( $filtered, $offset, $limit ),
            'next_offset' => ( $offset + $limit < count( $filtered ) ) ? $offset + $limit : null,
        ], 200 );
    }

    private static function walk( array $nodes, string $top_id, array &$controls ): void {
        foreach ( $nodes as $node ) {
            if ( ! is_array( $node ) ) continue;
            $settings = is_array( $node['settings'] ?? null ) ? $node['settings'] : [];
            $element_id = (string) ( $node['id'] ?? '' );
            $element_type = strtolower( (string) ( $node['elType'] ?? '' ) );
            $widget_type = strtolower( (string) ( $node['widgetType'] ?? '' ) );
            $globals = is_array( $settings['__globals__'] ?? null ) ? $settings['__globals__'] : [];
            $dynamic = is_array( $settings['__dynamic__'] ?? null ) ? $settings['__dynamic__'] : [];
            self::scan_settings( $settings, [], $top_id, $element_id, $element_type, $widget_type, $globals, $dynamic, $controls );
            $children = $node['elements'] ?? [];
            if ( is_array( $children ) && $children ) self::walk( $children, $top_id, $controls );
        }
    }

    private static function scan_settings( array $settings, array $prefix, string $top_id, string $element_id, string $element_type, string $widget_type, array $globals, array $dynamic, array &$controls ): void {
        foreach ( $settings as $key => $value ) {
            $key_s = (string) $key;
            if ( in_array( $key_s, [ '__globals__', '__dynamic__' ], true ) ) continue;
            $path = array_merge( $prefix, [ $key_s ] );
            $path_text = strtolower( implode( '.', array_map( 'strval', $path ) ) );
            $category = self::category( $path_text, $key_s, $value );

            if ( null !== $category && self::safe_value_shape( $value ) ) {
                $source = self::source( $path, $globals, $dynamic );
                $scope = self::responsive_scope( $key_s, $path_text );
                $candidate = 'explicit' === $source['type'] && self::writer_candidate( $category, $value, $path_text );
                $controls[] = [
                    'top_level_id' => $top_id,
                    'element_id' => $element_id,
                    'element_type' => $element_type,
                    'widget_type' => $widget_type ?: null,
                    'category' => $category,
                    'control_key' => $key_s,
                    'base_control_key' => self::base_control_key( $key_s ),
                    'setting_path' => $path,
                    'responsive_scope' => $scope,
                    'source' => $source['type'],
                    'source_reference' => $source['reference'],
                    'value_shape' => self::value_shape( $value ),
                    'unit' => self::unit( $value ),
                    'value' => self::clean_value( $value ),
                    'writer_candidate' => $candidate,
                    'writable_now' => false,
                    'writer_candidate_reason' => $candidate ? 'Explicit supported-looking design control observed; eligible for future guarded-writer mapping after runtime validation.' : self::non_candidate_reason( $source['type'], $category, $value, $path_text ),
                    'control_fingerprint' => substr( hash( 'sha256', $element_id . '|' . wp_json_encode( $path ) . '|' . wp_json_encode( $value ) ), 0, 20 ),
                ];
                if ( is_array( $value ) && self::atomic_array_control( $category, $value ) ) continue;
            }

            if ( is_array( $value ) ) self::scan_settings( $value, $path, $top_id, $element_id, $element_type, $widget_type, $globals, $dynamic, $controls );
        }
    }

    private static function category( string $path, string $key, $value ): ?string {
        $k = strtolower( $key );
        if ( preg_match( '/(?:^|[._-])(motion|animation|entrance|transition|transform|mask|css|class|id)(?:$|[._-])/', $path ) ) return null;
        if ( preg_match( '/(?:image|media|video|icon|url|link|href|content|description|title|text)(?:$|[._-])/', $k ) && ! preg_match( '/text[_-]?(?:color|shadow)|title[_-]?(?:color|shadow)/', $k ) ) return null;
        if ( preg_match( '/font[_-]?(size|weight|family)|line[_-]?height|letter[_-]?spacing|typography|text[_-]?transform|font[_-]?style|text[_-]?decoration/', $path ) ) return 'typography';
        if ( preg_match( '/margin|padding|(?:^|[._-])gap(?:$|[._-])|spacing|space[_-]?(?:between|before|after)|column[_-]?gap|row[_-]?gap/', $path ) ) return 'spacing';
        if ( preg_match( '/border[_-]?radius|radius/', $path ) ) return 'border_radius';
        if ( preg_match( '/box[_-]?shadow|text[_-]?shadow|shadow/', $path ) ) return 'shadow';
        if ( preg_match( '/border(?:[_-]?(?:width|style|color|type))?/', $path ) ) return 'border';
        if ( preg_match( '/background|gradient|overlay/', $path ) && ! preg_match( '/image|video/', $path ) ) return 'background';
        if ( preg_match( '/(?:^|[._-])(?:color|fill)(?:$|[._-])|text[_-]?color|heading[_-]?color|title[_-]?color/', $path ) ) return 'color';
        if ( preg_match( '/align|justify|text[_-]?align|vertical[_-]?align|horizontal[_-]?align/', $path ) ) return 'alignment';
        if ( preg_match( '/(?:^|[._-])(?:width|max[_-]?width|min[_-]?width|height|max[_-]?height|min[_-]?height|content[_-]?width|flex[_-]?basis|columns?)(?:$|[._-])/', $path ) ) return 'sizing';
        return null;
    }

    private static function source( array $path, array $globals, array $dynamic ): array {
        $keys = [];
        if ( $path ) {
            $keys[] = (string) $path[0];
            $keys[] = (string) end( $path );
            $keys[] = implode( '.', array_map( 'strval', $path ) );
        }
        foreach ( array_values( array_unique( $keys ) ) as $key ) {
            if ( array_key_exists( $key, $dynamic ) ) return [ 'type' => 'dynamic', 'reference' => self::reference_value( $dynamic[ $key ] ) ];
            if ( array_key_exists( $key, $globals ) ) return [ 'type' => 'global', 'reference' => self::reference_value( $globals[ $key ] ) ];
        }
        return [ 'type' => 'explicit', 'reference' => null ];
    }

    private static function reference_value( $value ) {
        if ( is_scalar( $value ) ) return substr( (string) $value, 0, 200 );
        return null;
    }

    private static function responsive_scope( string $key, string $path ): string {
        $s = strtolower( $key . ' ' . $path );
        foreach ( [ 'mobile_extra', 'tablet_extra', 'widescreen', 'laptop', 'tablet', 'mobile' ] as $scope ) {
            if ( preg_match( '/(?:_|-|\\.)' . preg_quote( $scope, '/' ) . '(?:$|_|-|\\.)/', $s ) ) return $scope;
        }
        return 'base';
    }

    private static function base_control_key( string $key ): string {
        return (string) preg_replace( '/(?:_|-)(mobile_extra|tablet_extra|widescreen|laptop|tablet|mobile)$/i', '', $key );
    }

    private static function writer_candidate( string $category, $value, string $path ): bool {
        if ( preg_match( '/gradient|overlay|background[_-]?type/', $path ) ) return false;
        if ( ! in_array( $category, [ 'typography', 'spacing', 'alignment', 'sizing', 'border_radius', 'border', 'shadow', 'background', 'color' ], true ) ) return false;
        if ( is_scalar( $value ) ) return is_string( $value ) || is_numeric( $value );
        if ( ! is_array( $value ) ) return false;
        return self::is_dimension_array( $value ) || self::is_box_array( $value ) || self::is_shadow_array( $value );
    }

    private static function non_candidate_reason( string $source, string $category, $value, string $path ): string {
        if ( 'global' === $source ) return 'Global style reference; keep read-only unless a future global-style workflow is explicitly designed.';
        if ( 'dynamic' === $source ) return 'Dynamic value; read-only.';
        if ( preg_match( '/gradient|overlay|background[_-]?type/', $path ) ) return 'Complex/composited visual control; discovery-only until a typed safe writer exists.';
        if ( is_array( $value ) && ! self::is_dimension_array( $value ) && ! self::is_box_array( $value ) && ! self::is_shadow_array( $value ) ) return 'Complex control shape; discovery-only.';
        return 'Observed design control is not yet mapped to a guarded writer type.';
    }

    private static function safe_value_shape( $value ): bool {
        if ( is_scalar( $value ) ) return strlen( (string) $value ) <= 500;
        if ( ! is_array( $value ) ) return false;
        if ( count( $value ) > 20 ) return false;
        return self::array_depth( $value ) <= 3;
    }

    private static function atomic_array_control( string $category, array $value ): bool {
        return in_array( $category, [ 'typography', 'spacing', 'sizing', 'border_radius', 'border', 'shadow' ], true ) && ( self::is_dimension_array( $value ) || self::is_box_array( $value ) || self::is_shadow_array( $value ) );
    }

    private static function is_dimension_array( array $value ): bool {
        return array_key_exists( 'size', $value ) && ( isset( $value['unit'] ) || is_numeric( $value['size'] ) );
    }

    private static function is_box_array( array $value ): bool {
        $hits = 0;
        foreach ( [ 'top', 'right', 'bottom', 'left' ] as $key ) if ( array_key_exists( $key, $value ) ) $hits++;
        return $hits >= 2;
    }

    private static function is_shadow_array( array $value ): bool {
        $keys = array_map( 'strtolower', array_map( 'strval', array_keys( $value ) ) );
        return (bool) array_intersect( $keys, [ 'horizontal', 'vertical', 'blur', 'spread', 'color' ] );
    }

    private static function value_shape( $value ): string {
        if ( is_array( $value ) ) {
            if ( self::is_dimension_array( $value ) ) return 'dimension';
            if ( self::is_box_array( $value ) ) return 'box';
            if ( self::is_shadow_array( $value ) ) return 'shadow';
            return 'map';
        }
        if ( is_bool( $value ) ) return 'boolean';
        if ( is_numeric( $value ) ) return 'number';
        return 'string';
    }

    private static function unit( $value ): ?string {
        if ( is_array( $value ) && isset( $value['unit'] ) && is_scalar( $value['unit'] ) ) return (string) $value['unit'];
        return null;
    }

    private static function clean_value( $value, int $depth = 0 ) {
        if ( $depth > 3 ) return null;
        if ( is_scalar( $value ) || null === $value ) return $value;
        if ( ! is_array( $value ) ) return null;
        $out = [];
        $count = 0;
        foreach ( $value as $key => $child ) {
            if ( $count++ >= 20 ) break;
            $out[ $key ] = self::clean_value( $child, $depth + 1 );
        }
        return $out;
    }

    private static function array_depth( array $value, int $depth = 1 ): int {
        $max = $depth;
        foreach ( $value as $child ) if ( is_array( $child ) ) $max = max( $max, self::array_depth( $child, $depth + 1 ) );
        return $max;
    }

    private static function hash( array $elements ): string {
        return hash( 'sha256', (string) wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) );
    }

    private static function document( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return new WP_Error( 'elementize_elementor_missing', 'Elementor is not loaded.', [ 'status' => 503 ] );
        try { $document = \\Elementor\\Plugin::$instance->documents->get( $id ); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_document_unavailable', 'Elementor could not load this page document: ' . $e->getMessage(), [ 'status' => 500 ] ); }
        if ( ! $document ) return new WP_Error( 'elementize_document_unavailable', 'Elementor document is unavailable for this page.', [ 'status' => 404 ] );
        return $document;
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_settings_discovery'] = true;
        $data['design_settings_discovery_read_only'] = true;
        $data['design_settings_discovery_categories'] = [ 'typography', 'spacing', 'alignment', 'sizing', 'border_radius', 'border', 'shadow', 'background', 'color' ];
        $data['design_settings_global_dynamic_classification'] = true;
        $data['design_settings_responsive_classification'] = true;
        $data['design_settings_writer_candidates_advisory'] = true;
        $response->set_data( $data );
        return $response;
    }
}

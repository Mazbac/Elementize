<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Render_Audit {
    private const VERSION = '0.5.6';
    private const NS = 'elementize/v1';
    private const MAX_MATCHES = 20;
    private const CONTEXT_RADIUS = 700;
    private const LOOKBACK = 7000;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 66 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 86, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/render-audit', [
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
        if ( ! is_user_logged_in() ) {
            return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        }

        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) {
            return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        }

        if ( ! current_user_can( 'manage_options' ) || ! current_user_can( 'edit_post', $id ) ) {
            return new WP_Error( 'elementize_render_audit_forbidden', 'Render audit requires an administrator who can edit the page.', [ 'status' => 403 ] );
        }

        return true;
    }

    public static function audit( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $q = trim( (string) $request->get_param( 'q' ) );
        if ( mb_strlen( $q ) < 2 ) {
            return new WP_Error( 'elementize_render_audit_query_short', 'q must contain at least 2 characters.', [ 'status' => 400 ] );
        }

        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->frontend ) ) {
            return new WP_Error( 'elementize_elementor_frontend_unavailable', 'Elementor frontend renderer is unavailable.', [ 'status' => 500 ] );
        }

        $frontend = \Elementor\Plugin::$instance->frontend;
        if ( ! method_exists( $frontend, 'get_builder_content_for_display' ) ) {
            return new WP_Error( 'elementize_builder_render_unavailable', 'Elementor builder render method is unavailable.', [ 'status' => 500 ] );
        }

        $target_post = get_post( $id );
        $previous_post = $GLOBALS['post'] ?? null;
        $html = '';
        $render_error = null;

        try {
            $GLOBALS['post'] = $target_post;
            setup_postdata( $target_post );
            $html = (string) $frontend->get_builder_content_for_display( $id, false );
        } catch ( Throwable $exception ) {
            $render_error = [
                'exception_class' => get_class( $exception ),
                'message' => $exception->getMessage(),
            ];
        } finally {
            wp_reset_postdata();
            $GLOBALS['post'] = $previous_post;
        }

        if ( $render_error ) {
            return new WP_REST_Response( [
                'page_id' => $id,
                'query' => $q,
                'render_error' => $render_error,
                'read_only' => true,
            ], 200 );
        }

        $elements = self::elements_for_post( $id );
        $matches = self::find_rendered_matches( $html, $q, is_array( $elements ) ? $elements : [] );

        return new WP_REST_Response( [
            'page_id' => $id,
            'query' => $q,
            'builder_html_length' => strlen( $html ),
            'rendered_match_count' => count( $matches ),
            'rendered_matches' => $matches,
            'diagnosis' => $matches ? 'query_found_in_rendered_builder_html' : 'query_not_found_in_rendered_builder_html',
            'read_only' => true,
        ], 200 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;

        $data['elementize_version'] = self::VERSION;
        $data['render_source_audit'] = true;
        $data['render_source_audit_read_only'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function find_rendered_matches( string $html, string $q, array $elements ): array {
        $matches = [];
        $offset = 0;
        $q_len = strlen( $q );

        while ( count( $matches ) < self::MAX_MATCHES ) {
            $pos = stripos( $html, $q, $offset );
            if ( false === $pos ) break;

            $lookback_start = max( 0, $pos - self::LOOKBACK );
            $prefix = substr( $html, $lookback_start, $pos - $lookback_start );
            $element_id = self::nearest_element_id( $prefix );
            $widget_type = self::nearest_widget_type( $prefix );
            $saved = $element_id ? self::find_saved_element( $elements, $element_id ) : null;

            $context_start = max( 0, $pos - self::CONTEXT_RADIUS );
            $context_len = min( strlen( $html ) - $context_start, ( self::CONTEXT_RADIUS * 2 ) + $q_len );
            $context_html = substr( $html, $context_start, $context_len );
            $context_text = trim( preg_replace( '/\\s+/', ' ', wp_strip_all_tags( $context_html ) ) );

            $matches[] = [
                'html_offset' => $pos,
                'nearest_element_id' => $element_id,
                'nearest_widget_type_from_html' => $widget_type,
                'saved_element_found' => is_array( $saved ),
                'saved_el_type' => is_array( $saved ) && isset( $saved['elType'] ) ? (string) $saved['elType'] : '',
                'saved_widget_type' => is_array( $saved ) && isset( $saved['widgetType'] ) ? (string) $saved['widgetType'] : '',
                'saved_setting_keys' => is_array( $saved ) && isset( $saved['settings'] ) && is_array( $saved['settings'] )
                    ? array_values( array_map( 'strval', array_keys( $saved['settings'] ) ) )
                    : [],
                'context_text' => mb_strlen( $context_text ) > 1200 ? mb_substr( $context_text, 0, 1200 ) . '…' : $context_text,
            ];

            $offset = $pos + max( 1, $q_len );
        }

        return $matches;
    }

    private static function nearest_element_id( string $prefix ): string {
        $patterns = [
            '/data-id=["\\\']([a-zA-Z0-9_-]+)["\\\']/i',
            '/elementor-element-([a-zA-Z0-9_-]+)/i',
        ];

        foreach ( $patterns as $pattern ) {
            if ( preg_match_all( $pattern, $prefix, $matches ) && ! empty( $matches[1] ) ) {
                return (string) end( $matches[1] );
            }
        }

        return '';
    }

    private static function nearest_widget_type( string $prefix ): string {
        if ( preg_match_all( '/elementor-widget-([a-zA-Z0-9_-]+)/i', $prefix, $matches ) && ! empty( $matches[1] ) ) {
            return (string) end( $matches[1] );
        }
        return '';
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

    private static function find_saved_element( array $elements, string $element_id ): ?array {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) return $element;
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $found = self::find_saved_element( $element['elements'], $element_id );
                if ( $found ) return $found;
            }
        }
        return null;
    }
}

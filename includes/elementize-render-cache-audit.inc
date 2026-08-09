<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Render_Cache_Audit {
    private const VERSION = '0.5.8';
    private const NS = 'elementize/v1';
    private const CACHE_META_KEY = '_elementor_element_cache';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 68 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 88, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/render-cache-audit', [
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
        if ( ! current_user_can( 'manage_options' ) ) {
            return new WP_Error( 'elementize_render_cache_audit_forbidden', 'Render-cache audit requires an administrator.', [ 'status' => 403 ] );
        }
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) {
            return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        }
        if ( ! current_user_can( 'edit_post', $id ) ) {
            return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function audit( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $q = trim( (string) $request->get_param( 'q' ) );
        if ( mb_strlen( $q ) < 2 ) {
            return new WP_Error( 'elementize_render_cache_audit_query_short', 'q must contain at least 2 characters.', [ 'status' => 400 ] );
        }

        $raw = get_post_meta( $id, self::CACHE_META_KEY, true );
        $raw_string = is_string( $raw ) ? $raw : ( '' === $raw || null === $raw ? '' : wp_json_encode( $raw ) );
        if ( ! is_string( $raw_string ) ) $raw_string = '';

        $decoded = null;
        if ( '' !== $raw_string ) {
            $decoded = json_decode( $raw_string, true );
        } elseif ( is_array( $raw ) ) {
            $decoded = $raw;
        }

        $raw_matches = self::count_matches( $raw_string, $q );
        $decoded_strings = [];
        if ( is_array( $decoded ) ) self::collect_matching_strings( $decoded, $q, [], $decoded_strings );

        $timeout = is_array( $decoded ) && isset( $decoded['timeout'] ) && is_numeric( $decoded['timeout'] ) ? (int) $decoded['timeout'] : 0;
        $content = is_array( $decoded ) && isset( $decoded['value']['content'] ) && is_string( $decoded['value']['content'] )
            ? $decoded['value']['content']
            : '';

        return new WP_REST_Response( [
            'page_id' => $id,
            'query' => $q,
            'cache_meta_key' => self::CACHE_META_KEY,
            'cache_present' => '' !== $raw_string || is_array( $raw ),
            'cache_raw_length' => strlen( $raw_string ),
            'cache_raw_hash' => '' !== $raw_string ? hash( 'sha256', $raw_string ) : '',
            'cache_raw_match_count' => $raw_matches,
            'cache_json_decoded' => is_array( $decoded ),
            'cache_timeout' => $timeout,
            'cache_expired' => $timeout > 0 ? $timeout < current_time( 'timestamp' ) : null,
            'cache_content_length' => strlen( $content ),
            'cache_content_match_count' => self::count_matches( $content, $q ),
            'matching_string_count' => count( $decoded_strings ),
            'matching_strings' => array_slice( $decoded_strings, 0, 20 ),
            'diagnosis' => self::diagnosis( $raw_string, $content, $q ),
            'read_only' => true,
        ], 200 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['render_cache_audit'] = true;
        $data['render_cache_audit_read_only'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function count_matches( string $haystack, string $needle ): int {
        if ( '' === $haystack || '' === $needle ) return 0;
        return substr_count( mb_strtolower( $haystack ), mb_strtolower( $needle ) );
    }

    private static function collect_matching_strings( $value, string $q, array $path, array &$matches ): void {
        if ( count( $matches ) >= 20 ) return;
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $next ) {
                self::collect_matching_strings( $next, $q, array_merge( $path, [ $key ] ), $matches );
                if ( count( $matches ) >= 20 ) return;
            }
            return;
        }
        if ( ! is_string( $value ) || false === mb_stripos( $value, $q ) ) return;
        $matches[] = [
            'path' => array_values( $path ),
            'length' => strlen( $value ),
            'match_count' => self::count_matches( $value, $q ),
            'snippet' => self::snippet( $value, $q ),
        ];
    }

    private static function snippet( string $value, string $q ): string {
        $pos = mb_stripos( $value, $q );
        if ( false === $pos ) return '';
        $start = max( 0, $pos - 180 );
        return mb_substr( wp_strip_all_tags( $value ), $start, 500 );
    }

    private static function diagnosis( string $raw, string $content, string $q ): string {
        if ( self::count_matches( $content, $q ) > 0 ) return 'query_found_in_elementor_document_cache_content';
        if ( self::count_matches( $raw, $q ) > 0 ) return 'query_found_in_elementor_document_cache_metadata';
        if ( '' !== $raw ) return 'elementor_document_cache_present_but_query_not_found';
        return 'elementor_document_cache_not_present';
    }
}

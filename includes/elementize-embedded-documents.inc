<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Embedded_Documents {
    private const VERSION = '0.5.10';
    private const NS = 'elementize/v1';
    private const MAX_DEPTH = 6;
    private const MAX_NODES = 100;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 70 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 90, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/embedded-documents', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'audit' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
            ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) {
            return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        }
        if ( ! current_user_can( 'manage_options' ) ) {
            return new WP_Error( 'elementize_embedded_documents_forbidden', 'Embedded document diagnostics require an administrator.', [ 'status' => 403 ] );
        }
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) {
            return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        }
        if ( ! current_user_can( 'edit_post', $id ) ) {
            return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot inspect this page.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function audit( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $query = trim( (string) $request->get_param( 'q' ) );
        $queue = [ [ 'post_id' => $page_id, 'depth' => 0 ] ];
        $seen = [ $page_id => true ];
        $edges = [];
        $documents = [];

        while ( $queue && count( $documents ) < self::MAX_NODES ) {
            $current = array_shift( $queue );
            $source_id = absint( $current['post_id'] );
            $depth = absint( $current['depth'] );
            if ( $depth >= self::MAX_DEPTH ) continue;

            $decoded = self::elementor_data_array( $source_id );
            if ( ! is_array( $decoded ) ) continue;
            $references = [];
            self::collect_document_references( $decoded, [], $references );

            foreach ( $references as $reference ) {
                $target_id = absint( $reference['post_id'] );
                if ( ! $target_id || $target_id === $source_id ) continue;
                $key = $source_id . ':' . $target_id . ':' . $reference['path'];
                $edges[ $key ] = [
                    'source_post_id' => $source_id,
                    'target_post_id' => $target_id,
                    'path' => $reference['path'],
                    'value' => $reference['value'],
                    'depth' => $depth + 1,
                ];
                if ( ! isset( $seen[ $target_id ] ) ) {
                    $seen[ $target_id ] = true;
                    $queue[] = [ 'post_id' => $target_id, 'depth' => $depth + 1 ];
                }
            }
        }

        $graph_ids = array_map( 'absint', array_keys( $seen ) );
        $embedded_ids = array_values( array_filter( $graph_ids, static fn( $id ) => $id !== $page_id ) );

        foreach ( $embedded_ids as $embedded_id ) {
            $post = get_post( $embedded_id );
            if ( ! $post ) continue;
            $elementor_raw = self::elementor_data_raw( $embedded_id );
            $post_content = (string) $post->post_content;
            $reverse = self::reverse_references( $embedded_id );
            $external = array_values( array_filter( $reverse, static function( $ref ) use ( $graph_ids ) {
                return ! in_array( absint( $ref['source_post_id'] ), $graph_ids, true );
            } ) );

            $documents[] = [
                'post_id' => $embedded_id,
                'post_type' => (string) $post->post_type,
                'post_status' => (string) $post->post_status,
                'post_title' => (string) $post->post_title,
                'elementor_edit_mode' => (string) get_post_meta( $embedded_id, '_elementor_edit_mode', true ),
                'elementor_template_type' => (string) get_post_meta( $embedded_id, '_elementor_template_type', true ),
                'elementor_data_present' => '' !== $elementor_raw,
                'elementor_data_length' => strlen( $elementor_raw ),
                'elementor_data_hash' => '' !== $elementor_raw ? hash( 'sha256', $elementor_raw ) : '',
                'query_match_counts' => [
                    'post_content' => self::count_query( $post_content, $query ),
                    'elementor_data' => self::count_query( $elementor_raw, $query ),
                ],
                'reverse_reference_count' => count( $reverse ),
                'external_reference_count' => count( $external ),
                'exclusive_to_page_graph' => 0 === count( $external ),
                'reverse_references' => $reverse,
                'external_references' => $external,
            ];
        }

        return new WP_REST_Response( [
            'page_id' => $page_id,
            'page_title' => (string) get_the_title( $page_id ),
            'page_status' => (string) get_post_status( $page_id ),
            'query' => $query,
            'embedded_document_count' => count( $documents ),
            'embedded_documents' => $documents,
            'reference_edge_count' => count( $edges ),
            'reference_edges' => array_values( $edges ),
            'graph_post_ids' => $graph_ids,
            'max_depth' => self::MAX_DEPTH,
            'read_only' => true,
        ], 200 );
    }

    private static function elementor_data_raw( int $post_id ): string {
        $raw = get_post_meta( $post_id, '_elementor_data', true );
        if ( is_string( $raw ) ) return $raw;
        if ( is_array( $raw ) || is_object( $raw ) ) {
            $json = wp_json_encode( $raw );
            return is_string( $json ) ? $json : '';
        }
        return is_scalar( $raw ) ? (string) $raw : '';
    }

    private static function elementor_data_array( int $post_id ): ?array {
        $raw = get_post_meta( $post_id, '_elementor_data', true );
        if ( is_array( $raw ) ) return $raw;
        if ( ! is_string( $raw ) || '' === $raw ) return null;
        $decoded = json_decode( $raw, true );
        return is_array( $decoded ) ? $decoded : null;
    }

    private static function collect_document_references( $value, array $path, array &$references ): void {
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $child ) {
                $next = $path;
                $next[] = is_int( $key ) ? $key : (string) $key;
                self::collect_document_references( $child, $next, $references );
            }
            return;
        }
        if ( ! is_scalar( $value ) ) return;
        $text = trim( (string) $value );
        if ( '' === $text ) return;

        $candidates = [];
        if ( ctype_digit( $text ) ) {
            $candidates[] = absint( $text );
        } elseif ( preg_match_all( '/(?<!\\d)(\\d{4,})(?!\\d)/', $text, $matches ) ) {
            foreach ( $matches[1] as $match ) $candidates[] = absint( $match );
        }

        foreach ( array_unique( array_filter( $candidates ) ) as $candidate ) {
            $post = get_post( $candidate );
            if ( ! $post ) continue;
            if ( ! in_array( $post->post_type, [ 'pixfort_template', 'elementor_library' ], true ) ) continue;
            $references[] = [
                'post_id' => $candidate,
                'path' => wp_json_encode( $path ),
                'value' => mb_substr( $text, 0, 500 ),
            ];
        }
    }

    private static function reverse_references( int $target_id ): array {
        global $wpdb;
        $like = '%' . $wpdb->esc_like( (string) $target_id ) . '%';
        $candidate_ids = $wpdb->get_col( $wpdb->prepare(
            "SELECT DISTINCT post_id FROM {$wpdb->postmeta} WHERE meta_key = '_elementor_data' AND meta_value LIKE %s LIMIT 500",
            $like
        ) );
        $result = [];
        foreach ( array_map( 'absint', (array) $candidate_ids ) as $source_id ) {
            if ( ! $source_id || $source_id === $target_id ) continue;
            $decoded = self::elementor_data_array( $source_id );
            if ( ! is_array( $decoded ) ) continue;
            $references = [];
            self::collect_document_references( $decoded, [], $references );
            foreach ( $references as $reference ) {
                if ( absint( $reference['post_id'] ) !== $target_id ) continue;
                $source = get_post( $source_id );
                if ( ! $source ) continue;
                $key = $source_id . ':' . $reference['path'];
                $result[ $key ] = [
                    'source_post_id' => $source_id,
                    'source_post_type' => (string) $source->post_type,
                    'source_post_status' => (string) $source->post_status,
                    'source_post_title' => (string) $source->post_title,
                    'path' => $reference['path'],
                ];
            }
        }
        return array_values( $result );
    }

    private static function count_query( string $haystack, string $query ): int {
        if ( '' === $query || '' === $haystack ) return 0;
        return substr_count( strtolower( $haystack ), strtolower( $query ) );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['embedded_documents_diagnostic'] = true;
        $data['embedded_documents_diagnostic_read_only'] = true;
        $response->set_data( $data );
        return $response;
    }
}

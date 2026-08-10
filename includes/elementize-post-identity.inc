<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Post_Identity {
    private const VERSION = '0.5.9';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 69 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 89, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/posts/(?P<id>\\d+)/identity', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'identity' ],
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
            return new WP_Error( 'elementize_post_identity_forbidden', 'Post identity diagnostics require an administrator.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function identity( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post ) {
            return new WP_REST_Response( [
                'post_id' => $id,
                'exists' => false,
                'read_only' => true,
            ], 200 );
        }

        $revision_parent = wp_is_post_revision( $id );
        $autosave_parent = wp_is_post_autosave( $id );
        $parent_id = $revision_parent ? absint( $revision_parent ) : absint( $post->post_parent );
        $parent = $parent_id ? get_post( $parent_id ) : null;
        $type_object = get_post_type_object( $post->post_type );
        $elementor_data = get_post_meta( $id, '_elementor_data', true );
        if ( ! is_string( $elementor_data ) ) {
            $elementor_data = is_scalar( $elementor_data ) ? (string) $elementor_data : wp_json_encode( $elementor_data );
        }
        if ( ! is_string( $elementor_data ) ) $elementor_data = '';

        $query = trim( (string) $request->get_param( 'q' ) );
        $post_content = (string) $post->post_content;
        $query_counts = [
            'post_content' => self::count_query( $post_content, $query ),
            'elementor_data' => self::count_query( $elementor_data, $query ),
        ];

        $document = null;
        $document_class = '';
        $document_name = '';
        $document_built_with_elementor = null;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $document = \Elementor\Plugin::$instance->documents->get( $id );
                if ( $document ) {
                    $document_class = get_class( $document );
                    $document_name = method_exists( $document, 'get_name' ) ? (string) $document->get_name() : '';
                    $document_built_with_elementor = method_exists( $document, 'is_built_with_elementor' ) ? (bool) $document->is_built_with_elementor() : null;
                }
            } catch ( Throwable $ignored ) {}
        }

        return new WP_REST_Response( [
            'post_id' => $id,
            'exists' => true,
            'post_type' => (string) $post->post_type,
            'post_status' => (string) $post->post_status,
            'post_title' => (string) $post->post_title,
            'post_name' => (string) $post->post_name,
            'post_parent' => absint( $post->post_parent ),
            'revision_parent' => $revision_parent ? absint( $revision_parent ) : 0,
            'autosave_parent' => $autosave_parent ? absint( $autosave_parent ) : 0,
            'parent_post_type' => $parent ? (string) $parent->post_type : '',
            'parent_post_status' => $parent ? (string) $parent->post_status : '',
            'parent_post_title' => $parent ? (string) $parent->post_title : '',
            'rest_base' => $type_object && ! empty( $type_object->rest_base ) ? (string) $type_object->rest_base : '',
            'show_in_rest' => $type_object ? (bool) $type_object->show_in_rest : false,
            'modified_gmt' => (string) $post->post_modified_gmt,
            'post_content_length' => strlen( $post_content ),
            'post_content_hash' => hash( 'sha256', $post_content ),
            'elementor_edit_mode' => (string) get_post_meta( $id, '_elementor_edit_mode', true ),
            'elementor_template_type' => (string) get_post_meta( $id, '_elementor_template_type', true ),
            'elementor_data_present' => '' !== $elementor_data,
            'elementor_data_length' => strlen( $elementor_data ),
            'elementor_data_hash' => '' !== $elementor_data ? hash( 'sha256', $elementor_data ) : '',
            'elementor_document_class' => $document_class,
            'elementor_document_name' => $document_name,
            'elementor_document_built' => $document_built_with_elementor,
            'query' => $query,
            'query_match_counts' => $query_counts,
            'read_only' => true,
        ], 200 );
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
        $data['post_identity_diagnostic'] = true;
        $data['post_identity_diagnostic_read_only'] = true;
        $response->set_data( $data );
        return $response;
    }
}

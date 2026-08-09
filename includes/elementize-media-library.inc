<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Media_Library {
    private const VERSION = '0.5.1';
    private const NS = 'elementize/v1';
    private const MAX_PER_PAGE = 50;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 60 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 80, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/media/images', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'search' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [
                'q' => [
                    'type' => 'string',
                    'required' => false,
                    'default' => '',
                    'sanitize_callback' => 'sanitize_text_field',
                ],
                'filename' => [
                    'type' => 'string',
                    'required' => false,
                    'default' => '',
                    'sanitize_callback' => 'sanitize_text_field',
                ],
                'page' => [
                    'type' => 'integer',
                    'required' => false,
                    'default' => 1,
                    'minimum' => 1,
                    'sanitize_callback' => 'absint',
                ],
                'per_page' => [
                    'type' => 'integer',
                    'required' => false,
                    'default' => 20,
                    'minimum' => 1,
                    'maximum' => self::MAX_PER_PAGE,
                    'sanitize_callback' => 'absint',
                ],
            ],
        ] );
    }

    public static function can_read() {
        if ( ! is_user_logged_in() ) {
            return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        }
        if ( ! current_user_can( 'manage_options' ) ) {
            return new WP_Error( 'elementize_media_library_forbidden', 'Media library lookup requires an administrator.', [ 'status' => 403 ] );
        }
        if ( ! current_user_can( 'upload_files' ) ) {
            return new WP_Error( 'elementize_media_library_forbidden', 'The authenticated WordPress user cannot access the Media Library.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function search( WP_REST_Request $request ) {
        $q = trim( (string) $request->get_param( 'q' ) );
        $filename = trim( (string) $request->get_param( 'filename' ) );
        $page = max( 1, absint( $request->get_param( 'page' ) ) );
        $per_page = min( self::MAX_PER_PAGE, max( 1, absint( $request->get_param( 'per_page' ) ) ) );

        $args = [
            'post_type' => 'attachment',
            'post_status' => 'inherit',
            'post_mime_type' => 'image',
            'posts_per_page' => $per_page,
            'paged' => $page,
            'orderby' => 'date',
            'order' => 'DESC',
            'fields' => 'ids',
            'no_found_rows' => false,
        ];

        if ( '' !== $filename ) {
            $args['meta_query'] = [
                [
                    'key' => '_wp_attached_file',
                    'value' => $filename,
                    'compare' => 'LIKE',
                ],
            ];
        } elseif ( '' !== $q ) {
            $args['s'] = $q;
        }

        $query = new WP_Query( $args );
        $images = [];
        foreach ( (array) $query->posts as $attachment_id ) {
            $attachment_id = absint( $attachment_id );
            if ( $attachment_id < 1 || ! current_user_can( 'edit_post', $attachment_id ) ) continue;
            $images[] = self::image_snapshot( $attachment_id );
        }

        return new WP_REST_Response( [
            'page' => $page,
            'per_page' => $per_page,
            'total' => absint( $query->found_posts ),
            'total_pages' => absint( $query->max_num_pages ),
            'image_count' => count( $images ),
            'search' => [
                'q' => $q,
                'filename' => $filename,
            ],
            'images' => $images,
        ], 200 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['media_library_search'] = true;
        $data['media_library_images_only'] = true;
        $data['media_library_max_per_page'] = self::MAX_PER_PAGE;
        $response->set_data( $data );
        return $response;
    }

    private static function image_snapshot( int $attachment_id ): array {
        $post = get_post( $attachment_id );
        $url = wp_get_attachment_url( $attachment_id );
        $file = get_attached_file( $attachment_id );
        $meta = wp_get_attachment_metadata( $attachment_id );
        if ( ! is_array( $meta ) ) $meta = [];

        return [
            'attachment_id' => $attachment_id,
            'title' => $post ? (string) $post->post_title : '',
            'filename' => is_string( $file ) && '' !== $file ? wp_basename( $file ) : '',
            'mime_type' => (string) get_post_mime_type( $attachment_id ),
            'url' => is_string( $url ) ? esc_url_raw( $url ) : '',
            'alt' => (string) get_post_meta( $attachment_id, '_wp_attachment_image_alt', true ),
            'width' => isset( $meta['width'] ) ? absint( $meta['width'] ) : 0,
            'height' => isset( $meta['height'] ) ? absint( $meta['height'] ) : 0,
            'date_gmt' => $post ? (string) $post->post_date_gmt : '',
        ];
    }
}

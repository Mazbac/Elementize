<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Media_Library {
    private const MAX_PER_PAGE = 50;

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/media/images', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'search' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [
                'q' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'filename' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'page' => [ 'type' => 'integer', 'required' => false, 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'per_page' => [ 'type' => 'integer', 'required' => false, 'default' => 20, 'minimum' => 1, 'maximum' => self::MAX_PER_PAGE, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function can_read() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) || ! current_user_can( 'upload_files' ) ) {
            return new WP_Error( 'elementize_media_forbidden', 'Media Library lookup requires an administrator who can upload files.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function search( WP_REST_Request $request ): WP_REST_Response {
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
            'update_post_term_cache' => false,
        ];

        $filename_match = '';
        if ( '' !== $filename ) {
            $basename = wp_basename( $filename );
            $filename_match = pathinfo( $basename, PATHINFO_FILENAME );
            if ( '' === $filename_match ) $filename_match = $basename;
            $args['meta_query'] = [ [ 'key' => '_wp_attached_file', 'value' => $filename_match, 'compare' => 'LIKE' ] ];
        } elseif ( '' !== $q ) {
            $args['s'] = $q;
        }

        $query = new WP_Query( $args );
        $images = [];
        foreach ( (array) $query->posts as $attachment_id ) {
            $attachment_id = absint( $attachment_id );
            if ( $attachment_id < 1 || ! current_user_can( 'edit_post', $attachment_id ) ) continue;
            $images[] = self::snapshot( $attachment_id );
        }

        return new WP_REST_Response( [
            'q' => $q,
            'filename' => $filename,
            'filename_match' => $filename_match,
            'page' => $page,
            'per_page' => $per_page,
            'total' => absint( $query->found_posts ),
            'total_pages' => absint( $query->max_num_pages ),
            'image_count' => count( $images ),
            'images' => $images,
        ], 200 );
    }

    public static function snapshot( int $attachment_id ): array {
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

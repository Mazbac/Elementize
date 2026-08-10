<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Media_Import {
    private const MAX_IMAGE_BYTES = 10485760;
    private const MAX_IMAGE_PIXELS = 60000000;
    private const MAX_SOURCE_ID = 512;
    private const MAX_URL = 8192;
    private const ALLOWED_MIMES = [
        'image/jpeg' => 'jpg',
        'image/png' => 'png',
        'image/webp' => 'webp',
        'image/avif' => 'avif',
        'image/gif' => 'gif',
    ];

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 35 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'annotate_status' ], 90, 3 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/media/images/import-chat-file', [
            'methods' => WP_REST_Server::CREATABLE,
            'callback' => [ self::class, 'import_chat_image' ],
            'permission_callback' => [ self::class, 'can_import' ],
            'args' => [
                'openaiFileIdRefs' => [ 'type' => 'array', 'required' => true ],
                'title' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'alt' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'confirm_import' => [ 'type' => 'boolean', 'required' => true ],
            ],
        ] );

        register_rest_route( Elementize_Core::NS, '/media/images/import-url', [
            'methods' => WP_REST_Server::CREATABLE,
            'callback' => [ self::class, 'import_remote_image' ],
            'permission_callback' => [ self::class, 'can_import' ],
            'args' => [
                'url' => [ 'type' => 'string', 'required' => true ],
                'source_page_url' => [ 'type' => 'string', 'default' => '' ],
                'title' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'alt' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'confirm_import' => [ 'type' => 'boolean', 'required' => true ],
            ],
        ] );
    }

    public static function can_import() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) || ! current_user_can( 'upload_files' ) ) {
            return new WP_Error( 'elementize_media_import_forbidden', 'Image import requires an administrator who can upload files.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function import_chat_image( WP_REST_Request $request ) {
        $confirm = self::require_confirmation( $request );
        if ( is_wp_error( $confirm ) ) return $confirm;

        $refs = $request->get_param( 'openaiFileIdRefs' );
        if ( ! is_array( $refs ) || 1 !== count( $refs ) ) return new WP_Error( 'elementize_chat_media_file_count', 'Exactly one conversation image file is required.', [ 'status' => 400 ] );
        $ref = reset( $refs );
        if ( ! is_array( $ref ) ) return new WP_Error( 'elementize_chat_media_reference_invalid', 'The conversation file reference was not expanded into file metadata.', [ 'status' => 400 ] );

        $source_name = sanitize_file_name( (string) ( $ref['name'] ?? '' ) );
        $source_id = sanitize_text_field( (string) ( $ref['id'] ?? '' ) );
        $source_mime = strtolower( trim( (string) ( $ref['mime_type'] ?? '' ) ) );
        $download_url = trim( (string) ( $ref['download_link'] ?? '' ) );
        if ( '' === $source_name || '' === $source_id || '' === $source_mime || '' === $download_url ) return new WP_Error( 'elementize_chat_media_reference_invalid', 'The conversation file reference is missing name, id, mime_type, or download_link.', [ 'status' => 400 ] );
        if ( strlen( $source_id ) > self::MAX_SOURCE_ID || strlen( $download_url ) > self::MAX_URL ) return new WP_Error( 'elementize_chat_media_reference_invalid', 'The conversation file reference exceeds Elementize safety limits.', [ 'status' => 400 ] );
        if ( ! str_starts_with( $source_mime, 'image/' ) ) return new WP_Error( 'elementize_chat_media_not_image', 'The supplied conversation file is not an image.', [ 'status' => 400 ] );
        if ( ! self::openai_file_url( $download_url ) ) return new WP_Error( 'elementize_chat_media_source_forbidden', 'The conversation image URL is not on an allowed OpenAI file host.', [ 'status' => 400 ] );

        $meta = [
            '_elementize_import_source' => 'chatgpt_conversation',
            '_elementize_chat_file_id' => $source_id,
            '_elementize_chat_file_name' => $source_name,
        ];
        $result = self::sideload( $download_url, $source_name, $request, $meta, 'conversation image' );
        if ( is_wp_error( $result ) ) return $result;
        $result['source_file_id'] = $source_id;
        $result['source_name'] = $source_name;
        $result['source_mime_type'] = $source_mime;
        return new WP_REST_Response( $result, 201 );
    }

    public static function import_remote_image( WP_REST_Request $request ) {
        $confirm = self::require_confirmation( $request );
        if ( is_wp_error( $confirm ) ) return $confirm;

        $url = trim( (string) $request->get_param( 'url' ) );
        if ( '' === $url || strlen( $url ) > self::MAX_URL || ! self::public_https_url( $url ) ) return new WP_Error( 'elementize_remote_media_url_invalid', 'url must be a public HTTPS image URL that passes WordPress safe-URL validation.', [ 'status' => 400 ] );

        $source_page = trim( (string) $request->get_param( 'source_page_url' ) );
        if ( '' !== $source_page && ( strlen( $source_page ) > self::MAX_URL || ! self::public_https_url( $source_page ) ) ) return new WP_Error( 'elementize_remote_media_source_page_invalid', 'source_page_url must be an optional public HTTPS URL.', [ 'status' => 400 ] );

        $path = (string) wp_parse_url( $url, PHP_URL_PATH );
        $name = sanitize_file_name( wp_basename( $path ) );
        if ( '' === $name || '.' === $name ) $name = 'remote-image';
        $meta = [
            '_elementize_import_source' => 'remote_https',
            '_elementize_remote_source_url' => esc_url_raw( $url, [ 'https' ] ),
        ];
        if ( '' !== $source_page ) $meta['_elementize_remote_source_page_url'] = esc_url_raw( $source_page, [ 'https' ] );

        $result = self::sideload( $url, $name, $request, $meta, 'remote image' );
        if ( is_wp_error( $result ) ) return $result;
        $result['source_url'] = esc_url_raw( $url, [ 'https' ] );
        $result['source_page_url'] = '' !== $source_page ? esc_url_raw( $source_page, [ 'https' ] ) : null;
        return new WP_REST_Response( $result, 201 );
    }

    public static function annotate_status( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . Elementize_Core::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['conversation_image_import'] = true;
        $data['remote_image_import'] = true;
        $data['remote_image_import_https_only'] = true;
        $data['media_import_max_bytes'] = self::MAX_IMAGE_BYTES;
        $data['media_import_allowed_mime_types'] = array_keys( self::ALLOWED_MIMES );
        $response->set_data( $data );
        return $response;
    }

    private static function require_confirmation( WP_REST_Request $request ) {
        return true === $request->get_param( 'confirm_import' ) ? true : new WP_Error( 'elementize_media_import_confirmation_required', 'Explicit confirm_import=true is required.', [ 'status' => 400 ] );
    }

    private static function sideload( string $url, string $source_name, WP_REST_Request $request, array $meta, string $label ) {
        require_once ABSPATH . 'wp-admin/includes/file.php';
        require_once ABSPATH . 'wp-admin/includes/media.php';
        require_once ABSPATH . 'wp-admin/includes/image.php';
        $tmp = wp_tempnam( $source_name );
        if ( ! is_string( $tmp ) || '' === $tmp ) return new WP_Error( 'elementize_media_temp_failed', 'WordPress could not create a temporary file.', [ 'status' => 500 ] );

        $remote = wp_safe_remote_get( $url, [
            'timeout' => 20,
            'redirection' => 2,
            'stream' => true,
            'filename' => $tmp,
            'limit_response_size' => self::MAX_IMAGE_BYTES + 1,
            'headers' => [ 'Accept' => 'image/*' ],
        ] );
        if ( is_wp_error( $remote ) ) { @unlink( $tmp ); return new WP_Error( 'elementize_media_download_failed', 'WordPress could not download the ' . $label . ': ' . $remote->get_error_message(), [ 'status' => 502 ] ); }
        $status = (int) wp_remote_retrieve_response_code( $remote );
        if ( 200 !== $status ) { @unlink( $tmp ); return new WP_Error( 'elementize_media_download_failed', 'The ' . $label . ' download returned HTTP ' . $status . '.', [ 'status' => 502 ] ); }

        clearstatcache( true, $tmp );
        $bytes = is_file( $tmp ) ? (int) filesize( $tmp ) : 0;
        if ( $bytes < 1 || $bytes > self::MAX_IMAGE_BYTES ) { @unlink( $tmp ); return new WP_Error( 'elementize_media_size_invalid', 'The image must be between 1 byte and 10 MiB.', [ 'status' => 400 ] ); }
        $actual_mime = function_exists( 'wp_get_image_mime' ) ? (string) wp_get_image_mime( $tmp ) : '';
        if ( ! isset( self::ALLOWED_MIMES[ $actual_mime ] ) ) { @unlink( $tmp ); return new WP_Error( 'elementize_media_mime_invalid', 'The downloaded file is not a supported JPG, PNG, WEBP, AVIF, or GIF image.', [ 'status' => 400 ] ); }
        $dimensions = @getimagesize( $tmp );
        $width = is_array( $dimensions ) && isset( $dimensions[0] ) ? absint( $dimensions[0] ) : 0;
        $height = is_array( $dimensions ) && isset( $dimensions[1] ) ? absint( $dimensions[1] ) : 0;
        if ( $width < 1 || $height < 1 || ( $width * $height ) > self::MAX_IMAGE_PIXELS ) { @unlink( $tmp ); return new WP_Error( 'elementize_media_dimensions_invalid', 'The image dimensions are invalid or exceed the 60-megapixel safety limit.', [ 'status' => 400 ] ); }

        $base = sanitize_file_name( pathinfo( $source_name, PATHINFO_FILENAME ) );
        if ( '' === $base ) $base = 'elementize-image';
        $filename = $base . '.' . self::ALLOWED_MIMES[ $actual_mime ];
        $title = trim( (string) $request->get_param( 'title' ) );
        if ( '' === $title ) $title = sanitize_text_field( str_replace( [ '-', '_' ], ' ', $base ) );
        $alt = trim( (string) $request->get_param( 'alt' ) );

        $attachment_id = media_handle_sideload( [ 'name' => $filename, 'tmp_name' => $tmp ], 0, $title );
        if ( is_wp_error( $attachment_id ) ) { @unlink( $tmp ); return new WP_Error( 'elementize_media_sideload_failed', 'WordPress could not add the image to the Media Library: ' . $attachment_id->get_error_message(), [ 'status' => 500 ] ); }
        $attachment_id = absint( $attachment_id );
        if ( $attachment_id < 1 || ! wp_attachment_is_image( $attachment_id ) || ! current_user_can( 'edit_post', $attachment_id ) ) {
            if ( $attachment_id > 0 ) wp_delete_attachment( $attachment_id, true );
            return new WP_Error( 'elementize_media_verification_failed', 'The imported Media Library item could not be verified as an editable image attachment.', [ 'status' => 500 ] );
        }

        if ( '' !== $alt ) update_post_meta( $attachment_id, '_wp_attachment_image_alt', $alt );
        foreach ( $meta as $key => $value ) update_post_meta( $attachment_id, $key, $value );
        update_post_meta( $attachment_id, '_elementize_imported_at_gmt', gmdate( 'Y-m-d H:i:s' ) );
        $file = get_attached_file( $attachment_id );
        $sha256 = is_string( $file ) && is_file( $file ) ? hash_file( 'sha256', $file ) : '';
        return [
            'imported' => true,
            'detected_mime_type' => $actual_mime,
            'bytes_downloaded' => $bytes,
            'sha256' => is_string( $sha256 ) ? $sha256 : '',
            'image' => Elementize_Media_Library::snapshot( $attachment_id ),
        ];
    }

    private static function openai_file_url( string $url ): bool {
        $parts = wp_parse_url( $url );
        if ( ! is_array( $parts ) ) return false;
        $scheme = strtolower( (string) ( $parts['scheme'] ?? '' ) );
        $host = strtolower( rtrim( (string) ( $parts['host'] ?? '' ), '.' ) );
        if ( 'https' !== $scheme || '' === $host ) return false;
        return 'oaiusercontent.com' === $host || str_ends_with( $host, '.oaiusercontent.com' );
    }

    private static function public_https_url( string $url ): bool {
        $parts = wp_parse_url( $url );
        if ( ! is_array( $parts ) || 'https' !== strtolower( (string) ( $parts['scheme'] ?? '' ) ) || empty( $parts['host'] ) ) return false;
        if ( ! empty( $parts['user'] ) || ! empty( $parts['pass'] ) ) return false;
        return (bool) wp_http_validate_url( $url );
    }
}

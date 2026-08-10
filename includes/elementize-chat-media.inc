<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Chat_Media {
    private const MAX_IMAGE_BYTES = 10485760;
    private const MAX_IMAGE_PIXELS = 60000000;
    private const MAX_SOURCE_ID = 512;
    private const MAX_DOWNLOAD_URL = 8192;
    private const ALLOWED_MIMES = [
        'image/jpeg' => 'jpg',
        'image/png' => 'png',
        'image/webp' => 'webp',
        'image/avif' => 'avif',
        'image/gif' => 'gif',
    ];

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 35 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/media/images/import-chat-file', [
            'methods' => WP_REST_Server::CREATABLE,
            'callback' => [ self::class, 'import_image' ],
            'permission_callback' => [ self::class, 'can_import' ],
            'args' => [
                'openaiFileIdRefs' => [ 'type' => 'array', 'required' => true ],
                'title' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'alt' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'confirm_import' => [ 'type' => 'boolean', 'required' => true ],
            ],
        ] );
    }

    public static function can_import() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) || ! current_user_can( 'upload_files' ) ) return new WP_Error( 'elementize_chat_media_forbidden', 'Conversation-image import requires an administrator who can upload files.', [ 'status' => 403 ] );
        return true;
    }

    public static function import_image( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_import' ) ) return new WP_Error( 'elementize_chat_media_confirmation_required', 'Explicit confirm_import=true is required.', [ 'status' => 400 ] );
        $refs = $request->get_param( 'openaiFileIdRefs' );
        if ( ! is_array( $refs ) || 1 !== count( $refs ) ) return new WP_Error( 'elementize_chat_media_file_count', 'Exactly one conversation image file is required.', [ 'status' => 400 ] );
        $ref = reset( $refs );
        if ( ! is_array( $ref ) ) return new WP_Error( 'elementize_chat_media_reference_invalid', 'The conversation file reference was not expanded into file metadata.', [ 'status' => 400 ] );

        $source_name = sanitize_file_name( (string) ( $ref['name'] ?? '' ) );
        $source_id = sanitize_text_field( (string) ( $ref['id'] ?? '' ) );
        $source_mime = strtolower( trim( (string) ( $ref['mime_type'] ?? '' ) ) );
        $download_link = trim( (string) ( $ref['download_link'] ?? '' ) );
        if ( '' === $source_name || '' === $source_id || '' === $source_mime || '' === $download_link ) return new WP_Error( 'elementize_chat_media_reference_invalid', 'The conversation file reference is missing name, id, mime_type, or download_link.', [ 'status' => 400 ] );
        if ( strlen( $source_id ) > self::MAX_SOURCE_ID || strlen( $download_link ) > self::MAX_DOWNLOAD_URL ) return new WP_Error( 'elementize_chat_media_reference_invalid', 'The conversation file reference exceeds Elementize safety limits.', [ 'status' => 400 ] );
        if ( ! str_starts_with( $source_mime, 'image/' ) ) return new WP_Error( 'elementize_chat_media_not_image', 'The supplied conversation file is not an image.', [ 'status' => 400 ] );
        if ( ! self::allowed_source_url( $download_link ) ) return new WP_Error( 'elementize_chat_media_source_forbidden', 'The conversation image URL is not on an allowed OpenAI file host.', [ 'status' => 400 ] );

        require_once ABSPATH . 'wp-admin/includes/file.php';
        require_once ABSPATH . 'wp-admin/includes/media.php';
        require_once ABSPATH . 'wp-admin/includes/image.php';
        $tmp = wp_tempnam( $source_name );
        if ( ! is_string( $tmp ) || '' === $tmp ) return new WP_Error( 'elementize_chat_media_temp_failed', 'WordPress could not create a temporary file.', [ 'status' => 500 ] );

        $remote = wp_safe_remote_get( $download_link, [
            'timeout' => 20,
            'redirection' => 2,
            'stream' => true,
            'filename' => $tmp,
            'limit_response_size' => self::MAX_IMAGE_BYTES + 1,
            'headers' => [ 'Accept' => 'image/*' ],
        ] );
        if ( is_wp_error( $remote ) ) { @unlink( $tmp ); return new WP_Error( 'elementize_chat_media_download_failed', 'WordPress could not download the conversation image: ' . $remote->get_error_message(), [ 'status' => 502 ] ); }
        $status = (int) wp_remote_retrieve_response_code( $remote );
        if ( 200 !== $status ) { @unlink( $tmp ); return new WP_Error( 'elementize_chat_media_download_failed', 'The conversation image download returned HTTP ' . $status . '.', [ 'status' => 502 ] ); }

        clearstatcache( true, $tmp );
        $bytes = is_file( $tmp ) ? (int) filesize( $tmp ) : 0;
        if ( $bytes < 1 || $bytes > self::MAX_IMAGE_BYTES ) { @unlink( $tmp ); return new WP_Error( 'elementize_chat_media_size_invalid', 'The conversation image must be between 1 byte and 10 MiB.', [ 'status' => 400 ] ); }
        $actual_mime = function_exists( 'wp_get_image_mime' ) ? (string) wp_get_image_mime( $tmp ) : '';
        if ( ! isset( self::ALLOWED_MIMES[ $actual_mime ] ) ) { @unlink( $tmp ); return new WP_Error( 'elementize_chat_media_mime_invalid', 'The downloaded file is not a supported JPG, PNG, WEBP, AVIF, or GIF image.', [ 'status' => 400 ] ); }

        $dimensions = @getimagesize( $tmp );
        $width = is_array( $dimensions ) && isset( $dimensions[0] ) ? absint( $dimensions[0] ) : 0;
        $height = is_array( $dimensions ) && isset( $dimensions[1] ) ? absint( $dimensions[1] ) : 0;
        if ( $width < 1 || $height < 1 || ( $width * $height ) > self::MAX_IMAGE_PIXELS ) { @unlink( $tmp ); return new WP_Error( 'elementize_chat_media_dimensions_invalid', 'The conversation image dimensions are invalid or exceed the 60-megapixel safety limit.', [ 'status' => 400 ] ); }

        $base = sanitize_file_name( pathinfo( $source_name, PATHINFO_FILENAME ) );
        if ( '' === $base ) $base = 'chat-image';
        $filename = $base . '.' . self::ALLOWED_MIMES[ $actual_mime ];
        $title = trim( (string) $request->get_param( 'title' ) );
        if ( '' === $title ) $title = sanitize_text_field( str_replace( [ '-', '_' ], ' ', $base ) );
        $alt = trim( (string) $request->get_param( 'alt' ) );

        $attachment_id = media_handle_sideload( [ 'name' => $filename, 'tmp_name' => $tmp ], 0, $title );
        if ( is_wp_error( $attachment_id ) ) { @unlink( $tmp ); return new WP_Error( 'elementize_chat_media_sideload_failed', 'WordPress could not add the conversation image to the Media Library: ' . $attachment_id->get_error_message(), [ 'status' => 500 ] ); }
        $attachment_id = absint( $attachment_id );
        if ( $attachment_id < 1 || ! wp_attachment_is_image( $attachment_id ) || ! current_user_can( 'edit_post', $attachment_id ) ) {
            if ( $attachment_id > 0 ) wp_delete_attachment( $attachment_id, true );
            return new WP_Error( 'elementize_chat_media_verification_failed', 'The imported Media Library item could not be verified as an editable image attachment.', [ 'status' => 500 ] );
        }

        if ( '' !== $alt ) update_post_meta( $attachment_id, '_wp_attachment_image_alt', $alt );
        update_post_meta( $attachment_id, '_elementize_chat_file_id', $source_id );
        update_post_meta( $attachment_id, '_elementize_chat_file_name', $source_name );
        $file = get_attached_file( $attachment_id );
        $sha256 = is_string( $file ) && is_file( $file ) ? hash_file( 'sha256', $file ) : '';

        return new WP_REST_Response( [
            'imported' => true,
            'source_file_id' => $source_id,
            'source_name' => $source_name,
            'source_mime_type' => $source_mime,
            'detected_mime_type' => $actual_mime,
            'bytes_downloaded' => $bytes,
            'sha256' => is_string( $sha256 ) ? $sha256 : '',
            'image' => Elementize_Media_Library::snapshot( $attachment_id ),
        ], 201 );
    }

    private static function allowed_source_url( string $url ): bool {
        $parts = wp_parse_url( $url );
        if ( ! is_array( $parts ) ) return false;
        $scheme = strtolower( (string) ( $parts['scheme'] ?? '' ) );
        $host = strtolower( rtrim( (string) ( $parts['host'] ?? '' ), '.' ) );
        if ( 'https' !== $scheme || '' === $host ) return false;
        return 'oaiusercontent.com' === $host || str_ends_with( $host, '.oaiusercontent.com' );
    }
}

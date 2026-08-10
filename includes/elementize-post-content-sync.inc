<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Post_Content_Sync {
    private const VERSION = '0.5.7';
    private const NS = 'elementize/v1';
    private const META_MANAGED = '_elementize_managed';
    private const META_BACKUP = '_elementize_post_content_backup';
    private const META_BACKUP_HASH = '_elementize_post_content_backup_hash';
    private const META_BACKUP_AT = '_elementize_post_content_backup_at_gmt';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 67 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 87, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/post-content-sync', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'snapshot' ],
                'permission_callback' => [ self::class, 'can_manage' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'mutate' ],
                'permission_callback' => [ self::class, 'can_manage' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'operation' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_key' ],
                    'expected_status' => [ 'type' => 'string', 'required' => true ],
                    'expected_title' => [ 'type' => 'string', 'required' => true ],
                    'content_hash' => [ 'type' => 'string', 'required' => true ],
                    'post_content_hash' => [ 'type' => 'string', 'required' => true ],
                    'confirm_post_content_change' => [ 'type' => 'boolean', 'required' => true ],
                ],
            ],
        ] );
    }

    public static function can_manage( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) {
            return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        }
        if ( ! current_user_can( 'manage_options' ) ) {
            return new WP_Error( 'elementize_post_content_forbidden', 'Post-content cleanup requires an administrator.', [ 'status' => 403 ] );
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

    public static function snapshot( WP_REST_Request $request ): WP_REST_Response {
        return new WP_REST_Response( self::state( absint( $request['id'] ) ), 200 );
    }

    public static function mutate( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) {
            return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        }
        if ( ! self::is_managed( $id ) ) {
            return new WP_Error( 'elementize_unmanaged_page', 'This page is not marked as Elementize-managed. Post-content changes are blocked.', [ 'status' => 409 ] );
        }
        if ( 'draft' !== $post->post_status || 'draft' !== (string) $request->get_param( 'expected_status' ) ) {
            return new WP_Error( 'elementize_status_changed', 'Post-content cleanup is draft-only and the page is not in the expected draft state.', [ 'status' => 409, 'status_now' => (string) $post->post_status ] );
        }
        if ( (string) $post->post_title !== (string) $request->get_param( 'expected_title' ) ) {
            return new WP_Error( 'elementize_title_changed', 'The page title changed after it was read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        }
        if ( true !== $request->get_param( 'confirm_post_content_change' ) ) {
            return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_post_content_change=true is required.', [ 'status' => 400 ] );
        }

        $elements = self::elements_for_post( $id );
        if ( ! is_array( $elements ) ) {
            return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor page data could not be read.', [ 'status' => 500 ] );
        }
        $current_content_hash = self::elements_hash( $elements );
        $expected_content_hash = (string) $request->get_param( 'content_hash' );
        if ( '' === $expected_content_hash || ! hash_equals( $current_content_hash, $expected_content_hash ) ) {
            return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read.', [ 'status' => 409, 'content_hash' => $current_content_hash ] );
        }

        $current_post_content = (string) $post->post_content;
        $current_post_content_hash = self::post_content_hash( $current_post_content );
        $expected_post_content_hash = (string) $request->get_param( 'post_content_hash' );
        if ( '' === $expected_post_content_hash || ! hash_equals( $current_post_content_hash, $expected_post_content_hash ) ) {
            return new WP_Error( 'elementize_post_content_changed', 'WordPress post_content changed after it was read.', [ 'status' => 409, 'post_content_hash' => $current_post_content_hash ] );
        }

        $operation = sanitize_key( (string) $request->get_param( 'operation' ) );
        if ( ! in_array( $operation, [ 'clear', 'restore_backup' ], true ) ) {
            return new WP_Error( 'elementize_post_content_operation_invalid', 'operation must be clear or restore_backup.', [ 'status' => 400 ] );
        }

        $revision_id = self::save_revision( $id );
        if ( 'clear' === $operation ) {
            return self::clear( $id, $post, $current_post_content, $current_content_hash, $revision_id );
        }
        return self::restore_backup( $id, $post, $current_post_content, $current_content_hash, $revision_id );
    }

    private static function clear( int $id, WP_Post $post, string $current_post_content, string $content_hash, ?int $revision_id ) {
        if ( '' === $current_post_content ) {
            return new WP_Error( 'elementize_post_content_already_empty', 'WordPress post_content is already empty; no cleanup is needed.', [ 'status' => 409 ] );
        }

        $backup_hash = self::post_content_hash( $current_post_content );
        update_post_meta( $id, self::META_BACKUP, $current_post_content );
        update_post_meta( $id, self::META_BACKUP_HASH, $backup_hash );
        update_post_meta( $id, self::META_BACKUP_AT, gmdate( 'Y-m-d H:i:s' ) );

        $updated = wp_update_post( [ 'ID' => $id, 'post_content' => '' ], true );
        if ( is_wp_error( $updated ) ) {
            return new WP_Error( 'elementize_post_content_clear_failed', 'WordPress could not clear post_content: ' . $updated->get_error_message(), [ 'status' => 500, 'revision_id' => $revision_id ] );
        }

        clean_post_cache( $id );
        $after = get_post( $id );
        if ( ! $after || '' !== (string) $after->post_content ) {
            self::restore_exact_content( $id, $current_post_content );
            return new WP_Error( 'elementize_post_content_clear_verification_failed', 'post_content was not verified as empty, so Elementize attempted to restore the exact pre-change content.', [ 'status' => 500, 'revision_id' => $revision_id ] );
        }

        $elements_after = self::elements_for_post( $id );
        $content_hash_after = is_array( $elements_after ) ? self::elements_hash( $elements_after ) : '';
        if ( '' === $content_hash_after || ! hash_equals( $content_hash, $content_hash_after ) ) {
            self::restore_exact_content( $id, $current_post_content );
            return new WP_Error( 'elementize_post_content_elementor_changed', 'The Elementor document changed unexpectedly during post_content cleanup, so Elementize attempted to restore post_content.', [ 'status' => 500, 'revision_id' => $revision_id, 'content_hash' => $content_hash_after ] );
        }

        return new WP_REST_Response( [
            'saved' => true,
            'operation' => 'clear',
            'page_id' => $id,
            'status' => (string) $after->post_status,
            'revision_id' => $revision_id,
            'content_hash' => $content_hash_after,
            'post_content_length_before' => strlen( $current_post_content ),
            'post_content_length_after' => 0,
            'post_content_hash_before' => $backup_hash,
            'post_content_hash_after' => self::post_content_hash( '' ),
            'backup_available' => true,
            'backup_hash' => $backup_hash,
            'restore_operation' => 'restore_backup',
            'preview_url' => get_preview_post_link( $id ),
        ], 200 );
    }

    private static function restore_backup( int $id, WP_Post $post, string $current_post_content, string $content_hash, ?int $revision_id ) {
        $backup = get_post_meta( $id, self::META_BACKUP, true );
        $backup_hash = (string) get_post_meta( $id, self::META_BACKUP_HASH, true );
        if ( ! is_string( $backup ) || '' === $backup || '' === $backup_hash || ! hash_equals( self::post_content_hash( $backup ), $backup_hash ) ) {
            return new WP_Error( 'elementize_post_content_backup_unavailable', 'No verified post_content backup is available for this managed page.', [ 'status' => 409 ] );
        }

        $updated = wp_update_post( [ 'ID' => $id, 'post_content' => $backup ], true );
        if ( is_wp_error( $updated ) ) {
            return new WP_Error( 'elementize_post_content_restore_failed', 'WordPress could not restore the post_content backup: ' . $updated->get_error_message(), [ 'status' => 500, 'revision_id' => $revision_id ] );
        }

        clean_post_cache( $id );
        $after = get_post( $id );
        if ( ! $after || ! hash_equals( $backup_hash, self::post_content_hash( (string) $after->post_content ) ) ) {
            self::restore_exact_content( $id, $current_post_content );
            return new WP_Error( 'elementize_post_content_restore_verification_failed', 'The backup was not restored exactly, so Elementize attempted to restore the pre-request content.', [ 'status' => 500, 'revision_id' => $revision_id ] );
        }

        $elements_after = self::elements_for_post( $id );
        $content_hash_after = is_array( $elements_after ) ? self::elements_hash( $elements_after ) : '';
        if ( '' === $content_hash_after || ! hash_equals( $content_hash, $content_hash_after ) ) {
            self::restore_exact_content( $id, $current_post_content );
            return new WP_Error( 'elementize_post_content_elementor_changed', 'The Elementor document changed unexpectedly during post_content restore, so Elementize attempted to restore the pre-request post_content.', [ 'status' => 500, 'revision_id' => $revision_id, 'content_hash' => $content_hash_after ] );
        }

        return new WP_REST_Response( [
            'saved' => true,
            'operation' => 'restore_backup',
            'page_id' => $id,
            'status' => (string) $after->post_status,
            'revision_id' => $revision_id,
            'content_hash' => $content_hash_after,
            'post_content_length_before' => strlen( $current_post_content ),
            'post_content_length_after' => strlen( (string) $after->post_content ),
            'post_content_hash_before' => self::post_content_hash( $current_post_content ),
            'post_content_hash_after' => self::post_content_hash( (string) $after->post_content ),
            'backup_hash' => $backup_hash,
            'preview_url' => get_preview_post_link( $id ),
        ], 200 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['post_content_sync'] = true;
        $data['post_content_sync_managed_draft_only'] = true;
        $data['post_content_sync_backup_restore'] = true;
        $data['post_content_sync_automatic'] = false;
        $response->set_data( $data );
        return $response;
    }

    private static function state( int $id ): array {
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return [ 'page_id' => $id, 'exists' => false ];
        $elements = self::elements_for_post( $id );
        $content_hash = is_array( $elements ) ? self::elements_hash( $elements ) : '';
        $post_content = (string) $post->post_content;
        $backup = get_post_meta( $id, self::META_BACKUP, true );
        $backup_hash = (string) get_post_meta( $id, self::META_BACKUP_HASH, true );
        $backup_valid = is_string( $backup ) && '' !== $backup && '' !== $backup_hash && hash_equals( self::post_content_hash( $backup ), $backup_hash );

        return [
            'page_id' => $id,
            'exists' => true,
            'title' => (string) $post->post_title,
            'status' => (string) $post->post_status,
            'elementize_managed' => self::is_managed( $id ),
            'is_elementor' => is_array( $elements ),
            'content_hash' => $content_hash,
            'post_content_length' => strlen( $post_content ),
            'post_content_hash' => self::post_content_hash( $post_content ),
            'post_content_empty' => '' === $post_content,
            'backup_available' => $backup_valid,
            'backup_length' => $backup_valid ? strlen( $backup ) : 0,
            'backup_hash' => $backup_valid ? $backup_hash : '',
            'backup_at_gmt' => $backup_valid ? (string) get_post_meta( $id, self::META_BACKUP_AT, true ) : '',
            'allowed_operations' => self::is_managed( $id ) && 'draft' === $post->post_status ? [ 'clear', 'restore_backup' ] : [],
            'read_only' => true,
        ];
    }

    private static function is_managed( int $id ): bool {
        return '1' === (string) get_post_meta( $id, self::META_MANAGED, true );
    }

    private static function elements_for_post( int $id ): ?array {
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $document = \Elementor\Plugin::$instance->documents->get( $id );
                if ( $document ) {
                    $elements = $document->get_elements_data();
                    if ( is_array( $elements ) ) return $elements;
                }
            } catch ( Throwable $exception ) {}
        }
        $raw = get_post_meta( $id, '_elementor_data', true );
        if ( is_array( $raw ) ) return $raw;
        if ( is_string( $raw ) && '' !== $raw ) {
            $decoded = json_decode( $raw, true );
            if ( is_array( $decoded ) ) return $decoded;
        }
        return null;
    }

    private static function elements_hash( array $elements ): string {
        return hash( 'sha256', wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) );
    }

    private static function post_content_hash( string $content ): string {
        return hash( 'sha256', $content );
    }

    private static function save_revision( int $id ): ?int {
        if ( ! function_exists( 'wp_save_post_revision' ) ) return null;
        add_filter( 'wp_save_post_revision_check_for_changes', '__return_false' );
        try {
            $revision = wp_save_post_revision( $id );
        } finally {
            remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false' );
        }
        return is_wp_error( $revision ) || ! $revision ? null : absint( $revision );
    }

    private static function restore_exact_content( int $id, string $content ): void {
        wp_update_post( [ 'ID' => $id, 'post_content' => $content ] );
        clean_post_cache( $id );
    }
}

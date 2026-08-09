<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Lifecycle {
    private const VERSION = '0.4.0';
    private const NS = 'elementize/v1';
    private const META_MANAGED = '_elementize_managed';
    private const META_VERSION = '_elementize_managed_version';
    private const META_CREATED_AT = '_elementize_managed_at_gmt';

    public static function init(): void {
        if ( class_exists( 'Elementize_Visual_Rendering', false ) ) {
            remove_action( 'admin_menu', [ Elementize_Visual_Rendering::class, 'menu' ] );
        }
        add_action( 'admin_menu', [ self::class, 'menu' ] );
        add_action( 'rest_api_init', [ self::class, 'routes' ], 40 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 40, 3 );
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', 'elementize', [ self::class, 'admin' ], 'dashicons-admin-tools', 58 );
    }

    public static function admin(): void {
        ob_start();
        if ( class_exists( 'Elementize_Visual_Rendering', false ) ) Elementize_Visual_Rendering::admin();
        else Elementize_Visual_Writes::admin();
        $html = (string) ob_get_clean();
        $html = preg_replace( '/Version 0\.3\.[0-9.]+/', 'Version ' . self::VERSION, $html, 1 );
        $card = '<div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Lifecycle safeguards</h2><p>0.4.0 marks newly created Elementize drafts as managed and adds guarded lifecycle read, trash, restore, publish, and unpublish endpoints. Hard delete is intentionally unavailable. Trash is draft-only; publish/unpublish require explicit confirmation plus fresh lifecycle and content hashes.</p></div>';
        $last = strrpos( $html, '</div>' );
        if ( false !== $last ) $html = substr( $html, 0, $last ) . $card . substr( $html, $last );
        echo $html; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/lifecycle', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'lifecycle' ],
            'permission_callback' => [ self::class, 'can_manage_lifecycle' ],
            'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
        ] );

        foreach ( [ 'trash', 'restore', 'publish', 'unpublish' ] as $action ) {
            register_rest_route( self::NS, '/pages/(?P<id>\\d+)/' . $action, [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, $action ],
                'permission_callback' => [ self::class, 'can_manage_lifecycle' ],
                'args' => self::mutation_args( $action ),
            ] );
        }
    }

    private static function mutation_args( string $action ): array {
        $args = [
            'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
            'expected_status' => [ 'type' => 'string', 'required' => true ],
            'expected_title' => [ 'type' => 'string', 'required' => true ],
            'lifecycle_token' => [ 'type' => 'string', 'required' => true ],
            'confirm_' . $action => [ 'type' => 'boolean', 'required' => true ],
        ];
        if ( in_array( $action, [ 'trash', 'publish', 'unpublish' ], true ) ) {
            $args['content_hash'] = [ 'type' => 'string', 'required' => true ];
        }
        return $args;
    }

    public static function can_manage_lifecycle( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_lifecycle_forbidden', 'Elementize lifecycle changes require an administrator.', [ 'status' => 403 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::annotate_status( $response );

        if ( '/' . self::NS . '/pages/create' !== $route || 'POST' !== strtoupper( $request->get_method() ) ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['created'] ) || empty( $data['page']['id'] ) ) return $response;

        $id = absint( $data['page']['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type || 'draft' !== $post->post_status ) return $response;

        update_post_meta( $id, self::META_MANAGED, '1' );
        update_post_meta( $id, self::META_VERSION, self::VERSION );
        update_post_meta( $id, self::META_CREATED_AT, gmdate( 'Y-m-d H:i:s' ) );
        $data['elementize_managed'] = true;
        $data['lifecycle'] = self::snapshot( $id );
        $response->set_data( $data );
        return $response;
    }

    private static function annotate_status( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['lifecycle_management'] = true;
        $data['lifecycle_managed_pages_only'] = true;
        $data['lifecycle_actions'] = [ 'read', 'trash-draft-only', 'restore', 'publish', 'unpublish' ];
        $data['hard_delete_available'] = false;
        $response->set_data( $data );
        return $response;
    }

    public static function lifecycle( WP_REST_Request $request ) {
        return new WP_REST_Response( self::snapshot( absint( $request['id'] ) ), 200 );
    }

    public static function trash( WP_REST_Request $request ) {
        $guard = self::guard( $request, 'draft', 'trash', true );
        if ( is_wp_error( $guard ) ) return $guard;
        $id = $guard['id'];
        $trashed = wp_trash_post( $id );
        if ( ! $trashed || 'trash' !== get_post_status( $id ) ) return new WP_Error( 'elementize_trash_failed', 'WordPress did not move the managed draft page to Trash.', [ 'status' => 500 ] );
        return new WP_REST_Response( [
            'trashed' => true,
            'page_id' => $id,
            'previous_status' => 'draft',
            'status' => 'trash',
            'hard_deleted' => false,
            'restore_available' => true,
            'lifecycle' => self::snapshot( $id ),
        ], 200 );
    }

    public static function restore( WP_REST_Request $request ) {
        $guard = self::guard( $request, 'trash', 'restore', false );
        if ( is_wp_error( $guard ) ) return $guard;
        $id = $guard['id'];
        $restored = wp_untrash_post( $id );
        if ( ! $restored || 'draft' !== get_post_status( $id ) ) return new WP_Error( 'elementize_restore_failed', 'WordPress did not restore the managed page from Trash back to draft.', [ 'status' => 500 ] );
        return new WP_REST_Response( [
            'restored' => true,
            'page_id' => $id,
            'previous_status' => 'trash',
            'status' => 'draft',
            'lifecycle' => self::snapshot( $id ),
        ], 200 );
    }

    public static function publish( WP_REST_Request $request ) {
        $guard = self::guard( $request, 'draft', 'publish', true );
        if ( is_wp_error( $guard ) ) return $guard;
        $id = $guard['id'];
        $content = self::content_state( $id );
        if ( empty( $content['is_elementor'] ) || (int) $content['top_level_count'] < 1 ) {
            return new WP_Error( 'elementize_publish_empty_page', 'Elementize will not publish a managed page that has no verified Elementor content.', [ 'status' => 400 ] );
        }
        $revision = self::save_revision( $id );
        $updated = wp_update_post( [ 'ID' => $id, 'post_status' => 'publish' ], true );
        if ( is_wp_error( $updated ) ) return new WP_Error( 'elementize_publish_failed', 'WordPress could not publish the managed page: ' . $updated->get_error_message(), [ 'status' => 500 ] );
        if ( 'publish' !== get_post_status( $id ) ) return new WP_Error( 'elementize_publish_verification_failed', 'The page status was not verified as published.', [ 'status' => 500 ] );
        return new WP_REST_Response( [
            'published' => true,
            'page_id' => $id,
            'previous_status' => 'draft',
            'status' => 'publish',
            'revision_id' => $revision,
            'permalink' => get_permalink( $id ),
            'lifecycle' => self::snapshot( $id ),
        ], 200 );
    }

    public static function unpublish( WP_REST_Request $request ) {
        $guard = self::guard( $request, 'publish', 'unpublish', true );
        if ( is_wp_error( $guard ) ) return $guard;
        $id = $guard['id'];
        $revision = self::save_revision( $id );
        $updated = wp_update_post( [ 'ID' => $id, 'post_status' => 'draft' ], true );
        if ( is_wp_error( $updated ) ) return new WP_Error( 'elementize_unpublish_failed', 'WordPress could not return the managed page to draft: ' . $updated->get_error_message(), [ 'status' => 500 ] );
        if ( 'draft' !== get_post_status( $id ) ) return new WP_Error( 'elementize_unpublish_verification_failed', 'The page status was not verified as draft.', [ 'status' => 500 ] );
        return new WP_REST_Response( [
            'unpublished' => true,
            'page_id' => $id,
            'previous_status' => 'publish',
            'status' => 'draft',
            'revision_id' => $revision,
            'preview_url' => get_preview_post_link( $id ),
            'lifecycle' => self::snapshot( $id ),
        ], 200 );
    }

    private static function guard( WP_REST_Request $request, string $required_status, string $action, bool $require_content_hash ) {
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! self::is_managed( $id ) ) return new WP_Error( 'elementize_unmanaged_page', 'This page is not marked as Elementize-managed. Lifecycle mutations are blocked.', [ 'status' => 409 ] );
        if ( true !== $request->get_param( 'confirm_' . $action ) ) return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_' . $action . '=true is required.', [ 'status' => 400 ] );

        $expected_status = (string) $request->get_param( 'expected_status' );
        if ( $expected_status !== $required_status || $post->post_status !== $required_status ) {
            return new WP_Error( 'elementize_status_changed', 'The page status is not the expected lifecycle state.', [ 'status' => 409, 'status_now' => $post->post_status ] );
        }

        $expected_title = (string) $request->get_param( 'expected_title' );
        if ( $expected_title !== (string) $post->post_title ) {
            return new WP_Error( 'elementize_title_changed', 'The page title changed after it was read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        }

        $expected_token = (string) $request->get_param( 'lifecycle_token' );
        $current_token = self::token( $post );
        if ( '' === $expected_token || ! hash_equals( $current_token, $expected_token ) ) {
            return new WP_Error( 'elementize_lifecycle_changed', 'The page lifecycle state changed after it was read. Read lifecycle again before applying this action.', [ 'status' => 409, 'lifecycle_token' => $current_token ] );
        }

        if ( $require_content_hash ) {
            $state = self::content_state( $id );
            $expected_hash = (string) $request->get_param( 'content_hash' );
            $current_hash = (string) ( $state['content_hash'] ?? '' );
            if ( '' === $expected_hash || '' === $current_hash || ! hash_equals( $current_hash, $expected_hash ) ) {
                return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read. Read lifecycle again before applying this action.', [ 'status' => 409, 'content_hash' => $current_hash ] );
            }
        }

        return [ 'id' => $id, 'post' => $post ];
    }

    private static function snapshot( int $id ): array {
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return [ 'page_id' => $id, 'exists' => false ];
        $content = self::content_state( $id );
        return [
            'page_id' => $id,
            'exists' => true,
            'title' => (string) $post->post_title,
            'status' => (string) $post->post_status,
            'elementize_managed' => self::is_managed( $id ),
            'managed_version' => (string) get_post_meta( $id, self::META_VERSION, true ),
            'managed_at_gmt' => (string) get_post_meta( $id, self::META_CREATED_AT, true ),
            'created_gmt' => (string) $post->post_date_gmt,
            'modified_gmt' => (string) $post->post_modified_gmt,
            'lifecycle_token' => self::token( $post ),
            'content_hash' => $content['content_hash'],
            'is_elementor' => $content['is_elementor'],
            'top_level_count' => $content['top_level_count'],
            'preview_url' => 'trash' === $post->post_status ? null : get_preview_post_link( $id ),
            'permalink' => 'publish' === $post->post_status ? get_permalink( $id ) : null,
            'allowed_actions' => self::allowed_actions( $id, (string) $post->post_status, $content ),
        ];
    }

    private static function allowed_actions( int $id, string $status, array $content ): array {
        if ( ! self::is_managed( $id ) ) return [];
        if ( 'trash' === $status ) return [ 'restore' ];
        if ( 'draft' === $status ) {
            $actions = [ 'trash' ];
            if ( ! empty( $content['is_elementor'] ) && (int) $content['top_level_count'] > 0 ) $actions[] = 'publish';
            return $actions;
        }
        if ( 'publish' === $status ) return [ 'unpublish' ];
        return [];
    }

    private static function content_state( int $id ): array {
        $elements = null;
        $is_elementor = false;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $document = \Elementor\Plugin::$instance->documents->get( $id );
                if ( $document ) {
                    $is_elementor = (bool) $document->is_built_with_elementor();
                    $candidate = $document->get_elements_data();
                    if ( is_array( $candidate ) ) $elements = $candidate;
                }
            } catch ( Throwable $exception ) {
                $elements = null;
            }
        }
        if ( ! is_array( $elements ) ) {
            $raw = get_post_meta( $id, '_elementor_data', true );
            if ( is_string( $raw ) && '' !== $raw ) {
                $decoded = json_decode( $raw, true );
                if ( is_array( $decoded ) ) $elements = $decoded;
            } elseif ( is_array( $raw ) ) {
                $elements = $raw;
            }
        }
        if ( ! is_array( $elements ) ) $elements = [];
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return [
            'content_hash' => hash( 'sha256', false === $json ? '' : $json ),
            'is_elementor' => $is_elementor || 'builder' === get_post_meta( $id, '_elementor_edit_mode', true ),
            'top_level_count' => count( $elements ),
        ];
    }

    private static function token( WP_Post $post ): string {
        $payload = implode( '|', [
            (string) $post->ID,
            (string) $post->post_status,
            (string) $post->post_title,
            (string) $post->post_modified_gmt,
            (string) get_post_meta( $post->ID, self::META_MANAGED, true ),
            (string) get_post_meta( $post->ID, self::META_VERSION, true ),
        ] );
        return hash( 'sha256', $payload );
    }

    private static function is_managed( int $id ): bool {
        return '1' === (string) get_post_meta( $id, self::META_MANAGED, true );
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
}

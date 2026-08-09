<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Lifecycle', false ) ) return;

final class Elementize_Page_Layout {
    private const VERSION = '0.5.0';
    private const NS = 'elementize/v1';
    private const META_MANAGED = '_elementize_managed';
    private const META_TEMPLATE = '_wp_page_template';
    private const TEMPLATE_SITE = 'elementor_theme';
    private const TEMPLATE_STANDALONE = 'elementor_canvas';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 50 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 70, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/layout', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'read' ],
                'permission_callback' => [ self::class, 'can_read' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update' ],
                'permission_callback' => [ self::class, 'can_write' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'expected_status' => [ 'type' => 'string', 'required' => true ],
                    'expected_title' => [ 'type' => 'string', 'required' => true ],
                    'expected_mode' => [ 'type' => 'string', 'required' => true ],
                    'layout_token' => [ 'type' => 'string', 'required' => true ],
                    'content_hash' => [ 'type' => 'string', 'required' => true ],
                    'mode' => [ 'type' => 'string', 'required' => true ],
                    'confirm_layout_change' => [ 'type' => 'boolean', 'required' => true ],
                ],
            ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function can_write( WP_REST_Request $request ) {
        $read = self::can_read( $request );
        if ( true !== $read ) return $read;
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_layout_forbidden', 'Elementize page layout changes require an administrator.', [ 'status' => 403 ] );
        return true;
    }

    public static function read( WP_REST_Request $request ) {
        return new WP_REST_Response( self::snapshot( absint( $request['id'] ) ), 200 );
    }

    public static function update( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! self::is_managed( $id ) ) return new WP_Error( 'elementize_unmanaged_page', 'Page layout writes are allowed only for Elementize-managed pages.', [ 'status' => 409 ] );
        if ( 'draft' !== $post->post_status || 'draft' !== (string) $request->get_param( 'expected_status' ) ) {
            return new WP_Error( 'elementize_layout_draft_only', 'Page layout changes are draft-only. Unpublish the managed page first.', [ 'status' => 409, 'status_now' => $post->post_status ] );
        }
        if ( (string) $request->get_param( 'expected_title' ) !== (string) $post->post_title ) {
            return new WP_Error( 'elementize_title_changed', 'The page title changed after the layout was read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        }
        if ( true !== $request->get_param( 'confirm_layout_change' ) ) {
            return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_layout_change=true is required.', [ 'status' => 400 ] );
        }

        $before = self::snapshot( $id );
        $expected_mode = strtolower( trim( (string) $request->get_param( 'expected_mode' ) ) );
        if ( $expected_mode !== (string) $before['mode'] ) {
            return new WP_Error( 'elementize_layout_changed', 'The page layout mode changed after it was read.', [ 'status' => 409, 'mode_now' => $before['mode'], 'layout_token' => $before['layout_token'] ] );
        }
        $expected_token = (string) $request->get_param( 'layout_token' );
        if ( '' === $expected_token || ! hash_equals( (string) $before['layout_token'], $expected_token ) ) {
            return new WP_Error( 'elementize_layout_changed', 'The page layout state changed after it was read. Read the layout again.', [ 'status' => 409, 'layout_token' => $before['layout_token'] ] );
        }
        $expected_hash = (string) $request->get_param( 'content_hash' );
        if ( '' === $expected_hash || ! hash_equals( (string) $before['content_hash'], $expected_hash ) ) {
            return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after the layout was read. Read the layout again.', [ 'status' => 409, 'content_hash' => $before['content_hash'] ] );
        }

        $mode = strtolower( trim( (string) $request->get_param( 'mode' ) ) );
        if ( ! in_array( $mode, [ 'site', 'standalone' ], true ) ) {
            return new WP_Error( 'elementize_invalid_layout_mode', 'mode must be site or standalone.', [ 'status' => 400 ] );
        }
        if ( $mode === $before['mode'] ) {
            return new WP_REST_Response( [
                'saved' => false,
                'changed' => false,
                'page_id' => $id,
                'previous_mode' => $before['mode'],
                'mode' => $before['mode'],
                'layout' => $before,
            ], 200 );
        }

        $target = 'standalone' === $mode ? self::TEMPLATE_STANDALONE : self::TEMPLATE_SITE;
        $revision = self::save_revision( $id );
        $updated = update_post_meta( $id, self::META_TEMPLATE, $target );
        if ( false === $updated && $target !== (string) get_post_meta( $id, self::META_TEMPLATE, true ) ) {
            return new WP_Error( 'elementize_layout_save_failed', 'WordPress could not save the requested page layout.', [ 'status' => 500 ] );
        }

        clean_post_cache( $id );
        $after = self::snapshot( $id );
        if ( $mode !== $after['mode'] || $target !== $after['elementor_template'] ) {
            update_post_meta( $id, self::META_TEMPLATE, (string) $before['elementor_template'] );
            clean_post_cache( $id );
            return new WP_Error( 'elementize_layout_verification_failed', 'The requested page layout could not be verified and the previous value was restored.', [ 'status' => 500 ] );
        }

        return new WP_REST_Response( [
            'saved' => true,
            'changed' => true,
            'page_id' => $id,
            'previous_mode' => $before['mode'],
            'mode' => $after['mode'],
            'previous_elementor_template' => $before['elementor_template'],
            'elementor_template' => $after['elementor_template'],
            'revision_id' => $revision,
            'content_hash' => $after['content_hash'],
            'layout_token' => $after['layout_token'],
            'preview_url' => $after['preview_url'],
        ], 200 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['page_layout_management'] = true;
        $data['page_layout_modes'] = [ 'site', 'standalone' ];
        $data['page_layout_write_managed_pages_only'] = true;
        $data['page_layout_write_draft_only'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function snapshot( int $id ): array {
        $post = get_post( $id );
        $raw = (string) get_post_meta( $id, self::META_TEMPLATE, true );
        if ( '' === $raw ) $raw = 'default';
        $mode = self::mode_from_template( $raw );
        $content_hash = self::content_hash( $id );
        return [
            'page_id' => $id,
            'title' => $post ? (string) $post->post_title : '',
            'status' => $post ? (string) $post->post_status : '',
            'elementize_managed' => self::is_managed( $id ),
            'mode' => $mode,
            'elementor_template' => $raw,
            'supported_modes' => [ 'site', 'standalone' ],
            'layout_token' => self::token( $post, $raw ),
            'content_hash' => $content_hash,
            'preview_url' => $post && 'trash' !== $post->post_status ? get_preview_post_link( $id ) : null,
        ];
    }

    private static function mode_from_template( string $template ): string {
        if ( self::TEMPLATE_SITE === $template ) return 'site';
        if ( self::TEMPLATE_STANDALONE === $template ) return 'standalone';
        return 'default';
    }

    private static function token( ?WP_Post $post, string $template ): string {
        if ( ! $post ) return '';
        return hash( 'sha256', implode( '|', [
            (string) $post->ID,
            (string) $post->post_status,
            (string) $post->post_title,
            (string) $post->post_modified_gmt,
            $template,
            (string) get_post_meta( $post->ID, self::META_MANAGED, true ),
        ] ) );
    }

    private static function is_managed( int $id ): bool {
        return '1' === (string) get_post_meta( $id, self::META_MANAGED, true );
    }

    private static function content_hash( int $id ): string {
        $elements = null;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $document = \Elementor\Plugin::$instance->documents->get( $id );
                if ( $document ) {
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
        return hash( 'sha256', false === $json ? '' : $json );
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

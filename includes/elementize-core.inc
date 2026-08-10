<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Core {
    public const NS = 'elementize/v1';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ] );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/status', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'status' ],
            'permission_callback' => [ self::class, 'can_access' ],
        ] );

        register_rest_route( self::NS, '/pages', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'pages' ],
            'permission_callback' => [ self::class, 'can_access' ],
            'args' => [
                'q' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'page' => [ 'type' => 'integer', 'required' => false, 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'per_page' => [ 'type' => 'integer', 'required' => false, 'default' => 30, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function can_access() {
        if ( ! is_user_logged_in() ) {
            return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        }
        if ( ! current_user_can( 'edit_pages' ) ) {
            return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit pages.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function can_edit_page( WP_REST_Request $request ) {
        $access = self::can_access();
        if ( is_wp_error( $access ) ) return $access;

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

    public static function can_write_page( WP_REST_Request $request ) {
        $access = self::can_edit_page( $request );
        if ( is_wp_error( $access ) ) return $access;
        if ( ! current_user_can( 'manage_options' ) ) {
            return new WP_Error( 'elementize_write_forbidden', 'Elementize content writes require an administrator.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function status(): WP_REST_Response {
        $pixfort = self::pixfort_info();
        $theme = wp_get_theme();
        $elementor_loaded = class_exists( '\\Elementor\\Plugin' );
        $pixfort_loaded = ! empty( $pixfort['active'] );

        return new WP_REST_Response( [
            'elementize_version' => defined( 'ELEMENTIZE_VERSION' ) ? ELEMENTIZE_VERSION : 'unknown',
            'scope' => 'elementor_pixfort_content_only',
            'ready' => $elementor_loaded && $pixfort_loaded,
            'wordpress_version' => get_bloginfo( 'version' ),
            'theme' => $theme->get( 'Name' ),
            'theme_version' => $theme->get( 'Version' ),
            'elementor_loaded' => $elementor_loaded,
            'elementor_version' => defined( 'ELEMENTOR_VERSION' ) ? ELEMENTOR_VERSION : null,
            'pixfort_loaded' => $pixfort_loaded,
            'pixfort_name' => $pixfort_loaded ? $pixfort['name'] : null,
            'pixfort_version' => $pixfort_loaded ? $pixfort['version'] : null,
            'supported_content_types' => [ 'text', 'link', 'media', 'pixfort_icon' ],
            'media_library_search' => true,
            'conversation_image_import' => true,
            'page_creation' => false,
            'template_insertion' => false,
            'layout_controls' => false,
            'design_controls' => false,
            'color_controls' => false,
            'page_lifecycle_controls' => false,
            'visual_ai' => false,
            'ollama_required' => false,
            'shared_template_writes' => false,
            'write_guards' => [
                'fresh_content_hash' => true,
                'expected_page_status' => true,
                'expected_page_title' => true,
                'expected_field_value' => true,
                'pre_change_revision' => true,
                'post_save_verification' => true,
                'published_page_confirmation' => true,
            ],
            'setup_url' => admin_url( 'admin.php?page=elementize' ),
        ], 200 );
    }

    public static function pages( WP_REST_Request $request ): WP_REST_Response {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) {
            return new WP_REST_Response( [
                'page' => 1,
                'per_page' => 0,
                'total' => 0,
                'total_pages' => 0,
                'pages' => [],
                'warning' => 'Elementor is not active.',
            ], 200 );
        }

        $page = max( 1, absint( $request->get_param( 'page' ) ) );
        $per_page = min( 100, max( 1, absint( $request->get_param( 'per_page' ) ) ) );
        $search = trim( (string) $request->get_param( 'q' ) );

        $args = [
            'post_type' => 'page',
            'post_status' => array_keys( get_post_stati( [ 'internal' => false ] ) ),
            'perm' => 'editable',
            'posts_per_page' => $per_page,
            'paged' => $page,
            'orderby' => 'modified',
            'order' => 'DESC',
            'meta_query' => [
                [
                    'key' => '_elementor_edit_mode',
                    'value' => 'builder',
                    'compare' => '=',
                ],
            ],
        ];
        if ( '' !== $search ) $args['s'] = $search;

        $query = new WP_Query( $args );
        $items = [];
        foreach ( $query->posts as $post ) {
            if ( ! $post instanceof WP_Post || ! current_user_can( 'edit_post', $post->ID ) ) continue;
            $document = self::document( $post->ID );
            if ( ! $document || ! $document->is_built_with_elementor() ) continue;
            $items[] = self::page_identity( $post->ID, $document );
        }

        return new WP_REST_Response( [
            'q' => $search,
            'page' => $page,
            'per_page' => $per_page,
            'total' => (int) $query->found_posts,
            'total_pages' => (int) $query->max_num_pages,
            'pages' => $items,
        ], 200 );
    }

    public static function page_identity( int $id, $document = null ): array {
        $post = get_post( $id );
        if ( ! $document ) $document = self::document( $id );

        return [
            'id' => $id,
            'title' => $post ? (string) $post->post_title : '',
            'status' => $post ? (string) $post->post_status : '',
            'modified_gmt' => $post ? (string) $post->post_modified_gmt : '',
            'permalink' => get_permalink( $id ),
            'preview_url' => get_preview_post_link( $id ),
            'elementor_edit_url' => $document && method_exists( $document, 'get_edit_url' ) ? $document->get_edit_url() : null,
        ];
    }

    public static function document( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->documents ) ) return null;
        try {
            return \Elementor\Plugin::$instance->documents->get( $id );
        } catch ( Throwable $e ) {
            return null;
        }
    }

    public static function need_document( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) {
            return new WP_Error( 'elementize_elementor_required', 'Elementor must be installed and active.', [ 'status' => 503 ] );
        }
        $document = self::document( $id );
        if ( ! $document ) {
            return new WP_Error( 'elementize_elementor_document_not_found', 'Elementor could not load this page as a document.', [ 'status' => 404 ] );
        }
        if ( ! $document->is_built_with_elementor() ) {
            return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        }
        return $document;
    }

    public static function elements( int $id ) {
        $document = self::need_document( $id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) {
            return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
        }
        return [ 'document' => $document, 'elements' => $elements ];
    }

    public static function hash_elements( array $elements ): string {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return hash( 'sha256', false === $json ? '' : $json );
    }

    public static function save_revision( int $id ) {
        $post = get_post( $id );
        if ( ! $post || ! post_type_supports( $post->post_type, 'revisions' ) || ! wp_revisions_enabled( $post ) ) return null;

        add_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        try {
            $revision = wp_save_post_revision( $id );
        } finally {
            remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        }

        if ( is_int( $revision ) && $revision > 0 ) return $revision;
        return new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the page was not modified.', [ 'status' => 500 ] );
    }

    public static function regenerate_css( int $id ): void {
        try {
            if ( class_exists( '\\Elementor\\Core\\Files\\CSS\\Post' ) ) {
                $css = new \Elementor\Core\Files\CSS\Post( $id );
                $css->update();
            }
        } catch ( Throwable $ignored ) {
        }
    }

    public static function pixfort_info(): array {
        if ( ! function_exists( 'get_plugins' ) ) require_once ABSPATH . 'wp-admin/includes/plugin.php';
        $active = (array) get_option( 'active_plugins', [] );
        if ( is_multisite() ) {
            $active = array_merge( $active, array_keys( (array) get_site_option( 'active_sitewide_plugins', [] ) ) );
        }
        $plugins = get_plugins();
        foreach ( array_unique( $active ) as $file ) {
            $data = $plugins[ $file ] ?? [];
            $name = (string) ( $data['Name'] ?? '' );
            if ( false === stripos( $file, 'pixfort-core' ) && false === stripos( $name, 'pixfort core' ) ) continue;
            return [
                'active' => true,
                'name' => '' !== $name ? $name : 'Pixfort Core',
                'version' => (string) ( $data['Version'] ?? '' ),
            ];
        }
        return [ 'active' => false, 'name' => '', 'version' => '' ];
    }
}

<?php
/**
 * Plugin Name: Elementize
 * Description: Controlled REST access to WordPress, Elementor, and Pixfort.
 * Version: 0.2.4
 * Requires at least: 6.5
 * Requires PHP: 8.0
 * Requires Plugins: elementor
 * Author: Mazbac
 * License: GPL-2.0-or-later
 */
if ( ! defined( 'ABSPATH' ) ) exit;

if ( ! class_exists( 'Elementize_Plugin', false ) ) {
final class Elementize_Plugin {
    private const VERSION = '0.2.4';
    private const NS = 'elementize/v1';
    private const MAX_TEXT = 20000;
    private const MAX_UPDATES = 100;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ] );
        add_action( 'admin_menu', [ self::class, 'menu' ] );
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', 'elementize', [ self::class, 'admin' ], 'dashicons-admin-tools', 58 );
    }

    public static function admin(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;
        $el = class_exists( '\\Elementor\\Plugin' );
        $px = self::pixfort_info();
        $theme = wp_get_theme();
        $lib = file_exists( self::library_loader() );
        ?>
        <div class="wrap"><h1>Elementize</h1><p>Controlled WordPress, Elementor, and Pixfort access for conversational tools.</p>
        <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px;max-width:1000px;margin-top:20px;">
        <?php
        self::card( 'Elementize', true, 'Version ' . self::VERSION );
        self::card( 'Elementor', $el, $el && defined( 'ELEMENTOR_VERSION' ) ? 'Version ' . ELEMENTOR_VERSION : 'Not detected' );
        self::card( 'Pixfort Core', $px['active'], $px['active'] ? trim( $px['name'] . ' ' . $px['version'] ) : 'Not detected' );
        self::card( 'Pixfort Library', $lib, $lib ? 'Catalogue source detected' : 'Catalogue source not detected' );
        self::card( 'REST API', true, 'Endpoints registered' );
        ?>
        </div>
        <div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Environment</h2><table class="widefat striped"><tbody>
        <tr><td><strong>WordPress</strong></td><td><?php echo esc_html( get_bloginfo( 'version' ) ); ?></td></tr>
        <tr><td><strong>Theme</strong></td><td><?php echo esc_html( trim( $theme->get( 'Name' ) . ' ' . $theme->get( 'Version' ) ) ); ?></td></tr>
        <tr><td><strong>Pixfort catalogue endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pixfort/templates' ) ); ?></code></td></tr>
        <tr><td><strong>Pixfort insert endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pages/{id}/pixfort/insert' ) ); ?></code></td></tr>
        </tbody></table></div>
        <div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Current test state</h2><p><strong>Copy editing and first Pixfort insertion are proven.</strong> 0.2.4 tightens copy detection and adds guarded start/end/before/after section positioning.</p></div>
        </div><?php
    }

    private static function card( string $label, bool $ok, string $detail ): void {
        $bg = $ok ? '#edfaef' : '#fcf0f1';
        $c = $ok ? '#116329' : '#8a2424';
        $t = $ok ? 'Detected' : 'Attention';
        echo '<div class="card" style="margin:0;padding:20px"><div style="display:flex;justify-content:space-between"><h2 style="margin:0">' . esc_html( $label ) . '</h2><span style="padding:4px 8px;border-radius:999px;background:' . esc_attr( $bg ) . ';color:' . esc_attr( $c ) . ';font-weight:600">' . esc_html( $t ) . '</span></div><p style="margin-bottom:0">' . esc_html( $detail ) . '</p></div>';
    }

    private static function pixfort_info(): array {
        if ( ! function_exists( 'get_plugins' ) ) require_once ABSPATH . 'wp-admin/includes/plugin.php';
        $active = (array) get_option( 'active_plugins', [] );
        if ( is_multisite() ) $active = array_merge( $active, array_keys( (array) get_site_option( 'active_sitewide_plugins', [] ) ) );
        $plugins = get_plugins();
        foreach ( array_unique( $active ) as $file ) {
            $data = $plugins[ $file ] ?? [];
            $name = (string) ( $data['Name'] ?? '' );
            if ( stripos( $file, 'pixfort-core' ) === false && stripos( $name, 'pixfort core' ) === false ) continue;
            return [ 'active' => true, 'name' => $name ?: 'Pixfort Core', 'version' => (string) ( $data['Version'] ?? '' ) ];
        }
        return [ 'active' => false, 'name' => '', 'version' => '' ];
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
                'page' => [ 'type' => 'integer', 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'per_page' => [ 'type' => 'integer', 'default' => 50, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
            ],
        ] );
        register_rest_route( self::NS, '/pixfort/templates', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'templates' ],
            'permission_callback' => [ self::class, 'can_manage_pixfort' ],
            'args' => [
                'type' => [ 'type' => 'string', 'default' => 'section', 'sanitize_callback' => 'sanitize_key' ],
                'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'category' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_key' ],
                'page' => [ 'type' => 'integer', 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'per_page' => [ 'type' => 'integer', 'default' => 50, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
            ],
        ] );
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/pixfort/insert', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'insert_template' ],
            'permission_callback' => [ self::class, 'can_manage_pixfort_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'content_hash' => [ 'type' => 'string', 'required' => true ],
                'template_id' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_key' ],
                'position' => [ 'type' => 'string', 'default' => 'end', 'sanitize_callback' => 'sanitize_key' ],
                'target_element_id' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
            ],
        ] );
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/text', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'page_text' ],
                'permission_callback' => [ self::class, 'can_edit_page' ],
                'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update_text' ],
                'permission_callback' => [ self::class, 'can_edit_page' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'content_hash' => [ 'type' => 'string', 'required' => true ],
                    'updates' => [ 'type' => 'array', 'required' => true ],
                ],
            ],
        ] );
    }

    public static function can_access() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'edit_pages' ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit pages.', [ 'status' => 403 ] );
        return true;
    }

    public static function can_manage_pixfort() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_pixfort_forbidden', 'Pixfort library access requires an administrator.', [ 'status' => 403 ] );
        return true;
    }

    public static function can_edit_page( WP_REST_Request $request ) {
        $permission = self::can_access();
        return is_wp_error( $permission ) ? $permission : self::page_permission( $request );
    }

    public static function can_manage_pixfort_page( WP_REST_Request $request ) {
        $permission = self::can_manage_pixfort();
        return is_wp_error( $permission ) ? $permission : self::page_permission( $request );
    }

    private static function page_permission( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || $post->post_type !== 'page' ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function status(): WP_REST_Response {
        $pixfort = self::pixfort_info();
        $theme = wp_get_theme();
        return new WP_REST_Response( [
            'elementize_version' => self::VERSION,
            'wordpress_version' => get_bloginfo( 'version' ),
            'elementor_loaded' => class_exists( '\\Elementor\\Plugin' ),
            'elementor_version' => defined( 'ELEMENTOR_VERSION' ) ? ELEMENTOR_VERSION : null,
            'pixfort_loaded' => $pixfort['active'],
            'pixfort_name' => $pixfort['active'] ? $pixfort['name'] : null,
            'pixfort_version' => $pixfort['active'] ? $pixfort['version'] : null,
            'pixfort_library_detected' => file_exists( self::library_loader() ),
            'pixfort_source_detected' => file_exists( self::source_path() ),
            'theme' => $theme->get( 'Name' ),
            'theme_version' => $theme->get( 'Version' ),
        ], 200 );
    }

    public static function pages( WP_REST_Request $request ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return self::el_missing();
        $page = max( 1, absint( $request->get_param( 'page' ) ) );
        $per_page = min( 100, max( 1, absint( $request->get_param( 'per_page' ) ) ) );
        $query = new WP_Query( [
            'post_type' => 'page',
            'post_status' => array_keys( get_post_stati( [ 'internal' => false ] ) ),
            'perm' => 'editable',
            'posts_per_page' => $per_page,
            'paged' => $page,
            'orderby' => 'modified',
            'order' => 'DESC',
        ] );
        $out = [];
        foreach ( $query->posts as $post ) {
            if ( ! current_user_can( 'edit_post', $post->ID ) ) continue;
            $document = self::doc( $post->ID );
            $is_elementor = $document && $document->is_built_with_elementor();
            $out[] = [
                'id' => $post->ID,
                'title' => get_the_title( $post ),
                'status' => $post->post_status,
                'modified_gmt' => $post->post_modified_gmt,
                'url' => get_permalink( $post ),
                'is_elementor' => (bool) $is_elementor,
                'elementor_edit_url' => $is_elementor ? $document->get_edit_url() : null,
            ];
        }
        return new WP_REST_Response( [
            'page' => $page,
            'per_page' => $per_page,
            'total' => (int) $query->found_posts,
            'total_pages' => (int) $query->max_num_pages,
            'pages' => $out,
        ], 200 );
    }

    public static function templates( WP_REST_Request $request ) {
        $library = self::library();
        if ( is_wp_error( $library ) ) return $library;
        $type = strtolower( (string) $request->get_param( 'type' ) ) ?: 'section';
        if ( ! in_array( $type, [ 'section', 'page', 'all' ], true ) ) return new WP_Error( 'elementize_invalid_pixfort_type', 'Pixfort type must be section, page, or all.', [ 'status' => 400 ] );
        $q = strtolower( trim( (string) $request->get_param( 'q' ) ) );
        $category = strtolower( trim( (string) $request->get_param( 'category' ) ) );
        $page = max( 1, absint( $request->get_param( 'page' ) ) );
        $per_page = min( 100, max( 1, absint( $request->get_param( 'per_page' ) ) ) );
        $all = [];
        if ( $type === 'section' || $type === 'all' ) self::append_templates( $all, $library['sections'] ?? [], 'section' );
        if ( $type === 'page' || $type === 'all' ) self::append_templates( $all, $library['pages'] ?? [], 'page' );
        $filtered = [];
        foreach ( array_values( $all ) as $template ) {
            $categories = array_map( 'strtolower', $template['categories'] );
            if ( $category && $category !== 'all' && ! in_array( $category, $categories, true ) ) continue;
            if ( $q && strpos( strtolower( implode( ' ', [ $template['id'], $template['title'], $template['subtype'], implode( ' ', $template['categories'] ) ] ) ), $q ) === false ) continue;
            $filtered[] = $template;
        }
        usort( $filtered, static fn( $a, $b ) => strcasecmp( $a['title'], $b['title'] ) );
        $total = count( $filtered );
        $items = array_slice( $filtered, ( $page - 1 ) * $per_page, $per_page );
        $categories = $type === 'section'
            ? self::categories( $library['sectionsCategories'] ?? [] )
            : ( $type === 'page'
                ? self::categories( $library['pagesCategories'] ?? [] )
                : [ 'section' => self::categories( $library['sectionsCategories'] ?? [] ), 'page' => self::categories( $library['pagesCategories'] ?? [] ) ] );
        return new WP_REST_Response( [
            'type' => $type,
            'q' => $q,
            'category' => $category,
            'page' => $page,
            'per_page' => $per_page,
            'total' => $total,
            'total_pages' => max( 1, (int) ceil( $total / $per_page ) ),
            'available_categories' => $categories,
            'templates' => $items,
        ], 200 );
    }

    private static function append_templates( array &$out, $source, string $kind ): void {
        if ( ! is_array( $source ) ) return;
        foreach ( $source as $raw ) {
            if ( ! is_array( $raw ) || empty( $raw['id'] ) ) continue;
            $id = (string) $raw['id'];
            $key = $kind . '|' . $id;
            $categories = [];
            foreach ( (array) ( $raw['categories'] ?? [] ) as $value ) if ( is_string( $value ) && trim( $value ) !== '' ) $categories[] = strtolower( trim( $value ) );
            if ( isset( $out[ $key ] ) ) {
                $out[ $key ]['categories'] = array_values( array_unique( array_merge( $out[ $key ]['categories'], $categories ) ) );
                continue;
            }
            $out[ $key ] = [
                'id' => $id,
                'kind' => $kind,
                'title' => (string) ( $raw['title'] ?? $id ),
                'thumbnail' => isset( $raw['thumbnail'] ) ? esc_url_raw( (string) $raw['thumbnail'] ) : null,
                'preview_url' => isset( $raw['url'] ) ? esc_url_raw( (string) $raw['url'] ) : null,
                'pixfort_type' => $raw['type'] ?? null,
                'subtype' => $raw['subtype'] ?? null,
                'categories' => array_values( array_unique( $categories ) ),
                'container_based' => ! empty( $raw['container_based'] ),
            ];
        }
    }

    private static function categories( $source ): array {
        $out = [];
        if ( ! is_array( $source ) ) return $out;
        foreach ( $source as $raw ) {
            if ( ! is_array( $raw ) || empty( $raw['id'] ) ) continue;
            $out[] = [
                'id' => (string) $raw['id'],
                'title' => (string) ( $raw['title'] ?? $raw['id'] ),
                'number' => isset( $raw['number'] ) ? absint( $raw['number'] ) : null,
            ];
        }
        return $out;
    }

    private static function library() {
        static $cache = null;
        if ( is_array( $cache ) ) return $cache;
        if ( ! function_exists( 'pixfort_elementor_library_data' ) && file_exists( self::library_loader() ) ) require_once self::library_loader();
        if ( ! function_exists( 'pixfort_elementor_library_data' ) ) return new WP_Error( 'elementize_pixfort_library_unavailable', 'The active theme does not expose the Pixfort Elementor template library.', [ 'status' => 503 ] );
        try {
            $library = pixfort_elementor_library_data();
        } catch ( Throwable $e ) {
            return new WP_Error( 'elementize_pixfort_library_failed', 'Pixfort could not load its Elementor template catalogue: ' . $e->getMessage(), [ 'status' => 500 ] );
        }
        if ( ! is_array( $library ) || ! isset( $library['sections'], $library['pages'] ) ) return new WP_Error( 'elementize_pixfort_library_invalid', 'Pixfort returned an invalid template catalogue.', [ 'status' => 500 ] );
        return $cache = $library;
    }

    private static function library_loader(): string {
        return trailingslashit( get_template_directory() ) . 'inc/demo-content/elementor/loader.php';
    }

    private static function source_path(): string {
        return defined( 'PIXFORT_PLUGIN_DIR' ) ? trailingslashit( PIXFORT_PLUGIN_DIR ) . 'includes/import/elementor/source.php' : '';
    }

    public static function insert_template( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $template_id = sanitize_key( (string) $request->get_param( 'template_id' ) );
        $position = sanitize_key( (string) $request->get_param( 'position' ) ) ?: 'end';
        $target_id = sanitize_text_field( (string) $request->get_param( 'target_element_id' ) );

        if ( ! $template_id ) return new WP_Error( 'elementize_pixfort_template_required', 'template_id is required.', [ 'status' => 400 ] );
        if ( ! in_array( $position, [ 'start', 'end', 'before', 'after' ], true ) ) return new WP_Error( 'elementize_invalid_insert_position', 'position must be start, end, before, or after.', [ 'status' => 400 ] );
        if ( in_array( $position, [ 'before', 'after' ], true ) && $target_id === '' ) return new WP_Error( 'elementize_insert_target_required', 'target_element_id is required for before/after insertion.', [ 'status' => 400 ] );

        $document = self::need_doc( $id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );

        $hash = self::hash( $elements );
        if ( ! hash_equals( $hash, (string) $request->get_param( 'content_hash' ) ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read. Read the page again before inserting a template.', [ 'status' => 409, 'content_hash' => $hash ] );

        $target_index = null;
        if ( in_array( $position, [ 'before', 'after' ], true ) ) {
            $target_index = self::top_level_index( $elements, $target_id );
            if ( $target_index === null ) return new WP_Error( 'elementize_insert_target_not_found', 'The requested top-level Elementor target no longer exists.', [ 'status' => 409 ] );
        }

        $ready = self::load_source();
        if ( is_wp_error( $ready ) ) return $ready;
        add_filter( 'pixfort_el_remote_get_args', [ self::class, 'remote_headers' ], 100 );
        try {
            $source = new \Elementor\TemplateLibrary\Source_Pixfort();
            $template = $source->get_data( [ 'template_id' => $template_id, 'editor_post_id' => $id ] );
        } catch ( Throwable $e ) {
            return new WP_Error( 'elementize_pixfort_import_failed', 'Pixfort could not prepare the selected template: ' . $e->getMessage(), [ 'status' => 500 ] );
        } finally {
            remove_filter( 'pixfort_el_remote_get_args', [ self::class, 'remote_headers' ], 100 );
        }

        if ( is_wp_error( $template ) ) return new WP_Error( 'elementize_pixfort_import_failed', $template->get_error_message(), [ 'status' => 502 ] );
        if ( ! is_array( $template ) || empty( $template['content'] ) || ! is_array( $template['content'] ) ) return new WP_Error( 'elementize_pixfort_invalid_template', 'Pixfort returned no insertable Elementor content for this template.', [ 'status' => 502 ] );

        $revision = self::revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        $inserted = array_values( $template['content'] );
        $ids = [];
        foreach ( $inserted as $element ) if ( is_array( $element ) && ! empty( $element['id'] ) ) $ids[] = (string) $element['id'];

        if ( $position === 'start' ) {
            $elements = array_merge( $inserted, $elements );
        } elseif ( $position === 'end' ) {
            $elements = array_merge( $elements, $inserted );
        } else {
            $offset = $position === 'before' ? $target_index : $target_index + 1;
            array_splice( $elements, $offset, 0, $inserted );
        }

        try {
            $saved = $document->save( [ 'elements' => $elements ] );
        } catch ( Throwable $e ) {
            return new WP_Error( 'elementize_save_failed', 'Elementor could not save the Pixfort insertion: ' . $e->getMessage(), [ 'status' => 500 ] );
        }
        if ( ! $saved ) return new WP_Error( 'elementize_save_failed', 'Elementor refused to save the Pixfort insertion.', [ 'status' => 500 ] );

        $reloaded_document = self::doc( $id );
        $reloaded_elements = $reloaded_document ? $reloaded_document->get_elements_data() : $elements;
        return new WP_REST_Response( [
            'saved' => true,
            'page_id' => $id,
            'template_id' => $template_id,
            'template_title' => $template['title'] ?? null,
            'insert_position' => $position,
            'target_element_id' => $target_id !== '' ? $target_id : null,
            'inserted_top_level_count' => count( $inserted ),
            'inserted_top_level_ids' => $ids,
            'revision_id' => $revision,
            'content_hash' => self::hash( is_array( $reloaded_elements ) ? $reloaded_elements : $elements ),
            'edit_url' => $document->get_edit_url(),
            'permalink' => get_permalink( $id ),
        ], 200 );
    }

    private static function top_level_index( array $elements, string $target_id ): ?int {
        foreach ( $elements as $index => $element ) {
            if ( is_array( $element ) && isset( $element['id'] ) && (string) $element['id'] === $target_id ) return $index;
        }
        return null;
    }

    private static function load_source() {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return self::el_missing();
        if ( ! function_exists( 'pixfort_elementor_library_data' ) && file_exists( self::library_loader() ) ) require_once self::library_loader();
        if ( ! function_exists( 'pixfort_elementor_library_data' ) ) return new WP_Error( 'elementize_pixfort_library_unavailable', 'The active theme does not expose the Pixfort Elementor template library.', [ 'status' => 503 ] );
        $path = self::source_path();
        if ( ! $path || ! file_exists( $path ) ) return new WP_Error( 'elementize_pixfort_source_unavailable', 'Pixfort Core template import source was not found.', [ 'status' => 503 ] );
        if ( ! class_exists( '\\Elementor\\TemplateLibrary\\Source_Base' ) ) return new WP_Error( 'elementize_elementor_template_library_unavailable', 'Elementor template-library classes are not available.', [ 'status' => 503 ] );
        if ( ! class_exists( '\\Elementor\\TemplateLibrary\\Source_Pixfort', false ) ) require_once $path;
        return class_exists( '\\Elementor\\TemplateLibrary\\Source_Pixfort', false ) ? true : new WP_Error( 'elementize_pixfort_source_unavailable', 'Pixfort Core template import source could not be loaded.', [ 'status' => 503 ] );
    }

    public static function remote_headers( $args ) {
        if ( ! is_array( $args ) ) $args = [];
        $key = (string) get_option( 'envato_purchase_code_27889640', '' );
        if ( trim( $key ) === '' ) return $args;
        $headers = is_array( $args['headers'] ?? null ) ? $args['headers'] : [];
        $headers['pix_domain'] = site_url();
        $headers['purchase_key'] = $key;
        $args['headers'] = $headers;
        return $args;
    }

    public static function page_text( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $document = self::need_doc( $id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
        $items = [];
        self::collect( $elements, $items );
        $post = get_post( $id );
        return new WP_REST_Response( [
            'page' => [
                'id' => $id,
                'title' => get_the_title( $id ),
                'status' => $post->post_status,
                'modified_gmt' => $post->post_modified_gmt,
                'edit_url' => $document->get_edit_url(),
                'permalink' => get_permalink( $id ),
            ],
            'content_hash' => self::hash( $elements ),
            'text_items' => $items,
        ], 200 );
    }

    public static function update_text( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $document = self::need_doc( $id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
        $hash = self::hash( $elements );
        if ( ! hash_equals( $hash, (string) $request->get_param( 'content_hash' ) ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read. Read the page again before applying edits.', [ 'status' => 409, 'content_hash' => $hash ] );
        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || ! $updates ) return new WP_Error( 'elementize_updates_required', 'At least one text update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_UPDATES ) return new WP_Error( 'elementize_too_many_updates', 'Send at most ' . self::MAX_UPDATES . ' text updates per request.', [ 'status' => 400 ] );

        $changes = [];
        foreach ( $updates as $index => $update ) {
            $valid = self::validate( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $old = null;
            $new = wp_kses_post( (string) $update['value'] );
            $result = self::apply( $elements, (string) $update['element_id'], $update['setting_path'], $new, array_key_exists( 'expected_value', $update ) ? (string) $update['expected_value'] : null, $old );
            if ( is_wp_error( $result ) ) return $result;
            if ( $result === false ) return new WP_Error( 'elementize_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409 ] );
            $changes[] = [ 'element_id' => (string) $update['element_id'], 'setting_path' => array_values( $update['setting_path'] ), 'old_value' => $old, 'new_value' => $new ];
        }

        $revision = self::revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        try {
            $saved = $document->save( [ 'elements' => $elements ] );
        } catch ( Throwable $e ) {
            return new WP_Error( 'elementize_save_failed', 'Elementor could not save the page: ' . $e->getMessage(), [ 'status' => 500 ] );
        }
        if ( ! $saved ) return new WP_Error( 'elementize_save_failed', 'Elementor refused to save the page.', [ 'status' => 500 ] );
        $reloaded_document = self::doc( $id );
        $reloaded_elements = $reloaded_document ? $reloaded_document->get_elements_data() : $elements;
        return new WP_REST_Response( [
            'saved' => true,
            'page_id' => $id,
            'updated_count' => count( $changes ),
            'revision_id' => $revision,
            'content_hash' => self::hash( is_array( $reloaded_elements ) ? $reloaded_elements : $elements ),
            'changes' => $changes,
            'edit_url' => $document->get_edit_url(),
            'permalink' => get_permalink( $id ),
        ], 200 );
    }

    private static function revision( int $id ) {
        $post = get_post( $id );
        if ( ! $post || ! post_type_supports( $post->post_type, 'revisions' ) || ! wp_revisions_enabled( $post ) ) return null;
        add_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        try {
            $revision = wp_save_post_revision( $id );
        } finally {
            remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        }
        return is_int( $revision ) && $revision > 0 ? $revision : new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the page was not modified.', [ 'status' => 500 ] );
    }

    private static function validate( $update, int $index ) {
        if ( ! is_array( $update ) ) return self::bad( $index, 'Each update must be an object.' );
        if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) return self::bad( $index, 'element_id is required.' );
        if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) ) return self::bad( $index, 'setting_path must be a non-empty array.' );
        if ( ! array_key_exists( 'value', $update ) || ! is_string( $update['value'] ) ) return self::bad( $index, 'value must be a string.' );
        if ( strlen( $update['value'] ) > self::MAX_TEXT ) return self::bad( $index, 'value is too long.' );
        if ( array_key_exists( 'expected_value', $update ) && ! is_string( $update['expected_value'] ) ) return self::bad( $index, 'expected_value must be a string when provided.' );
        return true;
    }

    private static function bad( int $index, string $message ): WP_Error {
        return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': ' . $message, [ 'status' => 400 ] );
    }

    private static function apply( array &$elements, string $id, array $path, string $new, ?string $expected, ?string &$old ) {
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $id ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) return new WP_Error( 'elementize_setting_not_found', 'The target Elementor element has no editable settings.', [ 'status' => 400 ] );
                $cursor =& $element['settings'];
                foreach ( $path as $segment ) {
                    if ( ! is_string( $segment ) && ! is_int( $segment ) ) return new WP_Error( 'elementize_invalid_setting_path', 'Each setting_path segment must be a string or integer.', [ 'status' => 400 ] );
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_setting_not_found', 'The requested text setting no longer exists.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }
                if ( ! is_string( $cursor ) || ! self::copy_field( $path, $cursor ) ) return new WP_Error( 'elementize_not_copy_field', 'Elementize only permits text updates to recognized copy fields in this build.', [ 'status' => 400 ] );
                if ( $expected !== null && $cursor !== $expected ) return new WP_Error( 'elementize_value_changed', 'The target text changed after it was read.', [ 'status' => 409 ] );
                $old = $cursor;
                $cursor = $new;
                return true;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $result = self::apply( $element['elements'], $id, $path, $new, $expected, $old );
                if ( $result === true || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function collect( array $elements, array &$items ): void {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            $element_type = $element['elType'] ?? null;
            $widget_type = $element['widgetType'] ?? null;
            if ( $id && ! empty( $element['settings'] ) && is_array( $element['settings'] ) ) self::collect_settings( $element['settings'], [], $id, $element_type, $widget_type, $items );
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) self::collect( $element['elements'], $items );
        }
    }

    private static function collect_settings( $value, array $path, string $id, ?string $element_type, ?string $widget_type, array &$items ): void {
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $nested ) self::collect_settings( $nested, array_merge( $path, [ $key ] ), $id, $element_type, $widget_type, $items );
            return;
        }
        if ( ! is_string( $value ) || ! self::copy_field( $path, $value ) ) return;
        $items[] = [
            'element_id' => $id,
            'element_type' => $element_type,
            'widget_type' => $widget_type,
            'setting_path' => array_values( $path ),
            'format' => wp_strip_all_tags( $value ) === $value ? 'text' : 'html',
            'value' => $value,
        ];
    }

    private static function copy_field( array $path, string $value ): bool {
        if ( trim( $value ) === '' || strlen( $value ) > self::MAX_TEXT || str_starts_with( ltrim( $value ), '[elementor-tag' ) ) return false;
        $key = '';
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) {
            if ( is_string( $path[ $i ] ) ) {
                $key = strtolower( $path[ $i ] );
                break;
            }
        }
        if ( ! $key ) return false;

        $blocked = '/(^|_)(url|link|href|src|id|class|selector|shortcode|icon|image|media|color|background|gradient|font|typography|size|width|height|margin|padding|border|radius|opacity|align|justify|position|offset|z_index|animation|transition|transform|query|taxonomy|post_id|flex|grid|layout|display|direction|gap|wrap|grow|shrink|basis|order|overflow)(_|$)/i';
        if ( preg_match( $blocked, $key ) ) return false;

        return (bool) preg_match( '/(^|_)(title|text|content|editor|description|heading|headline|subtitle|sub_title|label|caption|button_text|btn_text|placeholder|prefix|suffix|message)(_|$)/i', $key );
    }

    private static function need_doc( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return self::el_missing();
        $document = self::doc( $id );
        if ( ! $document ) return new WP_Error( 'elementize_elementor_document_not_found', 'Elementor could not load this page as a document.', [ 'status' => 404 ] );
        if ( ! $document->is_built_with_elementor() ) return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        return $document;
    }

    private static function doc( int $id ) {
        try {
            return \Elementor\Plugin::$instance->documents->get( $id );
        } catch ( Throwable $e ) {
            return null;
        }
    }

    private static function hash( array $elements ): string {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return hash( 'sha256', $json === false ? '' : $json );
    }

    private static function el_missing(): WP_Error {
        return new WP_Error( 'elementize_elementor_required', 'Elementor must be installed and active.', [ 'status' => 503 ] );
    }
}
Elementize_Plugin::init();
}

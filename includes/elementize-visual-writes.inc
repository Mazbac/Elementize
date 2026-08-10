<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Visual_Writes {
    private const VERSION = '0.3.0';
    private const NS = 'elementize/v1';
    private const MAX_UPDATES = 20;

    public static function init(): void {
        remove_action( 'admin_menu', [ Elementize_Plugin::class, 'menu' ] );
        add_action( 'admin_menu', [ self::class, 'menu' ] );
        add_action( 'rest_api_init', [ self::class, 'routes' ], 20 );
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', 'elementize', [ self::class, 'admin' ], 'dashicons-admin-tools', 58 );
    }

    public static function admin(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;
        $el = class_exists( '\\Elementor\\Plugin' );
        $px = self::pixfort_info();
        $theme = wp_get_theme();
        $lib = file_exists( trailingslashit( get_template_directory() ) . 'inc/demo-content/elementor/loader.php' );
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
        </div><div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Environment</h2><table class="widefat striped"><tbody>
        <tr><td><strong>WordPress</strong></td><td><?php echo esc_html( get_bloginfo( 'version' ) ); ?></td></tr>
        <tr><td><strong>Theme</strong></td><td><?php echo esc_html( trim( $theme->get( 'Name' ) . ' ' . $theme->get( 'Version' ) ) ); ?></td></tr>
        <tr><td><strong>Draft page create endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pages/create' ) ); ?></code></td></tr>
        <tr><td><strong>Pixfort catalogue endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pixfort/templates' ) ); ?></code></td></tr>
        <tr><td><strong>Pixfort visual probe endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pixfort/visual-probe' ) ); ?></code></td></tr>
        <tr><td><strong>Pixfort insert endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pages/{id}/pixfort/insert' ) ); ?></code></td></tr>
        <tr><td><strong>Visual settings endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pages/{id}/visual-settings' ) ); ?></code></td></tr>
        <tr><td><strong>Top-level remove endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::NS . '/pages/{id}/elements/remove' ) ); ?></code></td></tr>
        </tbody></table></div>
        <div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Current test state</h2><p><strong>Visual matching, guarded draft composition, copy editing, and visual-setting inspection are runtime-proven.</strong> 0.3.0 adds guarded writes for explicit CSS colors, Pixfort icon values, and WordPress image attachments. Global style references, dynamic values, divider colors, and icon behavior/layout settings remain protected.</p></div></div>
        <?php
    }

    private static function card( string $label, bool $ok, string $detail ): void {
        $bg = $ok ? '#edfaef' : '#fcf0f1';
        $color = $ok ? '#116329' : '#8a2424';
        $title = $ok ? 'Detected' : 'Attention';
        echo '<div class="card" style="margin:0;padding:20px"><div style="display:flex;justify-content:space-between"><h2 style="margin:0">' . esc_html( $label ) . '</h2><span style="padding:4px 8px;border-radius:999px;background:' . esc_attr( $bg ) . ';color:' . esc_attr( $color ) . ';font-weight:600">' . esc_html( $title ) . '</span></div><p style="margin-bottom:0">' . esc_html( $detail ) . '</p></div>';
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
            'permission_callback' => [ Elementize_Plugin::class, 'can_access' ],
        ], true );

        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/visual-settings', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'visual_settings' ],
                'permission_callback' => [ Elementize_Plugin::class, 'can_edit_page' ],
                'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update_visual_settings' ],
                'permission_callback' => [ self::class, 'can_write_visual' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'content_hash' => [ 'type' => 'string', 'required' => true ],
                    'updates' => [ 'type' => 'array', 'required' => true ],
                ],
            ],
        ], true );
    }

    public static function status() {
        $response = Elementize_Plugin::status();
        if ( is_wp_error( $response ) ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['elementize_version'] = self::VERSION;
        $data['visual_settings_read_only'] = false;
        $data['visual_settings_write_types'] = [ 'color', 'icon', 'media' ];
        return new WP_REST_Response( $data, 200 );
    }

    public static function can_write_visual( WP_REST_Request $request ) {
        $access = Elementize_Plugin::can_edit_page( $request );
        if ( is_wp_error( $access ) ) return $access;
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_visual_write_forbidden', 'Visual-setting changes require an administrator.', [ 'status' => 403 ] );
        return true;
    }

    public static function visual_settings( WP_REST_Request $request ) {
        $response = Elementize_Plugin::visual_settings( $request );
        if ( is_wp_error( $response ) ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        if ( isset( $data['visual_items'] ) && is_array( $data['visual_items'] ) ) {
            foreach ( $data['visual_items'] as &$item ) {
                if ( ! is_array( $item ) ) continue;
                $item['writable'] = self::visual_item_writable( $item );
            }
            unset( $item );
        }
        $data['read_only'] = true;
        $data['write_endpoint_available'] = true;
        $data['write_types'] = [ 'color', 'icon', 'media' ];
        return new WP_REST_Response( $data, 200 );
    }

    private static function visual_item_writable( array $item ): bool {
        $kind = (string) ( $item['kind'] ?? '' );
        $path = isset( $item['setting_path'] ) && is_array( $item['setting_path'] ) ? $item['setting_path'] : [];
        $value = $item['value'] ?? null;
        if ( $kind === 'color' && is_string( $value ) ) return self::safe_color_target( $path, $value );
        if ( $kind === 'icon' && is_string( $value ) ) return self::safe_icon_target( $path, $value );
        if ( $kind === 'media' && is_array( $value ) ) return self::safe_media_target( $path, $value );
        return false;
    }

    public static function update_visual_settings( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $document = self::need_doc( $page_id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );

        $current_hash = self::hash( $elements );
        if ( ! hash_equals( $current_hash, (string) $request->get_param( 'content_hash' ) ) ) {
            return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read. Read visual settings again before applying edits.', [ 'status' => 409, 'content_hash' => $current_hash ] );
        }

        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || ! $updates ) return new WP_Error( 'elementize_visual_updates_required', 'At least one visual-setting update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_UPDATES ) return new WP_Error( 'elementize_too_many_visual_updates', 'Send at most ' . self::MAX_UPDATES . ' visual-setting updates per request.', [ 'status' => 400 ] );

        $before = $elements;
        $changes = [];
        foreach ( $updates as $index => $update ) {
            $valid = self::validate_visual_update( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $old = null;
            $new = null;
            $applied = self::apply_visual_update( $elements, $update, $old, $new );
            if ( is_wp_error( $applied ) ) return $applied;
            if ( $applied === false ) return new WP_Error( 'elementize_visual_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409 ] );
            $changes[] = [
                'kind' => (string) $update['kind'],
                'element_id' => (string) $update['element_id'],
                'setting_path' => array_values( $update['setting_path'] ),
                'old_value' => $old,
                'new_value' => $new,
            ];
        }

        $revision = self::revision( $page_id );
        if ( is_wp_error( $revision ) ) return $revision;

        try { $saved = $document->save( [ 'elements' => $elements ] ); }
        catch ( Throwable $exception ) { return new WP_Error( 'elementize_save_failed', 'Elementor could not save the visual-setting changes: ' . $exception->getMessage(), [ 'status' => 500 ] ); }
        if ( ! $saved ) return new WP_Error( 'elementize_save_failed', 'Elementor refused to save the visual-setting changes.', [ 'status' => 500 ] );

        $reloaded = self::doc( $page_id );
        $saved_elements = $reloaded ? $reloaded->get_elements_data() : null;
        if ( ! is_array( $saved_elements ) || ! self::verify_visual_changes( $saved_elements, $changes ) ) {
            try { $document->save( [ 'elements' => $before ] ); }
            catch ( Throwable $ignored ) {}
            return new WP_Error( 'elementize_visual_verification_failed', 'Elementor did not preserve every requested visual-setting change, so Elementize attempted to restore the pre-change page.', [ 'status' => 500, 'revision_id' => $revision ] );
        }

        return new WP_REST_Response( [
            'saved' => true,
            'page_id' => $page_id,
            'updated_count' => count( $changes ),
            'revision_id' => $revision,
            'content_hash' => self::hash( $saved_elements ),
            'changes' => $changes,
            'edit_url' => $document->get_edit_url(),
            'permalink' => get_permalink( $page_id ),
        ], 200 );
    }

    private static function validate_visual_update( $update, int $index ) {
        if ( ! is_array( $update ) ) return self::bad_update( $index, 'Each update must be an object.' );
        $kind = isset( $update['kind'] ) && is_string( $update['kind'] ) ? strtolower( trim( $update['kind'] ) ) : '';
        if ( ! in_array( $kind, [ 'color', 'icon', 'media' ], true ) ) return self::bad_update( $index, 'kind must be color, icon, or media.' );
        if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) return self::bad_update( $index, 'element_id is required.' );
        if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) ) return self::bad_update( $index, 'setting_path must be a non-empty array.' );
        if ( count( $update['setting_path'] ) > 20 ) return self::bad_update( $index, 'setting_path is too deep.' );
        foreach ( $update['setting_path'] as $segment ) if ( ! is_string( $segment ) && ! is_int( $segment ) ) return self::bad_update( $index, 'Each setting_path segment must be a string or integer.' );
        if ( self::path_has_key( $update['setting_path'], '__globals__' ) ) return self::bad_update( $index, 'Global style references are read-only.' );

        if ( $kind === 'media' ) {
            if ( ! array_key_exists( 'expected_attachment_id', $update ) || ! is_numeric( $update['expected_attachment_id'] ) ) return self::bad_update( $index, 'expected_attachment_id is required for media.' );
            if ( empty( $update['attachment_id'] ) || ! is_numeric( $update['attachment_id'] ) ) return self::bad_update( $index, 'attachment_id is required for media.' );
        } else {
            if ( ! array_key_exists( 'expected_value', $update ) || ! is_string( $update['expected_value'] ) ) return self::bad_update( $index, 'expected_value is required for color and icon updates.' );
            if ( ! array_key_exists( 'value', $update ) || ! is_string( $update['value'] ) ) return self::bad_update( $index, 'value is required for color and icon updates.' );
        }
        return true;
    }

    private static function bad_update( int $index, string $message ): WP_Error {
        return new WP_Error( 'elementize_invalid_visual_update', 'Invalid visual update at index ' . $index . ': ' . $message, [ 'status' => 400 ] );
    }

    private static function apply_visual_update( array &$elements, array $update, &$old, &$new ) {
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === (string) $update['element_id'] ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) return new WP_Error( 'elementize_visual_setting_not_found', 'The target Elementor element has no settings.', [ 'status' => 409 ] );
                $cursor =& $element['settings'];
                foreach ( $update['setting_path'] as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_visual_setting_not_found', 'The requested visual setting no longer exists.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }

                $kind = strtolower( (string) $update['kind'] );
                if ( $kind === 'color' ) return self::apply_color( $cursor, $update, $old, $new );
                if ( $kind === 'icon' ) return self::apply_icon( $cursor, $update, $old, $new );
                return self::apply_media( $cursor, $update, $old, $new );
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $result = self::apply_visual_update( $element['elements'], $update, $old, $new );
                if ( $result === true || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function apply_color( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::safe_color_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_visual_target_not_writable', 'This color setting is not in the guarded writable set.', [ 'status' => 400 ] );
        if ( $cursor !== $update['expected_value'] ) return new WP_Error( 'elementize_visual_value_changed', 'The target color changed after it was read.', [ 'status' => 409 ] );
        $candidate = trim( (string) $update['value'] );
        if ( ! self::safe_css_color( $candidate ) ) return new WP_Error( 'elementize_invalid_color', 'The new color must be an explicit hex, rgb/rgba, hsl/hsla, or transparent value.', [ 'status' => 400 ] );
        $old = $cursor;
        $cursor = $candidate;
        $new = $candidate;
        return true;
    }

    private static function apply_icon( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::safe_icon_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_visual_target_not_writable', 'This icon setting is not in the guarded writable set.', [ 'status' => 400 ] );
        if ( $cursor !== $update['expected_value'] ) return new WP_Error( 'elementize_visual_value_changed', 'The target icon changed after it was read.', [ 'status' => 409 ] );
        $candidate = trim( (string) $update['value'] );
        if ( ! self::safe_pixfort_icon( $candidate ) ) return new WP_Error( 'elementize_invalid_icon', 'The new icon must be a Pixfort Solid/... or Line/... icon value.', [ 'status' => 400 ] );
        $old = $cursor;
        $cursor = $candidate;
        $new = $candidate;
        return true;
    }

    private static function apply_media( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_array( $cursor ) || ! self::safe_media_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_visual_target_not_writable', 'This media setting is not in the guarded writable set.', [ 'status' => 400 ] );
        $current_id = absint( $cursor['id'] ?? 0 );
        if ( $current_id !== absint( $update['expected_attachment_id'] ) ) return new WP_Error( 'elementize_visual_value_changed', 'The target media attachment changed after it was read.', [ 'status' => 409 ] );

        $attachment_id = absint( $update['attachment_id'] );
        if ( $attachment_id < 1 || get_post_type( $attachment_id ) !== 'attachment' || ! wp_attachment_is_image( $attachment_id ) ) return new WP_Error( 'elementize_invalid_media', 'attachment_id must reference an existing WordPress image attachment.', [ 'status' => 400 ] );
        if ( ! current_user_can( 'edit_post', $attachment_id ) ) return new WP_Error( 'elementize_media_forbidden', 'The authenticated WordPress user cannot use this attachment.', [ 'status' => 403 ] );
        $url = wp_get_attachment_url( $attachment_id );
        if ( ! is_string( $url ) || $url === '' ) return new WP_Error( 'elementize_invalid_media', 'WordPress could not resolve the image attachment URL.', [ 'status' => 400 ] );

        $old = $cursor;
        $next = $cursor;
        $next['id'] = $attachment_id;
        $next['url'] = esc_url_raw( $url );
        unset( $next['source'] );
        $cursor = $next;
        $new = $next;
        return true;
    }

    private static function safe_color_target( array $path, string $current ): bool {
        if ( self::path_has_key( $path, '__globals__' ) || ! self::safe_css_color( $current ) ) return false;
        foreach ( $path as $segment ) if ( is_string( $segment ) && str_contains( strtolower( $segment ), 'moving_divider' ) ) return false;
        $key = self::last_path_key( $path );
        if ( $key === '' || ! str_contains( $key, 'color' ) ) return false;
        if ( preg_match( '/^d_color_[12]$/i', $key ) ) return false;
        return true;
    }

    private static function safe_icon_target( array $path, string $current ): bool {
        if ( self::path_has_key( $path, '__globals__' ) ) return false;
        $key = self::last_path_key( $path );
        if ( ! in_array( $key, [ 'icon', 'btn_icon' ], true ) ) return false;
        return self::safe_pixfort_icon( $current );
    }

    private static function safe_media_target( array $path, array $current ): bool {
        if ( self::path_has_key( $path, '__globals__' ) ) return false;
        $key = self::last_path_key( $path );
        if ( ! in_array( $key, [ 'image', 'text_image', 'media', 'logo', 'thumbnail', 'avatar', 'photo', 'poster', 'background_image' ], true ) ) return false;
        return array_key_exists( 'id', $current ) || array_key_exists( 'url', $current );
    }

    private static function safe_css_color( string $value ): bool {
        $value = trim( $value );
        if ( preg_match( '/^#[0-9a-f]{3,8}$/i', $value ) ) return true;
        if ( preg_match( '/^rgba?\(\s*[0-9.%\s,]+\)$/i', $value ) ) return true;
        if ( preg_match( '/^hsla?\(\s*[0-9.%\s,]+\)$/i', $value ) ) return true;
        return strtolower( $value ) === 'transparent';
    }

    private static function safe_pixfort_icon( string $value ): bool {
        return (bool) preg_match( '#^(Solid|Line)/pixfort-icon-[A-Za-z0-9_-]+$#', trim( $value ) );
    }

    private static function verify_visual_changes( array $elements, array $changes ): bool {
        foreach ( $changes as $change ) {
            $found = false;
            $value = self::get_setting_value( $elements, (string) $change['element_id'], $change['setting_path'], $found );
            if ( ! $found ) return false;
            if ( (string) ( $change['kind'] ?? '' ) === 'media' ) {
                if ( ! is_array( $value ) || ! is_array( $change['new_value'] ) ) return false;
                if ( absint( $value['id'] ?? 0 ) !== absint( $change['new_value']['id'] ?? 0 ) ) return false;
                if ( (string) ( $value['url'] ?? '' ) !== (string) ( $change['new_value']['url'] ?? '' ) ) return false;
                continue;
            }
            if ( $value !== $change['new_value'] ) return false;
        }
        return true;
    }

    private static function get_setting_value( array $elements, string $element_id, array $path, bool &$found ) {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) { $found = false; return null; }
                $cursor = $element['settings'];
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) { $found = false; return null; }
                    $cursor = $cursor[ $segment ];
                }
                $found = true;
                return $cursor;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $value = self::get_setting_value( $element['elements'], $element_id, $path, $found );
                if ( $found ) return $value;
            }
        }
        $found = false;
        return null;
    }

    private static function revision( int $page_id ) {
        $post = get_post( $page_id );
        if ( ! $post || ! post_type_supports( $post->post_type, 'revisions' ) || ! wp_revisions_enabled( $post ) ) return null;
        add_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        try { $revision = wp_save_post_revision( $page_id ); }
        finally { remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX ); }
        return is_int( $revision ) && $revision > 0 ? $revision : new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the page was not modified.', [ 'status' => 500 ] );
    }

    private static function need_doc( int $page_id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return new WP_Error( 'elementize_elementor_required', 'Elementor must be installed and active.', [ 'status' => 503 ] );
        $document = self::doc( $page_id );
        if ( ! $document ) return new WP_Error( 'elementize_elementor_document_not_found', 'Elementor could not load this page as a document.', [ 'status' => 404 ] );
        if ( ! $document->is_built_with_elementor() ) return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        return $document;
    }

    private static function doc( int $page_id ) {
        try { return \Elementor\Plugin::$instance->documents->get( $page_id ); }
        catch ( Throwable $exception ) { return null; }
    }

    private static function hash( array $elements ): string {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return hash( 'sha256', $json === false ? '' : $json );
    }

    private static function last_path_key( array $path ): string {
        for ( $index = count( $path ) - 1; $index >= 0; $index-- ) if ( is_string( $path[ $index ] ) ) return strtolower( $path[ $index ] );
        return '';
    }

    private static function path_has_key( array $path, string $needle ): bool {
        $needle = strtolower( $needle );
        foreach ( $path as $segment ) if ( is_string( $segment ) && strtolower( $segment ) === $needle ) return true;
        return false;
    }
}

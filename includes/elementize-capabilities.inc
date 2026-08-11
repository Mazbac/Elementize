<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Capabilities {
    private const OPT_PROFILE = 'elementize_editing_profile';
    private const OPT_SCOPE_PAGE = 'elementize_creative_scope_page';
    private const OPT_REVISION = 'elementize_capability_revision';
    private const PROFILE_STANDARD = 'standard';
    private const PROFILE_CREATIVE = 'creative';

    public static function init(): void {
        add_filter( 'rest_request_before_callbacks', [ self::class, 'preflight_creative_request' ], 5, 3 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'annotate_status' ], 80, 3 );
        add_action( 'admin_enqueue_scripts', [ self::class, 'inject_admin_config' ], 99 );
        add_action( 'wp_ajax_elementize_set_editing_profile', [ self::class, 'ajax_set_profile' ] );
    }

    public static function state(): array {
        $profile = (string) get_option( self::OPT_PROFILE, self::PROFILE_STANDARD );
        if ( ! in_array( $profile, [ self::PROFILE_STANDARD, self::PROFILE_CREATIVE ], true ) ) $profile = self::PROFILE_STANDARD;
        $page_id = self::PROFILE_CREATIVE === $profile ? absint( get_option( self::OPT_SCOPE_PAGE, 0 ) ) : 0;
        if ( self::PROFILE_CREATIVE === $profile ) {
            $post = $page_id > 0 ? get_post( $page_id ) : null;
            if ( ! $post || 'page' !== $post->post_type ) {
                $profile = self::PROFILE_STANDARD;
                $page_id = 0;
            }
        }
        $revision = max( 1, absint( get_option( self::OPT_REVISION, 1 ) ) );
        $creative = self::PROFILE_CREATIVE === $profile && $page_id > 0;
        return [
            'profile' => $profile,
            'creative_enabled' => $creative,
            'scope_page_id' => $creative ? $page_id : 0,
            'revision' => $revision,
            'capabilities' => self::capability_map( $profile ),
        ];
    }

    public static function capability_map( ?string $profile = null ): array {
        if ( null === $profile ) {
            $stored = (string) get_option( self::OPT_PROFILE, self::PROFILE_STANDARD );
            $profile = in_array( $stored, [ self::PROFILE_STANDARD, self::PROFILE_CREATIVE ], true ) ? $stored : self::PROFILE_STANDARD;
        }
        $creative = self::PROFILE_CREATIVE === $profile;
        return [
            'copy' => true,
            'links' => true,
            'images' => true,
            'pixfort_icons' => true,
            'natural_targeting' => true,
            'screenshot_grounding' => true,
            'insert_templates' => $creative,
            'remove_elements' => $creative,
            'duplicate_elements' => $creative,
            'move_elements' => $creative,
            'reorder_elements' => $creative,
            'design_controls' => $creative,
            'creative_transactions' => $creative,
        ];
    }

    public static function require_creative( int $page_id, int $expected_revision ) {
        $state = self::state();
        if ( empty( $state['creative_enabled'] ) ) {
            return new WP_Error( 'elementize_creative_control_disabled', 'Creative Control is currently disabled. Enable it in Elementize Settings before using structural or design operations.', [ 'status' => 403, 'capability_state' => $state ] );
        }
        if ( absint( $state['scope_page_id'] ) !== $page_id ) {
            return new WP_Error( 'elementize_creative_scope_mismatch', 'Creative Control is enabled for a different page.', [ 'status' => 403, 'capability_state' => $state ] );
        }
        if ( $expected_revision < 1 || absint( $state['revision'] ) !== $expected_revision ) {
            return new WP_Error( 'elementize_capabilities_changed', 'Elementize editing capabilities changed after the creative read. Refresh the creative page state before writing.', [ 'status' => 409, 'capability_state' => $state ] );
        }
        return true;
    }

    public static function preflight_creative_request( $response, array $handler, WP_REST_Request $request ) {
        if ( null !== $response || 'POST' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/(\d+)/creative$#', $request->get_route() ) ) return $response;

        $operations = $request->get_param( 'operations' );
        if ( ! is_array( $operations ) ) return $response;

        foreach ( array_values( $operations ) as $index => $operation ) {
            if ( ! is_array( $operation ) ) continue;
            $type = sanitize_key( (string) ( $operation['type'] ?? '' ) );
            if ( 'content' === $type && 'media' === sanitize_key( (string) ( $operation['kind'] ?? '' ) ) ) {
                if ( ! array_key_exists( 'expected_attachment_id', $operation ) || ! array_key_exists( 'attachment_id', $operation ) ) {
                    return new WP_Error( 'elementize_creative_media_guard_missing', 'Creative media operations require both expected_attachment_id and attachment_id.', [ 'status' => 400, 'operation_index' => $index ] );
                }
                if ( ! is_numeric( $operation['expected_attachment_id'] ) || (int) $operation['expected_attachment_id'] < 0 || ! is_numeric( $operation['attachment_id'] ) || (int) $operation['attachment_id'] < 1 ) {
                    return new WP_Error( 'elementize_creative_media_guard_invalid', 'Creative media attachment guards must be non-negative expected_attachment_id and positive attachment_id integers.', [ 'status' => 400, 'operation_index' => $index ] );
                }
            }
            if ( 'style' === $type ) {
                foreach ( [ 'expected_json', 'value_json' ] as $key ) {
                    $value = $operation[ $key ] ?? null;
                    if ( is_string( $value ) && ( false !== strpos( $value, '{{' ) || false !== stripos( $value, '[elementor-tag' ) ) ) {
                        return new WP_Error( 'elementize_creative_dynamic_design_read_only', 'Dynamic Elementor design values remain read-only in Creative Control.', [ 'status' => 400, 'operation_index' => $index ] );
                    }
                }
            }
        }
        return $response;
    }

    public static function annotate_status( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . Elementize_Core::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $state = self::state();
        $creative = (bool) $state['creative_enabled'];
        $data['scope'] = $creative ? 'elementor_pixfort_page_scoped_creative_control' : 'elementor_pixfort_content_only';
        $data['editing_profile'] = $state['profile'];
        $data['creative_control_available'] = true;
        $data['creative_control_default'] = self::PROFILE_STANDARD;
        $data['creative_control_enabled'] = $creative;
        $data['creative_scope'] = 'one_page';
        $data['creative_scope_page_id'] = $state['scope_page_id'];
        $data['capability_revision'] = $state['revision'];
        $data['capabilities'] = $state['capabilities'];
        $data['template_insertion'] = $creative;
        $data['layout_controls'] = $creative;
        $data['design_controls'] = $creative;
        $data['color_controls'] = $creative;
        $data['structural_element_controls'] = $creative;
        $data['global_design_controls'] = false;
        $data['creative_write_guards'] = [
            'server_enforced_profile' => true,
            'single_page_scope' => true,
            'capability_revision' => true,
            'fresh_content_hash' => true,
            'pre_change_revision_required' => true,
            'single_transaction_save' => true,
            'persisted_full_tree_verification' => true,
            'activity_snapshot_and_undo' => true,
            'media_expected_attachment_guard' => true,
            'global_dynamic_shared_values_read_only' => true,
        ];
        $response->set_data( $data );
        return $response;
    }

    public static function inject_admin_config( string $hook ): void {
        if ( 'toplevel_page_elementize' !== $hook || ! current_user_can( 'manage_options' ) ) return;
        if ( ! wp_script_is( 'elementize-admin', 'enqueued' ) ) return;
        wp_add_inline_script(
            'elementize-admin',
            'window.ElementizeCreativeConfig = ' . wp_json_encode( self::admin_config() ) . ';',
            'before'
        );
    }

    public static function admin_config(): array {
        $state = self::state();
        $pages = [];
        $query = new WP_Query( [
            'post_type' => 'page',
            'post_status' => array_keys( get_post_stati( [ 'internal' => false ] ) ),
            'posts_per_page' => 200,
            'orderby' => 'modified',
            'order' => 'DESC',
            'meta_query' => [ [ 'key' => '_elementor_edit_mode', 'value' => 'builder', 'compare' => '=' ] ],
            'no_found_rows' => true,
        ] );
        foreach ( $query->posts as $post ) {
            if ( ! $post instanceof WP_Post || ! current_user_can( 'edit_post', $post->ID ) ) continue;
            $pages[] = [
                'id' => (int) $post->ID,
                'title' => '' !== trim( (string) $post->post_title ) ? (string) $post->post_title : '(Untitled)',
                'status' => (string) $post->post_status,
            ];
        }
        return [
            'state' => $state,
            'pages' => $pages,
            'ajax' => admin_url( 'admin-ajax.php' ),
            'nonce' => wp_create_nonce( 'elementize_creative_control' ),
        ];
    }

    public static function ajax_set_profile(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_send_json_error( [ 'message' => 'Administrator permission is required.' ], 403 );
        check_ajax_referer( 'elementize_creative_control', 'nonce' );

        $profile = sanitize_key( (string) wp_unslash( $_POST['profile'] ?? '' ) );
        if ( ! in_array( $profile, [ self::PROFILE_STANDARD, self::PROFILE_CREATIVE ], true ) ) wp_send_json_error( [ 'message' => 'Invalid editing profile.' ], 400 );

        $page_id = self::PROFILE_CREATIVE === $profile ? absint( $_POST['page_id'] ?? 0 ) : 0;
        if ( self::PROFILE_CREATIVE === $profile ) {
            $post = get_post( $page_id );
            if ( ! $post || 'page' !== $post->post_type || ! current_user_can( 'edit_post', $page_id ) ) wp_send_json_error( [ 'message' => 'Choose an editable WordPress page for Creative Control.' ], 400 );
            $document = Elementize_Core::need_document( $page_id );
            if ( is_wp_error( $document ) ) wp_send_json_error( [ 'message' => $document->get_error_message() ], 400 );
        }

        $current = self::state();
        $changed = (string) $current['profile'] !== $profile || absint( $current['scope_page_id'] ) !== $page_id;
        if ( $changed ) {
            $new_revision = max( 1, absint( $current['revision'] ) + 1 );
            update_option( self::OPT_PROFILE, $profile, false );
            update_option( self::OPT_SCOPE_PAGE, $page_id, false );
            update_option( self::OPT_REVISION, $new_revision, false );

            $stored_profile = (string) get_option( self::OPT_PROFILE, self::PROFILE_STANDARD );
            $stored_page = absint( get_option( self::OPT_SCOPE_PAGE, 0 ) );
            $stored_revision = absint( get_option( self::OPT_REVISION, 0 ) );
            if ( $stored_profile !== $profile || $stored_page !== $page_id || $stored_revision !== $new_revision ) {
                update_option( self::OPT_PROFILE, (string) $current['profile'], false );
                update_option( self::OPT_SCOPE_PAGE, absint( $current['scope_page_id'] ), false );
                update_option( self::OPT_REVISION, absint( $current['revision'] ), false );
                wp_send_json_error( [ 'message' => 'Elementize could not persist the editing profile. No capability change was applied.' ], 500 );
            }
        }

        wp_send_json_success( [ 'state' => self::state() ] );
    }
}

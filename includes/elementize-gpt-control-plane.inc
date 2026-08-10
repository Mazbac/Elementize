<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_GPT_Control_Plane {
    private const VERSION = '0.10.0';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 90 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 240, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/state', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'read_state' ],
                'permission_callback' => [ self::class, 'can_read' ],
                'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update_state' ],
                'permission_callback' => [ self::class, 'can_write' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'action' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_key' ],
                    'expected_status' => [ 'type' => 'string', 'required' => true ],
                    'expected_title' => [ 'type' => 'string', 'required' => true ],
                    'content_hash' => [ 'type' => 'string', 'required' => false ],
                    'lifecycle_token' => [ 'type' => 'string', 'required' => false ],
                    'expected_mode' => [ 'type' => 'string', 'required' => false ],
                    'layout_token' => [ 'type' => 'string', 'required' => false ],
                    'mode' => [ 'type' => 'string', 'required' => false ],
                    'confirm_state_change' => [ 'type' => 'boolean', 'required' => true ],
                ],
            ],
        ] );

        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/completion-audit', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'completion_audit' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot inspect this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function can_write( WP_REST_Request $request ) {
        $read = self::can_read( $request );
        if ( true !== $read ) return $read;
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_state_write_forbidden', 'Page state changes require an administrator.', [ 'status' => 403 ] );
        return true;
    }

    public static function read_state( WP_REST_Request $request ) {
        if ( ! class_exists( 'Elementize_Page_Layout', false ) || ! class_exists( 'Elementize_Lifecycle', false ) ) {
            return new WP_Error( 'elementize_state_backend_missing', 'Page layout or lifecycle support is unavailable.', [ 'status' => 503 ] );
        }
        $layout = Elementize_Page_Layout::read( $request );
        $lifecycle = Elementize_Lifecycle::lifecycle( $request );
        if ( is_wp_error( $layout ) ) return $layout;
        if ( is_wp_error( $lifecycle ) ) return $lifecycle;
        return new WP_REST_Response( [
            'control_plane_version' => self::VERSION,
            'read_only' => true,
            'page_id' => absint( $request['id'] ),
            'layout' => self::response_data( $layout ),
            'lifecycle' => self::response_data( $lifecycle ),
            'supported_state_actions' => [ 'set_layout', 'trash', 'restore', 'publish', 'unpublish' ],
            'mutation_endpoint' => '/' . self::NS . '/pages/' . absint( $request['id'] ) . '/state',
        ], 200 );
    }

    public static function update_state( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_state_change' ) ) {
            return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_state_change=true is required.', [ 'status' => 400 ] );
        }
        $action = sanitize_key( (string) $request->get_param( 'action' ) );
        if ( ! in_array( $action, [ 'set_layout', 'trash', 'restore', 'publish', 'unpublish' ], true ) ) {
            return new WP_Error( 'elementize_invalid_state_action', 'action must be set_layout, trash, restore, publish, or unpublish.', [ 'status' => 400 ] );
        }

        if ( 'set_layout' === $action ) return self::set_layout( $request );
        return self::lifecycle_action( $request, $action );
    }

    private static function set_layout( WP_REST_Request $request ) {
        if ( ! class_exists( 'Elementize_Page_Layout', false ) ) return new WP_Error( 'elementize_layout_backend_missing', 'Page layout support is unavailable.', [ 'status' => 503 ] );
        foreach ( [ 'content_hash', 'expected_mode', 'layout_token', 'mode' ] as $key ) {
            if ( '' === trim( (string) $request->get_param( $key ) ) ) return new WP_Error( 'elementize_state_guard_missing', $key . ' is required for set_layout.', [ 'status' => 400 ] );
        }
        $inner = new WP_REST_Request( 'POST', '/' . self::NS . '/pages/' . absint( $request['id'] ) . '/layout' );
        foreach ( [ 'expected_status', 'expected_title', 'content_hash', 'expected_mode', 'layout_token', 'mode' ] as $key ) $inner->set_param( $key, $request->get_param( $key ) );
        $inner->set_param( 'id', absint( $request['id'] ) );
        $inner->set_param( 'confirm_layout_change', true );
        $result = Elementize_Page_Layout::update( $inner );
        return self::wrap_mutation( $result, 'set_layout' );
    }

    private static function lifecycle_action( WP_REST_Request $request, string $action ) {
        if ( ! class_exists( 'Elementize_Lifecycle', false ) || ! is_callable( [ Elementize_Lifecycle::class, $action ] ) ) {
            return new WP_Error( 'elementize_lifecycle_backend_missing', 'Lifecycle support is unavailable.', [ 'status' => 503 ] );
        }
        if ( '' === trim( (string) $request->get_param( 'lifecycle_token' ) ) ) return new WP_Error( 'elementize_state_guard_missing', 'lifecycle_token is required for lifecycle changes.', [ 'status' => 400 ] );
        if ( in_array( $action, [ 'trash', 'publish', 'unpublish' ], true ) && '' === trim( (string) $request->get_param( 'content_hash' ) ) ) {
            return new WP_Error( 'elementize_state_guard_missing', 'content_hash is required for this lifecycle change.', [ 'status' => 400 ] );
        }
        $inner = new WP_REST_Request( 'POST', '/' . self::NS . '/pages/' . absint( $request['id'] ) . '/' . $action );
        $inner->set_param( 'id', absint( $request['id'] ) );
        foreach ( [ 'expected_status', 'expected_title', 'lifecycle_token', 'content_hash' ] as $key ) {
            $value = $request->get_param( $key );
            if ( null !== $value ) $inner->set_param( $key, $value );
        }
        $inner->set_param( 'confirm_' . $action, true );
        $result = call_user_func( [ Elementize_Lifecycle::class, $action ], $inner );
        return self::wrap_mutation( $result, $action );
    }

    private static function wrap_mutation( $result, string $action ) {
        if ( is_wp_error( $result ) ) return $result;
        if ( ! $result instanceof WP_REST_Response ) return new WP_Error( 'elementize_state_backend_invalid', 'The delegated state operation returned an invalid response.', [ 'status' => 500 ] );
        $data = $result->get_data();
        if ( ! is_array( $data ) ) $data = [ 'result' => $data ];
        $data['control_plane_version'] = self::VERSION;
        $data['state_action'] = $action;
        $result->set_data( $data );
        return $result;
    }

    public static function completion_audit( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $quality = self::internal_get( '/' . self::NS . '/pages/' . $id . '/quality-audit', 'quality' );
        if ( is_wp_error( $quality ) ) return $quality;
        $design = self::internal_get( '/' . self::NS . '/pages/' . $id . '/design-audit', 'design' );
        if ( is_wp_error( $design ) ) return $design;

        $quality_pass = ! empty( $quality['default_completion_pass'] );
        $design_pass = ! empty( $design['default_design_pass'] );
        return new WP_REST_Response( [
            'control_plane_version' => self::VERSION,
            'read_only' => true,
            'page_id' => $id,
            'quality' => $quality,
            'design' => $design,
            'summary' => [
                'quality_default_completion_pass' => $quality_pass,
                'design_default_pass' => $design_pass,
                'combined_default_pass' => $quality_pass && $design_pass,
                'design_audit_is_advisory' => ! empty( $design['audit_policy']['initial_release_advisory_only'] ),
            ],
            'note' => 'This compact GPT-facing audit preserves the full underlying quality and design audit payloads. Rendered visual inspection is still required for appearance.',
        ], 200 );
    }

    private static function internal_get( string $route, string $component ) {
        $response = rest_do_request( new WP_REST_Request( 'GET', $route ) );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response ) return new WP_Error( 'elementize_completion_audit_invalid', 'The ' . $component . ' audit returned an invalid response.', [ 'status' => 500 ] );
        $data = $response->get_data();
        if ( $response->get_status() >= 400 ) {
            return new WP_Error( 'elementize_completion_audit_component_failed', 'The ' . $component . ' audit failed.', [ 'status' => $response->get_status(), 'component' => $component, 'details' => $data ] );
        }
        return is_array( $data ) ? $data : [ 'result' => $data ];
    }

    private static function response_data( $response ): array {
        if ( $response instanceof WP_REST_Response ) {
            $data = $response->get_data();
            return is_array( $data ) ? $data : [ 'result' => $data ];
        }
        return [];
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['gpt_control_plane'] = true;
        $data['gpt_action_budget_target'] = 24;
        $data['gpt_state_read_consolidated'] = true;
        $data['gpt_state_mutation_consolidated'] = true;
        $data['gpt_completion_audit_consolidated'] = true;
        $data['legacy_rest_endpoints_retained'] = true;
        $response->set_data( $data );
        return $response;
    }
}

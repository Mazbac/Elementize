<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Write_Discovery {
    private const VERSION = '0.11.1';
    private const NS = 'elementize/v1';
    private const META_MANAGED = '_elementize_managed';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 260, 3 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/design-settings$#', $route, $matches ) ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;

        $page_id = isset( $matches[1] ) ? absint( $matches[1] ) : absint( $request['id'] );
        $post = get_post( $page_id );
        $endpoint_available = class_exists( 'Elementize_Design_Writes', false );
        $page_writable = $endpoint_available
            && $post
            && 'page' === $post->post_type
            && 'draft' === $post->post_status
            && '1' === (string) get_post_meta( $page_id, self::META_MANAGED, true )
            && current_user_can( 'manage_options' )
            && current_user_can( 'edit_post', $page_id );

        $data['design_settings_version'] = self::VERSION;
        $data['write_endpoint_available'] = $endpoint_available;
        $data['write_available_for_page'] = $page_writable;
        $data['write_contract'] = [
            'managed_draft_required' => true,
            'supported_categories' => [ 'spacing', 'alignment', 'typography', 'border_radius' ],
            'fresh_hash_and_fingerprint_required' => true,
            'global_dynamic_read_only' => true,
            'complex_composited_read_only' => true,
        ];

        if ( isset( $data['discovery_policy'] ) && is_array( $data['discovery_policy'] ) ) {
            $data['discovery_policy']['writer_candidate_is_advisory'] = true;
            $data['discovery_policy']['note'] = 'writer_candidate is broad discovery. writable_now is the narrower current writer contract for this managed draft; valid new values are still range/type checked during the write.';
        }

        $controls = is_array( $data['controls'] ?? null ) ? $data['controls'] : [];
        $writable_count = 0;
        foreach ( $controls as &$control ) {
            if ( ! is_array( $control ) ) continue;
            $supported = self::control_supported( $control );
            $control['writable_now'] = $page_writable && $supported;
            if ( $control['writable_now'] ) {
                $writable_count++;
                $control['writable_now_reason'] = 'Supported by the guarded typed design writer on this managed draft. Use a fresh exact value and control fingerprint.';
            } else {
                $control['writable_now_reason'] = self::not_writable_reason( $control, $page_writable, $endpoint_available );
            }
        }
        unset( $control );
        $data['controls'] = $controls;

        if ( isset( $data['summary'] ) && is_array( $data['summary'] ) ) {
            $data['summary']['writable_now_returned_count'] = $writable_count;
        }

        $response->set_data( $data );
        return $response;
    }

    private static function control_supported( array $control ): bool {
        if ( 'explicit' !== (string) ( $control['source'] ?? '' ) ) return false;
        $category = sanitize_key( (string) ( $control['category'] ?? '' ) );
        if ( ! in_array( $category, [ 'spacing', 'alignment', 'typography', 'border_radius' ], true ) ) return false;
        $path = is_array( $control['setting_path'] ?? null ) ? $control['setting_path'] : [];
        if ( ! $path ) return false;
        $path_text = strtolower( implode( '.', array_map( 'strval', $path ) ) );
        if ( preg_match( '/moving_divider|gradient|overlay|slideshow|motion|animation|transform|custom_css|css_class|css_id/', $path_text ) ) return false;
        $value = $control['value'] ?? null;

        if ( 'spacing' === $category ) {
            if ( ! preg_match( '/margin|padding|(?:^|[._-])gap(?:$|[._-])|column[_-]?gap|row[_-]?gap|spacing|space[_-]?(?:between|before|after)/', $path_text ) ) return false;
            return self::is_box( $value ) || self::is_dimension( $value );
        }

        if ( 'border_radius' === $category ) {
            if ( ! preg_match( '/border[_-]?radius|radius/', $path_text ) ) return false;
            return self::is_box( $value ) || self::is_dimension( $value );
        }

        if ( 'alignment' === $category ) {
            return is_string( $value ) && (bool) preg_match( '/align|justify/', $path_text );
        }

        if ( preg_match( '/font[_-]?family/', $path_text ) ) return false;
        if ( self::is_dimension( $value ) ) {
            return (bool) preg_match( '/font[_-]?size|line[_-]?height|letter[_-]?spacing/', $path_text );
        }
        if ( is_string( $value ) || is_numeric( $value ) ) {
            return (bool) preg_match( '/font[_-]?weight|text[_-]?transform|font[_-]?style|text[_-]?decoration/', $path_text );
        }
        return false;
    }

    private static function not_writable_reason( array $control, bool $page_writable, bool $endpoint_available ): string {
        if ( ! $endpoint_available ) return 'Typed design writer endpoint is unavailable.';
        if ( ! $page_writable ) return 'This page is not currently eligible for guarded design writes; it must be an Elementize-managed draft and the authenticated user must be allowed to edit it.';
        if ( 'explicit' !== (string) ( $control['source'] ?? '' ) ) return 'Global or dynamic design controls are read-only.';
        $category = sanitize_key( (string) ( $control['category'] ?? '' ) );
        if ( ! in_array( $category, [ 'spacing', 'alignment', 'typography', 'border_radius' ], true ) ) return 'This category is discovery-only in the current typed writer.';
        return 'This control shape/path is outside the guarded writable subset.';
    }

    private static function is_dimension( $value ): bool {
        return is_array( $value ) && array_key_exists( 'size', $value ) && ( isset( $value['unit'] ) || is_numeric( $value['size'] ) );
    }

    private static function is_box( $value ): bool {
        if ( ! is_array( $value ) ) return false;
        foreach ( [ 'top', 'right', 'bottom', 'left' ] as $side ) {
            if ( ! array_key_exists( $side, $value ) || ! is_numeric( $value[ $side ] ) ) return false;
        }
        return isset( $value['unit'] ) && is_scalar( $value['unit'] );
    }

    private static function status( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['gpt_action_budget_target'] = 25;
        $data['design_settings_read_write_contract'] = true;
        $data['design_settings_write_endpoint_advertised'] = true;
        $data['design_settings_writable_now_classification'] = true;
        $response->set_data( $data );
        return $response;
    }
}

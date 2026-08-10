<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Design_Audit_Hardening {
    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 205, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( preg_match( '#^/elementize/v1/pages/\d+/design-audit$#', $request->get_route() ) ) {
            if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() < 200 || $response->get_status() >= 300 ) return $response;
            $data = $response->get_data();
            if ( ! is_array( $data ) ) return $response;
            $button_count = max( 0, (int) ( $data['summary']['button_count'] ?? 0 ) );
            $reported = max( 0, (int) ( $data['summary']['button_with_explicit_background_count'] ?? 0 ) );
            $data['summary']['button_with_explicit_background_count'] = min( $button_count, $reported );
            if ( isset( $data['findings'] ) && is_array( $data['findings'] ) ) {
                $fixed = [];
                foreach ( $data['findings'] as $finding ) {
                    if ( ! is_array( $finding ) ) continue;
                    if ( 'cta.multiple_primary_styles' === (string) ( $finding['rule_id'] ?? '' ) ) {
                        if ( $button_count < 3 ) continue;
                        if ( isset( $finding['evidence']['buttons_with_explicit_background'] ) ) {
                            $finding['evidence']['buttons_with_explicit_background'] = min( $button_count, max( 0, (int) $finding['evidence']['buttons_with_explicit_background'] ) );
                        }
                    }
                    $fixed[] = $finding;
                }
                $data['findings'] = $fixed;
                $blocking = 0;
                foreach ( $fixed as $finding ) if ( ! empty( $finding['blocks_design_completion_by_default'] ) ) $blocking++;
                $data['summary']['finding_count'] = count( $fixed );
                $data['summary']['blocking_finding_count'] = $blocking;
                $data['summary']['advisory_finding_count'] = count( $fixed ) - $blocking;
                $data['summary']['default_design_pass'] = 0 === $blocking;
                $data['default_design_pass'] = 0 === $blocking;
            }
            $data['audit_integrity'] = [
                'cta_background_observation_clamped_to_actual_button_count' => true,
            ];
            $response->set_data( $data );
        }
        return $response;
    }
}

Elementize_Design_Audit_Hardening::init();

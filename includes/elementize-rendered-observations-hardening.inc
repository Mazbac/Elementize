<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observations', false ) ) return;

final class Elementize_Rendered_Observations_Hardening {
    private const VERSION = '0.21.1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 470, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $observations = is_array( $visual['render_observations'] ?? null ) ? $visual['render_observations'] : [];
        $metrics = is_array( $visual['render_metrics'] ?? null ) ? $visual['render_metrics'] : [];
        if ( ! $observations ) return $response;

        $observations['observation_version'] = self::VERSION;
        $observations['render_metrics_dependency'] = self::dependency_snapshot( $metrics );
        if ( empty( $observations['available'] ) ) {
            $stage = trim( (string) ( $metrics['failure_stage'] ?? '' ) );
            $reason = trim( (string) ( $metrics['reason'] ?? '' ) );
            if ( '' !== $stage ) $observations['dependency_failure_stage'] = $stage;
            if ( '' !== $reason ) $observations['reason'] = 'Rendered metrics unavailable: ' . self::clip( $reason, 420 );
        }
        $observations['automatic_write_allowed'] = false;
        $visual['render_observations'] = $observations;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function dependency_snapshot( array $metrics ): array {
        return [
            'present' => ! empty( $metrics ),
            'available' => ! empty( $metrics['available'] ),
            'render_metrics_version' => (string) ( $metrics['render_metrics_version'] ?? '' ),
            'provider' => (string) ( $metrics['provider'] ?? '' ),
            'failure_stage' => (string) ( $metrics['failure_stage'] ?? '' ),
            'reason' => self::clip( (string) ( $metrics['reason'] ?? '' ), 420 ),
            'page_ready_state' => (string) ( $metrics['page_ready_state'] ?? '' ),
            'stable_poll_count' => is_numeric( $metrics['stable_poll_count'] ?? null ) ? (int) $metrics['stable_poll_count'] : null,
            'navigation_transient_count' => is_numeric( $metrics['navigation_transient_count'] ?? null ) ? (int) $metrics['navigation_transient_count'] : null,
            'section_count' => is_numeric( $metrics['section_count'] ?? null ) ? (int) $metrics['section_count'] : null,
            'read_only' => true,
            'writes_performed' => false,
            'signed_preview_url_exposed' => false,
            'cdp_endpoint_exposed' => false,
            'raw_dom_exposed' => false,
        ];
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( $value );
        return function_exists( 'mb_substr' ) ? mb_substr( $value, 0, $max ) : substr( $value, 0, $max );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_observations_dependency_diagnostics'] = true;
        $data['rendered_visual_observations_dependency_failure_stage'] = true;
        $data['rendered_visual_observations_dependency_reason_propagation'] = true;
        $data['rendered_visual_observations_dependency_snapshot_bounded'] = true;
        $data['rendered_visual_observations_dependency_snapshot_secrets_exposed'] = false;
        $data['rendered_visual_observations_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

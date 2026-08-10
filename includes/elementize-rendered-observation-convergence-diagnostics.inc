<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observation_Convergence', false ) ) return;

final class Elementize_Rendered_Observation_Convergence_Diagnostics {
    private const VERSION = '0.22.2';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 500, 3 );
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
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        if ( ! $conv ) return $response;

        $observations = is_array( $visual['render_observations'] ?? null ) ? $visual['render_observations'] : [];
        $conv['convergence_version'] = self::VERSION;
        $conv['render_observations_dependency'] = self::observation_snapshot( $observations );
        $conv['dependency_diagnostics'] = true;
        $conv['dependency_retry_performed'] = false;
        $conv['automatic_write_allowed'] = false;
        $conv['writes_performed'] = false;

        if ( empty( $conv['available'] ) ) {
            $stage = self::dependency_stage( $observations );
            $reason = trim( (string) ( $observations['reason'] ?? '' ) );
            $conv['dependency_failure_stage'] = $stage;
            if ( '' !== $reason ) {
                $conv['reason'] = 'Fresh rendered observations unavailable: ' . self::clip( $reason, 420 );
            } elseif ( empty( $observations ) ) {
                $conv['reason'] = 'Fresh rendered observations were not present in this completion-audit response.';
            } else {
                $conv['reason'] = 'Fresh rendered observations were present but not available for convergence-to-control mapping.';
            }
            $conv['note'] = 'Convergence did not run because its read-only rendered-observation dependency was unavailable. No duplicate Chrome/vision retry was started inside this request; inspect the bounded dependency snapshot before changing any gate.';
        }

        $visual['repair_convergence'] = $conv;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function observation_snapshot( array $observations ): array {
        $metrics = is_array( $observations['render_metrics_dependency'] ?? null ) ? $observations['render_metrics_dependency'] : [];
        return [
            'present' => ! empty( $observations ),
            'available' => ! empty( $observations['available'] ),
            'observation_version' => (string) ( $observations['observation_version'] ?? '' ),
            'reason' => self::clip( (string) ( $observations['reason'] ?? '' ), 420 ),
            'dependency_failure_stage' => (string) ( $observations['dependency_failure_stage'] ?? '' ),
            'observation_count' => is_numeric( $observations['observation_count'] ?? null ) ? (int) $observations['observation_count'] : null,
            'visually_converged_observation_count' => is_numeric( $observations['visually_converged_observation_count'] ?? null ) ? (int) $observations['visually_converged_observation_count'] : null,
            'render_metrics_dependency' => [
                'present' => ! empty( $metrics['present'] ),
                'available' => ! empty( $metrics['available'] ),
                'render_metrics_version' => (string) ( $metrics['render_metrics_version'] ?? '' ),
                'provider' => (string) ( $metrics['provider'] ?? '' ),
                'failure_stage' => (string) ( $metrics['failure_stage'] ?? '' ),
                'reason' => self::clip( (string) ( $metrics['reason'] ?? '' ), 420 ),
                'page_ready_state' => (string) ( $metrics['page_ready_state'] ?? '' ),
                'stable_poll_count' => is_numeric( $metrics['stable_poll_count'] ?? null ) ? (int) $metrics['stable_poll_count'] : null,
                'navigation_transient_count' => is_numeric( $metrics['navigation_transient_count'] ?? null ) ? (int) $metrics['navigation_transient_count'] : null,
                'section_count' => is_numeric( $metrics['section_count'] ?? null ) ? (int) $metrics['section_count'] : null,
            ],
            'read_only' => true,
            'writes_performed' => false,
            'signed_preview_url_exposed' => false,
            'cdp_endpoint_exposed' => false,
            'raw_dom_exposed' => false,
        ];
    }

    private static function dependency_stage( array $observations ): string {
        $stage = trim( (string) ( $observations['dependency_failure_stage'] ?? '' ) );
        if ( '' !== $stage ) return $stage;
        if ( empty( $observations ) ) return 'render_observations_missing';
        if ( empty( $observations['available'] ) ) return 'render_observations_unavailable';
        return 'convergence_dependency_unresolved';
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
        $data['rendered_visual_observation_convergence_dependency_diagnostics'] = true;
        $data['rendered_visual_observation_convergence_dependency_snapshot_bounded'] = true;
        $data['rendered_visual_observation_convergence_dependency_failure_stage'] = true;
        $data['rendered_visual_observation_convergence_dependency_reason_propagation'] = true;
        $data['rendered_visual_observation_convergence_dependency_retry_performed'] = false;
        $data['rendered_visual_observation_convergence_dependency_snapshot_secrets_exposed'] = false;
        $data['rendered_visual_observation_convergence_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

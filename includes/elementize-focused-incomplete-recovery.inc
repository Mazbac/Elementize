<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Focused_Incomplete_Recovery {
    private const VERSION = '0.24.5';
    private const NS = 'elementize/v1';

    public static function init(): void {
        // The base focused verifier runs at 485. If Gemma returns an incomplete
        // multi-candidate object, degrade every candidate to explicit uncertainty
        // instead of making the whole downstream section-crop backstop unavailable.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 486, 3 );
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
        $focused = is_array( $visual['focused_verification'] ?? null ) ? $visual['focused_verification'] : [];

        if ( ! empty( $focused['available'] ) ) return $response;
        if ( 'vision' !== (string) ( $focused['stage'] ?? '' ) ) return $response;
        if ( 'The focused model output did not assess every exact candidate exactly once.' !== (string) ( $focused['reason'] ?? '' ) ) return $response;

        $candidates = is_array( $focused['candidates'] ?? null ) ? $focused['candidates'] : [];
        if ( ! $candidates ) return $response;

        $assessments = [];
        $seen = [];
        foreach ( $candidates as $candidate ) {
            if ( ! is_array( $candidate ) ) continue;
            $target_id = trim( (string) ( $candidate['target_id'] ?? '' ) );
            if ( '' === $target_id || isset( $seen[$target_id] ) ) continue;
            $seen[$target_id] = true;
            $assessments[] = [
                'target_id' => $target_id,
                'verdict' => 'uncertain',
                'confidence' => 'low',
                'direction' => 'uncertain',
                'evidence' => 'The local model did not return one valid assessment for every exact candidate in the bounded batch, so this candidate is conservatively treated as uncertain.',
                'assessment_consistent' => true,
                'focused_visual_support' => false,
                'eligible_for_hardened_convergence_review' => false,
                'automatic_write_allowed' => false,
                'evidence_visual_grounded' => false,
                'evidence_grounding_reason' => 'incomplete_model_batch_recovered_as_uncertain',
                'incomplete_batch_recovery' => true,
            ];
        }
        if ( ! $assessments ) return $response;

        $focused['available'] = true;
        $focused['stage'] = 'vision_incomplete_recovered';
        $focused['original_failure_reason'] = (string) ( $focused['reason'] ?? '' );
        unset( $focused['reason'] );
        $focused['assessments'] = $assessments;
        $focused['visibly_problematic_count'] = 0;
        $focused['hardened_convergence_review_count'] = 0;
        $focused['grounded_visibly_problematic_count'] = 0;
        $focused['automatic_write_allowed'] = false;
        $focused['incomplete_model_output_recovered'] = true;
        $focused['incomplete_model_output_recovery_version'] = self::VERSION;
        $focused['recovery_policy'] = 'all_exact_candidates_uncertain_no_support';
        $focused['recovered_assessment_count'] = count( $assessments );
        $focused['note'] = 'The local model returned an incomplete multi-candidate assessment. Elementize did not reuse partial or missing model claims; every exact candidate was conservatively degraded to uncertain so independent section-crop backstops may still run. No repair support or write authority was granted.';

        $visual['focused_verification'] = $focused;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['focused_incomplete_recovery_version'] = self::VERSION;
        $data['focused_incomplete_model_batch_degrades_to_uncertain'] = true;
        $data['focused_incomplete_recovery_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

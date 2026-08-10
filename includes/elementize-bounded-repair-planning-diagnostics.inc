<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Bounded_Repair_Planning', false ) ) return;

final class Elementize_Bounded_Repair_Planning_Diagnostics {
    private const VERSION = '0.20.1';
    private const NS = 'elementize/v1';
    private const LARGE_SPACING_PX = 60.0;
    private const MAX_REJECTIONS = 8;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 450, 3 );
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
        $plan = is_array( $visual['repair_plan'] ?? null ) ? $visual['repair_plan'] : [];
        $correlation = is_array( $visual['repair_correlation'] ?? null ) ? $visual['repair_correlation'] : [];
        if ( empty( $plan['available'] ) ) return $response;

        $plan['planning_version'] = self::VERSION;
        $plan['planning_diagnostics'] = self::diagnose( $correlation );
        $plan['automatic_write_allowed'] = false;
        if ( empty( $plan['plans'] ) ) {
            $plan['note'] = 'No bounded proposal was produced. See planning_diagnostics for the exact gate or proposal precondition that rejected the current fresh candidates. Do not invent an edit.';
        }
        $visual['repair_plan'] = $plan;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function diagnose( array $correlation ): array {
        $diag = [
            'enabled' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'correlation_version' => (string) ( $correlation['correlation_version'] ?? '' ),
            'correlation_available' => ! empty( $correlation['available'] ),
            'correlation_planning_ready_count_reported' => absint( $correlation['bounded_value_planning_ready_count'] ?? 0 ),
            'candidate_count_seen' => 0,
            'candidate_count_correlation_ready' => 0,
            'candidate_count_passed_core_gates' => 0,
            'candidate_count_passed_proposal_preconditions' => 0,
            'rejections' => [],
        ];

        foreach ( is_array( $correlation['findings'] ?? null ) ? $correlation['findings'] : [] as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            $finding_category = strtolower( trim( (string) ( $finding['finding_category'] ?? '' ) ) );
            $finding_evidence = is_array( $finding['rendered_finding_evidence'] ?? null ) ? $finding['rendered_finding_evidence'] : [];
            foreach ( is_array( $finding['repair_candidates'] ?? null ) ? $finding['repair_candidates'] : [] as $candidate ) {
                if ( ! is_array( $candidate ) ) continue;
                $diag['candidate_count_seen']++;
                $corr = is_array( $candidate['rendered_correlation'] ?? null ) ? $candidate['rendered_correlation'] : [];
                $reasons = self::core_rejections( $candidate, $corr );
                if ( ! empty( $corr['bounded_value_planning_ready'] ) ) $diag['candidate_count_correlation_ready']++;

                if ( $reasons ) {
                    self::append_rejection( $diag['rejections'], $finding_category, $candidate, $corr, $reasons, 'core_gate' );
                    continue;
                }
                $diag['candidate_count_passed_core_gates']++;

                $proposal_reasons = self::proposal_rejections( $finding_category, $finding_evidence, $candidate, $corr );
                if ( $proposal_reasons ) {
                    self::append_rejection( $diag['rejections'], $finding_category, $candidate, $corr, $proposal_reasons, 'proposal_precondition' );
                    continue;
                }
                $diag['candidate_count_passed_proposal_preconditions']++;
            }
        }

        if ( 0 === $diag['candidate_count_correlation_ready'] ) {
            $diag['primary_diagnosis'] = 'fresh_correlation_has_no_planning_ready_candidate';
            $diag['explanation'] = 'The fresh visual/localization/correlation run did not produce a planning-ready candidate, so the planner correctly returned no proposal.';
        } elseif ( 0 === $diag['candidate_count_passed_core_gates'] ) {
            $diag['primary_diagnosis'] = 'candidate_blocked_by_core_planning_gate';
            $diag['explanation'] = 'At least one candidate was correlation-ready, but it failed a later exact safety gate such as semantic actionability, measurement, scope, writability, or fingerprint presence.';
        } elseif ( 0 === $diag['candidate_count_passed_proposal_preconditions'] ) {
            $diag['primary_diagnosis'] = 'candidate_blocked_by_value_proposal_precondition';
            $diag['explanation'] = 'A candidate passed the core safety gates but the conservative value-proposal rules could not derive a bounded change from the current fresh evidence.';
        } else {
            $diag['primary_diagnosis'] = 'proposal_preconditions_satisfied';
            $diag['explanation'] = 'At least one candidate satisfies the same conservative proposal preconditions. If plans is still empty, inspect planner implementation ordering or ranking.';
        }
        return $diag;
    }

    private static function core_rejections( array $candidate, array $corr ): array {
        $reasons = [];
        if ( empty( $corr['bounded_value_planning_ready'] ) ) $reasons[] = 'correlation_not_planning_ready';
        if ( 'supported' !== (string) ( $corr['causal_support'] ?? '' ) ) $reasons[] = 'causal_support_not_supported';
        if ( empty( $corr['property_measured'] ) ) $reasons[] = 'rendered_property_not_measured';
        if ( 'direct' !== strtolower( (string) ( $candidate['actionability'] ?? '' ) ) ) $reasons[] = 'semantic_actionability_not_direct';
        if ( 'design_control' !== strtolower( (string) ( $candidate['candidate_type'] ?? '' ) ) ) $reasons[] = 'candidate_not_design_control';
        if ( 'base' !== strtolower( (string) ( $candidate['responsive_scope'] ?? 'base' ) ) ) $reasons[] = 'responsive_scope_not_base';
        if ( empty( $candidate['writable_now'] ) ) $reasons[] = 'control_not_writable_now';
        if ( '' === trim( (string) ( $candidate['control_fingerprint'] ?? '' ) ) ) $reasons[] = 'missing_control_fingerprint';
        if ( ! in_array( strtolower( (string) ( $candidate['category'] ?? '' ) ), [ 'spacing', 'typography' ], true ) ) $reasons[] = 'unsupported_plan_category';
        return $reasons;
    }

    private static function proposal_rejections( string $finding, array $finding_evidence, array $candidate, array $corr ): array {
        $category = strtolower( (string) ( $candidate['category'] ?? '' ) );
        if ( 'spacing' === $category ) return self::spacing_rejections( $finding, $finding_evidence, $candidate, $corr );
        if ( 'typography' === $category ) return self::typography_rejections( $finding, $candidate, $corr );
        return [ 'unsupported_proposal_category' ];
    }

    private static function spacing_rejections( string $finding, array $finding_evidence, array $candidate, array $corr ): array {
        $reasons = [];
        if ( ! self::contains_any( $finding, [ 'whitespace', 'spacing', 'rhythm', 'balance', 'flow' ] ) ) $reasons[] = 'finding_category_not_spacing_like';
        $value = $candidate['current_value'] ?? null;
        if ( ! is_array( $value ) || 'px' !== strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) ) $reasons[] = 'current_value_not_px_box';
        $rendered = is_array( $corr['rendered_evidence'] ?? null ) ? $corr['rendered_evidence'] : [];
        $box = is_array( $rendered['box'] ?? null ) ? $rendered['box'] : [];
        if ( ! $box ) $reasons[] = 'rendered_spacing_box_missing';
        if ( ! empty( $finding_evidence['inter_section_gap_is_primary_signal'] ) ) $reasons[] = 'inter_section_gap_is_primary_signal';
        if ( empty( $finding_evidence['large_internal_padding_detected'] ) ) $reasons[] = 'large_internal_padding_not_detected';

        if ( is_array( $value ) && $box ) {
            $matched_large_side = false;
            foreach ( [ 'top', 'bottom', 'right', 'left' ] as $side ) {
                if ( ! isset( $value[$side], $box[$side] ) || ! is_numeric( $value[$side] ) || ! is_numeric( $box[$side] ) ) continue;
                $current = (float) $value[$side];
                $measured = (float) $box[$side];
                if ( $current >= self::LARGE_SPACING_PX && abs( $current - $measured ) <= 1.0 ) { $matched_large_side = true; break; }
            }
            if ( ! $matched_large_side ) $reasons[] = 'no_large_current_side_matches_rendered_box';
        }
        return array_values( array_unique( $reasons ) );
    }

    private static function typography_rejections( string $finding, array $candidate, array $corr ): array {
        $reasons = [];
        if ( ! self::contains_any( $finding, [ 'typograph', 'readab', 'legib', 'hierarchy' ] ) ) $reasons[] = 'finding_category_not_typography_like';
        if ( 'font_size' !== (string) ( $corr['rendered_property'] ?? '' ) ) $reasons[] = 'rendered_property_not_font_size';
        $matches = is_array( $corr['rendered_evidence']['matches'] ?? null ) ? $corr['rendered_evidence']['matches'] : [];
        $small = false;
        foreach ( $matches as $match ) {
            if ( is_array( $match ) && is_numeric( $match['rendered_value'] ?? null ) && (float) $match['rendered_value'] <= 15.0 ) { $small = true; break; }
        }
        if ( ! $small ) $reasons[] = 'no_small_measured_font_size';
        return $reasons;
    }

    private static function append_rejection( array &$out, string $finding, array $candidate, array $corr, array $reasons, string $stage ): void {
        if ( count( $out ) >= self::MAX_REJECTIONS ) return;
        $out[] = [
            'stage' => $stage,
            'finding_category' => $finding,
            'top_level_element_id' => (string) ( $candidate['top_level_element_id'] ?? '' ),
            'element_id' => (string) ( $candidate['element_id'] ?? '' ),
            'category' => (string) ( $candidate['category'] ?? '' ),
            'control_key' => (string) ( $candidate['control_key'] ?? '' ),
            'actionability' => (string) ( $candidate['actionability'] ?? '' ),
            'correlation_planning_ready' => ! empty( $corr['bounded_value_planning_ready'] ),
            'property_measured' => ! empty( $corr['property_measured'] ),
            'causal_support' => (string) ( $corr['causal_support'] ?? '' ),
            'reasons' => array_values( array_unique( $reasons ) ),
        ];
    }

    private static function contains_any( string $haystack, array $needles ): bool {
        foreach ( $needles as $needle ) if ( str_contains( $haystack, $needle ) ) return true;
        return false;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_bounded_repair_planning_diagnostics'] = true;
        $data['rendered_visual_bounded_repair_planning_diagnostics_read_only'] = true;
        $data['rendered_visual_bounded_repair_planning_rejection_reasons'] = true;
        $data['rendered_visual_bounded_repair_planning_correlation_ready_count'] = true;
        $data['rendered_visual_bounded_repair_planning_proposal_precondition_diagnostics'] = true;
        $data['rendered_visual_bounded_repair_planning_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

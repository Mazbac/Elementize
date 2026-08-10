<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Focused_Section_Verification', false ) ) return;

final class Elementize_Converged_Bounded_Repair_Planning {
    private const VERSION = '0.24.2';
    private const NS = 'elementize/v1';
    private const MAX_PLANS = 1;
    private const MIN_SPACING_DELTA_PX = 4.0;
    private const MAX_SPACING_DELTA_PX = 12.0;
    private const LARGE_SPACING_PX = 60.0;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 520, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $convergence = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $section_focus = is_array( $visual['focused_section_verification'] ?? null ) ? $visual['focused_section_verification'] : [];
        $visual['repair_plan'] = self::plan( absint( $m[1] ?? 0 ), $convergence, $section_focus );
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function plan( int $id, array $convergence, array $section_focus ): array {
        $base = [
            'planning_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'advisory_only' => true,
            'automatic_write_allowed' => false,
            'first_experiment_max_changes' => 1,
            'fresh_exact_read_required_before_write' => true,
            'source' => 'final_section_verified_repair_convergence',
            'requires_exact_guarded_control' => true,
            'requires_section_specific_visual_support' => true,
            'requires_semantic_localized_support' => true,
            'requires_current_hash_match' => true,
            'max_spacing_delta_px' => self::MAX_SPACING_DELTA_PX,
            'plans' => [],
            'action_slot_cost' => 0,
        ];

        if ( $id < 1 ) { $base['reason'] = 'A valid page ID is required.'; return $base; }
        if ( empty( $convergence['available'] ) ) {
            $base['reason'] = 'Final repair convergence is unavailable.';
            return $base;
        }

        $source_hash = trim( (string) ( $convergence['page_content_hash'] ?? '' ) );
        $current_hash = self::content_hash( $id );
        if ( '' === $source_hash || '' === $current_hash || ! hash_equals( $source_hash, $current_hash ) ) {
            $base['reason'] = 'The page changed before post-convergence bounded planning. Re-run the completion audit.';
            $base['stage'] = 'stale_hash';
            return $base;
        }

        $eligible = [];
        foreach ( is_array( $convergence['promoted_targets'] ?? null ) ? $convergence['promoted_targets'] : [] as $target ) {
            if ( ! is_array( $target ) ) continue;
            if ( empty( $target['exact_control_match'] ) ) continue;
            if ( empty( $target['section_specific_visual_support'] ) || empty( $target['semantic_localized_support'] ) ) continue;
            if ( empty( $target['eligible_for_bounded_planning_review'] ) ) continue;
            if ( 'promoted_read_only_section_verified' !== (string) ( $target['promotion_status'] ?? '' ) ) continue;
            if ( 'base' !== strtolower( (string) ( $target['responsive_scope'] ?? 'base' ) ) ) continue;
            if ( '' === trim( (string) ( $target['control_fingerprint'] ?? '' ) ) continue;
            if ( 'spacing' !== strtolower( (string) ( $target['category'] ?? '' ) ) ) continue;
            if ( 'large_internal_padding_present' !== (string) ( $target['observation_type'] ?? '' ) ) continue;
            if ( 'padding' !== (string) ( $target['rendered_property'] ?? '' ) ) continue;

            $proposal = self::spacing_proposal( $target );
            if ( ! is_array( $proposal ) ) continue;
            $eligible[] = [
                'target' => $target,
                'proposal' => $proposal,
                'rank' => self::rank( $target ),
            ];
        }

        usort( $eligible, static function ( array $a, array $b ): int {
            $rank = (int) ( $b['rank'] ?? 0 ) <=> (int) ( $a['rank'] ?? 0 );
            if ( 0 !== $rank ) return $rank;
            return strcmp( (string) ( $a['target']['target_id'] ?? '' ), (string) ( $b['target']['target_id'] ?? '' ) );
        } );

        foreach ( array_slice( $eligible, 0, self::MAX_PLANS ) as $entry ) {
            $target = $entry['target'];
            $proposal = $entry['proposal'];
            $identity = [
                'page_content_hash' => $current_hash,
                'target_id' => (string) ( $target['target_id'] ?? '' ),
                'top_level_element_id' => (string) ( $target['top_level_element_id'] ?? '' ),
                'element_id' => (string) ( $target['element_id'] ?? '' ),
                'setting_path' => is_array( $target['setting_path'] ?? null ) ? array_values( $target['setting_path'] ) : [],
                'control_fingerprint' => (string) ( $target['control_fingerprint'] ?? '' ),
                'expected_value' => $target['current_value'] ?? null,
                'proposed_value' => $proposal['proposed_value'],
            ];
            $plan_hash = hash( 'sha256', (string) wp_json_encode( $identity, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) );
            $base['plans'][] = [
                'plan_id' => substr( $plan_hash, 0, 20 ),
                'source_target_id' => (string) ( $target['target_id'] ?? '' ),
                'observation_type' => (string) ( $target['observation_type'] ?? '' ),
                'top_level_element_id' => (string) ( $target['top_level_element_id'] ?? '' ),
                'element_id' => (string) ( $target['element_id'] ?? '' ),
                'widget_type' => $target['widget_type'] ?? null,
                'category' => (string) ( $target['category'] ?? '' ),
                'control_key' => (string) ( $target['control_key'] ?? '' ),
                'setting_path' => is_array( $target['setting_path'] ?? null ) ? array_values( $target['setting_path'] ) : [],
                'responsive_scope' => (string) ( $target['responsive_scope'] ?? 'base' ),
                'expected_current_value' => $target['current_value'] ?? null,
                'proposed_value' => $proposal['proposed_value'],
                'delta' => $proposal['delta'],
                'changed_component' => $proposal['changed_component'],
                'control_fingerprint' => (string) ( $target['control_fingerprint'] ?? '' ),
                'writer_route' => (string) ( $target['writer_route'] ?? ( 'POST /' . self::NS . '/pages/{id}/design-settings' ) ),
                'rendered_property' => (string) ( $target['rendered_property'] ?? '' ),
                'rendered_value' => $target['rendered_value'] ?? null,
                'section_specific_visual_support' => true,
                'semantic_localized_support' => true,
                'localized_support' => is_array( $target['localized_support'] ?? null ) ? $target['localized_support'] : [],
                'section_crop_required_for_this_plan' => false,
                'section_crop_backstop_available' => ! empty( $section_focus['available'] ),
                'planning_confidence' => 'medium',
                'planning_reason' => $proposal['reason'],
                'bounded_change' => true,
                'single_change_only' => true,
                'automatic_write_allowed' => false,
                'fresh_exact_read_required_before_write' => true,
                'experiment_requirements' => [
                    're_read_same_control_and_hash' => true,
                    'expected_value_must_still_match' => true,
                    'control_fingerprint_must_still_match' => true,
                    'create_revision' => true,
                    'verify_saved_value' => true,
                    'rerun_completion_audit_with_visual' => true,
                    'compare_before_after_render_metrics' => true,
                    'compare_before_after_visual_findings' => true,
                    'rollback_on_write_or_verification_failure' => true,
                    'rollback_on_clear_visual_or_metric_regression' => true,
                ],
                'success_signals' => [
                    'target_rendered_spacing_moves_by_expected_delta' => true,
                    'inter_section_gap_does_not_increase' => true,
                    'section_height_does_not_increase' => true,
                    'no_new_major_visual_finding' => true,
                ],
            ];
        }

        $base['available'] = true;
        $base['page_content_hash'] = $current_hash;
        $base['convergence_hash_matches_current'] = true;
        $base['convergence_promoted_target_count'] = absint( $convergence['promoted_target_count'] ?? 0 );
        $base['eligible_candidate_count'] = count( $eligible );
        $base['planned_change_count'] = count( $base['plans'] );
        $base['automatic_write_allowed'] = false;
        $base['note'] = $base['plans']
            ? 'A conservative one-control spacing proposal was derived only after final section-specific semantic convergence. This object is read-only and grants no write authority; the exact hash, expected value, setting path, and control fingerprint must be freshly re-read before any guarded experiment.'
            : 'No final section-verified convergence target could be converted into a conservative bounded spacing proposal. Do not invent an edit.';
        return $base;
    }

    private static function spacing_proposal( array $target ): ?array {
        $side = strtolower( trim( (string) ( $target['matched_component'] ?? '' ) ) );
        if ( ! in_array( $side, [ 'top', 'bottom', 'left', 'right' ], true ) ) return null;
        $rendered = is_numeric( $target['rendered_value'] ?? null ) ? (float) $target['rendered_value'] : null;
        $value = $target['current_value'] ?? null;
        if ( null === $rendered || $rendered < self::LARGE_SPACING_PX ) return null;
        if ( ! is_array( $value ) || 'px' !== strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) || ! is_numeric( $value[$side] ?? null ) ) return null;
        $current = (float) $value[$side];
        if ( abs( $current - $rendered ) > 1.0 ) return null;

        $delta = max( self::MIN_SPACING_DELTA_PX, min( self::MAX_SPACING_DELTA_PX, round( $current * 0.10, 0 ) ) );
        $new_value = max( 0.0, $current - $delta );
        if ( abs( $new_value - $current ) < 0.1 ) return null;
        $proposed = $value;
        $proposed[$side] = self::number_string( $new_value );
        return [
            'proposed_value' => $proposed,
            'delta' => -$delta,
            'changed_component' => $side,
            'reason' => sprintf( 'This exact %s padding control renders at %.0fpx and has fresh section-specific semantic visual support. Reduce only that side by a conservative %.0fpx (about 10%%, capped) for the first reversible experiment.', $side, $rendered, $delta ),
        ];
    }

    private static function rank( array $target ): int {
        $rendered = is_numeric( $target['rendered_value'] ?? null ) ? (int) round( (float) $target['rendered_value'] ) : 0;
        $confidence = strtolower( (string) ( $target['localized_support']['confidence'] ?? '' ) );
        $bonus = 'high' === $confidence ? 10000 : ( 'medium' === $confidence ? 5000 : 0 );
        return $bonus + $rendered;
    }

    private static function content_hash( int $id ): string {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return '';
        try {
            $doc = \Elementor\Plugin::$instance->documents->get( $id );
            $elements = $doc ? $doc->get_elements_data() : null;
            if ( ! is_array( $elements ) ) return '';
            $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
            return hash( 'sha256', false === $json ? '' : $json );
        } catch ( Throwable $ignored ) { return ''; }
    }

    private static function number_string( float $value ) {
        return abs( $value - round( $value ) ) < 0.0001 ? (string) (int) round( $value ) : rtrim( rtrim( number_format( $value, 2, '.', '' ), '0' ), '.' );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_post_convergence_bounded_planning'] = true;
        $data['rendered_visual_post_convergence_bounded_planning_read_only'] = true;
        $data['rendered_visual_post_convergence_bounded_planning_max_changes'] = self::MAX_PLANS;
        $data['rendered_visual_post_convergence_bounded_planning_automatic_write_allowed'] = false;
        $data['rendered_visual_post_convergence_bounded_planning_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

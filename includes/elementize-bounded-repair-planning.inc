<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Repair_Correlation_Hardening', false ) ) return;

final class Elementize_Bounded_Repair_Planning {
    private const VERSION = '0.20.0';
    private const NS = 'elementize/v1';
    private const MAX_PLANS = 1;
    private const MIN_SPACING_DELTA_PX = 4.0;
    private const MAX_SPACING_DELTA_PX = 12.0;
    private const LARGE_SPACING_PX = 60.0;
    private const MAX_FONT_DELTA_PX = 2.0;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 440, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $correlation = is_array( $visual['repair_correlation'] ?? null ) ? $visual['repair_correlation'] : [];
        $visual['repair_plan'] = self::plan( absint( $m[1] ?? 0 ), $correlation );
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function plan( int $id, array $correlation ): array {
        $base = [
            'planning_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'advisory_only' => true,
            'automatic_write_allowed' => false,
            'first_experiment_max_changes' => 1,
            'fresh_exact_read_required_before_write' => true,
            'supported_plan_categories' => [ 'spacing', 'typography' ],
            'max_spacing_delta_px' => self::MAX_SPACING_DELTA_PX,
            'max_font_delta_px' => self::MAX_FONT_DELTA_PX,
            'plans' => [],
            'action_slot_cost' => 0,
        ];
        if ( $id < 1 ) { $base['reason'] = 'A valid page ID is required.'; return $base; }
        if ( empty( $correlation['available'] ) || empty( $correlation['repair_discovery_hash_matches_current'] ) ) {
            $base['reason'] = 'Fresh rendered repair correlation is required before bounded value planning.';
            return $base;
        }
        $source_hash = trim( (string) ( $correlation['page_content_hash'] ?? '' ) );
        $current_hash = self::content_hash( $id );
        if ( '' === $source_hash || '' === $current_hash || ! hash_equals( $source_hash, $current_hash ) ) {
            $base['reason'] = 'The page changed before bounded value planning. Re-run the completion audit.';
            $base['stage'] = 'stale_hash';
            return $base;
        }

        $eligible = [];
        foreach ( is_array( $correlation['findings'] ?? null ) ? $correlation['findings'] : [] as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            $finding_category = strtolower( trim( (string) ( $finding['finding_category'] ?? '' ) ) );
            $finding_evidence = is_array( $finding['rendered_finding_evidence'] ?? null ) ? $finding['rendered_finding_evidence'] : [];
            foreach ( is_array( $finding['repair_candidates'] ?? null ) ? $finding['repair_candidates'] : [] as $candidate ) {
                if ( ! is_array( $candidate ) ) continue;
                $corr = is_array( $candidate['rendered_correlation'] ?? null ) ? $candidate['rendered_correlation'] : [];
                if ( empty( $corr['bounded_value_planning_ready'] ) ) continue;
                if ( 'supported' !== (string) ( $corr['causal_support'] ?? '' ) || empty( $corr['property_measured'] ) ) continue;
                if ( 'direct' !== strtolower( (string) ( $candidate['actionability'] ?? '' ) ) ) continue;
                if ( 'design_control' !== strtolower( (string) ( $candidate['candidate_type'] ?? '' ) ) ) continue;
                if ( 'base' !== strtolower( (string) ( $candidate['responsive_scope'] ?? 'base' ) ) ) continue;
                if ( empty( $candidate['writable_now'] ) || '' === trim( (string) ( $candidate['control_fingerprint'] ?? '' ) ) ) continue;
                $category = strtolower( (string) ( $candidate['category'] ?? '' ) );
                if ( ! in_array( $category, [ 'spacing', 'typography' ], true ) ) continue;
                $proposal = self::proposal( $finding_category, $finding_evidence, $candidate, $corr );
                if ( ! is_array( $proposal ) ) continue;
                $eligible[] = [
                    'finding_index' => absint( $finding['finding_index'] ?? 0 ),
                    'finding_category' => $finding_category,
                    'candidate' => $candidate,
                    'correlation' => $corr,
                    'proposal' => $proposal,
                    'rank' => self::rank( $finding_category, $candidate, $corr, $proposal ),
                ];
            }
        }

        usort( $eligible, static function( $a, $b ) {
            $rank = (int) ( $b['rank'] ?? 0 ) <=> (int) ( $a['rank'] ?? 0 );
            if ( 0 !== $rank ) return $rank;
            $aid = (string) ( $a['candidate']['element_id'] ?? '' );
            $bid = (string) ( $b['candidate']['element_id'] ?? '' );
            return strcmp( $aid, $bid );
        } );

        foreach ( array_slice( $eligible, 0, self::MAX_PLANS ) as $entry ) {
            $candidate = $entry['candidate'];
            $corr = $entry['correlation'];
            $proposal = $entry['proposal'];
            $identity = [
                'page_content_hash' => $current_hash,
                'finding_index' => $entry['finding_index'],
                'finding_category' => $entry['finding_category'],
                'top_level_element_id' => (string) ( $candidate['top_level_element_id'] ?? '' ),
                'element_id' => (string) ( $candidate['element_id'] ?? '' ),
                'setting_path' => is_array( $candidate['setting_path'] ?? null ) ? array_values( $candidate['setting_path'] ) : [],
                'control_fingerprint' => (string) ( $candidate['control_fingerprint'] ?? '' ),
                'expected_value' => $candidate['current_value'] ?? null,
                'proposed_value' => $proposal['proposed_value'],
            ];
            $plan_hash = hash( 'sha256', (string) wp_json_encode( $identity, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) );
            $base['plans'][] = [
                'plan_id' => substr( $plan_hash, 0, 20 ),
                'finding_index' => $entry['finding_index'],
                'finding_category' => $entry['finding_category'],
                'top_level_element_id' => (string) ( $candidate['top_level_element_id'] ?? '' ),
                'element_id' => (string) ( $candidate['element_id'] ?? '' ),
                'widget_type' => $candidate['widget_type'] ?? null,
                'category' => (string) ( $candidate['category'] ?? '' ),
                'control_key' => (string) ( $candidate['control_key'] ?? '' ),
                'setting_path' => is_array( $candidate['setting_path'] ?? null ) ? array_values( $candidate['setting_path'] ) : [],
                'responsive_scope' => (string) ( $candidate['responsive_scope'] ?? 'base' ),
                'expected_current_value' => $candidate['current_value'] ?? null,
                'proposed_value' => $proposal['proposed_value'],
                'delta' => $proposal['delta'],
                'changed_component' => $proposal['changed_component'],
                'control_fingerprint' => (string) ( $candidate['control_fingerprint'] ?? '' ),
                'writer_route' => (string) ( $candidate['writer_route'] ?? ( 'POST /' . self::NS . '/pages/{id}/design-settings' ) ),
                'rendered_property' => (string) ( $corr['rendered_property'] ?? '' ),
                'rendered_evidence' => is_array( $corr['rendered_evidence'] ?? null ) ? $corr['rendered_evidence'] : [],
                'property_measured' => true,
                'causal_support' => 'supported',
                'planning_confidence' => $proposal['confidence'],
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
                    'rerun_visual_audit' => true,
                    'rerun_render_metrics' => true,
                    'compare_before_after' => true,
                    'rollback_on_write_or_verification_failure' => true,
                    'rollback_on_clear_visual_or_metric_regression' => true,
                ],
                'success_signals' => $proposal['success_signals'],
            ];
        }

        $base['available'] = true;
        $base['page_content_hash'] = $current_hash;
        $base['correlation_hash_matches_current'] = true;
        $base['eligible_candidate_count'] = count( $eligible );
        $base['planned_change_count'] = count( $base['plans'] );
        $base['automatic_write_allowed'] = false;
        $base['note'] = $base['plans']
            ? 'A conservative single-control value proposal is available for a later guarded experiment. This object performs no write and does not authorize an automatic write; the exact control/hash/value/fingerprint must be freshly re-read before any experiment.'
            : 'No exact semantically direct rendered-evidence candidate could be converted into a conservative bounded value proposal. Do not invent an edit.';
        return $base;
    }

    private static function proposal( string $finding, array $finding_evidence, array $candidate, array $corr ): ?array {
        $category = strtolower( (string) ( $candidate['category'] ?? '' ) );
        if ( 'spacing' === $category ) return self::spacing_proposal( $finding, $finding_evidence, $candidate, $corr );
        if ( 'typography' === $category ) return self::typography_proposal( $finding, $candidate, $corr );
        return null;
    }

    private static function spacing_proposal( string $finding, array $finding_evidence, array $candidate, array $corr ): ?array {
        if ( ! self::contains_any( $finding, [ 'whitespace', 'spacing', 'rhythm', 'balance', 'flow' ] ) ) return null;
        $value = $candidate['current_value'] ?? null;
        if ( ! is_array( $value ) || 'px' !== strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) ) return null;
        $rendered = is_array( $corr['rendered_evidence'] ?? null ) ? $corr['rendered_evidence'] : [];
        $box = is_array( $rendered['box'] ?? null ) ? $rendered['box'] : [];
        if ( ! $box ) return null;

        $primary_gap = ! empty( $finding_evidence['inter_section_gap_is_primary_signal'] );
        $large_internal = ! empty( $finding_evidence['large_internal_padding_detected'] );
        if ( $primary_gap || ! $large_internal ) return null;

        $best_side = '';
        $best = 0.0;
        foreach ( [ 'top', 'bottom', 'right', 'left' ] as $side ) {
            if ( ! isset( $value[$side], $box[$side] ) || ! is_numeric( $value[$side] ) || ! is_numeric( $box[$side] ) ) continue;
            $current = (float) $value[$side];
            $measured = (float) $box[$side];
            if ( $current < self::LARGE_SPACING_PX || abs( $current - $measured ) > 1.0 ) continue;
            if ( $current > $best ) { $best = $current; $best_side = $side; }
        }
        if ( '' === $best_side ) return null;

        $delta = max( self::MIN_SPACING_DELTA_PX, min( self::MAX_SPACING_DELTA_PX, round( $best * 0.10, 0 ) ) );
        $new_value = max( 0.0, $best - $delta );
        if ( abs( $new_value - $best ) < 0.1 ) return null;
        $proposed = $value;
        $proposed[$best_side] = self::number_string( $new_value );

        return [
            'proposed_value' => $proposed,
            'delta' => -$delta,
            'changed_component' => $best_side,
            'confidence' => 'medium',
            'reason' => sprintf( 'The localized whitespace finding has no large inter-section gap, while this exact top-level %s side renders at %.0fpx. Reduce only that one side by a conservative %.0fpx (about 10%%, capped) rather than changing the whole layout.', $best_side, $best, $delta ),
            'success_signals' => [
                'target_rendered_spacing_moves_by_expected_delta' => true,
                'inter_section_gap_does_not_increase' => true,
                'section_height_does_not_increase' => true,
                'no_new_major_visual_finding' => true,
            ],
        ];
    }

    private static function typography_proposal( string $finding, array $candidate, array $corr ): ?array {
        if ( ! self::contains_any( $finding, [ 'typograph', 'readab', 'legib', 'hierarchy' ] ) ) return null;
        $property = (string) ( $corr['rendered_property'] ?? '' );
        if ( 'font_size' !== $property ) return null;
        $rendered = is_array( $corr['rendered_evidence'] ?? null ) ? $corr['rendered_evidence'] : [];
        $matches = is_array( $rendered['matches'] ?? null ) ? $rendered['matches'] : [];
        $measured = null;
        foreach ( $matches as $match ) {
            if ( is_array( $match ) && is_numeric( $match['rendered_value'] ?? null ) ) { $measured = (float) $match['rendered_value']; break; }
        }
        if ( null === $measured || $measured > 15.0 ) return null;

        $value = $candidate['current_value'] ?? null;
        $delta = $measured <= 13.0 ? self::MAX_FONT_DELTA_PX : 1.0;
        if ( is_array( $value ) && 'px' === strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) && is_numeric( $value['size'] ?? null ) ) {
            $current = (float) $value['size'];
            if ( abs( $current - $measured ) > 1.0 ) return null;
            $proposed = $value;
            $proposed['size'] = $current + $delta;
        } elseif ( is_numeric( $value ) ) {
            $current = (float) $value;
            if ( abs( $current - $measured ) > 1.0 ) return null;
            $proposed = $current + $delta;
        } else return null;

        return [
            'proposed_value' => $proposed,
            'delta' => $delta,
            'changed_component' => 'font_size',
            'confidence' => 'medium',
            'reason' => sprintf( 'This exact base-scope font-size control renders at %.0fpx and is directly correlated to the localized typography finding. Increase only the font size by %.0fpx for a reversible readability experiment.', $measured, $delta ),
            'success_signals' => [
                'target_rendered_font_size_moves_by_expected_delta' => true,
                'text_remains_visible_and_non_overflowing' => true,
                'section_height_regression_is_bounded' => true,
                'no_new_major_visual_finding' => true,
            ],
        ];
    }

    private static function rank( string $finding, array $candidate, array $corr, array $proposal ): int {
        $score = 100;
        if ( 'exact_top_level' === (string) ( $corr['evidence_match'] ?? '' ) ) $score += 20;
        if ( 'spacing' === (string) ( $candidate['category'] ?? '' ) && self::contains_any( $finding, [ 'whitespace', 'spacing' ] ) ) $score += 15;
        if ( 'typography' === (string) ( $candidate['category'] ?? '' ) ) $score += 10;
        if ( 'medium' === (string) ( $proposal['confidence'] ?? '' ) ) $score += 5;
        return $score;
    }

    private static function number_string( float $value ): string {
        if ( abs( $value - round( $value ) ) < 0.0001 ) return (string) (int) round( $value );
        return rtrim( rtrim( number_format( $value, 2, '.', '' ), '0' ), '.' );
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
        $data['rendered_visual_bounded_repair_planning'] = true;
        $data['rendered_visual_bounded_repair_planning_read_only'] = true;
        $data['rendered_visual_bounded_repair_planning_single_change'] = true;
        $data['rendered_visual_bounded_repair_planning_fresh_hash_guard'] = true;
        $data['rendered_visual_bounded_repair_planning_exact_rendered_evidence_only'] = true;
        $data['rendered_visual_bounded_repair_planning_spacing_max_delta_px'] = self::MAX_SPACING_DELTA_PX;
        $data['rendered_visual_bounded_repair_planning_font_max_delta_px'] = self::MAX_FONT_DELTA_PX;
        $data['rendered_visual_bounded_repair_planning_automatic_write_allowed'] = false;
        $data['rendered_visual_bounded_repair_planning_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

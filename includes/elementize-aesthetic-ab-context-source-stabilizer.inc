<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Judgment', false ) ) return;

/**
 * Deterministic source stabilizer for contextual Pixfort A/B judgment.
 *
 * A fact-conditioned grounded reassessment is intentionally skipped when the earlier
 * deterministic semantic-grounding pass is already complete and transition-valid.
 * In that healthy case the reassessment diagnostic contains needed=false and no
 * section_ledger. The 0.27.2 context recovery was reassessment-ledger-only, so it
 * could reject a better audit merely because the expensive reassessment was not
 * required.
 *
 * This pass runs after the existing A/B context hardener and recovers only from a
 * complete deterministic semantic-grounding ledger: exact section count, zero
 * semantic conflicts, zero material conflicts, valid transitions, usable rhythm,
 * and sequential S1..Sn markers. It caches the same read-only context shape keyed to
 * the exact current Elementor state. No page mutation capability is introduced.
 */
final class Elementize_Aesthetic_AB_Context_Source_Stabilizer {
    private const VERSION = '0.27.4-s1';
    private const NS = 'elementize/v1';
    private const CONTEXT_TTL = 900;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 576, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $page_id = absint( $m[1] ?? 0 );
        if ( $page_id < 1 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) ) return $response;
        $visual = $data['visual'];
        $base = is_array( $visual['aesthetic_ab_context'] ?? null ) ? $visual['aesthetic_ab_context'] : [];
        if ( ! empty( $base['available'] ) ) return $response;
        if ( 'Professional visual approval is not yet fully grounded and usable.' !== trim( (string) ( $base['reason'] ?? '' ) ) ) return $response;

        $semantic = is_array( $visual['aesthetic_semantic_grounding'] ?? null ) ? $visual['aesthetic_semantic_grounding'] : [];
        $reassessment = is_array( $visual['aesthetic_grounded_reassessment'] ?? null ) ? $visual['aesthetic_grounded_reassessment'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];
        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $dna = is_array( $visual['design_dna'] ?? null ) ? $visual['design_dna'] : [];

        $stabilization = [
            'version' => self::VERSION,
            'attempted' => true,
            'succeeded' => false,
            'source' => 'deterministic_semantic_grounding',
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'reassessment_needed' => array_key_exists( 'needed', $reassessment ) ? ! empty( $reassessment['needed'] ) : null,
        ];

        $eligibility = self::semantic_eligibility( $semantic, $coherence );
        $stabilization['eligibility'] = $eligibility;
        if ( empty( $eligibility['eligible'] ) ) {
            $stabilization['reason'] = 'Deterministic semantic grounding is not complete enough to stabilize contextual A/B readiness.';
            $base['context_source_stabilization'] = $stabilization;
            $visual['aesthetic_ab_context'] = $base;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $snapshot = self::build_snapshot( $page_id, $dna, $art, $coherence, $semantic );
        if ( is_wp_error( $snapshot ) ) {
            $stabilization['reason'] = $snapshot->get_error_message();
            $base['context_source_stabilization'] = $stabilization;
            $visual['aesthetic_ab_context'] = $base;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $key = self::cache_key( $page_id, (string) $snapshot['page_state_hash'] );
        set_transient( $key, $snapshot, self::CONTEXT_TTL );

        $stabilization['succeeded'] = true;
        $stabilization['context_evidence_usable'] = true;
        $stabilization['marker_count'] = count( (array) ( $snapshot['section_ledger'] ?? [] ) );
        $stabilization['policy_note'] = 'A completed deterministic semantic-grounding ledger is a valid A/B context source when fact-conditioned reassessment is unnecessary. Candidate selection remains separately confidence-calibrated and read-only.';

        $base['version'] = (string) ( $base['version'] ?? '0.27.0-a1' );
        $base['available'] = true;
        $base['read_only'] = true;
        $base['writes_performed'] = false;
        $base['automatic_write_allowed'] = false;
        $base['action_slot_cost'] = 0;
        $base['context_ttl_seconds'] = self::CONTEXT_TTL;
        $base['page_id'] = $page_id;
        $base['page_state_hash'] = (string) $snapshot['page_state_hash'];
        $base['captured_gmt'] = (string) $snapshot['captured_gmt'];
        $base['grounding_source'] = (string) $snapshot['grounding_source'];
        $base['marker_count'] = count( (array) ( $snapshot['section_ledger'] ?? [] ) );
        $base['context_evidence_complete'] = true;
        $base['overall_approval_usable'] = ! empty( $art['approval_usable'] );
        $base['context_source_stabilization'] = $stabilization;
        $base['note'] = 'Grounded A/B context stabilized from deterministic semantic grounding because fact-conditioned reassessment was unnecessary or unavailable for this audit run.';
        unset( $base['reason'] );

        $visual['aesthetic_ab_context'] = $base;
        $data['visual'] = $visual;
        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_ab_context_ready'] = true;
        $data['summary']['aesthetic_ab_context_stabilized_from_semantic_grounding'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function semantic_eligibility( array $semantic, array $coherence ): array {
        $rows = is_array( $semantic['section_grounding'] ?? null ) ? array_values( $semantic['section_grounding'] ) : [];
        $expected = max( 0, absint( $semantic['section_fact_count'] ?? 0 ) );
        $semantic_conflicts = max( 0, absint( $semantic['semantic_conflict_count'] ?? 0 ) );
        $material_conflicts = max( 0, absint( $semantic['material_semantic_conflict_count'] ?? 0 ) );

        $sequential = $expected > 0 && count( $rows ) === $expected;
        $transitions_valid = $sequential;
        if ( $sequential ) {
            foreach ( $rows as $index => $row ) {
                if ( ! is_array( $row ) ) { $sequential = false; $transitions_valid = false; break; }
                $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
                if ( 'S' . ( $index + 1 ) !== $marker ) $sequential = false;
                if ( empty( $row['semantic_grounded'] ) || ! empty( $row['semantic_conflicts'] ) ) $sequential = false;
                $transition = strtolower( trim( (string) ( $row['transition_from_previous'] ?? '' ) ) );
                $transition_ok = 0 === $index ? 'n/a' === $transition : in_array( $transition, [ 'smooth', 'neutral', 'abrupt', 'repetitive' ], true );
                if ( ! $transition_ok || empty( $row['transition_contract_valid'] ) ) $transitions_valid = false;
            }
        }

        $checks = [
            'semantic_grounding_available' => ! empty( $semantic['available'] ),
            'approval_grounding_complete' => ! empty( $semantic['approval_grounding_complete'] ),
            'section_grounding_present' => ! empty( $rows ),
            'section_count_matches_exact_facts' => $expected > 0 && count( $rows ) === $expected,
            'section_markers_sequential_and_semantically_grounded' => $sequential,
            'semantic_conflict_count_zero' => 0 === $semantic_conflicts,
            'material_semantic_conflict_count_zero' => 0 === $material_conflicts,
            'semantic_transition_contract_valid' => ! empty( $semantic['transition_contract_valid'] ) && $transitions_valid,
            'semantic_rhythm_judgment_usable' => ! empty( $semantic['rhythm_judgment_usable'] ),
            'coherence_transition_contract_valid' => ! empty( $coherence['transition_contract_valid'] ),
            'coherence_rhythm_judgment_usable' => ! empty( $coherence['rhythm_judgment_usable'] ),
        ];
        $checks['eligible'] = ! in_array( false, array_values( $checks ), true );
        return $checks;
    }

    private static function build_snapshot( int $page_id, array $dna, array $art, array $coherence, array $semantic ) {
        $ledger = [];
        foreach ( is_array( $semantic['section_grounding'] ?? null ) ? $semantic['section_grounding'] : [] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            if ( ! preg_match( '/^S\d{1,2}$/', $marker ) ) continue;
            $ledger[] = [
                'marker' => $marker,
                'top_level_element_id' => self::clip( (string) ( $row['top_level_element_id'] ?? '' ), 32 ),
                'visual_intensity' => self::clamp_score( $row['visual_intensity'] ?? 1 ),
                'transition_from_previous' => self::enum( $row['transition_from_previous'] ?? '', [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ], 'neutral' ),
                'coherence_fit' => self::enum( $row['effective_coherence_fit'] ?? $row['coherence_fit'] ?? '', [ 'fits', 'mixed', 'violates' ], 'mixed' ),
                'issue_category' => self::enum( $row['issue_category'] ?? '', [ 'none', 'hierarchy', 'spacing', 'density', 'typography', 'imagery', 'component_style', 'composition', 'transition', 'conversion', 'section_choice', 'other' ], 'none' ),
                'severity' => self::enum( $row['severity'] ?? '', [ 'none', 'low', 'medium', 'high' ], 'none' ),
                'evidence' => self::clip( (string) ( $row['evidence'] ?? '' ), 320 ),
                'recommendation' => self::clip( (string) ( $row['recommendation'] ?? '' ), 320 ),
                'intervention_level' => self::enum( $row['grounded_intervention_level'] ?? $row['calibrated_intervention_level'] ?? $row['intervention_level'] ?? '', [ 'none', 'tweak', 'content', 'imagery', 'restructure', 'replace_section' ], 'none' ),
            ];
        }
        if ( ! $ledger ) return new WP_Error( 'elementize_ab_context_stabilizer_ledger_invalid', 'Deterministic semantic grounding did not provide a usable section ledger.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_ab_context_stabilizer_hash_unavailable', 'The current Elementor page state could not be fingerprinted while stabilizing A/B context.' );

        return [
            'version' => self::VERSION,
            'page_id' => $page_id,
            'page_state_hash' => $state_hash,
            'captured_gmt' => gmdate( 'Y-m-d H:i:s' ),
            'grounding_source' => 'deterministic_semantic_grounding_context_stabilizer',
            'context_evidence_complete' => true,
            'overall_approval_usable' => ! empty( $art['approval_usable'] ),
            'design_dna' => self::compact_design_dna( $dna ),
            'art_direction' => [
                'grounded_professional_approval' => self::enum( $semantic['grounded_effective_professional_approval'] ?? $art['grounded_effective_professional_approval'] ?? $art['effective_professional_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' ),
                'overall_approval_usable' => ! empty( $art['approval_usable'] ),
                'aesthetic_score' => self::clamp_score( $art['calibrated_aesthetic_score'] ?? $art['aesthetic_score'] ?? 1 ),
                'coherence_score' => self::clamp_score( $art['calibrated_coherence_score'] ?? $art['coherence_score'] ?? 1 ),
                'hierarchy_score' => self::clamp_score( $art['calibrated_hierarchy_score'] ?? $art['hierarchy_score'] ?? 1 ),
                'rhythm_score' => self::clamp_score( $art['calibrated_rhythm_score'] ?? $art['rhythm_score'] ?? 1 ),
                'polish_score' => self::clamp_score( $art['calibrated_polish_score'] ?? $art['polish_score'] ?? 1 ),
                'conversion_focus_score' => self::clamp_score( $art['calibrated_conversion_focus_score'] ?? $art['conversion_focus_score'] ?? 1 ),
                'first_impression' => self::clip( (string) ( $art['first_impression'] ?? '' ), 120 ),
                'grounded_issues' => self::compact_issues( (array) ( $semantic['grounded_issues'] ?? $art['grounded_issues'] ?? [] ) ),
            ],
            'section_coherence' => [
                'effective_verdict' => self::enum( $coherence['effective_coherence_verdict'] ?? $coherence['coherence_verdict'] ?? '', [ 'strong', 'mixed', 'weak' ], 'mixed' ),
                'rhythm_quality' => self::enum( $coherence['rhythm_quality'] ?? '', [ 'strong', 'acceptable', 'weak' ], 'acceptable' ),
                'transition_contract_valid' => true,
                'rhythm_judgment_usable' => true,
            ],
            'section_ledger' => $ledger,
        ];
    }

    private static function compact_design_dna( array $dna ): array {
        $out = [];
        foreach ( [ 'aesthetic_direction', 'visual_density', 'whitespace_character', 'typography_character', 'geometry_language', 'color_behavior', 'imagery_language', 'layout_language', 'component_language', 'restraint_level', 'effective_design_system_coherence' ] as $key ) {
            if ( ! array_key_exists( $key, $dna ) ) continue;
            $value = $dna[ $key ];
            if ( is_array( $value ) ) {
                $out[$key] = array_values( array_slice( array_filter( array_map( static fn ( $v ): string => self::clip( (string) $v, 100 ), $value ) ), 0, 8 ) );
            } elseif ( is_scalar( $value ) ) {
                $out[$key] = self::clip( (string) $value, 180 );
            }
        }
        return $out;
    }

    private static function compact_issues( array $issues ): array {
        $out = [];
        foreach ( $issues as $issue ) {
            if ( ! is_array( $issue ) ) continue;
            $out[] = [
                'category' => self::clip( (string) ( $issue['category'] ?? '' ), 40 ),
                'severity' => self::enum( $issue['severity'] ?? '', [ 'low', 'medium', 'high' ], 'medium' ),
                'section_markers' => array_values( array_slice( array_filter( array_map( 'strval', (array) ( $issue['section_markers'] ?? [] ) ) ), 0, 4 ) ),
                'evidence' => self::clip( (string) ( $issue['evidence'] ?? '' ), 280 ),
                'recommendation' => self::clip( (string) ( $issue['recommendation'] ?? '' ), 280 ),
                'intervention_level' => self::enum( $issue['grounded_intervention_level'] ?? $issue['calibrated_intervention_level'] ?? $issue['intervention_level'] ?? '', [ 'none', 'tweak', 'content', 'imagery', 'restructure', 'replace_section' ], 'none' ),
            ];
            if ( count( $out ) >= 8 ) break;
        }
        return $out;
    }

    private static function page_state_hash( int $page_id ): string {
        $raw = get_post_meta( $page_id, '_elementor_data', true );
        if ( is_string( $raw ) ) return hash( 'sha256', $raw );
        $json = wp_json_encode( $raw, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) ? hash( 'sha256', $json ) : '';
    }

    private static function cache_key( int $page_id, string $hash ): string {
        return 'elementize_ab_' . $page_id . '_' . substr( preg_replace( '/[^a-f0-9]/i', '', $hash ), 0, 24 );
    }

    private static function enum( $value, array $allowed, string $fallback ): string {
        $value = strtolower( trim( (string) $value ) );
        return in_array( $value, $allowed, true ) ? $value : $fallback;
    }

    private static function clamp_score( $value ): int {
        $score = is_numeric( $value ) ? (int) round( (float) $value ) : 1;
        return max( 1, min( 10, $score ) );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\s+/u', ' ', $value ) ?? $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_ab_context_source_stabilizer_version'] = self::VERSION;
        $data['aesthetic_ab_context_source_stabilizer'] = true;
        $data['aesthetic_ab_context_source_stabilizer_read_only'] = true;
        $data['aesthetic_ab_context_source_stabilizer_action_slot_cost'] = 0;
        $data['aesthetic_ab_context_source_stabilizer_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

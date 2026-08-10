<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Grounded_Reassessment', false ) ) return;

/**
 * Deterministic acceptance hardening for the fact-conditioned aesthetic reassessment.
 *
 * The local model may still make two classes of contract mistake even after it is
 * given exact Elementor facts: it can describe a transition for S1 (which has no
 * previous section), and it can quote implementation-looking tokens such as
 * `secondary-font` as though they were visible copy. Neither is aesthetic evidence.
 * This pass normalizes only those mechanically decidable cases, recalculates the
 * grounded acceptance contract, and keeps every page mutation capability disabled.
 */
final class Elementize_Aesthetic_Grounded_Reassessment_Hardening {
    private const VERSION = '0.26.5-h1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 565, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) ) return $response;
        $visual = $data['visual'];
        $diag = is_array( $visual['aesthetic_grounded_reassessment'] ?? null ) ? $visual['aesthetic_grounded_reassessment'] : [];
        if ( ! $diag ) return $response;

        $diag['hardening_version'] = self::VERSION;
        $diag['deterministic_first_section_transition_normalization'] = false;
        $diag['ignored_nonsemantic_conflicts'] = [];

        if ( empty( $diag['needed'] ) || ! empty( $diag['available'] ) ) {
            $visual['aesthetic_grounded_reassessment'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $rows = is_array( $diag['section_ledger'] ?? null ) ? $diag['section_ledger'] : [];
        $expected_count = max( 0, absint( $diag['section_fact_count'] ?? 0 ) );
        if ( ! $rows || count( $rows ) !== $expected_count || empty( $diag['exact_section_coverage_complete'] ) ) {
            $visual['aesthetic_grounded_reassessment'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $transition_valid = true;
        $remaining_conflicts = [];
        $ignored_conflicts = [];
        foreach ( $rows as $index => &$row ) {
            if ( ! is_array( $row ) ) { $transition_valid = false; continue; }
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            $transition = strtolower( trim( (string) ( $row['transition_from_previous'] ?? '' ) ) );

            if ( 0 === $index ) {
                if ( 'S1' !== $marker ) $transition_valid = false;
                if ( 'n/a' !== $transition ) {
                    $row['model_transition_from_previous'] = $transition;
                    $row['transition_from_previous'] = 'n/a';
                    $row['transition_normalized_reason'] = 'The first visible section has no previous section; its transition is deterministically n/a.';
                    $diag['deterministic_first_section_transition_normalization'] = true;
                }
                $row['transition_contract_valid'] = 'S1' === $marker;
            } else {
                $ok = in_array( $transition, [ 'smooth', 'neutral', 'abrupt', 'repetitive' ], true );
                $row['transition_contract_valid'] = $ok;
                if ( ! $ok ) $transition_valid = false;
            }

            $row_conflicts = is_array( $row['semantic_conflicts'] ?? null ) ? $row['semantic_conflicts'] : [];
            $usable_conflicts = [];
            foreach ( $row_conflicts as $conflict ) {
                $conflict = (string) $conflict;
                if ( self::is_nonsemantic_implementation_token_conflict( $conflict ) ) {
                    $ignored_conflicts[] = [ 'marker' => $marker, 'conflict' => $conflict ];
                    continue;
                }
                $usable_conflicts[] = $conflict;
                $remaining_conflicts[] = [ 'marker' => $marker, 'conflict' => $conflict ];
            }
            $row['semantic_conflicts'] = array_values( array_unique( $usable_conflicts ) );
            $row['semantic_grounded'] = empty( $row['semantic_conflicts'] );
        }
        unset( $row );

        $diag['section_ledger'] = $rows;
        $diag['transition_contract_valid'] = $transition_valid;
        $diag['semantic_conflicts'] = $remaining_conflicts;
        $diag['semantic_conflict_count'] = count( $remaining_conflicts );
        $diag['ignored_nonsemantic_conflicts'] = $ignored_conflicts;
        $diag['ignored_nonsemantic_conflict_count'] = count( $ignored_conflicts );

        $issues = [];
        foreach ( is_array( $diag['issues'] ?? null ) ? $diag['issues'] : [] as $issue ) {
            if ( ! is_array( $issue ) ) continue;
            $copy = $issue;
            $model_level = strtolower( trim( (string) ( $copy['intervention_level'] ?? 'none' ) ) );
            $copy['model_intervention_level'] = $model_level;
            $copy['grounded_intervention_level'] = self::least_destructive_intervention( (string) ( $copy['category'] ?? '' ), $model_level );
            $copy['intervention_calibration_reason'] = 'Grounded reassessment is normalized to the least-destructive intervention supported by the issue category; section replacement remains reserved for section-choice evidence.';
            $copy['semantic_grounded'] = ! empty( $copy['semantic_grounded'] );
            $copy['evidence_usable'] = ! empty( $copy['evidence_usable'] ) && $copy['semantic_grounded'];
            $copy['automatic_write_allowed'] = false;
            $issues[] = $copy;
        }
        $diag['issues'] = $issues;

        $complete = $transition_valid && 0 === count( $remaining_conflicts ) && ! empty( $diag['exact_section_coverage_complete'] );
        $diag['assessment_complete'] = $complete;
        $diag['available'] = $complete;
        if ( $complete ) {
            unset( $diag['reason'] );
            $diag['acceptance_note'] = 'Grounded reassessment passed after deterministic contract normalization only; no visual judgment was invented or rewritten by this hardening pass.';
        }

        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];

        if ( $complete ) {
            $prior = self::approval( $art['effective_professional_approval'] ?? 'revise' );
            $reassessed = self::approval( $diag['overall_approval'] ?? 'revise' );
            $grounded = self::more_conservative_approval( $prior, $reassessed );
            $grounded_issues = array_values( array_filter( $issues, static fn ( array $issue ): bool => ! empty( $issue['evidence_usable'] ) ) );

            $art['grounded_reassessment_hardening_version'] = self::VERSION;
            $art['grounded_reassessment_approval'] = $reassessed;
            $art['grounded_effective_professional_approval'] = $grounded;
            $art['approval_grounding_complete'] = true;
            $art['approval_usable'] = in_array( strtolower( (string) ( $diag['confidence'] ?? 'low' ) ), [ 'medium', 'high' ], true );
            $art['grounded_issues'] = $grounded_issues;
            $art['grounded_issue_count'] = count( $grounded_issues );
            $art['grounded_high_severity_issue_count'] = count( array_filter( $grounded_issues, static fn ( array $issue ): bool => 'high' === strtolower( (string) ( $issue['severity'] ?? '' ) ) ) );
            $art['grounded_medium_severity_issue_count'] = count( array_filter( $grounded_issues, static fn ( array $issue ): bool => 'medium' === strtolower( (string) ( $issue['severity'] ?? '' ) ) ) );
            $art['automatic_write_allowed'] = false;

            $coherence['grounded_reassessment_hardening_version'] = self::VERSION;
            $coherence['transition_contract_valid'] = true;
            $coherence['rhythm_judgment_usable'] = true;
            $coherence['rhythm_sequence'] = array_map( static function ( array $row ): array {
                return [
                    'marker' => (string) ( $row['marker'] ?? '' ),
                    'top_level_element_id' => (string) ( $row['top_level_element_id'] ?? '' ),
                    'visual_intensity' => max( 1, min( 10, absint( $row['visual_intensity'] ?? 1 ) ) ),
                    'transition_from_previous' => (string) ( $row['transition_from_previous'] ?? '' ),
                ];
            }, $rows );
            $coherence['automatic_write_allowed'] = false;

            if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
            $data['summary']['aesthetic_grounded_reassessment_available'] = true;
            $data['summary']['aesthetic_approval_grounding_complete'] = true;
            $data['summary']['grounded_professional_visual_approval'] = $grounded;
            $data['summary']['professional_visual_approval_usable'] = ! empty( $art['approval_usable'] );
            $data['summary']['aesthetic_transition_contract_valid'] = true;
        }

        $visual['page_art_direction'] = $art;
        $visual['section_coherence'] = $coherence;
        $visual['aesthetic_grounded_reassessment'] = $diag;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_grounded_reassessment_hardening_version'] = self::VERSION;
        $response->set_data( $data );
        return $response;
    }

    private static function is_nonsemantic_implementation_token_conflict( string $conflict ): bool {
        if ( ! preg_match( '/Quoted visible text is not present in the exact Elementor section: "([^"]+)"\./', $conflict, $m ) ) return false;
        $token = trim( (string) ( $m[1] ?? '' ) );
        if ( '' === $token || preg_match( '/\s/u', $token ) ) return false;
        if ( in_array( strtolower( $token ), [ 'div', 'custom', 'container', 'section', 'column' ], true ) ) return true;
        return strlen( $token ) <= 40 && ( false !== strpos( $token, '-' ) || false !== strpos( $token, '_' ) ) && (bool) preg_match( '/^[A-Za-z0-9_-]+$/', $token );
    }

    private static function least_destructive_intervention( string $category, string $model ): string {
        $category = strtolower( trim( $category ) );
        $model = strtolower( trim( $model ) );
        if ( 'section_choice' === $category ) return 'replace_section';
        if ( 'imagery' === $category ) return 'imagery';
        if ( in_array( $category, [ 'typography', 'hierarchy', 'spacing', 'component_style', 'conversion' ], true ) ) return 'tweak';
        if ( 'density' === $category ) return 'content';
        if ( in_array( $category, [ 'composition', 'transition' ], true ) ) return 'restructure';
        return in_array( $model, [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ], true ) ? $model : 'none';
    }

    private static function approval( $value ): string {
        $value = strtolower( trim( (string) $value ) );
        return in_array( $value, [ 'approve', 'revise', 'reject' ], true ) ? $value : 'revise';
    }

    private static function more_conservative_approval( string $a, string $b ): string {
        $rank = [ 'approve' => 0, 'revise' => 1, 'reject' => 2 ];
        return ( $rank[$a] ?? 1 ) >= ( $rank[$b] ?? 1 ) ? $a : $b;
    }
}

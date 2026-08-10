<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Semantic_Shortlist_Evidence_Cache', false ) ) return;

/**
 * Deterministic semantic/visual tie resolution.
 *
 * Runs only after visual calibration has explicitly retained a top-score tie. Visual
 * score remains primary and is never modified. Among candidates tied at the highest
 * visual score and already meeting the absolute 6/10 floor, exact-state hardened
 * semantic structure evidence may choose a winner only when one candidate has a
 * material semantic lead. This is a separate selection basis, not a visual-score edit.
 *
 * Read-only. No insertion, replacement, copy, design, or page-state write exists here.
 */
final class Elementize_Aesthetic_Semantic_Visual_Tie_Resolution {
    private const VERSION = '0.28.2-f1';
    private const NS = 'elementize/v1';
    private const VISUAL_SCORE_FLOOR = 6;
    private const MIN_SEMANTIC_GAP = 5;

    public static function init(): void {
        // Calibration is 610. Resolve only after its visual-only verdict is explicit.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 615, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/visual-probe' !== $route || 'POST' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'aesthetic_compare' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['aesthetic_comparison'] ?? null ) ) return $response;
        $comparison = $data['aesthetic_comparison'];
        $cal = is_array( $comparison['judgment_calibration'] ?? null ) ? $comparison['judgment_calibration'] : [];
        if ( empty( $cal['applied'] ) || empty( $cal['tied_top_score'] ) ) return $response;

        $diag = [
            'version' => self::VERSION,
            'attempted' => true,
            'applied' => false,
            'trigger' => 'calibrated_visual_top_score_tie',
            'visual_score_modified' => false,
            'visual_score_primary' => true,
            'semantic_only_breaks_visual_tie' => true,
            'minimum_visual_score_floor' => self::VISUAL_SCORE_FLOOR,
            'minimum_semantic_gap' => self::MIN_SEMANTIC_GAP,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
        ];

        $result = self::resolve( $comparison, $request );
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = self::clip( $result->get_error_message(), 500 );
            $comparison['semantic_visual_tie_resolution'] = $diag;
            $data['aesthetic_comparison'] = $comparison;
            $response->set_data( $data );
            return $response;
        }

        $diag = array_merge( $diag, $result['diagnostics'], [ 'applied' => true ] );
        $comparison['calibrated_visual_winner_slot'] = (string) ( $comparison['winner_slot'] ?? 'none' );
        $comparison['calibrated_visual_winner_template_id'] = $comparison['winner_template_id'] ?? null;
        $comparison['calibrated_visual_confidence'] = (string) ( $comparison['confidence'] ?? 'low' );
        $comparison['winner_slot'] = $result['winner_slot'];
        $comparison['winner_template_id'] = $result['winner_template_id'];
        $comparison['confidence'] = 'medium';
        $comparison['winner_margin'] = 'slight';
        $comparison['usable_for_selection'] = true;
        $comparison['effective_selection_score'] = $result['visual_score'];
        $comparison['selection_basis'] = 'calibrated_visual_tie_broken_by_exact_state_semantic_structure';
        $comparison['professional_rationale'] = $result['professional_rationale'];
        $comparison['selection_policy'] = 'A calibrated visual tie at or above 6/10 was resolved only by a material exact-state semantic/structural lead. This is a read-only recommendation and still does not authorize insertion or replacement.';
        $comparison['semantic_visual_tie_resolution'] = $diag;

        $data['aesthetic_comparison'] = $comparison;
        $response->set_data( $data );
        return $response;
    }

    private static function resolve( array $comparison, WP_REST_Request $request ) {
        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        $hash = strtolower( trim( (string) ( $comparison['page_state_hash'] ?? '' ) ) );
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) || ! preg_match( '/^[a-f0-9]{64}$/', $hash ) ) {
            return new WP_Error( 'elementize_semantic_visual_tie_context_invalid', 'Semantic visual tie resolution requires valid exact-state page, marker, and hash evidence.' );
        }

        $evidence = Elementize_Aesthetic_Semantic_Shortlist_Evidence_Cache::get( $page_id, $hash, $marker );
        if ( ! is_array( $evidence ) ) return new WP_Error( 'elementize_semantic_visual_tie_cache_missing', 'Exact-state hardened semantic shortlist evidence is unavailable or expired.' );
        if ( (int) ( $evidence['page_id'] ?? 0 ) !== $page_id || strtoupper( (string) ( $evidence['target_marker'] ?? '' ) ) !== $marker || ! hash_equals( $hash, strtolower( (string) ( $evidence['page_state_hash'] ?? '' ) ) ) ) {
            return new WP_Error( 'elementize_semantic_visual_tie_cache_stale', 'Cached semantic shortlist evidence does not match the current exact page state.' );
        }

        $judgments = array_values( array_filter( (array) ( $comparison['candidate_judgments'] ?? [] ), 'is_array' ) );
        if ( count( $judgments ) < 2 || count( $judgments ) > 4 ) return new WP_Error( 'elementize_semantic_visual_tie_candidates_invalid', 'Visual candidate judgments are incomplete.' );

        $visual_by_id = [];
        $top_score = 0;
        foreach ( $judgments as $row ) {
            $id = sanitize_key( (string) ( $row['template_id'] ?? '' ) );
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            $score = max( 1, min( 10, absint( $row['score'] ?? 0 ) ) );
            if ( '' === $id || ! in_array( $slot, [ 'A', 'B', 'C', 'D' ], true ) ) continue;
            $visual_by_id[ $id ] = [ 'slot' => $slot, 'score' => $score ];
            $top_score = max( $top_score, $score );
        }
        if ( count( $visual_by_id ) !== count( $judgments ) ) return new WP_Error( 'elementize_semantic_visual_tie_candidates_invalid', 'Visual candidate identity coverage is incomplete.' );
        if ( $top_score < self::VISUAL_SCORE_FLOOR ) return new WP_Error( 'elementize_semantic_visual_tie_below_floor', 'The tied visual top score is below the absolute selection floor.' );

        $semantic_by_id = [];
        foreach ( array_values( array_filter( (array) ( $evidence['candidates'] ?? [] ), 'is_array' ) ) as $row ) {
            $id = sanitize_key( (string) ( $row['template_id'] ?? '' ) );
            if ( '' === $id ) continue;
            $semantic_by_id[ $id ] = absint( $row['semantic_fit_score'] ?? 0 );
        }
        $visual_ids = array_keys( $visual_by_id );
        $semantic_ids = array_keys( $semantic_by_id );
        sort( $visual_ids );
        sort( $semantic_ids );
        if ( $visual_ids !== $semantic_ids ) return new WP_Error( 'elementize_semantic_visual_tie_candidate_set_mismatch', 'The visual candidate set does not exactly match the cached hardened semantic shortlist.' );

        $tied = [];
        foreach ( $visual_by_id as $id => $visual ) {
            if ( (int) $visual['score'] !== $top_score ) continue;
            $tied[] = [
                'template_id' => $id,
                'slot' => (string) $visual['slot'],
                'visual_score' => $top_score,
                'semantic_fit_score' => (int) ( $semantic_by_id[ $id ] ?? 0 ),
            ];
        }
        if ( count( $tied ) < 2 ) return new WP_Error( 'elementize_semantic_visual_tie_not_present', 'The current candidate judgments no longer contain a visual top-score tie.' );

        usort( $tied, static function ( array $a, array $b ): int {
            if ( $a['semantic_fit_score'] === $b['semantic_fit_score'] ) return strcmp( $a['template_id'], $b['template_id'] );
            return $b['semantic_fit_score'] <=> $a['semantic_fit_score'];
        } );
        $top = $tied[0];
        $runner = $tied[1];
        $semantic_gap = max( 0, (int) $top['semantic_fit_score'] - (int) $runner['semantic_fit_score'] );
        if ( (int) $top['semantic_fit_score'] === (int) $runner['semantic_fit_score'] || $semantic_gap < self::MIN_SEMANTIC_GAP ) {
            return new WP_Error( 'elementize_semantic_visual_tie_semantic_gap_insufficient', 'The visual tie does not have a unique semantic/structural leader with the required material score gap.' );
        }

        $rationale = 'The calibrated visual ranking tied at ' . $top_score . '/10. Exact-state hardened semantic structure therefore acted only as a tie-break: ' . $top['template_id'] . ' scored ' . $top['semantic_fit_score'] . '/100 structurally versus ' . $runner['template_id'] . ' at ' . $runner['semantic_fit_score'] . '/100, a material gap of ' . $semantic_gap . ' points. Visual scores were not modified.';

        return [
            'winner_slot' => $top['slot'],
            'winner_template_id' => $top['template_id'],
            'visual_score' => $top_score,
            'professional_rationale' => self::clip( $rationale, 900 ),
            'diagnostics' => [
                'candidate_count' => count( $visual_by_id ),
                'visual_top_score' => $top_score,
                'visual_tied_candidate_count' => count( $tied ),
                'visual_tied_candidates' => $tied,
                'semantic_winner_template_id' => $top['template_id'],
                'semantic_winner_score' => (int) $top['semantic_fit_score'],
                'semantic_runner_up_template_id' => $runner['template_id'],
                'semantic_runner_up_score' => (int) $runner['semantic_fit_score'],
                'semantic_score_gap' => $semantic_gap,
                'candidate_set_exact_match' => true,
                'page_state_hash' => $hash,
            ],
        ];
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\s+/u', ' ', $value ) ?? $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_semantic_visual_tie_resolution_version'] = self::VERSION;
        $data['aesthetic_semantic_visual_tie_resolution'] = true;
        $data['aesthetic_semantic_visual_tie_visual_score_primary'] = true;
        $data['aesthetic_semantic_visual_tie_minimum_visual_floor'] = self::VISUAL_SCORE_FLOOR;
        $data['aesthetic_semantic_visual_tie_minimum_semantic_gap'] = self::MIN_SEMANTIC_GAP;
        $data['aesthetic_semantic_visual_tie_read_only'] = true;
        $data['aesthetic_semantic_visual_tie_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

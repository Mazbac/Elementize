<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Judgment', false ) ) return;

/**
 * Deterministic confidence and consistency calibration for contextual Pixfort A/B judgment.
 *
 * The local vision model is useful for relative visual judgment, but its numeric score,
 * categorical fit labels, confidence, and margin can disagree. This layer never invents
 * a better judgment and never raises model confidence. It preserves the raw model-facing
 * fields, computes mechanically checkable score/label consistency, enforces an absolute
 * fit floor for selection usability, and conservatively lowers effective confidence or
 * removes tie winners when warranted.
 *
 * Read-only: no Elementor/page mutation capability exists in this class.
 */
final class Elementize_Aesthetic_AB_Judgment_Calibration {
    private const VERSION = '0.27.3-j1';
    private const NS = 'elementize/v1';
    private const ABSOLUTE_SELECTION_SCORE_FLOOR = 6;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 610, 3 );
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

        if ( empty( $comparison['available'] ) || empty( $comparison['assessment_complete'] ) || empty( $comparison['exact_candidate_coverage'] ) ) return $response;
        if ( ! is_array( $comparison['candidate_judgments'] ?? null ) || count( $comparison['candidate_judgments'] ) < 2 ) return $response;

        $data['aesthetic_comparison'] = self::calibrate( $comparison );
        $response->set_data( $data );
        return $response;
    }

    private static function calibrate( array $comparison ): array {
        $rows = array_values( array_filter( $comparison['candidate_judgments'], 'is_array' ) );
        if ( count( $rows ) < 2 ) return $comparison;

        $model_winner = self::slot_or_none( $comparison['winner_slot'] ?? 'none' );
        $model_confidence = self::enum( $comparison['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' );
        $model_margin = self::enum( $comparison['winner_margin'] ?? '', [ 'slight', 'clear', 'decisive' ], 'slight' );
        $model_usable = ! empty( $comparison['usable_for_selection'] );
        $model_template_id = isset( $comparison['winner_template_id'] ) ? (string) $comparison['winner_template_id'] : null;

        $scored = [];
        $contradictions = [];
        foreach ( $rows as $row ) {
            $slot = self::slot_or_none( $row['slot'] ?? 'none' );
            if ( 'none' === $slot ) continue;
            $score = self::score( $row['score'] ?? 1 );
            $overall = self::enum( $row['overall_fit'] ?? '', [ 'strong', 'acceptable', 'weak' ], 'weak' );
            $dimension_values = [];
            foreach ( [ 'design_dna_fit', 'rhythm_fit', 'hierarchy_fit', 'conversion_fit' ] as $key ) {
                $dimension_values[$key] = self::enum( $row[$key] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' );
            }
            $strong_dimensions = count( array_filter( $dimension_values, static fn ( string $v ): bool => 'strong' === $v ) );
            $weak_dimensions = count( array_filter( $dimension_values, static fn ( string $v ): bool => 'weak' === $v ) );

            $row_contradictions = [];
            if ( 'strong' === $overall && $score <= 4 ) $row_contradictions[] = [ 'severity' => 'severe', 'reason' => 'overall_fit is strong while numeric score is 4/10 or lower' ];
            if ( 'weak' === $overall && $score >= 7 ) $row_contradictions[] = [ 'severity' => 'severe', 'reason' => 'overall_fit is weak while numeric score is 7/10 or higher' ];
            if ( $strong_dimensions >= 3 && $score <= 4 ) $row_contradictions[] = [ 'severity' => 'severe', 'reason' => 'at least three fit dimensions are strong while numeric score is 4/10 or lower' ];
            if ( $weak_dimensions >= 3 && $score >= 7 ) $row_contradictions[] = [ 'severity' => 'severe', 'reason' => 'at least three fit dimensions are weak while numeric score is 7/10 or higher' ];
            if ( 'strong' === $overall && 5 === $score ) $row_contradictions[] = [ 'severity' => 'moderate', 'reason' => 'overall_fit is strong while numeric score is only 5/10' ];
            if ( 'acceptable' === $overall && ( $score <= 2 || $score >= 9 ) ) $row_contradictions[] = [ 'severity' => 'moderate', 'reason' => 'overall_fit is acceptable while numeric score is at an extreme' ];

            foreach ( $row_contradictions as $item ) {
                $contradictions[] = [
                    'slot' => $slot,
                    'severity' => $item['severity'],
                    'reason' => $item['reason'],
                ];
            }

            $scored[] = [
                'slot' => $slot,
                'template_id' => (string) ( $row['template_id'] ?? '' ),
                'score' => $score,
                'overall_fit' => $overall,
                'strong_dimension_count' => $strong_dimensions,
                'weak_dimension_count' => $weak_dimensions,
            ];
        }

        if ( count( $scored ) < 2 ) return $comparison;
        usort( $scored, static function ( array $a, array $b ): int {
            if ( $a['score'] === $b['score'] ) return strcmp( $a['slot'], $b['slot'] );
            return $b['score'] <=> $a['score'];
        } );

        $top = $scored[0];
        $runner = $scored[1];
        $gap = max( 0, (int) $top['score'] - (int) $runner['score'] );
        $tied_top = (int) $top['score'] === (int) $runner['score'];

        $effective_winner = $model_winner;
        $effective_template_id = $model_template_id;
        $reasons = [];

        if ( $tied_top ) {
            $effective_winner = 'none';
            $effective_template_id = null;
            $reasons[] = 'The two highest candidate scores are tied, so no effective winner is retained.';
        } elseif ( 'none' !== $model_winner && $model_winner !== $top['slot'] ) {
            $effective_winner = 'none';
            $effective_template_id = null;
            $reasons[] = 'The model winner does not match the highest normalized candidate score.';
        }

        $winner_row = null;
        if ( 'none' !== $effective_winner ) {
            foreach ( $scored as $row ) if ( $row['slot'] === $effective_winner ) { $winner_row = $row; break; }
        }
        $winner_score = is_array( $winner_row ) ? (int) $winner_row['score'] : 0;
        $winner_meets_floor = $winner_score >= self::ABSOLUTE_SELECTION_SCORE_FLOOR;

        $winner_severe = false;
        $winner_moderate = false;
        foreach ( $contradictions as $conflict ) {
            if ( $conflict['slot'] !== $effective_winner ) continue;
            if ( 'severe' === $conflict['severity'] ) $winner_severe = true;
            if ( 'moderate' === $conflict['severity'] ) $winner_moderate = true;
        }

        $derived_confidence = 'low';
        if ( 'none' !== $effective_winner && ! $tied_top ) {
            if ( $winner_score >= 8 && $gap >= 3 ) $derived_confidence = 'high';
            elseif ( $winner_score >= 6 && $gap >= 1 ) $derived_confidence = 'medium';
        }
        if ( $winner_severe ) $derived_confidence = 'low';
        elseif ( $winner_moderate && 'high' === $derived_confidence ) $derived_confidence = 'medium';

        $effective_confidence = self::lower_confidence( $model_confidence, $derived_confidence );

        $derived_margin = 'slight';
        if ( 'none' !== $effective_winner && ! $tied_top ) {
            if ( $winner_score >= 9 && $gap >= 5 && ! $winner_severe && ! $winner_moderate ) $derived_margin = 'decisive';
            elseif ( $gap >= 3 && ! $winner_severe ) $derived_margin = 'clear';
        }
        if ( $winner_severe ) $derived_margin = 'slight';
        $effective_margin = self::lower_margin( $model_margin, $derived_margin );

        if ( ! $winner_meets_floor && 'none' !== $effective_winner ) $reasons[] = 'The relative winner does not meet the absolute selection score floor of ' . self::ABSOLUTE_SELECTION_SCORE_FLOOR . '/10.';
        if ( $winner_severe ) $reasons[] = 'The winning candidate has a severe numeric-score versus categorical-fit inconsistency.';
        elseif ( $winner_moderate ) $reasons[] = 'The winning candidate has a moderate numeric-score versus categorical-fit inconsistency.';
        if ( self::confidence_rank( $effective_confidence ) < self::confidence_rank( $model_confidence ) ) $reasons[] = 'Effective confidence was lowered deterministically from the model-reported confidence.';
        if ( self::margin_rank( $effective_margin ) < self::margin_rank( $model_margin ) ) $reasons[] = 'Effective winner margin was lowered deterministically from the model-reported margin.';

        $effective_usable = 'none' !== $effective_winner
            && $winner_meets_floor
            && ! $winner_severe
            && in_array( $effective_confidence, [ 'medium', 'high' ], true )
            && is_array( $winner_row )
            && 'weak' !== $winner_row['overall_fit'];

        $comparison['model_winner_slot'] = $model_winner;
        $comparison['model_winner_template_id'] = $model_template_id;
        $comparison['model_confidence'] = $model_confidence;
        $comparison['model_winner_margin'] = $model_margin;
        $comparison['model_usable_for_selection'] = $model_usable;

        $comparison['winner_slot'] = $effective_winner;
        $comparison['winner_template_id'] = $effective_template_id;
        $comparison['confidence'] = $effective_confidence;
        $comparison['winner_margin'] = $effective_margin;
        $comparison['usable_for_selection'] = $effective_usable;
        $comparison['selection_policy'] = $effective_usable
            ? 'The calibrated winner may be used as a read-only recommendation for section selection. It still does not authorize insertion or replacement.'
            : ( 'none' !== $effective_winner
                ? 'A relative winner exists, but deterministic calibration does not consider it strong and internally consistent enough for selection. Keep it advisory only.'
                : 'No calibrated winner is safe for selection from this comparison. Obtain stronger or more clearly separated candidates.' );

        $comparison['judgment_calibration'] = [
            'version' => self::VERSION,
            'applied' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'confidence_can_only_decrease' => true,
            'absolute_selection_score_floor' => self::ABSOLUTE_SELECTION_SCORE_FLOOR,
            'top_score' => (int) $top['score'],
            'runner_up_score' => (int) $runner['score'],
            'score_gap' => $gap,
            'tied_top_score' => $tied_top,
            'winner_score' => $winner_score,
            'winner_meets_absolute_fit_floor' => $winner_meets_floor,
            'model_winner_slot' => $model_winner,
            'effective_winner_slot' => $effective_winner,
            'model_confidence' => $model_confidence,
            'derived_confidence_ceiling' => $derived_confidence,
            'effective_confidence' => $effective_confidence,
            'model_winner_margin' => $model_margin,
            'derived_margin_ceiling' => $derived_margin,
            'effective_winner_margin' => $effective_margin,
            'model_usable_for_selection' => $model_usable,
            'effective_usable_for_selection' => $effective_usable,
            'winner_severe_consistency_conflict' => $winner_severe,
            'winner_moderate_consistency_conflict' => $winner_moderate,
            'consistency_conflict_count' => count( $contradictions ),
            'consistency_conflicts' => array_slice( $contradictions, 0, 12 ),
            'calibration_reasons' => array_values( array_unique( $reasons ) ),
        ];

        return $comparison;
    }

    private static function lower_confidence( string $model, string $derived ): string {
        return self::confidence_rank( $model ) <= self::confidence_rank( $derived ) ? $model : $derived;
    }

    private static function confidence_rank( string $value ): int {
        return [ 'low' => 0, 'medium' => 1, 'high' => 2 ][$value] ?? 0;
    }

    private static function lower_margin( string $model, string $derived ): string {
        return self::margin_rank( $model ) <= self::margin_rank( $derived ) ? $model : $derived;
    }

    private static function margin_rank( string $value ): int {
        return [ 'slight' => 0, 'clear' => 1, 'decisive' => 2 ][$value] ?? 0;
    }

    private static function slot_or_none( $value ): string {
        $value = strtoupper( trim( (string) $value ) );
        if ( in_array( $value, [ 'A', 'B', 'C', 'D' ], true ) ) return $value;
        return 'none';
    }

    private static function score( $value ): int {
        $score = is_numeric( $value ) ? (int) round( (float) $value ) : 1;
        return max( 1, min( 10, $score ) );
    }

    private static function enum( $value, array $allowed, string $fallback ): string {
        $value = strtolower( trim( (string) $value ) );
        return in_array( $value, $allowed, true ) ? $value : $fallback;
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_ab_judgment_calibration_version'] = self::VERSION;
        $data['aesthetic_ab_judgment_calibration'] = true;
        $data['aesthetic_ab_calibration_read_only'] = true;
        $data['aesthetic_ab_calibration_confidence_can_only_decrease'] = true;
        $data['aesthetic_ab_absolute_selection_score_floor'] = self::ABSOLUTE_SELECTION_SCORE_FLOOR;
        $data['aesthetic_ab_calibration_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

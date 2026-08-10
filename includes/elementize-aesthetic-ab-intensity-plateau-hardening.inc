<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Independent_Discrimination', false ) ) return;

/**
 * Deterministic intensity plateau hardening for anchored independent discrimination.
 *
 * A small local vision model can return the same visual-intensity estimate and the
 * same anchored fit axes for several genuinely different thumbnails. When anchored
 * discrimination has completed but remains tied, this layer measures each candidate
 * thumbnail independently using a fixed sampled-image complexity signature. It then
 * replaces only the model-derived intensity penalty with the deterministic proxy and
 * recalculates the already anchored PHP score. The eight model fit axes and mismatch
 * severity are not upgraded or weakened.
 *
 * Read-only: no Elementor/page mutation capability exists in this class.
 */
final class Elementize_Aesthetic_AB_Intensity_Plateau_Hardening {
    private const VERSION = '0.27.8-p1';
    private const NS = 'elementize/v1';
    private const GRID = 32;

    public static function init(): void {
        // Anchored discrimination is 605; calibration is 610.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 607, 3 );
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
        $disc = is_array( $comparison['independent_discrimination'] ?? null ) ? $comparison['independent_discrimination'] : [];
        if ( empty( $disc['applied'] ) || empty( $disc['tied_top_score'] ) ) return $response;

        $diag = [
            'version' => self::VERSION,
            'attempted' => true,
            'applied' => false,
            'trigger' => 'anchored_discrimination_top_score_tie',
            'deterministic_thumbnail_measurement' => true,
            'model_intensity_replaced_only' => true,
            'anchored_axes_preserved' => true,
            'mismatch_severity_preserved' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
        ];

        $result = self::recalculate( $data, $comparison, $disc );
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = self::clip( $result->get_error_message(), 500 );
            $comparison['intensity_plateau_hardening'] = $diag;
            $data['aesthetic_comparison'] = $comparison;
            $response->set_data( $data );
            return $response;
        }

        $diag = array_merge( $diag, $result['diagnostics'], [ 'applied' => true ] );
        $comparison['candidate_judgments'] = $result['candidate_judgments'];
        $comparison['winner_slot'] = $result['winner_slot'];
        $comparison['winner_template_id'] = $result['winner_template_id'];
        $comparison['confidence'] = $result['confidence'];
        $comparison['winner_margin'] = $result['winner_margin'];
        $comparison['professional_rationale'] = $result['professional_rationale'];
        $comparison['decision_basis'] = array_merge(
            is_array( $comparison['decision_basis'] ?? null ) ? $comparison['decision_basis'] : [],
            [
                'Anchored discrimination tied, so deterministic thumbnail complexity replaced only the collapsed model visual-intensity estimate.',
                'The original eight anchored fit axes and mismatch severity remained unchanged.',
                'No pairwise image comparison or slot/template identity was introduced into scoring.',
            ]
        );
        $comparison['comparison_method'] = 'slot_blind_independent_candidate_scoring_with_anchored_discrimination_and_deterministic_intensity_plateau_hardening';
        $comparison['usable_for_selection'] = 'none' !== $result['winner_slot'] && in_array( $result['confidence'], [ 'medium', 'high' ], true );
        $comparison['selection_policy'] = $comparison['usable_for_selection']
            ? 'The deterministic plateau-hardened winner may be used only as a read-only recommendation; deterministic calibration still runs afterward and may block it.'
            : 'Deterministic intensity plateau hardening did not establish a sufficiently strong winner. Keep the result advisory-only.';
        $comparison['intensity_plateau_hardening'] = $diag;

        $data['aesthetic_comparison'] = $comparison;
        $response->set_data( $data );
        return $response;
    }

    private static function recalculate( array $data, array $comparison, array $disc ) {
        $candidate_results = array_values( array_filter( (array) ( $disc['candidate_results'] ?? [] ), 'is_array' ) );
        $judgments = array_values( array_filter( (array) ( $comparison['candidate_judgments'] ?? [] ), 'is_array' ) );
        if ( count( $candidate_results ) < 2 || count( $candidate_results ) > 4 || count( $judgments ) !== count( $candidate_results ) ) {
            return new WP_Error( 'elementize_intensity_plateau_candidate_shape', 'Deterministic intensity plateau hardening requires matching anchored diagnostics and candidate judgments for two to four candidates.' );
        }

        $proof = self::extract_proof_candidates( $data );
        if ( is_wp_error( $proof ) ) return $proof;

        $proof_by_slot = [];
        foreach ( $proof as $row ) $proof_by_slot[ (string) $row['slot'] ] = $row;
        $diag_by_slot = [];
        foreach ( $candidate_results as $row ) $diag_by_slot[ (string) ( $row['slot'] ?? '' ) ] = $row;

        $target_intensity = self::clamp_score( $disc['target_visual_intensity'] ?? 5 );
        $adjusted = [];
        $measurements = [];

        foreach ( $judgments as $row ) {
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            if ( ! isset( $proof_by_slot[ $slot ], $diag_by_slot[ $slot ] ) ) {
                return new WP_Error( 'elementize_intensity_plateau_slot_missing', 'A candidate slot is missing from deterministic intensity plateau evidence.' );
            }
            $measurement = self::measure_image( (string) $proof_by_slot[ $slot ]['thumbnail_base64'] );
            if ( is_wp_error( $measurement ) ) return $measurement;

            $anchored = $diag_by_slot[ $slot ];
            $raw_axis_score = (float) ( $anchored['raw_axis_score'] ?? 0 );
            if ( $raw_axis_score <= 0 ) return new WP_Error( 'elementize_intensity_plateau_raw_axis_missing', 'Anchored discrimination did not expose a usable raw axis score.' );
            $mismatch_penalty = (float) ( $anchored['mismatch_penalty'] ?? 0.5 );
            $measured_intensity = (int) $measurement['intensity_proxy'];
            $intensity_penalty = self::intensity_penalty( abs( $measured_intensity - $target_intensity ) );
            $float_score = $raw_axis_score - $intensity_penalty - $mismatch_penalty;
            $score = self::clamp_score( round( $float_score ) );
            // The plateau breaker can never exceed the rounded support of the eight anchored axes.
            $axis_ceiling = self::clamp_score( round( $raw_axis_score ) );
            $score = min( $score, $axis_ceiling );

            $row['score'] = $score;
            $row['overall_fit'] = self::overall_fit_from_score( $score );
            $adjusted[] = $row;
            $measurements[] = [
                'slot' => $slot,
                'template_id' => (string) ( $row['template_id'] ?? '' ),
                'model_estimated_visual_intensity' => absint( $anchored['estimated_visual_intensity'] ?? 0 ),
                'measured_visual_intensity_proxy' => $measured_intensity,
                'target_visual_intensity' => $target_intensity,
                'measured_intensity_delta' => abs( $measured_intensity - $target_intensity ),
                'model_intensity_penalty' => (float) ( $anchored['intensity_penalty'] ?? 0 ),
                'measured_intensity_penalty' => $intensity_penalty,
                'mismatch_penalty_preserved' => $mismatch_penalty,
                'raw_axis_score_preserved' => round( $raw_axis_score, 2 ),
                'adjusted_float_score' => round( $float_score, 3 ),
                'axis_ceiling' => $axis_ceiling,
                'effective_score' => $score,
                'thumbnail_signature' => $measurement,
            ];
        }

        $ranked = $adjusted;
        usort( $ranked, static function ( array $a, array $b ): int {
            if ( (int) $a['score'] === (int) $b['score'] ) return strcmp( (string) $a['template_id'], (string) $b['template_id'] );
            return (int) $b['score'] <=> (int) $a['score'];
        } );
        $top = $ranked[0];
        $runner = $ranked[1];
        $top_score = (int) $top['score'];
        $runner_score = (int) $runner['score'];
        $gap = max( 0, $top_score - $runner_score );
        $tie = $top_score === $runner_score;
        $winner_slot = $tie ? 'none' : (string) $top['slot'];
        $winner_template_id = $tie ? null : (string) $top['template_id'];

        $confidence = 'low';
        if ( ! $tie && $top_score >= 6 && $gap >= 1 ) $confidence = 'medium';
        if ( ! $tie && $top_score >= 8 && $gap >= 3 ) $confidence = 'high';
        $margin = ( ! $tie && $gap >= 3 ) ? 'clear' : 'slight';

        $rationale = $tie
            ? 'Anchored fit axes remained tied after replacing the collapsed model intensity estimate with deterministic thumbnail-complexity measurements, so no winner is retained.'
            : 'Deterministic thumbnail-complexity hardening broke the anchored tie without pairwise comparison: ' . (string) $top['template_id'] . ' scored ' . $top_score . '/10 versus ' . (string) $runner['template_id'] . ' at ' . $runner_score . '/10. The eight anchored fit axes and mismatch penalties were preserved.';

        return [
            'candidate_judgments' => $adjusted,
            'winner_slot' => $winner_slot,
            'winner_template_id' => $winner_template_id,
            'confidence' => $confidence,
            'winner_margin' => $margin,
            'professional_rationale' => self::clip( $rationale, 900 ),
            'diagnostics' => [
                'candidate_count' => count( $adjusted ),
                'target_visual_intensity' => $target_intensity,
                'candidate_measurements' => $measurements,
                'top_score' => $top_score,
                'runner_up_score' => $runner_score,
                'score_gap' => $gap,
                'tied_top_score' => $tie,
                'effective_winner_template_id' => $winner_template_id,
            ],
        ];
    }

    private static function extract_proof_candidates( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_intensity_plateau_proof_missing', 'The Pixfort visual proof is unavailable for deterministic thumbnail measurement.' );
        $encoded = (string) ( $files[0]['content'] ?? '' );
        $json = base64_decode( $encoded, true );
        if ( false === $json || '' === $json ) return new WP_Error( 'elementize_intensity_plateau_proof_invalid', 'The Pixfort visual proof could not be decoded.' );
        $proof = json_decode( $json, true );
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_intensity_plateau_proof_invalid', 'The Pixfort visual proof is not valid candidate JSON.' );
        $out = [];
        foreach ( $proof['candidates'] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            $image = trim( (string) ( $row['thumbnail_base64'] ?? '' ) );
            if ( ! in_array( $slot, [ 'A', 'B', 'C', 'D' ], true ) || '' === $image ) continue;
            $out[] = [ 'slot' => $slot, 'thumbnail_base64' => $image ];
        }
        return count( $out ) >= 2 ? $out : new WP_Error( 'elementize_intensity_plateau_proof_incomplete', 'Fewer than two candidate thumbnails are available for deterministic measurement.' );
    }

    private static function measure_image( string $base64 ) {
        if ( ! function_exists( 'imagecreatefromstring' ) || ! function_exists( 'imagesx' ) || ! function_exists( 'imagecolorat' ) ) {
            return new WP_Error( 'elementize_intensity_plateau_gd_missing', 'PHP GD image sampling is unavailable for deterministic thumbnail measurement.' );
        }
        $bytes = base64_decode( $base64, true );
        if ( false === $bytes || '' === $bytes ) return new WP_Error( 'elementize_intensity_plateau_image_decode', 'A thumbnail could not be base64-decoded for deterministic measurement.' );
        $image = @imagecreatefromstring( $bytes );
        if ( false === $image ) return new WP_Error( 'elementize_intensity_plateau_image_open', 'PHP GD could not decode a candidate thumbnail for deterministic measurement.' );

        try {
            $width = max( 1, imagesx( $image ) );
            $height = max( 1, imagesy( $image ) );
            $grid = self::GRID;
            $lumas = [];
            $sats = [];
            $rgbs = [];
            $bins = [];
            for ( $gy = 0; $gy < $grid; $gy++ ) {
                $y = min( $height - 1, (int) floor( ( $gy + 0.5 ) * $height / $grid ) );
                for ( $gx = 0; $gx < $grid; $gx++ ) {
                    $x = min( $width - 1, (int) floor( ( $gx + 0.5 ) * $width / $grid ) );
                    $rgb = imagecolorat( $image, $x, $y );
                    $r = ( $rgb >> 16 ) & 0xFF;
                    $g = ( $rgb >> 8 ) & 0xFF;
                    $b = $rgb & 0xFF;
                    $lum = ( 0.2126 * $r + 0.7152 * $g + 0.0722 * $b ) / 255.0;
                    $max = max( $r, $g, $b );
                    $min = min( $r, $g, $b );
                    $sat = $max > 0 ? ( $max - $min ) / $max : 0.0;
                    $idx = $gy * $grid + $gx;
                    $lumas[ $idx ] = $lum;
                    $sats[ $idx ] = $sat;
                    $rgbs[ $idx ] = [ $r / 255.0, $g / 255.0, $b / 255.0 ];
                    $bin = ( intdiv( $r, 32 ) << 6 ) | ( intdiv( $g, 32 ) << 3 ) | intdiv( $b, 32 );
                    $bins[ $bin ] = ( $bins[ $bin ] ?? 0 ) + 1;
                }
            }

            $count = count( $lumas );
            $mean_luma = array_sum( $lumas ) / max( 1, $count );
            $variance = 0.0;
            foreach ( $lumas as $lum ) $variance += ( $lum - $mean_luma ) * ( $lum - $mean_luma );
            $luma_std = sqrt( $variance / max( 1, $count ) );
            $mean_sat = array_sum( $sats ) / max( 1, $count );

            $edge_sum = 0.0;
            $edge_count = 0;
            for ( $gy = 0; $gy < $grid; $gy++ ) {
                for ( $gx = 0; $gx < $grid; $gx++ ) {
                    $idx = $gy * $grid + $gx;
                    foreach ( [ [ 1, 0 ], [ 0, 1 ] ] as $d ) {
                        $nx = $gx + $d[0]; $ny = $gy + $d[1];
                        if ( $nx >= $grid || $ny >= $grid ) continue;
                        $nidx = $ny * $grid + $nx;
                        $a = $rgbs[ $idx ]; $b = $rgbs[ $nidx ];
                        $edge_sum += ( abs( $a[0] - $b[0] ) + abs( $a[1] - $b[1] ) + abs( $a[2] - $b[2] ) ) / 3.0;
                        $edge_count++;
                    }
                }
            }
            $edge = $edge_sum / max( 1, $edge_count );
            $dominant = $bins ? max( $bins ) / max( 1, $count ) : 0.0;
            $diversity = count( $bins ) / max( 1, $count );

            $edge_component = min( 1.0, $edge * 4.0 );
            $contrast_component = min( 1.0, $luma_std * 3.0 );
            $diversity_component = min( 1.0, $diversity * 2.0 );
            $background_component = max( 0.0, min( 1.0, 1.0 - $dominant ) );
            $complexity =
                0.35 * $edge_component +
                0.20 * $contrast_component +
                0.15 * max( 0.0, min( 1.0, $mean_sat ) ) +
                0.15 * $diversity_component +
                0.15 * $background_component;
            $intensity = self::clamp_score( round( 1.0 + 9.0 * max( 0.0, min( 1.0, $complexity ) ) ) );

            return [
                'sample_grid' => $grid . 'x' . $grid,
                'width' => $width,
                'height' => $height,
                'mean_luminance' => round( $mean_luma, 4 ),
                'luminance_stddev' => round( $luma_std, 4 ),
                'mean_saturation' => round( $mean_sat, 4 ),
                'edge_change' => round( $edge, 4 ),
                'quantized_color_diversity' => round( $diversity, 4 ),
                'dominant_quantized_color_ratio' => round( $dominant, 4 ),
                'complexity_index' => round( $complexity, 4 ),
                'intensity_proxy' => $intensity,
            ];
        } finally {
            @imagedestroy( $image );
        }
    }

    private static function intensity_penalty( int $delta ): float {
        if ( $delta >= 5 ) return 2.0;
        if ( $delta >= 4 ) return 1.5;
        if ( $delta >= 3 ) return 1.0;
        if ( $delta >= 2 ) return 0.5;
        return 0.0;
    }

    private static function overall_fit_from_score( int $score ): string {
        if ( $score >= 7 ) return 'strong';
        if ( $score >= 4 ) return 'acceptable';
        return 'weak';
    }

    private static function clamp_score( $value ): int {
        return max( 1, min( 10, (int) round( (float) $value ) ) );
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
        $data['aesthetic_ab_intensity_plateau_hardening_version'] = self::VERSION;
        $data['aesthetic_ab_intensity_plateau_hardening'] = true;
        $data['aesthetic_ab_intensity_plateau_hardening_read_only'] = true;
        $data['aesthetic_ab_intensity_plateau_hardening_deterministic'] = true;
        $data['aesthetic_ab_intensity_plateau_hardening_pairwise_comparison'] = false;
        $data['aesthetic_ab_intensity_plateau_hardening_action_slot_cost'] = 0;
        $data['aesthetic_ab_intensity_plateau_hardening_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

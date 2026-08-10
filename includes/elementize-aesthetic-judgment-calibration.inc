<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Brain', false ) ) return;

/**
 * Reconcile Aesthetic Brain output with the evidence it already collected.
 *
 * This pass is deliberately read-only. It preserves raw model scores/verdicts,
 * adds conservative calibrated fields, and reduces intervention overreach so a
 * local typography or CTA issue is not mislabeled as a section replacement.
 */
final class Elementize_Aesthetic_Judgment_Calibration {
    private const VERSION = '0.26.2-c1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 545, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . self::NS . '/pages/(\\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) ) return $response;
        $visual = $data['visual'];
        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];
        $dna = is_array( $visual['design_dna'] ?? null ) ? $visual['design_dna'] : [];
        $recovery = is_array( $visual['aesthetic_coverage_recovery'] ?? null ) ? $visual['aesthetic_coverage_recovery'] : [];

        if ( empty( $art['available'] ) || empty( $art['assessment_complete'] ) ) return $response;

        $issues = is_array( $recovery['issues'] ?? null ) ? $recovery['issues'] : ( is_array( $art['top_issues'] ?? null ) ? $art['top_issues'] : [] );
        $issue_sections = [];
        $high = 0;
        $medium = 0;
        $has_conversion_high = false;
        $has_typography = false;
        $calibrated_issues = [];

        foreach ( $issues as $issue ) {
            if ( ! is_array( $issue ) ) continue;
            $severity = strtolower( trim( (string) ( $issue['severity'] ?? 'low' ) ) );
            $category = strtolower( trim( (string) ( $issue['category'] ?? 'other' ) ) );
            if ( 'high' === $severity ) $high++;
            if ( 'medium' === $severity ) $medium++;
            if ( 'high' === $severity && 'conversion' === $category ) $has_conversion_high = true;
            if ( 'typography' === $category ) $has_typography = true;
            foreach ( is_array( $issue['section_markers'] ?? null ) ? $issue['section_markers'] : [] as $marker ) {
                $marker = strtoupper( trim( (string) $marker ) );
                if ( '' !== $marker ) $issue_sections[$marker] = true;
            }
            $copy = $issue;
            $copy['model_intervention_level'] = (string) ( $issue['intervention_level'] ?? 'none' );
            $copy['calibrated_intervention_level'] = self::calibrate_intervention( $issue );
            $copy['intervention_calibration_reason'] = self::intervention_reason( $issue, $copy['calibrated_intervention_level'] );
            $copy['automatic_write_allowed'] = false;
            $calibrated_issues[] = $copy;
        }

        $marker_count = max( 1, absint( $art['marker_count'] ?? $coherence['marker_count'] ?? 1 ) );
        $issue_ratio = min( 1, count( $issue_sections ) / $marker_count );
        $effective = strtolower( trim( (string) ( $art['effective_professional_approval'] ?? $art['professional_approval'] ?? 'unavailable' ) ) );

        $raw_scores = [
            'aesthetic' => self::score( $art['aesthetic_score'] ?? 1 ),
            'coherence' => self::score( $art['coherence_score'] ?? 1 ),
            'hierarchy' => self::score( $art['hierarchy_score'] ?? 1 ),
            'rhythm' => self::score( $art['rhythm_score'] ?? 1 ),
            'polish' => self::score( $art['polish_score'] ?? 1 ),
            'restraint' => self::score( $art['restraint_score'] ?? 1 ),
            'conversion_focus' => self::score( $art['conversion_focus_score'] ?? 1 ),
        ];
        $cal = $raw_scores;

        if ( 'reject' === $effective ) {
            $cal['aesthetic'] = min( $cal['aesthetic'], 6 );
            $cal['polish'] = min( $cal['polish'], 6 );
            $cal['coherence'] = min( $cal['coherence'], 7 );
        } elseif ( 'revise' === $effective ) {
            $cal['aesthetic'] = min( $cal['aesthetic'], 7 );
            $cal['polish'] = min( $cal['polish'], 7 );
        }
        if ( $high >= 2 ) {
            $cal['aesthetic'] = min( $cal['aesthetic'], 6 );
            $cal['polish'] = min( $cal['polish'], 6 );
        }
        if ( $issue_ratio >= 0.40 ) $cal['coherence'] = min( $cal['coherence'], 6 );
        if ( $has_conversion_high ) $cal['conversion_focus'] = min( $cal['conversion_focus'], 5 );
        if ( $has_typography && ! empty( $art['revision_required_by_cross_checks'] ) ) $cal['hierarchy'] = min( $cal['hierarchy'], 6 );

        $effective_coherence = (string) ( $coherence['coherence_verdict'] ?? 'mixed' );
        if ( $issue_ratio >= 0.40 || $high >= 2 ) $effective_coherence = 'mixed';
        if ( 'reject' === $effective && $issue_ratio >= 0.60 ) $effective_coherence = 'weak';

        $effective_design_system = (string) ( $dna['design_system_coherence'] ?? 'mixed' );
        if ( 'strong' === $effective_design_system && ( $issue_ratio >= 0.40 || $high >= 2 ) ) $effective_design_system = 'mixed';

        $art['raw_model_scores'] = $raw_scores;
        $art['calibrated_scores'] = $cal;
        $art['calibrated_aesthetic_score'] = $cal['aesthetic'];
        $art['calibrated_coherence_score'] = $cal['coherence'];
        $art['calibrated_hierarchy_score'] = $cal['hierarchy'];
        $art['calibrated_rhythm_score'] = $cal['rhythm'];
        $art['calibrated_polish_score'] = $cal['polish'];
        $art['calibrated_restraint_score'] = $cal['restraint'];
        $art['calibrated_conversion_focus_score'] = $cal['conversion_focus'];
        $art['calibrated_issues'] = array_slice( $calibrated_issues, 0, 8 );
        $art['issue_bearing_section_count'] = count( $issue_sections );
        $art['issue_bearing_section_ratio'] = round( $issue_ratio, 3 );
        $art['high_severity_issue_count'] = $high;
        $art['medium_severity_issue_count'] = $medium;
        $art['judgment_calibration_version'] = self::VERSION;

        $coherence['model_coherence_verdict'] = (string) ( $coherence['coherence_verdict'] ?? 'mixed' );
        $coherence['effective_coherence_verdict'] = $effective_coherence;
        $coherence['issue_bearing_section_count'] = count( $issue_sections );
        $coherence['issue_bearing_section_ratio'] = round( $issue_ratio, 3 );
        $coherence['judgment_calibration_version'] = self::VERSION;

        $dna['model_design_system_coherence'] = (string) ( $dna['design_system_coherence'] ?? 'mixed' );
        $dna['effective_design_system_coherence'] = $effective_design_system;
        $dna['judgment_calibration_version'] = self::VERSION;

        foreach ( [ &$art, &$coherence, &$dna ] as &$object ) {
            $object['read_only'] = true;
            $object['writes_performed'] = false;
            $object['automatic_write_allowed'] = false;
        }
        unset( $object );

        $visual['page_art_direction'] = $art;
        $visual['section_coherence'] = $coherence;
        $visual['design_dna'] = $dna;
        $visual['aesthetic_judgment_calibration'] = [
            'version' => self::VERSION,
            'available' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'issue_bearing_section_count' => count( $issue_sections ),
            'issue_bearing_section_ratio' => round( $issue_ratio, 3 ),
            'high_severity_issue_count' => $high,
            'medium_severity_issue_count' => $medium,
            'effective_professional_approval' => $effective,
            'effective_coherence_verdict' => $effective_coherence,
            'effective_design_system_coherence' => $effective_design_system,
            'note' => 'Raw model scores are preserved. Calibrated scores and intervention levels are conservative evidence-based overlays used to prevent contradictory approval, inflated scoring, and unnecessarily destructive repair advice.',
        ];
        $data['visual'] = $visual;

        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_judgment_calibration_available'] = true;
        $data['summary']['calibrated_aesthetic_score'] = $cal['aesthetic'];
        $data['summary']['calibrated_coherence_score'] = $cal['coherence'];
        $data['summary']['effective_section_coherence'] = $effective_coherence;
        $data['summary']['effective_design_system_coherence'] = $effective_design_system;
        $response->set_data( $data );
        return $response;
    }

    private static function calibrate_intervention( array $issue ): string {
        $category = strtolower( trim( (string) ( $issue['category'] ?? 'other' ) ) );
        $evidence = strtolower( (string) ( $issue['evidence'] ?? '' ) . ' ' . (string) ( $issue['recommendation'] ?? '' ) );
        if ( 'section_choice' === $category ) return 'replace_section';
        if ( 'imagery' === $category ) return 'imagery';
        if ( 'typography' === $category || 'component_style' === $category || 'conversion' === $category || 'spacing' === $category ) return 'tweak';
        if ( 'density' === $category ) {
            if ( self::contains_any( $evidence, [ 'reduce the amount of text', 'shorten', 'simplify copy', 'too much text' ] ) ) return 'content';
            return 'restructure';
        }
        if ( 'composition' === $category || 'hierarchy' === $category || 'transition' === $category ) return 'restructure';
        return (string) ( $issue['intervention_level'] ?? 'none' );
    }

    private static function intervention_reason( array $issue, string $calibrated ): string {
        $model = (string) ( $issue['intervention_level'] ?? 'none' );
        if ( $model === $calibrated ) return 'Model intervention already matches the least-destructive evidence class.';
        return 'Calibrated to the least-destructive intervention supported by the issue category and recommendation; section replacement is reserved for section-choice evidence.';
    }

    private static function contains_any( string $haystack, array $needles ): bool {
        foreach ( $needles as $needle ) if ( false !== strpos( $haystack, $needle ) ) return true;
        return false;
    }

    private static function score( $value ): int {
        return max( 1, min( 10, absint( $value ) ) );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_judgment_calibration'] = true;
        $data['aesthetic_judgment_calibration_version'] = self::VERSION;
        $data['aesthetic_judgment_calibration_read_only'] = true;
        $data['aesthetic_judgment_calibration_automatic_writes'] = false;
        $response->set_data( $data );
        return $response;
    }
}

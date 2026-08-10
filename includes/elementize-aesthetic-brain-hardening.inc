<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Brain', false ) ) return;

/**
 * Conservative post-processing for Aesthetic Brain judgments.
 *
 * This pass fail-closes professional approval when section coverage is
 * insufficient, cross-checks the visual judgment against deterministic rendered
 * observations and the existing visual critique, and never grants write authority.
 */
final class Elementize_Aesthetic_Brain_Hardening {
    private const VERSION = '0.26.1-h2';
    private const NS = 'elementize/v1';

    public static function init(): void {
        // Run after optional exact-section coverage recovery.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 540, 3 );
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

        $design = is_array( $visual['design_dna'] ?? null ) ? $visual['design_dna'] : [];
        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];

        if ( self::complete_design_dna( $design ) ) $design['available'] = true;
        if ( self::complete_art_direction( $art ) ) $art['available'] = true;
        if ( self::complete_section_coherence( $coherence ) ) $coherence['available'] = true;

        $marker_count = max( 0, absint( $coherence['marker_count'] ?? $art['marker_count'] ?? $design['marker_count'] ?? 0 ) );
        $covered = max( 0, absint( $coherence['covered_section_count'] ?? 0 ) );
        $ratio = $marker_count > 0 ? min( 1, $covered / $marker_count ) : 0.0;
        if ( is_numeric( $coherence['section_coverage_ratio'] ?? null ) ) $ratio = min( 1, max( 0, (float) $coherence['section_coverage_ratio'] ) );
        $coverage_complete = $marker_count > 0 && ( $marker_count <= 3 ? $covered >= $marker_count : $ratio >= 0.60 );

        $cross_checks = [];
        $requires_revision = false;
        $observations = is_array( $visual['render_observations']['observations'] ?? null ) ? $visual['render_observations']['observations'] : [];
        foreach ( $observations as $observation ) {
            if ( ! is_array( $observation ) ) continue;
            $type = (string) ( $observation['type'] ?? '' );
            if ( 'cta_style_divergence_present' === $type ) {
                $converged = ! empty( $observation['visual_convergence'] );
                $cross_checks[] = [
                    'type' => $type,
                    'measured' => true,
                    'visual_convergence' => $converged,
                    'note' => 'Rendered CTA style signatures are not all identical; this may be intentional, but a blanket claim of CTA consistency is not accepted without qualification.',
                ];
                if ( isset( $coherence['component_consistency']['cta_language'] ) && 'consistent' === $coherence['component_consistency']['cta_language'] ) {
                    $coherence['component_consistency']['cta_language'] = 'mixed';
                    $coherence['component_consistency']['cta_language_hardened_from'] = 'consistent';
                }
                if ( $converged ) $requires_revision = true;
            } elseif ( 'micro_text_present' === $type ) {
                $converged = ! empty( $observation['visual_convergence'] );
                $cross_checks[] = [
                    'type' => $type,
                    'measured' => true,
                    'visual_convergence' => $converged,
                    'note' => 'Visible microtext exists in the rendered page; size alone is not a defect, but it must not be silently ignored when the visual critique also flags typography or legibility.',
                ];
                if ( $converged ) $requires_revision = true;
            } elseif ( 'large_internal_padding_present' === $type ) {
                $cross_checks[] = [
                    'type' => $type,
                    'measured' => true,
                    'visual_convergence' => ! empty( $observation['visual_convergence'] ),
                    'note' => 'Large internal padding exists in the rendered page. This remains advisory unless independent visual evidence supports a problem.',
                ];
            }
        }

        $high_findings = [];
        foreach ( is_array( $visual['critique']['findings'] ?? null ) ? $visual['critique']['findings'] : [] as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            if ( 'high' !== strtolower( trim( (string) ( $finding['confidence'] ?? '' ) ) ) ) continue;
            $high_findings[] = [
                'category' => self::clip( (string) ( $finding['category'] ?? 'visual' ), 80 ),
                'evidence' => self::clip( (string) ( $finding['evidence'] ?? '' ), 350 ),
            ];
        }
        if ( $high_findings ) {
            $cross_checks[] = [
                'type' => 'high_confidence_visual_findings_present',
                'count' => count( $high_findings ),
                'findings' => array_slice( $high_findings, 0, 4 ),
                'note' => 'A page-level approval cannot ignore independent high-confidence visual findings from the established critique pass.',
            ];
            if ( empty( $art['top_issues'] ) ) $requires_revision = true;
        }

        $model_approval = (string) ( $art['professional_approval'] ?? 'unavailable' );
        if ( ! isset( $art['model_professional_approval'] ) ) $art['model_professional_approval'] = $model_approval;
        $effective = $model_approval;
        if ( ! $coverage_complete ) {
            $effective = 'unavailable';
        } elseif ( $requires_revision && 'approve' === $effective ) {
            $effective = 'revise';
        }

        $art['effective_professional_approval'] = $effective;
        $art['assessment_complete'] = $coverage_complete;
        $art['section_coverage_ratio'] = round( $ratio, 3 );
        $art['scores_usable'] = $coverage_complete;
        $art['approval_usable'] = $coverage_complete
            && in_array( (string) ( $art['confidence'] ?? '' ), [ 'medium', 'high' ], true )
            && in_array( $effective, [ 'approve', 'revise', 'reject' ], true );
        $art['evidence_cross_checks'] = $cross_checks;
        $art['revision_required_by_cross_checks'] = $requires_revision;

        $design['assessment_complete'] = $coverage_complete;
        $design['usable_as_page_design_context'] = $coverage_complete
            && in_array( (string) ( $design['confidence'] ?? '' ), [ 'medium', 'high' ], true );

        $coherence['assessment_complete'] = $coverage_complete;
        $coherence['section_coverage_ratio'] = round( $ratio, 3 );
        $coherence['evidence_cross_checks'] = $cross_checks;

        foreach ( [ &$design, &$art, &$coherence ] as &$object ) {
            if ( ! $object ) continue;
            $object['hardening_version'] = self::VERSION;
            $object['read_only'] = true;
            $object['writes_performed'] = false;
            $object['automatic_write_allowed'] = false;
        }
        unset( $object );

        $visual['design_dna'] = $design;
        $visual['page_art_direction'] = $art;
        $visual['section_coherence'] = $coherence;
        $data['visual'] = $visual;

        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_brain_available'] = ! empty( $art['available'] ) && ! empty( $design['available'] ) && ! empty( $coherence['available'] );
        $data['summary']['professional_visual_approval'] = $effective;
        $data['summary']['professional_visual_approval_usable'] = ! empty( $art['available'] ) && ! empty( $art['approval_usable'] );
        $data['summary']['aesthetic_brain_read_only'] = true;
        $data['summary']['aesthetic_brain_blocks_completion_by_default'] = false;
        $response->set_data( $data );
        return $response;
    }

    private static function complete_design_dna( array $data ): bool {
        if ( ! empty( $data['reason'] ) ) return false;
        return isset( $data['aesthetic_direction'], $data['visual_density'], $data['design_system_coherence'], $data['confidence'] )
            && is_array( $data['aesthetic_direction'] );
    }

    private static function complete_art_direction( array $data ): bool {
        if ( ! empty( $data['reason'] ) ) return false;
        return in_array( (string) ( $data['professional_approval'] ?? '' ), [ 'approve', 'revise', 'reject' ], true )
            && isset( $data['aesthetic_score'], $data['coherence_score'], $data['professional_rationale'], $data['confidence'] );
    }

    private static function complete_section_coherence( array $data ): bool {
        if ( ! empty( $data['reason'] ) ) return false;
        return in_array( (string) ( $data['coherence_verdict'] ?? '' ), [ 'weak', 'mixed', 'strong' ], true )
            && isset( $data['rhythm_sequence'], $data['covered_section_count'], $data['component_consistency'], $data['confidence'] )
            && is_array( $data['rhythm_sequence'] );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( wp_strip_all_tags( $value ) );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_brain_hardening'] = true;
        $data['aesthetic_brain_hardening_version'] = self::VERSION;
        $data['aesthetic_brain_hardening_fail_closed_approval'] = true;
        $data['aesthetic_brain_hardening_render_cross_checks'] = true;
        $response->set_data( $data );
        return $response;
    }
}

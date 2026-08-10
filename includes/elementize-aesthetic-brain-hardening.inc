<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Brain', false ) ) return;

/**
 * Post-process the first Aesthetic Brain payload conservatively.
 *
 * The v0.26.0 producer intentionally starts from an unavailable metadata base.
 * PHP array-union semantics preserve that base flag even when a complete local
 * model result was produced. This hardening pass promotes availability only
 * when the expected result-specific evidence is actually present and no reason
 * for unavailability was recorded. It never changes aesthetic judgments and
 * never grants write authority.
 */
final class Elementize_Aesthetic_Brain_Hardening {
    private const VERSION = '0.26.0-h1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 535, 3 );
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
        $data['summary']['professional_visual_approval'] = (string) ( $art['professional_approval'] ?? 'unavailable' );
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

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_brain_hardening'] = true;
        $data['aesthetic_brain_hardening_version'] = self::VERSION;
        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Section_Convergence_Backstop', false ) ) return;
if ( ! class_exists( 'Elementize_Focused_Section_Verification', false ) ) return;

final class Elementize_Section_Convergence_Backstop_Hardening {
    private const VERSION = '0.24.4';
    private const NS = 'elementize/v1';

    public static function init(): void {
        // Backstop seeds probe-only records at 512. Remove duplicate assessments before
        // the section crop verifier runs at 515, then harden the crop evidence before
        // the backstop consumes it at 518.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'dedupe_before_section' ], 514, 3 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'ground_before_backstop' ], 517, 3 );
    }

    public static function dedupe_before_section( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( ! self::is_visual_audit_request( $request, $response ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $focused = is_array( $visual['focused_verification'] ?? null ) ? $visual['focused_verification'] : [];
        $diag = is_array( $visual['section_convergence_backstop'] ?? null ) ? $visual['section_convergence_backstop'] : [];
        $probe_ids = array_fill_keys( array_map( 'strval', is_array( $diag['probe_target_ids'] ?? null ) ? $diag['probe_target_ids'] : [] ), true );
        if ( ! $probe_ids || ! is_array( $focused['assessments'] ?? null ) ) return $response;

        $chosen = [];
        $order = [];
        $removed = 0;
        foreach ( $focused['assessments'] as $assessment ) {
            if ( ! is_array( $assessment ) ) continue;
            $tid = trim( (string) ( $assessment['target_id'] ?? '' ) );
            if ( '' === $tid || empty( $probe_ids[$tid] ) ) {
                $key = '__other_' . count( $order );
                $chosen[$key] = $assessment;
                $order[] = $key;
                continue;
            }
            if ( ! array_key_exists( $tid, $chosen ) ) {
                $chosen[$tid] = $assessment;
                $order[] = $tid;
                continue;
            }
            $removed++;
            if ( self::trigger_score( $assessment ) > self::trigger_score( $chosen[$tid] ) ) $chosen[$tid] = $assessment;
        }

        if ( $removed < 1 ) return $response;
        $deduped = [];
        foreach ( $order as $key ) if ( isset( $chosen[$key] ) ) $deduped[] = $chosen[$key];
        $focused['assessments'] = $deduped;
        $visual['focused_verification'] = $focused;
        $diag['hardening_version'] = self::VERSION;
        $diag['duplicate_probe_assessment_count_removed'] = $removed;
        $diag['duplicate_probe_assessments_removed'] = true;
        $visual['section_convergence_backstop'] = $diag;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    public static function ground_before_backstop( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( ! self::is_visual_audit_request( $request, $response ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $section = is_array( $visual['focused_section_verification'] ?? null ) ? $visual['focused_section_verification'] : [];
        $focused = is_array( $visual['focused_verification'] ?? null ) ? $visual['focused_verification'] : [];
        $diag = is_array( $visual['section_convergence_backstop'] ?? null ) ? $visual['section_convergence_backstop'] : [];
        if ( empty( $section['available'] ) || ! is_array( $section['results'] ?? null ) ) return $response;

        $changed = [];
        $results_by_id = [];
        $result_order = [];
        $duplicates = 0;
        foreach ( $section['results'] as $result ) {
            if ( ! is_array( $result ) ) continue;
            $tid = trim( (string) ( $result['target_id'] ?? '' ) );
            if ( '' === $tid ) continue;

            if ( empty( $result['support'] ) && self::explicit_spatial_causality( $result ) ) {
                $result['evidence_grounded'] = true;
                $result['grounding_reason'] = 'section_crop_explicit_gap_relationship_present';
                $result['support'] = true;
                $result['automatic_write_allowed'] = false;
                $changed[$tid] = true;
            }

            if ( ! array_key_exists( $tid, $results_by_id ) ) {
                $results_by_id[$tid] = $result;
                $result_order[] = $tid;
            } else {
                $duplicates++;
                if ( self::result_score( $result ) > self::result_score( $results_by_id[$tid] ) ) $results_by_id[$tid] = $result;
            }
        }

        $results = [];
        foreach ( $result_order as $tid ) if ( isset( $results_by_id[$tid] ) ) $results[] = $results_by_id[$tid];
        $section['results'] = $results;
        $section['candidate_count'] = count( $results );
        $section['reviewable_count'] = count( array_filter( $results, static function ( $r ): bool {
            return is_array( $r ) && ! empty( $r['support'] );
        } ) );
        $section['evidence_hardening_version'] = self::VERSION;
        $section['explicit_spatial_relationship_support_count'] = count( $changed );
        $section['duplicate_result_count_removed'] = $duplicates;
        $visual['focused_section_verification'] = $section;

        if ( $changed && is_array( $focused['assessments'] ?? null ) ) {
            foreach ( $focused['assessments'] as &$assessment ) {
                if ( ! is_array( $assessment ) ) continue;
                $tid = trim( (string) ( $assessment['target_id'] ?? '' ) );
                if ( '' === $tid || empty( $changed[$tid] ) ) continue;
                $assessment['section_focus_evidence_grounded'] = true;
                $assessment['section_focus_grounding_reason'] = 'section_crop_explicit_gap_relationship_present';
                $assessment['section_focus_support'] = true;
                $assessment['focused_visual_support'] = true;
                $assessment['eligible_for_hardened_convergence_review'] = true;
                $assessment['evidence_visual_grounded'] = true;
                $assessment['evidence_grounding_reason'] = 'section_crop_explicit_gap_relationship_present';
                $assessment['automatic_write_allowed'] = false;
            }
            unset( $assessment );
            $focused['hardened_convergence_review_count'] = count( array_filter( $focused['assessments'], static function ( $a ): bool {
                return is_array( $a ) && ! empty( $a['eligible_for_hardened_convergence_review'] );
            } ) );
            $focused['grounded_visibly_problematic_count'] = count( array_filter( $focused['assessments'], static function ( $a ): bool {
                return is_array( $a ) && 'visibly_problematic' === (string) ( $a['verdict'] ?? '' ) && ! empty( $a['evidence_visual_grounded'] );
            } ) );
            $visual['focused_verification'] = $focused;
        }

        $diag['hardening_version'] = self::VERSION;
        $diag['explicit_spatial_relationship_support_count'] = count( $changed );
        $diag['explicit_spatial_relationship_target_ids'] = array_values( array_keys( $changed ) );
        $diag['duplicate_section_result_count_removed'] = $duplicates;
        $diag['automatic_write_allowed'] = false;
        $visual['section_convergence_backstop'] = $diag;

        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function trigger_score( array $assessment ): int {
        $qualifies = 'visibly_problematic' === (string) ( $assessment['verdict'] ?? '' )
            && in_array( (string) ( $assessment['confidence'] ?? '' ), [ 'medium', 'high' ], true )
            && ! empty( $assessment['assessment_consistent'] )
            && empty( $assessment['evidence_visual_grounded'] );
        if ( ! $qualifies ) return 0;
        return empty( $assessment['section_probe_only'] ) ? 3 : 2;
    }

    private static function result_score( array $result ): int {
        if ( ! empty( $result['support'] ) ) return 4;
        if ( 'visibly_problematic' === (string) ( $result['verdict'] ?? '' ) && 'high' === (string) ( $result['confidence'] ?? '' ) ) return 3;
        if ( 'visibly_problematic' === (string) ( $result['verdict'] ?? '' ) ) return 2;
        return ! empty( $result['available'] ) ? 1 : 0;
    }

    private static function explicit_spatial_causality( array $result ): bool {
        if ( 'visibly_problematic' !== (string) ( $result['verdict'] ?? '' ) ) return false;
        if ( ! in_array( (string) ( $result['confidence'] ?? '' ), [ 'medium', 'high' ], true ) ) return false;
        if ( 'reduce' !== (string) ( $result['direction'] ?? '' ) ) return false;
        $text = strtolower( trim( preg_replace( '/\s+/', ' ', (string) ( $result['evidence'] ?? '' ) ) ?? '' ) );
        if ( '' === $text ) return false;
        if ( preg_match( '/\b\d+(?:\.\d+)?\s*(?:px|pixels?)\b|\b(?:padding|spacing)\s*(?:value|of|=|:)\s*\d+/i', $text ) ) return false;

        // Require an explicit visible relationship rather than a bare adjective such
        // as "excessive padding". This accepts evidence like "a significant gap
        // between the content and the top edge" while remaining numeric-independent.
        if ( preg_match( '/\b(?:gap|space|distance)\b.{0,120}\b(?:between|from)\b.{0,120}\b(?:content|text|heading|copy|top|bottom|edge|section)\b/i', $text ) ) return true;
        if ( preg_match( '/\b(?:content|text|heading|copy)\b.{0,120}\b(?:far|separated|detached)\b.{0,120}\b(?:top|bottom|edge|section)\b/i', $text ) ) return true;
        if ( preg_match( '/\b(?:top|bottom|section)\s+edge\b.{0,120}\b(?:gap|space|distance)\b/i', $text ) ) return true;
        return false;
    }

    private static function is_visual_audit_request( WP_REST_Request $request, WP_REST_Response $response ): bool {
        if ( $response->get_status() >= 400 || 'GET' !== strtoupper( $request->get_method() ) ) return false;
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $request->get_route() ) ) return false;
        return self::truthy( $request->get_param( 'include_visual' ) );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['section_convergence_backstop_hardening_version'] = self::VERSION;
        $data['section_convergence_backstop_duplicate_probe_dedupe'] = true;
        $data['section_convergence_backstop_explicit_gap_relationship_grounding'] = true;
        $data['section_convergence_backstop_hardening_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

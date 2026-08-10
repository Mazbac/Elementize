<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Focused_Visual_Verification', false ) ) return;

final class Elementize_Focused_Visual_Verification_Hardening {
    private const VERSION = '0.23.5';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 510, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $focused = is_array( $visual['focused_verification'] ?? null ) ? $visual['focused_verification'] : [];
        if ( empty( $focused['available'] ) ) return $response;

        $candidates = [];
        foreach ( is_array( $focused['candidates'] ?? null ) ? $focused['candidates'] : [] as $candidate ) {
            if ( ! is_array( $candidate ) ) continue;
            $target_id = trim( (string) ( $candidate['target_id'] ?? '' ) );
            if ( '' !== $target_id ) $candidates[$target_id] = $candidate;
        }

        $raw_reviewable = is_numeric( $focused['hardened_convergence_review_count'] ?? null ) ? (int) $focused['hardened_convergence_review_count'] : 0;
        $reviewable = 0;
        $grounded_problematic = 0;
        $rejected = 0;
        $measurement_context_accepted = 0;
        $assessments = [];

        foreach ( is_array( $focused['assessments'] ?? null ) ? $focused['assessments'] : [] as $assessment ) {
            if ( ! is_array( $assessment ) ) continue;
            $target_id = trim( (string) ( $assessment['target_id'] ?? '' ) );
            $candidate = is_array( $candidates[$target_id] ?? null ) ? $candidates[$target_id] : [];
            $grounding = self::evidence_grounding( $candidate, (string) ( $assessment['evidence'] ?? '' ) );
            $pre_support = ! empty( $assessment['focused_visual_support'] );

            $assessment['pre_evidence_hardening_focused_visual_support'] = $pre_support;
            $assessment['evidence_visual_grounded'] = ! empty( $grounding['grounded'] );
            $assessment['evidence_grounding_reason'] = (string) ( $grounding['reason'] ?? '' );
            $assessment['measurement_context_present'] = ! empty( $grounding['measurement_context_present'] );
            $assessment['focused_visual_support'] = $pre_support && ! empty( $grounding['grounded'] );
            $assessment['eligible_for_hardened_convergence_review'] = ! empty( $assessment['focused_visual_support'] );
            $assessment['automatic_write_allowed'] = false;

            if ( $pre_support && empty( $grounding['grounded'] ) ) $rejected++;
            if ( ! empty( $grounding['grounded'] ) && ! empty( $grounding['measurement_context_present'] ) ) $measurement_context_accepted++;
            if ( 'visibly_problematic' === (string) ( $assessment['verdict'] ?? '' ) && ! empty( $grounding['grounded'] ) ) $grounded_problematic++;
            if ( ! empty( $assessment['eligible_for_hardened_convergence_review'] ) ) $reviewable++;
            $assessments[] = $assessment;
        }

        $focused['base_verification_version'] = (string) ( $focused['verification_version'] ?? '' );
        $focused['verification_version'] = self::VERSION;
        $focused['evidence_grounding_hardening'] = true;
        $focused['numeric_measurement_alone_is_insufficient'] = true;
        $focused['measurement_context_allowed_when_visual_causality_present'] = true;
        $focused['visual_causality_language_required'] = true;
        $focused['raw_hardened_convergence_review_count'] = $raw_reviewable;
        $focused['hardened_convergence_review_count'] = $reviewable;
        $focused['grounded_visibly_problematic_count'] = $grounded_problematic;
        $focused['evidence_grounding_rejected_count'] = $rejected;
        $focused['measurement_context_accepted_count'] = $measurement_context_accepted;
        $focused['assessments'] = $assessments;
        $focused['automatic_write_allowed'] = false;
        $focused['note'] = 'Focused visual support requires screenshot-grounded causal evidence. Numeric measurement, threshold, or acceptable-range language cannot promote a target by itself, but measurement context is allowed when the same evidence also states an explicit visual spatial or legibility consequence. No write authority is granted.';

        $visual['focused_verification'] = $focused;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function evidence_grounding( array $candidate, string $evidence ): array {
        $text = strtolower( trim( preg_replace( '/\s+/', ' ', $evidence ) ?? $evidence ) );
        if ( '' === $text ) return [ 'grounded' => false, 'reason' => 'empty_evidence', 'measurement_context_present' => false ];

        $measurement_context = (bool) preg_match(
            '/\b(?:padding|font(?:-|\s)?size|spacing)?\s*value(?:\s+of)?\b|\bacceptable\s+range\b|\bwithin\s+(?:an\s+)?acceptable\s+range\b|\b(?:padding|font(?:-|\s)?size|spacing)\s*(?:of|=|:)\s*\d+(?:\.\d+)?\b/i',
            $text
        );

        $type = strtolower( trim( (string) ( $candidate['observation_type'] ?? '' ) ) );
        if ( 'large_internal_padding_present' === $type ) {
            $visual_terms = [
                'empty space', 'blank space', 'white space', 'whitespace', 'breathing room',
                'spacing rhythm', 'visual rhythm', 'section balance', 'visual balance', 'poor balance', 'unbalanced',
                'space above', 'space below', 'gap above', 'gap below', 'content starts',
                'content sits', 'adjacent section', 'next section', 'previous section', 'composition',
                'layout feels', 'layout appears', 'layout looks', 'too much space',
            ];
            if ( self::contains_any( $text, $visual_terms ) ) {
                return [
                    'grounded' => true,
                    'reason' => $measurement_context ? 'visual_spatial_causality_present_with_measurement_context' : 'visual_spatial_causality_present',
                    'measurement_context_present' => $measurement_context,
                ];
            }
            return [
                'grounded' => false,
                'reason' => $measurement_context ? 'measurement_or_range_language_without_visual_causality' : 'visual_spatial_causality_missing',
                'measurement_context_present' => $measurement_context,
            ];
        }

        if ( 'micro_text_present' === $type ) {
            $visual_terms = [
                'hard to read', 'difficult to read', 'too small to read', 'legibility', 'legible',
                'readability', 'readable', 'tiny text', 'small text', 'caption', 'label', 'microcopy',
                'comfortable to read', 'comfortably readable', 'read comfortably',
            ];
            if ( self::contains_any( $text, $visual_terms ) ) {
                return [
                    'grounded' => true,
                    'reason' => $measurement_context ? 'visual_legibility_causality_present_with_measurement_context' : 'visual_legibility_causality_present',
                    'measurement_context_present' => $measurement_context,
                ];
            }
            return [
                'grounded' => false,
                'reason' => $measurement_context ? 'measurement_or_range_language_without_visual_causality' : 'visual_legibility_causality_missing',
                'measurement_context_present' => $measurement_context,
            ];
        }

        return [ 'grounded' => false, 'reason' => 'unsupported_observation_type', 'measurement_context_present' => $measurement_context ];
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
        $data['rendered_visual_focused_verification_evidence_hardening'] = true;
        $data['rendered_visual_focused_verification_numeric_measurement_alone_insufficient'] = true;
        $data['rendered_visual_focused_verification_measurement_context_allowed_with_visual_causality'] = true;
        $data['rendered_visual_focused_verification_visual_causality_language_required'] = true;
        $data['rendered_visual_focused_verification_evidence_hardening_read_only'] = true;
        $data['rendered_visual_focused_verification_evidence_hardening_automatic_write_allowed'] = false;
        $data['rendered_visual_focused_verification_evidence_hardening_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

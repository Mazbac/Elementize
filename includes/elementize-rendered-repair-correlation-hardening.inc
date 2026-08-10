<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Repair_Correlation', false ) ) return;

final class Elementize_Rendered_Repair_Correlation_Hardening {
    private const VERSION = '0.19.1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 430, 3 );
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
        $correlation = is_array( $visual['repair_correlation'] ?? null ) ? $visual['repair_correlation'] : [];
        $metrics = is_array( $visual['render_metrics'] ?? null ) ? $visual['render_metrics'] : [];
        if ( empty( $correlation['available'] ) || empty( $metrics['available'] ) ) return $response;

        $sections = [];
        foreach ( is_array( $metrics['sections'] ?? null ) ? $metrics['sections'] : [] as $section ) {
            if ( ! is_array( $section ) ) continue;
            $top = trim( (string) ( $section['top_level_element_id'] ?? '' ) );
            if ( '' !== $top ) $sections[$top] = $section;
        }

        $findings = is_array( $correlation['findings'] ?? null ) ? $correlation['findings'] : [];
        $supported_total = 0;
        $planning_total = 0;
        $property_measured_total = 0;
        $corrected_total = 0;

        foreach ( $findings as &$finding ) {
            if ( ! is_array( $finding ) ) continue;
            $candidates = is_array( $finding['repair_candidates'] ?? null ) ? $finding['repair_candidates'] : [];
            $supported = 0;
            $planning = 0;

            foreach ( $candidates as &$candidate ) {
                if ( ! is_array( $candidate ) ) continue;
                $corr = is_array( $candidate['rendered_correlation'] ?? null ) ? $candidate['rendered_correlation'] : [];
                $before = wp_json_encode( $corr );
                $corr = self::harden_candidate( $candidate, $corr, $sections );
                $after = wp_json_encode( $corr );
                if ( is_string( $before ) && is_string( $after ) && ! hash_equals( $before, $after ) ) $corrected_total++;
                if ( ! empty( $corr['property_measured'] ) ) $property_measured_total++;
                if ( 'supported' === (string) ( $corr['causal_support'] ?? '' ) ) { $supported++; $supported_total++; }
                if ( ! empty( $corr['bounded_value_planning_ready'] ) ) { $planning++; $planning_total++; }
                $candidate['rendered_correlation'] = $corr;
            }
            unset( $candidate );

            $finding['repair_candidates'] = $candidates;
            $counts = is_array( $finding['candidate_counts'] ?? null ) ? $finding['candidate_counts'] : [];
            $counts['supported'] = $supported;
            $counts['planning_ready'] = $planning;
            $finding['candidate_counts'] = $counts;
        }
        unset( $finding );

        $correlation['correlation_version'] = self::VERSION;
        $correlation['findings'] = $findings;
        $correlation['causally_supported_candidate_count'] = $supported_total;
        $correlation['bounded_value_planning_ready_count'] = $planning_total;
        $correlation['automatic_write_allowed'] = false;
        $correlation['measurement_accuracy_hardening'] = [
            'enabled' => true,
            'composite_spacing_box_measurement' => true,
            'exact_top_level_background_color_measurement' => true,
            'property_measured_explicit' => true,
            'semantic_actionability_still_required' => true,
            'property_measured_candidate_count' => $property_measured_total,
            'corrected_candidate_count' => $corrected_total,
            'automatic_write_allowed' => false,
        ];
        $correlation['note'] = 'Rendered correlation remains read-only. 0.19.1 distinguishes an exact measured property from semantic causal support: composite top-level padding/margin boxes and exact top-level background colors now count as measured evidence, but supporting/weak candidates remain blocked from bounded value planning.';
        $visual['repair_correlation'] = $correlation;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function harden_candidate( array $candidate, array $corr, array $sections ): array {
        $match = (string) ( $corr['evidence_match'] ?? '' );
        $property = (string) ( $corr['rendered_property'] ?? '' );
        $evidence = is_array( $corr['rendered_evidence'] ?? null ) ? $corr['rendered_evidence'] : [];
        $semantic = strtolower( (string) ( $candidate['actionability'] ?? 'weak' ) );
        $type = strtolower( (string) ( $candidate['candidate_type'] ?? '' ) );
        $category = strtolower( (string) ( $candidate['category'] ?? '' ) );
        $scope = strtolower( (string) ( $candidate['responsive_scope'] ?? 'base' ) );
        $top = trim( (string) ( $candidate['top_level_element_id'] ?? '' ) );
        $element_id = trim( (string) ( $candidate['element_id'] ?? '' ) );
        $exact = in_array( $match, [ 'exact_element', 'exact_top_level' ], true );
        $property_measured = self::evidence_measured( $property, $evidence );

        if ( ! $property_measured && 'exact_top_level' === $match && $element_id === $top && isset( $sections[$top] ) && is_array( $sections[$top] ) ) {
            $section = $sections[$top];
            if ( in_array( $property, [ 'padding', 'margin' ], true ) ) {
                $box = is_array( $section[$property] ?? null ) ? $section[$property] : [];
                if ( self::numeric_box( $box ) ) {
                    $evidence = [ 'box' => $box ];
                    $property_measured = true;
                }
            } elseif ( 'background_color' === $property ) {
                $value = trim( (string) ( $section['background_color'] ?? '' ) );
                if ( '' !== $value ) {
                    $evidence = [ 'rendered_value' => $value ];
                    $property_measured = true;
                }
            }
        }

        $causal = (string) ( $corr['causal_support'] ?? 'unmeasured' );
        if ( $property_measured && 'direct' === $semantic && $exact ) $causal = 'supported';
        elseif ( $property_measured && in_array( $semantic, [ 'supporting', 'weak' ], true ) && $exact ) $causal = 'context_only';
        elseif ( ! $property_measured && $exact ) $causal = 'exact_element_but_property_unmeasured';
        elseif ( 'section_context_only' === $match ) $causal = 'section_context_only';
        else $causal = 'unmeasured';

        $planning_ready = ! empty( $candidate['value_planning_ready'] )
            && 'direct' === $semantic
            && 'supported' === $causal
            && 'base' === $scope
            && 'design_control' === $type
            && in_array( $category, [ 'spacing', 'typography' ], true );

        $block = (string) ( $corr['planning_or_write_block_reason'] ?? '' );
        if ( $planning_ready ) {
            $block = 'No write is authorized yet. This exact base-scope reversible design control has sufficient semantic and rendered-property evidence to enter a separate bounded value-planning step.';
        } elseif ( 'direct' !== $semantic ) {
            $block = 'Only semantically direct candidates can enter bounded value planning, even when the rendered property is measured exactly.';
        } elseif ( ! $property_measured ) {
            $block = 'The candidate is semantically direct, but the exact affected rendered property was not measured on this element.';
        } elseif ( 'base' !== $scope ) {
            $block = 'First bounded experiments require base-scope controls so desktop rendered evidence is not confused with a responsive override.';
        }

        $corr['rendered_evidence'] = $evidence;
        $corr['property_measured'] = $property_measured;
        $corr['semantic_actionability'] = $semantic;
        $corr['causal_support'] = $causal;
        $corr['bounded_value_planning_ready'] = $planning_ready;
        $corr['automatic_write_allowed'] = false;
        $corr['planning_or_write_block_reason'] = $block;
        return $corr;
    }

    private static function evidence_measured( string $property, array $evidence ): bool {
        if ( isset( $evidence['rendered_value_px'] ) && is_numeric( $evidence['rendered_value_px'] ) ) return true;
        if ( array_key_exists( 'rendered_value', $evidence ) && '' !== trim( (string) $evidence['rendered_value'] ) ) return true;
        if ( isset( $evidence['box'] ) && is_array( $evidence['box'] ) && self::numeric_box( $evidence['box'] ) ) return true;
        if ( ! empty( $evidence['matches'] ) && is_array( $evidence['matches'] ) ) return true;
        return false;
    }

    private static function numeric_box( array $box ): bool {
        $found = false;
        foreach ( [ 'top', 'right', 'bottom', 'left' ] as $side ) {
            if ( ! array_key_exists( $side, $box ) ) continue;
            if ( ! is_numeric( $box[$side] ) ) return false;
            $found = true;
        }
        return $found;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_repair_correlation_measurement_hardening'] = true;
        $data['rendered_visual_repair_correlation_composite_spacing_measured'] = true;
        $data['rendered_visual_repair_correlation_top_level_background_measured'] = true;
        $data['rendered_visual_repair_correlation_property_measured_explicit'] = true;
        $data['rendered_visual_repair_correlation_semantic_gate_preserved'] = true;
        $data['rendered_visual_repair_value_planning_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

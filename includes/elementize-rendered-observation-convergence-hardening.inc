<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observation_Convergence', false ) ) return;

final class Elementize_Rendered_Observation_Convergence_Hardening {
    private const VERSION = '0.22.1';
    private const NS = 'elementize/v1';
    private const MAX_BLOCKED_TARGETS = 8;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 490, 3 );
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
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        if ( empty( $conv['available'] ) ) return $response;

        $loc = is_array( $visual['localization'] ?? null ) ? $visual['localization'] : [];
        $supports = self::localized_supports( $loc );
        $original = is_array( $conv['promoted_targets'] ?? null ) ? $conv['promoted_targets'] : [];
        $kept = [];
        $blocked = [];

        foreach ( $original as $target ) {
            if ( ! is_array( $target ) ) continue;
            $assessment = self::assess_target( $target, $supports );
            if ( ! empty( $assessment['supported'] ) ) {
                $target['promotion_status'] = 'promoted_read_only_section_verified';
                $target['section_specific_visual_support'] = true;
                $target['semantic_localized_support'] = true;
                $target['localized_support'] = $assessment['support'];
                $target['global_visual_convergence_alone_was_not_used'] = true;
                $target['eligible_for_bounded_planning_review'] = ! empty( $target['eligible_for_bounded_planning_review'] );
                $target['automatic_write_allowed'] = false;
                $kept[] = $target;
                continue;
            }

            if ( count( $blocked ) < self::MAX_BLOCKED_TARGETS ) {
                $blocked[] = [
                    'target_id' => (string) ( $target['target_id'] ?? '' ),
                    'observation_type' => (string) ( $target['observation_type'] ?? '' ),
                    'top_level_element_id' => (string) ( $target['top_level_element_id'] ?? '' ),
                    'element_id' => (string) ( $target['element_id'] ?? '' ),
                    'rendered_property' => (string) ( $target['rendered_property'] ?? '' ),
                    'matched_component' => $target['matched_component'] ?? null,
                    'rendered_value' => $target['rendered_value'] ?? null,
                    'exact_control_match' => ! empty( $target['exact_control_match'] ),
                    'global_visual_convergence' => ! empty( $target['visual_convergence'] ),
                    'section_specific_visual_support' => false,
                    'semantic_localized_support' => false,
                    'reason_code' => (string) ( $assessment['reason_code'] ?? 'section_support_missing' ),
                    'reason' => (string) ( $assessment['reason'] ?? 'Section-specific semantic visual support was not available.' ),
                    'localized_section_categories' => $assessment['localized_section_categories'] ?? [],
                    'eligible_for_bounded_planning_review' => false,
                    'automatic_write_allowed' => false,
                ];
            }
        }

        $conv['convergence_version'] = self::VERSION;
        $conv['section_specific_visual_support_required'] = true;
        $conv['semantic_localized_support_required'] = true;
        $conv['global_visual_convergence_alone_is_insufficient'] = true;
        $conv['consistency_hardened_localization_required'] = true;
        $conv['localization_available'] = ! empty( $loc['available'] );
        $conv['localized_support_record_count'] = count( $supports );
        $conv['pre_hardening_promoted_target_count'] = count( $original );
        $conv['promoted_target_count'] = count( $kept );
        $conv['demoted_target_count'] = max( 0, count( $original ) - count( $kept ) );
        $conv['promoted_targets'] = $kept;
        $conv['blocked_targets'] = $blocked;
        $conv['defects_asserted'] = false;
        $conv['automatic_write_allowed'] = false;
        $conv['note'] = $kept
            ? 'Promoted targets now require section-specific medium/high-confidence consistency-hardened visual localization with a semantically matching issue class in addition to deterministic measurement and exact same-property control mapping. Global critique convergence alone cannot promote every measured sample.'
            : 'No target had all required evidence after section-specific semantic localization hardening. Exact control matches remain read-only observations and must not be turned into edits.';

        $visual['repair_convergence'] = $conv;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function localized_supports( array $loc ): array {
        if ( empty( $loc['available'] ) ) return [];
        $out = [];
        foreach ( is_array( $loc['localized_findings'] ?? null ) ? $loc['localized_findings'] : [] as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            if ( 'localized' !== (string) ( $finding['localization_status'] ?? '' ) ) continue;
            if ( empty( $finding['usable_for_repair_discovery'] ) ) continue;
            $confidence = strtolower( trim( (string) ( $finding['confidence'] ?? '' ) ) );
            if ( ! in_array( $confidence, [ 'medium', 'high' ], true ) ) continue;
            $category = strtolower( trim( (string) ( $finding['finding_category'] ?? '' ) ) );
            $ids = array_values( array_unique( array_filter( array_map( 'strval', is_array( $finding['top_level_element_ids'] ?? null ) ? $finding['top_level_element_ids'] : [] ) ) ) );
            foreach ( $ids as $top ) {
                $out[] = [
                    'finding_index' => absint( $finding['finding_index'] ?? 0 ),
                    'top_level_element_id' => $top,
                    'finding_category' => $category,
                    'confidence' => $confidence,
                    'localization_status' => 'localized',
                ];
            }
        }
        return $out;
    }

    private static function assess_target( array $target, array $supports ): array {
        $type = strtolower( trim( (string) ( $target['observation_type'] ?? '' ) ) );
        $top = trim( (string) ( $target['top_level_element_id'] ?? '' ) );
        $same_section = [];
        $semantic = [];

        foreach ( $supports as $support ) {
            if ( ! is_array( $support ) || (string) ( $support['top_level_element_id'] ?? '' ) !== $top ) continue;
            $same_section[] = (string) ( $support['finding_category'] ?? '' );
            if ( self::category_matches_observation( (string) ( $support['finding_category'] ?? '' ), $type ) ) $semantic[] = $support;
        }

        $same_section = array_values( array_unique( array_filter( $same_section ) ) );
        if ( $semantic ) {
            usort( $semantic, static function ( array $a, array $b ): int {
                $rank = [ 'high' => 2, 'medium' => 1 ];
                return ( $rank[$b['confidence'] ?? ''] ?? 0 ) <=> ( $rank[$a['confidence'] ?? ''] ?? 0 );
            } );
            return [
                'supported' => true,
                'support' => $semantic[0],
                'localized_section_categories' => $same_section,
            ];
        }

        if ( $same_section ) {
            return [
                'supported' => false,
                'reason_code' => 'section_localized_but_semantic_category_indirect',
                'reason' => 'The same section is visually localized, but only for issue classes that do not directly justify changing this rendered property.',
                'localized_section_categories' => $same_section,
            ];
        }

        return [
            'supported' => false,
            'reason_code' => 'no_section_specific_visual_support',
            'reason' => 'The measured target has exact control evidence and page-wide visual convergence, but no medium/high-confidence consistency-hardened visual finding localizes the same issue class to this section.',
            'localized_section_categories' => [],
        ];
    }

    private static function category_matches_observation( string $category, string $type ): bool {
        $category = strtolower( $category );
        if ( 'large_internal_padding_present' === $type ) {
            return self::contains_any( $category, [ 'whitespace', 'spacing', 'rhythm', 'balance', 'flow' ] );
        }
        if ( 'micro_text_present' === $type ) {
            return self::contains_any( $category, [ 'typograph', 'readab', 'legib' ] );
        }
        return false;
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
        $data['rendered_visual_observation_convergence_section_hardening'] = true;
        $data['rendered_visual_observation_convergence_section_specific_support_required'] = true;
        $data['rendered_visual_observation_convergence_semantic_localized_support_required'] = true;
        $data['rendered_visual_observation_convergence_global_signal_not_enough'] = true;
        $data['rendered_visual_observation_convergence_consistency_hardened_localization'] = true;
        $data['rendered_visual_observation_convergence_hierarchy_does_not_directly_authorize_spacing'] = true;
        $data['rendered_visual_observation_convergence_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

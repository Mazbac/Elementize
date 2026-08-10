<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Visual_Repair_Discovery_Hardening {
    private const VERSION = '0.16.1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 370, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual']['repair_discovery']['available'] ) ) return $response;
        $repair = $data['visual']['repair_discovery'];
        $findings = is_array( $repair['findings'] ?? null ) ? $repair['findings'] : [];

        $total_direct = 0;
        $total_supporting = 0;
        $total_weak = 0;
        $total_value_planning_ready = 0;

        foreach ( $findings as &$finding ) {
            if ( ! is_array( $finding ) ) continue;
            $category = strtolower( trim( (string) ( $finding['finding_category'] ?? '' ) ) );
            $candidates = is_array( $finding['repair_candidates'] ?? null ) ? $finding['repair_candidates'] : [];
            $counts = [ 'direct' => 0, 'supporting' => 0, 'weak' => 0, 'value_planning_ready' => 0 ];

            foreach ( $candidates as &$candidate ) {
                if ( ! is_array( $candidate ) ) continue;
                $assessment = self::assess( $category, $candidate );
                $candidate['actionability'] = $assessment['actionability'];
                $candidate['semantic_match_reason'] = $assessment['reason'];
                $candidate['value_planning_ready'] = $assessment['value_planning_ready'];
                $candidate['requires_reference_style'] = $assessment['requires_reference_style'];
                $candidate['requires_replacement_asset'] = $assessment['requires_replacement_asset'];
                $candidate['requires_verified_destination'] = $assessment['requires_verified_destination'];
                $candidate['automatic_write_allowed'] = false;
                $candidate['automatic_write_block_reason'] = $assessment['write_block_reason'];
                $counts[ $assessment['actionability'] ]++;
                if ( $assessment['value_planning_ready'] ) $counts['value_planning_ready']++;
            }
            unset( $candidate );

            $finding['repair_candidates'] = $candidates;
            $finding['semantic_actionability'] = [
                'direct_candidate_count' => $counts['direct'],
                'supporting_candidate_count' => $counts['supporting'],
                'weak_candidate_count' => $counts['weak'],
                'value_planning_ready_count' => $counts['value_planning_ready'],
                'automatic_write_allowed' => false,
                'note' => $counts['value_planning_ready'] > 0
                    ? 'At least one direct reversible design-control target is suitable for bounded value planning. A fresh exact read is still required before any experiment.'
                    : 'No candidate is ready for bounded value planning yet; do not turn these targets into an automatic edit.',
            ];

            $total_direct += $counts['direct'];
            $total_supporting += $counts['supporting'];
            $total_weak += $counts['weak'];
            $total_value_planning_ready += $counts['value_planning_ready'];
        }
        unset( $finding );

        $repair['repair_discovery_version'] = self::VERSION;
        $repair['findings'] = $findings;
        $repair['semantic_actionability_hardening'] = [
            'enabled' => true,
            'generic_plausibility_is_not_write_authority' => true,
            'direct_candidate_count' => $total_direct,
            'supporting_candidate_count' => $total_supporting,
            'weak_candidate_count' => $total_weak,
            'value_planning_ready_count' => $total_value_planning_ready,
            'automatic_write_allowed' => false,
            'first_auto_experiment_scope' => 'direct reversible spacing/typography controls only after separate bounded value planning',
        ];
        $repair['note'] = 'Repair discovery remains read-only. Candidate location and writability are not enough: semantic actionability is now graded separately. Only direct reversible spacing/typography targets may advance to bounded value planning; writes still require a fresh exact guarded read and a separate explicit experiment step.';
        $data['visual']['repair_discovery'] = $repair;
        $response->set_data( $data );
        return $response;
    }

    private static function assess( string $finding, array $candidate ): array {
        $type = strtolower( (string) ( $candidate['candidate_type'] ?? '' ) );
        $design_category = strtolower( (string) ( $candidate['category'] ?? '' ) );
        $kind = strtolower( (string) ( $candidate['kind'] ?? '' ) );
        $widget = strtolower( (string) ( $candidate['widget_type'] ?? '' ) );
        $key = strtolower( (string) ( $candidate['control_key'] ?? '' ) );
        $path = strtolower( implode( '.', array_map( 'strval', is_array( $candidate['setting_path'] ?? null ) ? $candidate['setting_path'] : [] ) ) );
        $text = trim( $widget . ' ' . $key . ' ' . $path );
        $button_like = (bool) preg_match( '/button|btn|cta|call[-_ ]?to[-_ ]?action/', $text );
        $heading_like = (bool) preg_match( '/heading|headline|title/', $text );
        $font_size_like = (bool) preg_match( '/font[_-]?size|line[_-]?height/', $text );
        $spacing_like = (bool) preg_match( '/padding|margin|gap|spacing|space[_-]?(?:before|after|between)/', $text );

        $base = [
            'actionability' => 'weak',
            'reason' => 'The target is writable and localized but does not directly express the visual change described by this finding.',
            'value_planning_ready' => false,
            'requires_reference_style' => false,
            'requires_replacement_asset' => false,
            'requires_verified_destination' => false,
            'write_block_reason' => 'No automatic write: this candidate is not a direct bounded repair target.',
        ];

        if ( self::contains_any( $finding, [ 'typograph', 'readab', 'legib' ] ) ) {
            if ( 'design_control' === $type && 'typography' === $design_category && $font_size_like ) {
                $base['actionability'] = 'direct';
                $base['reason'] = 'A font-size/line-height control directly addresses a localized typography/readability finding.';
                $base['value_planning_ready'] = true;
                $base['write_block_reason'] = 'No automatic write yet: a bounded new value must be planned and re-read immediately before an experiment.';
            } elseif ( 'design_control' === $type && 'typography' === $design_category ) {
                $base['actionability'] = 'supporting';
                $base['reason'] = 'This typography control may influence readability, but it is not a direct font-size/line-height lever.';
            }
            return $base;
        }

        if ( self::contains_any( $finding, [ 'whitespace', 'spacing', 'rhythm' ] ) ) {
            if ( 'design_control' === $type && 'spacing' === $design_category && $spacing_like ) {
                $base['actionability'] = 'direct';
                $base['reason'] = 'An explicit padding/margin/gap control directly addresses a localized spacing or whitespace finding.';
                $base['value_planning_ready'] = true;
                $base['write_block_reason'] = 'No automatic write yet: a bounded spacing delta must be planned and re-read immediately before an experiment.';
            }
            return $base;
        }

        if ( str_contains( $finding, 'hierarchy' ) ) {
            if ( 'design_control' === $type && 'typography' === $design_category && $heading_like && $font_size_like ) {
                $base['actionability'] = 'direct';
                $base['reason'] = 'A heading font-size/line-height control can directly strengthen hierarchy in the localized section.';
                $base['value_planning_ready'] = true;
                $base['write_block_reason'] = 'No automatic write yet: a bounded heading-scale change must be planned and freshly guarded.';
            } elseif ( 'design_control' === $type && in_array( $design_category, [ 'spacing', 'alignment', 'typography' ], true ) ) {
                $base['actionability'] = 'supporting';
                $base['reason'] = 'This control can influence hierarchy indirectly, but the current evidence does not show that its present value causes the hierarchy problem.';
            } elseif ( 'visual_setting' === $type && 'color' === $kind ) {
                $base['actionability'] = 'supporting';
                $base['reason'] = 'Color can influence hierarchy, but a target/reference relationship is required before changing it.';
                $base['requires_reference_style'] = true;
            }
            return $base;
        }

        if ( str_contains( $finding, 'cta' ) ) {
            if ( ! $button_like ) return $base;
            if ( 'visual_setting' === $type && 'color' === $kind ) {
                $base['actionability'] = 'direct';
                $base['reason'] = 'A button color is a direct CTA styling target, but consistency requires a verified reference CTA/style before a value can be chosen.';
                $base['requires_reference_style'] = true;
                $base['write_block_reason'] = 'No automatic write: CTA consistency needs a verified reference CTA/style token first.';
            } elseif ( 'design_control' === $type && in_array( $design_category, [ 'border_radius', 'typography' ], true ) ) {
                $base['actionability'] = 'supporting';
                $base['reason'] = 'This button control affects CTA styling, but consistency cannot be inferred from this isolated value alone.';
                $base['requires_reference_style'] = true;
            } elseif ( 'design_control' === $type && in_array( $design_category, [ 'spacing', 'alignment' ], true ) ) {
                $base['actionability'] = 'weak';
                $base['reason'] = 'Button spacing/alignment changes placement but do not directly resolve a CTA consistency/style finding.';
            }
            return $base;
        }

        if ( self::contains_any( $finding, [ 'imagery', 'image', 'media' ] ) ) {
            if ( 'visual_setting' === $type && 'media' === $kind ) {
                $base['actionability'] = 'direct';
                $base['reason'] = 'This is the exact current media target implicated by the localized imagery finding.';
                $base['requires_replacement_asset'] = true;
                $base['write_block_reason'] = 'No automatic write: a semantically relevant replacement image must be independently selected and verified first.';
            }
            return $base;
        }

        if ( self::contains_any( $finding, [ 'link', 'destination', 'navigation' ] ) ) {
            if ( 'link_target' === $type ) {
                $base['actionability'] = 'direct';
                $base['reason'] = 'This exact link target directly addresses a localized destination/navigation finding.';
                $base['requires_verified_destination'] = true;
                $base['write_block_reason'] = 'No automatic write: the destination must be freshly verified from an existing real page or user-supplied URL.';
            }
            return $base;
        }

        if ( self::contains_any( $finding, [ 'section', 'balance', 'flow' ] ) ) {
            if ( 'design_control' === $type && 'spacing' === $design_category ) {
                $base['actionability'] = 'supporting';
                $base['reason'] = 'Spacing may affect section balance/flow, but this broad finding does not identify the responsible control value.';
            }
            return $base;
        }

        if ( self::contains_any( $finding, [ 'cohesion', 'consisten' ] ) ) {
            if ( 'visual_setting' === $type && 'color' === $kind ) {
                $base['actionability'] = 'supporting';
                $base['reason'] = 'Color may affect cohesion/consistency, but a verified reference style is needed before changing it.';
                $base['requires_reference_style'] = true;
            }
            return $base;
        }

        return $base;
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
        $data['rendered_visual_repair_discovery_semantic_hardening'] = true;
        $data['rendered_visual_repair_discovery_actionability_levels'] = [ 'direct', 'supporting', 'weak' ];
        $data['rendered_visual_repair_discovery_generic_candidate_not_write_authority'] = true;
        $data['rendered_visual_repair_value_planning_direct_reversible_only'] = true;
        $data['rendered_visual_repair_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

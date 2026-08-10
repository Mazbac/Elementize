<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Focused_Section_Verification', false ) ) return;

final class Elementize_Section_Convergence_Backstop {
    private const VERSION = '0.24.3';
    private const NS = 'elementize/v1';
    private const MAX_PROBES = 2;

    public static function init(): void {
        // Evidence hardening is complete by 510. Seed a probe-only candidate before
        // the section crop pass at 515, then consume that result before planning at 520.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'before_section' ], 512, 3 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_section' ], 518, 3 );
    }

    public static function before_section( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( ! self::is_visual_audit_request( $request, $response ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $focused = is_array( $visual['focused_verification'] ?? null ) ? $visual['focused_verification'] : [];
        $metrics = is_array( $visual['render_metrics'] ?? null ) ? $visual['render_metrics'] : [];

        $diag = [
            'version' => self::VERSION,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'probe_only' => true,
            'probe_requested_count' => 0,
            'probe_target_ids' => [],
            'action_slot_cost' => 0,
        ];

        if ( empty( $conv['available'] ) || empty( $focused['available'] ) || empty( $metrics['available'] ) ) {
            $diag['reason'] = 'Fresh convergence, focused verification, and rendered metrics are required.';
            $visual['section_convergence_backstop'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $candidates = is_array( $focused['candidates'] ?? null ) ? $focused['candidates'] : [];
        $assessments = is_array( $focused['assessments'] ?? null ) ? $focused['assessments'] : [];
        $requested = 0;

        foreach ( is_array( $conv['blocked_targets'] ?? null ) ? $conv['blocked_targets'] : [] as $blocked ) {
            if ( $requested >= self::MAX_PROBES ) break;
            if ( ! self::probe_eligible( $blocked ) ) continue;

            $top = trim( (string) ( $blocked['top_level_element_id'] ?? '' ) );
            $marker = self::marker_for_top( $metrics, $top );
            if ( '' === $top || '' === $marker ) continue;

            $target_id = trim( (string) ( $blocked['target_id'] ?? '' ) );
            if ( '' === $target_id ) continue;

            // This candidate/assessment exists only to trigger the independent section
            // crop verifier. It is removed again at priority 518 and never grants support.
            $candidates[] = [
                'target_id' => $target_id,
                'observation_type' => 'large_internal_padding_present',
                'marker' => $marker,
                'top_level_element_id' => $top,
                'element_id' => (string) ( $blocked['element_id'] ?? $top ),
                'rendered_property' => 'padding',
                'matched_component' => strtolower( (string) ( $blocked['matched_component'] ?? '' ) ),
                'rendered_value' => $blocked['rendered_value'] ?? null,
                'localized_section_categories' => is_array( $blocked['localized_section_categories'] ?? null ) ? $blocked['localized_section_categories'] : [],
                'sample_text' => '',
                'section_probe_only' => true,
                'automatic_write_allowed' => false,
            ];
            $assessments[] = [
                'target_id' => $target_id,
                // These trigger values are intentionally synthetic and are removed after
                // the crop pass. They are not evidence and are never copied to convergence.
                'verdict' => 'visibly_problematic',
                'confidence' => 'medium',
                'direction' => 'reduce',
                'evidence' => 'Probe trigger only; no defect is asserted by this synthetic assessment.',
                'assessment_consistent' => true,
                'focused_visual_support' => false,
                'eligible_for_hardened_convergence_review' => false,
                'automatic_write_allowed' => false,
                'evidence_visual_grounded' => false,
                'evidence_grounding_reason' => 'section_crop_probe_requested',
                'section_probe_only' => true,
            ];

            $diag['probe_target_ids'][] = $target_id;
            $requested++;
        }

        $diag['probe_requested_count'] = $requested;
        $diag['note'] = $requested
            ? 'Exact blocked spacing targets were queued only for an independent section-crop backstop. Synthetic trigger records are removed before the final audit response and grant no repair authority.'
            : 'No exact blocked spacing target required a section-crop backstop probe.';

        if ( $requested > 0 ) {
            $focused['candidates'] = $candidates;
            $focused['assessments'] = $assessments;
            $focused['candidate_count'] = count( $candidates );
        }
        $visual['focused_verification'] = $focused;
        $visual['section_convergence_backstop'] = $diag;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    public static function after_section( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( ! self::is_visual_audit_request( $request, $response ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $focused = is_array( $visual['focused_verification'] ?? null ) ? $visual['focused_verification'] : [];
        $section = is_array( $visual['focused_section_verification'] ?? null ) ? $visual['focused_section_verification'] : [];
        $diag = is_array( $visual['section_convergence_backstop'] ?? null ) ? $visual['section_convergence_backstop'] : [];

        $probe_ids = [];
        foreach ( is_array( $focused['candidates'] ?? null ) ? $focused['candidates'] : [] as $candidate ) {
            if ( is_array( $candidate ) && ! empty( $candidate['section_probe_only'] ) ) {
                $tid = trim( (string) ( $candidate['target_id'] ?? '' ) );
                if ( '' !== $tid ) $probe_ids[$tid] = true;
            }
        }

        $promoted_by_crop = [];
        $errors = [];
        if ( $probe_ids && ! empty( $section['available'] ) ) {
            $id = self::page_id_from_route( $request->get_route() );
            foreach ( is_array( $section['results'] ?? null ) ? $section['results'] : [] as $result ) {
                if ( ! is_array( $result ) || empty( $result['support'] ) ) continue;
                $tid = trim( (string) ( $result['target_id'] ?? '' ) );
                if ( '' === $tid || empty( $probe_ids[$tid] ) ) continue;
                $blocked = self::find_target( is_array( $conv['blocked_targets'] ?? null ) ? $conv['blocked_targets'] : [], $tid );
                if ( ! $blocked ) continue;
                $rebuilt = self::rebuild_target( $id, $blocked, $conv, $result );
                if ( is_wp_error( $rebuilt ) ) {
                    $errors[] = [ 'target_id' => $tid, 'reason' => self::safe_error( $rebuilt, 'Could not refresh the exact guarded control.' ) ];
                    continue;
                }
                $promoted_by_crop[$tid] = $rebuilt;
            }
        }

        if ( $promoted_by_crop ) {
            $promoted = is_array( $conv['promoted_targets'] ?? null ) ? $conv['promoted_targets'] : [];
            $existing = [];
            foreach ( $promoted as $target ) if ( is_array( $target ) ) $existing[(string) ( $target['target_id'] ?? '' )] = true;
            foreach ( $promoted_by_crop as $tid => $target ) {
                if ( empty( $existing[$tid] ) ) $promoted[] = $target;
            }
            $blocked_remaining = [];
            foreach ( is_array( $conv['blocked_targets'] ?? null ) ? $conv['blocked_targets'] : [] as $blocked ) {
                $tid = is_array( $blocked ) ? (string) ( $blocked['target_id'] ?? '' ) : '';
                if ( '' !== $tid && isset( $promoted_by_crop[$tid] ) ) continue;
                $blocked_remaining[] = $blocked;
            }
            $conv['promoted_targets'] = $promoted;
            $conv['blocked_targets'] = $blocked_remaining;
            $conv['promoted_target_count'] = count( $promoted );
            $conv['demoted_target_count'] = count( $blocked_remaining );
            $conv['section_crop_backstop_version'] = self::VERSION;
            $conv['section_crop_promoted_target_count'] = count( $promoted_by_crop );
            $conv['automatic_write_allowed'] = false;
            $conv['defects_asserted'] = false;
            $conv['note'] = 'Final convergence may use direct consistency-hardened localization or an independent section-crop backstop with explicit spatial causality. Both paths still require deterministic rendered measurement and a fresh exact guarded control; no write authority is granted.';
            $visual['repair_convergence'] = $conv;
        }

        // Remove every synthetic trigger record so the final focused-verification object
        // never presents the bridge's placeholder verdict as model evidence.
        if ( isset( $focused['candidates'] ) && is_array( $focused['candidates'] ) ) {
            $focused['candidates'] = array_values( array_filter( $focused['candidates'], static function ( $candidate ): bool {
                return ! is_array( $candidate ) || empty( $candidate['section_probe_only'] );
            } ) );
            $focused['candidate_count'] = count( $focused['candidates'] );
        }
        if ( isset( $focused['assessments'] ) && is_array( $focused['assessments'] ) ) {
            $focused['assessments'] = array_values( array_filter( $focused['assessments'], static function ( $assessment ): bool {
                return ! is_array( $assessment ) || empty( $assessment['section_probe_only'] );
            } ) );
            $focused['hardened_convergence_review_count'] = count( array_filter( $focused['assessments'], static function ( $a ): bool {
                return is_array( $a ) && ! empty( $a['eligible_for_hardened_convergence_review'] );
            } ) );
            $focused['grounded_visibly_problematic_count'] = count( array_filter( $focused['assessments'], static function ( $a ): bool {
                return is_array( $a ) && 'visibly_problematic' === (string) ( $a['verdict'] ?? '' ) && ! empty( $a['evidence_visual_grounded'] );
            } ) );
        }
        $visual['focused_verification'] = $focused;

        $diag['version'] = self::VERSION;
        $diag['read_only'] = true;
        $diag['writes_performed'] = false;
        $diag['automatic_write_allowed'] = false;
        $diag['section_crop_support_count'] = count( $promoted_by_crop );
        $diag['section_crop_support_target_ids'] = array_values( array_keys( $promoted_by_crop ) );
        $diag['refresh_errors'] = $errors;
        $diag['synthetic_probe_records_removed'] = true;
        $diag['note'] = $promoted_by_crop
            ? 'An unresolved exact spacing target gained direct screenshot-grounded section-crop support and was restored to read-only convergence after a fresh exact control read. No write authority was granted.'
            : 'No unresolved exact spacing target gained sufficient section-crop support in this audit. No edit may be inferred from the probe.';
        $visual['section_convergence_backstop'] = $diag;

        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function probe_eligible( $blocked ): bool {
        if ( ! is_array( $blocked ) ) return false;
        if ( empty( $blocked['exact_control_match'] ) || empty( $blocked['global_visual_convergence'] ) ) return false;
        if ( 'large_internal_padding_present' !== (string) ( $blocked['observation_type'] ?? '' ) ) return false;
        if ( 'padding' !== (string) ( $blocked['rendered_property'] ?? '' ) ) return false;
        $side = strtolower( trim( (string) ( $blocked['matched_component'] ?? '' ) ) );
        if ( ! in_array( $side, [ 'top', 'bottom' ], true ) ) return false;
        $rendered = $blocked['rendered_value'] ?? null;
        if ( ! is_numeric( $rendered ) || (float) $rendered < 60.0 ) return false;
        $reason = (string) ( $blocked['reason_code'] ?? '' );
        return in_array( $reason, [ 'section_localized_but_semantic_category_indirect', 'no_section_specific_visual_support' ], true );
    }

    private static function marker_for_top( array $metrics, string $top ): string {
        foreach ( is_array( $metrics['sections'] ?? null ) ? $metrics['sections'] : [] as $section ) {
            if ( is_array( $section ) && (string) ( $section['top_level_element_id'] ?? '' ) === $top ) return trim( (string) ( $section['marker'] ?? '' ) );
        }
        return '';
    }

    private static function find_target( array $targets, string $target_id ): array {
        foreach ( $targets as $target ) {
            if ( is_array( $target ) && (string) ( $target['target_id'] ?? '' ) === $target_id ) return $target;
        }
        return [];
    }

    private static function rebuild_target( int $id, array $blocked, array $conv, array $result ) {
        if ( $id < 1 ) return new WP_Error( 'elementize_backstop_page_invalid', 'A valid page ID is required.' );
        $element = trim( (string) ( $blocked['element_id'] ?? '' ) );
        $side = strtolower( trim( (string) ( $blocked['matched_component'] ?? '' ) ) );
        $rendered = is_numeric( $blocked['rendered_value'] ?? null ) ? (float) $blocked['rendered_value'] : null;
        if ( '' === $element || ! in_array( $side, [ 'top', 'bottom' ], true ) || null === $rendered ) {
            return new WP_Error( 'elementize_backstop_target_invalid', 'The blocked target does not contain a usable exact spacing identity.' );
        }

        $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/design-settings' );
        foreach ( [ 'id' => $id, 'category' => 'spacing', 'element_id' => $element, 'responsive_scope' => 'base', 'offset' => 0, 'limit' => 300 ] as $key => $value ) $request->set_param( $key, $value );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_backstop_design_read_failed', 'Fresh exact design settings could not be read.' );
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return new WP_Error( 'elementize_backstop_design_read_invalid', 'Fresh exact design settings returned invalid data.' );

        $source_hash = trim( (string) ( $conv['page_content_hash'] ?? '' ) );
        $read_hash = trim( (string) ( $data['content_hash'] ?? '' ) );
        if ( '' === $source_hash || '' === $read_hash || ! hash_equals( $source_hash, $read_hash ) ) {
            return new WP_Error( 'elementize_backstop_stale_hash', 'The page changed while the section-crop backstop refreshed its exact control.' );
        }

        $match = null;
        foreach ( is_array( $data['controls'] ?? null ) ? $data['controls'] : [] as $control ) {
            if ( ! is_array( $control ) ) continue;
            if ( (string) ( $control['element_id'] ?? '' ) !== $element ) continue;
            if ( 'spacing' !== strtolower( (string) ( $control['category'] ?? '' ) ) ) continue;
            if ( 'base' !== strtolower( (string) ( $control['responsive_scope'] ?? 'base' ) ) ) continue;
            if ( empty( $control['writable_now'] ) || '' === trim( (string) ( $control['control_fingerprint'] ?? '' ) ) ) continue;
            $key = strtolower( (string) ( $control['control_key'] ?? '' ) );
            $path = strtolower( implode( '.', array_map( 'strval', is_array( $control['setting_path'] ?? null ) ? $control['setting_path'] : [] ) ) );
            if ( ! str_contains( $key . ' ' . $path, 'padding' ) ) continue;
            $value = $control['value'] ?? null;
            if ( ! is_array( $value ) || 'px' !== strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) || ! is_numeric( $value[$side] ?? null ) ) continue;
            if ( abs( (float) $value[$side] - $rendered ) > 1.0 ) continue;
            $match = $control;
            break;
        }
        if ( ! is_array( $match ) ) return new WP_Error( 'elementize_backstop_exact_control_missing', 'The exact writable padding control no longer matches the rendered target.' );

        $confidence = strtolower( trim( (string) ( $result['confidence'] ?? '' ) ) );
        if ( ! in_array( $confidence, [ 'medium', 'high' ], true ) ) $confidence = 'medium';
        return [
            'target_id' => (string) ( $blocked['target_id'] ?? '' ),
            'promotion_status' => 'promoted_read_only_section_verified',
            'observation_type' => 'large_internal_padding_present',
            'top_level_element_id' => (string) ( $blocked['top_level_element_id'] ?? '' ),
            'element_id' => (string) ( $match['element_id'] ?? '' ),
            'widget_type' => $match['widget_type'] ?? null,
            'category' => 'spacing',
            'control_key' => (string) ( $match['control_key'] ?? '' ),
            'setting_path' => is_array( $match['setting_path'] ?? null ) ? array_values( $match['setting_path'] ) : [],
            'responsive_scope' => (string) ( $match['responsive_scope'] ?? 'base' ),
            'current_value' => $match['value'] ?? null,
            'control_fingerprint' => (string) ( $match['control_fingerprint'] ?? '' ),
            'writer_route' => 'POST /' . self::NS . '/pages/{id}/design-settings',
            'rendered_property' => 'padding',
            'rendered_value' => $rendered,
            'matched_component' => $side,
            'visual_convergence' => true,
            'exact_control_match' => true,
            'section_specific_visual_support' => true,
            'semantic_localized_support' => true,
            'localized_support' => [
                'finding_index' => 0,
                'top_level_element_id' => (string) ( $blocked['top_level_element_id'] ?? '' ),
                'finding_category' => 'whitespace',
                'confidence' => $confidence,
                'localization_status' => 'section_crop_verified',
                'source' => 'focused_section_verification',
                'evidence' => self::clip( (string) ( $result['evidence'] ?? '' ), 300 ),
            ],
            'global_visual_convergence_alone_was_not_used' => true,
            'section_crop_backstop_used' => true,
            'section_crop_grounding_reason' => (string) ( $result['grounding_reason'] ?? '' ),
            'eligible_for_bounded_planning_review' => true,
            'defect_asserted' => false,
            'automatic_write_allowed' => false,
            'fresh_exact_read_required_before_any_write' => true,
            'promotion_basis' => 'Deterministic rendered spacing measurement + exact same-property guarded control + independent section-crop visual verification with explicit spatial causality.',
        ];
    }

    private static function is_visual_audit_request( WP_REST_Request $request, WP_REST_Response $response ): bool {
        if ( 'GET' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return false;
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/\d+/completion-audit$#', $request->get_route() ) ) return false;
        return self::truthy( $request->get_param( 'include_visual' ) );
    }

    private static function page_id_from_route( string $route ): int {
        return preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ? absint( $m[1] ?? 0 ) : 0;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function clip( string $text, int $limit ): string {
        $text = trim( preg_replace( '/\s+/', ' ', $text ) ?? $text );
        if ( function_exists( 'mb_substr' ) ) return mb_substr( $text, 0, $limit );
        return substr( $text, 0, $limit );
    }

    private static function safe_error( $error, string $fallback ): string {
        if ( is_wp_error( $error ) ) {
            $message = trim( (string) $error->get_error_message() );
            if ( '' !== $message ) return self::clip( $message, 400 );
        }
        return $fallback;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_section_convergence_backstop'] = true;
        $data['rendered_visual_section_convergence_backstop_read_only'] = true;
        $data['rendered_visual_section_convergence_backstop_probe_only'] = true;
        $data['rendered_visual_section_convergence_backstop_max_probes'] = self::MAX_PROBES;
        $data['rendered_visual_section_convergence_backstop_automatic_write_allowed'] = false;
        $data['rendered_visual_section_convergence_backstop_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

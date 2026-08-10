<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Brain', false ) ) return;

/**
 * Deterministic semantic grounding for Aesthetic Brain output.
 *
 * Local vision is allowed to judge appearance, but it must not invent page
 * semantics (for example a contact form or CTA label that is not actually in
 * the Elementor section). This pass cross-checks model claims against exact
 * top-level Elementor content and widget capabilities. It is read-only.
 */
final class Elementize_Aesthetic_Semantic_Grounding {
    private const VERSION = '0.26.3-g1';
    private const NS = 'elementize/v1';
    private const MAX_MARKERS = 24;
    private const MAX_TEXT_SAMPLES = 24;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 550, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) ) return $response;
        $visual = $data['visual'];
        $recovery = is_array( $visual['aesthetic_coverage_recovery'] ?? null ) ? $visual['aesthetic_coverage_recovery'] : [];
        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];

        if ( empty( $recovery['available'] ) || empty( $recovery['exact_section_coverage_complete'] ) ) return $response;

        $id = absint( $m[1] ?? 0 );
        $facts = self::section_facts( $id );
        $diag = [
            'version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'deterministic_elementor_grounding' => true,
            'section_fact_count' => count( $facts ),
            'marker_count' => max( 0, absint( $recovery['marker_count'] ?? 0 ) ),
            'semantic_conflict_count' => 0,
            'material_semantic_conflict_count' => 0,
            'transition_contract_valid' => false,
            'approval_grounding_complete' => false,
            'grounded_issue_count' => 0,
            'grounded_high_severity_issue_count' => 0,
            'grounded_medium_severity_issue_count' => 0,
            'action_slot_cost' => 0,
        ];

        if ( $id < 1 || ! $facts ) {
            $diag['reason'] = 'Exact top-level Elementor section facts are unavailable.';
            $visual['aesthetic_semantic_grounding'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $ledger = is_array( $recovery['section_ledger'] ?? null ) ? $recovery['section_ledger'] : [];
        $grounded_rows = [];
        $row_by_marker = [];
        $transition_valid = count( $ledger ) === count( $facts );
        foreach ( $ledger as $index => $row ) {
            if ( ! is_array( $row ) ) continue;
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            if ( ! isset( $facts[$marker] ) ) continue;
            $claims = self::semantic_conflicts( $row, $facts[$marker] );
            $transition = strtolower( trim( (string) ( $row['transition_from_previous'] ?? '' ) ) );
            $transition_ok = 0 === $index ? 'n/a' === $transition : 'n/a' !== $transition && '' !== $transition;
            if ( ! $transition_ok ) $transition_valid = false;

            $copy = $row;
            $copy['semantic_grounded'] = empty( $claims );
            $copy['semantic_conflicts'] = $claims;
            $copy['transition_contract_valid'] = $transition_ok;
            $copy['section_fact_summary'] = self::public_fact_summary( $facts[$marker] );
            $grounded_rows[] = $copy;
            $row_by_marker[$marker] = $copy;
            $diag['semantic_conflict_count'] += count( $claims );
        }

        $raw_issues = is_array( $art['calibrated_issues'] ?? null ) ? $art['calibrated_issues'] : ( is_array( $recovery['issues'] ?? null ) ? $recovery['issues'] : [] );
        $grounded_issues = [];
        $material_conflicts = [];
        $grounded_high = 0;
        $grounded_medium = 0;
        foreach ( $raw_issues as $issue ) {
            if ( ! is_array( $issue ) ) continue;
            $markers = is_array( $issue['section_markers'] ?? null ) ? $issue['section_markers'] : [];
            $marker = $markers ? strtoupper( trim( (string) reset( $markers ) ) ) : '';
            $fact = isset( $facts[$marker] ) ? $facts[$marker] : null;
            $claims = is_array( $fact ) ? self::semantic_conflicts( $issue, $fact ) : [ 'No exact Elementor section fact matched this issue marker.' ];
            if ( isset( $row_by_marker[$marker] ) && ! empty( $row_by_marker[$marker]['semantic_conflicts'] ) ) {
                $claims = array_values( array_unique( array_merge( $claims, $row_by_marker[$marker]['semantic_conflicts'] ) ) );
            }
            $severity = strtolower( trim( (string) ( $issue['severity'] ?? 'low' ) ) );
            $usable = empty( $claims );
            $copy = $issue;
            $copy['semantic_grounded'] = $usable;
            $copy['semantic_conflicts'] = $claims;
            $copy['evidence_usable'] = $usable;
            if ( ! $usable ) {
                $copy['grounded_intervention_level'] = 'none';
                $copy['grounding_note'] = 'This issue is preserved as raw model output but cannot drive professional approval or repair planning because its semantic claim conflicts with exact Elementor content.';
                if ( in_array( $severity, [ 'medium', 'high' ], true ) ) {
                    $material_conflicts[] = [ 'marker' => $marker, 'severity' => $severity, 'category' => (string) ( $issue['category'] ?? 'other' ), 'conflicts' => $claims ];
                }
            } else {
                $copy['grounded_intervention_level'] = (string) ( $issue['calibrated_intervention_level'] ?? $issue['intervention_level'] ?? 'none' );
                $copy['grounding_note'] = 'Issue semantics are consistent with exact Elementor section content.';
                if ( 'high' === $severity ) $grounded_high++;
                if ( 'medium' === $severity ) $grounded_medium++;
            }
            $copy['automatic_write_allowed'] = false;
            $grounded_issues[] = $copy;
        }

        $effective_fits = [];
        foreach ( $grounded_rows as $row ) {
            $marker = (string) ( $row['marker'] ?? '' );
            $raw_fit = (string) ( $row['coherence_fit'] ?? 'mixed' );
            $severity = 'none';
            foreach ( $grounded_issues as $issue ) {
                if ( ! is_array( $issue ) || empty( $issue['evidence_usable'] ) ) continue;
                if ( ! in_array( $marker, is_array( $issue['section_markers'] ?? null ) ? $issue['section_markers'] : [], true ) ) continue;
                $candidate = strtolower( trim( (string) ( $issue['severity'] ?? 'none' ) ) );
                if ( 'high' === $candidate ) { $severity = 'high'; break; }
                if ( 'medium' === $candidate ) $severity = 'medium';
                elseif ( 'low' === $candidate && 'none' === $severity ) $severity = 'low';
            }
            $effective_fit = $raw_fit;
            if ( 'high' === $severity ) $effective_fit = 'violates';
            elseif ( 'medium' === $severity && 'fits' === $effective_fit ) $effective_fit = 'mixed';
            $effective_fits[] = [ 'marker' => $marker, 'model_coherence_fit' => $raw_fit, 'effective_coherence_fit' => $effective_fit, 'grounded_severity' => $severity ];
        }

        $facts_complete = count( $facts ) === max( 0, absint( $recovery['marker_count'] ?? count( $facts ) ) );
        $approval_grounding_complete = $facts_complete && empty( $material_conflicts );
        $grounded_approval = $approval_grounding_complete ? (string) ( $art['effective_professional_approval'] ?? 'unavailable' ) : 'unavailable';

        $diag['available'] = $facts_complete;
        $diag['section_fact_count'] = count( $facts );
        $diag['semantic_conflict_count'] = array_sum( array_map( static fn ( array $row ): int => count( $row['semantic_conflicts'] ?? [] ), $grounded_rows ) );
        $diag['material_semantic_conflict_count'] = count( $material_conflicts );
        $diag['material_semantic_conflicts'] = array_slice( $material_conflicts, 0, 8 );
        $diag['transition_contract_valid'] = $transition_valid;
        $diag['rhythm_judgment_usable'] = $transition_valid;
        $diag['approval_grounding_complete'] = $approval_grounding_complete;
        $diag['grounded_effective_professional_approval'] = $grounded_approval;
        $diag['grounded_issue_count'] = count( array_filter( $grounded_issues, static fn ( array $issue ): bool => ! empty( $issue['evidence_usable'] ) ) );
        $diag['grounded_high_severity_issue_count'] = $grounded_high;
        $diag['grounded_medium_severity_issue_count'] = $grounded_medium;
        $diag['section_grounding'] = $grounded_rows;
        $diag['grounded_issues'] = $grounded_issues;
        $diag['effective_section_fits'] = $effective_fits;
        $diag['note'] = 'Visual taste remains model-based, but semantic claims are usable only when they agree with exact Elementor section text/widget facts. The first section alone may use transition_from_previous=n/a.';

        $art['semantic_grounding_version'] = self::VERSION;
        $art['grounded_effective_professional_approval'] = $grounded_approval;
        $art['approval_grounding_complete'] = $approval_grounding_complete;
        $art['approval_usable_before_semantic_grounding'] = ! empty( $art['approval_usable'] );
        $art['approval_usable'] = ! empty( $art['approval_usable'] ) && $approval_grounding_complete;
        $art['grounded_issue_count'] = $diag['grounded_issue_count'];
        $art['grounded_high_severity_issue_count'] = $grounded_high;
        $art['grounded_medium_severity_issue_count'] = $grounded_medium;
        $art['grounded_issues'] = $grounded_issues;
        $art['automatic_write_allowed'] = false;

        $coherence['semantic_grounding_version'] = self::VERSION;
        $coherence['transition_contract_valid'] = $transition_valid;
        $coherence['rhythm_judgment_usable'] = $transition_valid;
        $coherence['effective_section_fits'] = $effective_fits;
        $coherence['automatic_write_allowed'] = false;

        $visual['page_art_direction'] = $art;
        $visual['section_coherence'] = $coherence;
        $visual['aesthetic_semantic_grounding'] = $diag;
        $data['visual'] = $visual;

        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_semantic_grounding_available'] = $diag['available'];
        $data['summary']['aesthetic_approval_grounding_complete'] = $approval_grounding_complete;
        $data['summary']['grounded_professional_visual_approval'] = $grounded_approval;
        $data['summary']['professional_visual_approval_usable'] = ! empty( $art['approval_usable'] );
        $data['summary']['aesthetic_transition_contract_valid'] = $transition_valid;
        $response->set_data( $data );
        return $response;
    }

    private static function section_facts( int $id ): array {
        $elements = null;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $doc = \Elementor\Plugin::$instance->documents->get( $id );
                $elements = $doc ? $doc->get_elements_data() : null;
            } catch ( Throwable $ignored ) {}
        }
        if ( ! is_array( $elements ) ) {
            $raw = get_post_meta( $id, '_elementor_data', true );
            $elements = is_string( $raw ) ? json_decode( $raw, true ) : null;
        }
        if ( ! is_array( $elements ) ) return [];

        $facts = [];
        $n = 0;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $eid = trim( (string) ( $element['id'] ?? '' ) );
            if ( '' === $eid || ! preg_match( '/^[A-Za-z0-9_-]+$/', $eid ) ) continue;
            $n++;
            if ( $n > self::MAX_MARKERS ) break;
            $fact = [ 'marker' => 'S' . $n, 'top_level_element_id' => $eid, 'widget_types' => [], 'text_samples' => [], 'has_form' => false, 'has_button' => false, 'has_image' => false ];
            self::walk_element( $element, $fact );
            $fact['widget_types'] = array_values( array_unique( $fact['widget_types'] ) );
            $fact['text_samples'] = array_slice( array_values( array_unique( $fact['text_samples'] ) ), 0, self::MAX_TEXT_SAMPLES );
            $fact['_text_blob'] = self::normalize( implode( ' ', $fact['text_samples'] ) );
            $facts[$fact['marker']] = $fact;
        }
        return $facts;
    }

    private static function walk_element( array $element, array &$fact ): void {
        $widget = strtolower( trim( (string) ( $element['widgetType'] ?? $element['elType'] ?? '' ) ) );
        if ( '' !== $widget ) {
            $fact['widget_types'][] = $widget;
            if ( preg_match( '/(?:^|[-_])(form|contact|wpforms|forminator|gravity|ninja)(?:$|[-_])/', $widget ) || str_contains( $widget, 'form' ) ) $fact['has_form'] = true;
            if ( str_contains( $widget, 'button' ) || str_contains( $widget, 'cta' ) ) $fact['has_button'] = true;
            if ( str_contains( $widget, 'image' ) || str_contains( $widget, 'gallery' ) || str_contains( $widget, 'video' ) ) $fact['has_image'] = true;
        }
        if ( is_array( $element['settings'] ?? null ) ) self::collect_settings( $element['settings'], $fact );
        foreach ( is_array( $element['elements'] ?? null ) ? $element['elements'] : [] as $child ) if ( is_array( $child ) ) self::walk_element( $child, $fact );
    }

    private static function collect_settings( array $settings, array &$fact, string $parent_key = '' ): void {
        foreach ( $settings as $key => $value ) {
            $name = strtolower( trim( $parent_key . ' ' . (string) $key ) );
            if ( is_array( $value ) ) {
                if ( isset( $value['url'] ) && is_string( $value['url'] ) && '' !== trim( $value['url'] ) ) $fact['has_image'] = true;
                self::collect_settings( $value, $fact, $name );
                continue;
            }
            if ( ! is_string( $value ) ) continue;
            $raw = trim( $value );
            if ( '' === $raw ) continue;
            if ( str_contains( strtolower( $raw ), '[contact-form' ) || str_contains( strtolower( $raw ), '[wpforms' ) || str_contains( strtolower( $raw ), '[gravityform' ) ) $fact['has_form'] = true;
            if ( preg_match( '/button|cta/', $name ) ) $fact['has_button'] = true;
            if ( preg_match( '/image|background|media/', $name ) && preg_match( '#https?://|\.(?:png|jpe?g|webp|gif|svg)(?:\?|$)#i', $raw ) ) $fact['has_image'] = true;
            if ( ! preg_match( '/title|text|editor|description|content|label|button|quote|name|heading|subtitle|caption|placeholder/', $name ) ) continue;
            if ( preg_match( '#^https?://#i', $raw ) || preg_match( '/^#[0-9a-f]{3,8}$/i', $raw ) ) continue;
            $text = trim( wp_strip_all_tags( html_entity_decode( $raw, ENT_QUOTES | ENT_HTML5, 'UTF-8' ) ) );
            $text = preg_replace( '/\s+/u', ' ', $text );
            if ( ! is_string( $text ) || mb_strlen( $text ) < 2 ) continue;
            $fact['text_samples'][] = self::clip( $text, 220 );
        }
    }

    private static function semantic_conflicts( array $model, array $fact ): array {
        $role = (string) ( $model['role'] ?? '' );
        $evidence = (string) ( $model['evidence'] ?? '' );
        $text = self::normalize( $role . ' ' . $evidence );
        $conflicts = [];

        if ( preg_match( '/\b(contact form|form fields?|lead capture form|form design)\b/i', $text ) && empty( $fact['has_form'] ) ) {
            $conflicts[] = 'Model describes a form, but the exact Elementor section contains no detected form widget or form shortcode.';
        }
        if ( preg_match( '/\b(call[- ]to[- ]action button|cta button|button)\b/i', $text ) && empty( $fact['has_button'] ) ) {
            $quoted = self::quoted_visible_claims( $role . ' ' . $evidence );
            if ( $quoted ) $conflicts[] = 'Model describes a visible button/CTA, but the exact Elementor section contains no detected button/CTA widget.';
        }

        foreach ( self::quoted_visible_claims( $role . ' ' . $evidence ) as $claim ) {
            $needle = self::normalize( $claim );
            if ( '' === $needle || mb_strlen( $needle ) < 4 ) continue;
            if ( false === strpos( (string) ( $fact['_text_blob'] ?? '' ), $needle ) ) {
                $conflicts[] = 'Quoted visible text is not present in the exact Elementor section: "' . self::clip( $claim, 80 ) . '".';
            }
        }
        return array_values( array_unique( $conflicts ) );
    }

    private static function quoted_visible_claims( string $text ): array {
        $claims = [];
        if ( preg_match_all( '/[\x{2018}\x{2019}\x{201C}\x{201D}\'\"]([^\x{2018}\x{2019}\x{201C}\x{201D}\'\"]{4,80})[\x{2018}\x{2019}\x{201C}\x{201D}\'\"]/u', $text, $matches ) ) {
            foreach ( $matches[1] as $claim ) {
                $claim = trim( (string) $claim );
                if ( '' !== $claim && ! in_array( $claim, $claims, true ) ) $claims[] = $claim;
            }
        }
        return array_slice( $claims, 0, 6 );
    }

    private static function public_fact_summary( array $fact ): array {
        return [
            'top_level_element_id' => (string) ( $fact['top_level_element_id'] ?? '' ),
            'widget_types' => array_slice( is_array( $fact['widget_types'] ?? null ) ? $fact['widget_types'] : [], 0, 16 ),
            'text_sample_count' => count( is_array( $fact['text_samples'] ?? null ) ? $fact['text_samples'] : [] ),
            'has_form' => ! empty( $fact['has_form'] ),
            'has_button' => ! empty( $fact['has_button'] ),
            'has_image' => ! empty( $fact['has_image'] ),
        ];
    }

    private static function normalize( string $value ): string {
        $value = html_entity_decode( wp_strip_all_tags( $value ), ENT_QUOTES | ENT_HTML5, 'UTF-8' );
        $value = function_exists( 'mb_strtolower' ) ? mb_strtolower( $value ) : strtolower( $value );
        $value = preg_replace( '/[^\p{L}\p{N}]+/u', ' ', $value );
        return trim( is_string( $value ) ? preg_replace( '/\s+/u', ' ', $value ) : '' );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_semantic_grounding'] = true;
        $data['aesthetic_semantic_grounding_version'] = self::VERSION;
        $data['aesthetic_semantic_grounding_read_only'] = true;
        $data['aesthetic_semantic_grounding_automatic_writes'] = false;
        $data['aesthetic_semantic_grounding_transition_contract'] = true;
        $response->set_data( $data );
        return $response;
    }
}

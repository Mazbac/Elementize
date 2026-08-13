<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only senior-designer context for the active Creative Control page.
 *
 * This module does not make aesthetic decisions on the server. It packages
 * fresh page structure, design language, capability state and explicit
 * blueprint/QA contracts so ChatGPT can reason page-wide before mutating.
 */
final class Elementize_Designer {
    private const VERSION = '1';
    private const MAX_SECTIONS = 80;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 28 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/designer-context', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'read' ],
            'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function read( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $state = Elementize_Capabilities::state();
        $profile = Elementize_Design::profile_for_elements( $loaded['elements'], '', 600 );
        $sections = self::section_outline( $loaded['elements'] );
        return new WP_REST_Response( [
            'designer_context_version' => self::VERSION,
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'editing_profile' => $state['profile'],
            'creative_control_enabled' => $state['creative_enabled'],
            'creative_scope_page_id' => $state['scope_page_id'],
            'capability_revision' => $state['revision'],
            'read_only' => true,
            'page_outline' => [
                'top_level_section_count' => count( $sections ),
                'sections' => $sections,
            ],
            'design_language' => [
                'palette' => $profile['palette'] ?? [],
                'spacing_scale' => $profile['spacing_scale'] ?? [],
                'radius_scale' => $profile['radius_scale'] ?? [],
                'typography_tokens' => $profile['typography_tokens'] ?? [],
                'normalization_candidates' => $profile['normalization_candidates'] ?? [],
            ],
            'responsive_control_summary' => self::responsive_summary( (array) ( $profile['controls'] ?? [] ) ),
            'blueprint_contract' => self::blueprint_contract(),
            'reference_analysis_contract' => self::reference_contract(),
            'quality_contract' => self::quality_contract(),
        ], 200 );
    }

    private static function section_outline( array $elements ): array {
        $out = [];
        foreach ( array_slice( $elements, 0, self::MAX_SECTIONS ) as $index => $element ) {
            if ( ! is_array( $element ) ) continue;
            $out[] = self::section_summary( $element, $index );
        }
        return $out;
    }

    private static function section_summary( array $element, int $index ): array {
        $counts = [ 'containers' => 0, 'widgets' => 0, 'depth' => 0 ];
        $widget_types = [];
        $snippets = [];
        self::walk_summary( $element, 0, $counts, $widget_types, $snippets );
        arsort( $widget_types );
        return [
            'index' => $index,
            'element_id' => (string) ( $element['id'] ?? '' ),
            'element_type' => (string) ( $element['elType'] ?? '' ),
            'widget_type' => (string) ( $element['widgetType'] ?? '' ),
            'container_count' => $counts['containers'],
            'widget_count' => $counts['widgets'],
            'max_depth' => $counts['depth'],
            'dominant_widget_types' => array_slice( array_keys( $widget_types ), 0, 8 ),
            'text_snippets' => array_slice( array_values( array_unique( $snippets ) ), 0, 8 ),
        ];
    }

    private static function walk_summary( array $element, int $depth, array &$counts, array &$widget_types, array &$snippets ): void {
        $counts['depth'] = max( $counts['depth'], $depth );
        $type = (string) ( $element['elType'] ?? '' );
        if ( in_array( $type, [ 'section', 'container', 'column' ], true ) ) $counts['containers']++;
        $widget = (string) ( $element['widgetType'] ?? '' );
        if ( '' !== $widget ) { $counts['widgets']++; $widget_types[ $widget ] = ( $widget_types[ $widget ] ?? 0 ) + 1; }
        foreach ( (array) ( $element['settings'] ?? [] ) as $value ) self::collect_text( $value, $snippets, 0 );
        if ( $depth >= 12 ) return;
        foreach ( (array) ( $element['elements'] ?? [] ) as $child ) if ( is_array( $child ) ) self::walk_summary( $child, $depth + 1, $counts, $widget_types, $snippets );
    }

    private static function collect_text( $value, array &$snippets, int $depth ): void {
        if ( count( $snippets ) >= 16 || $depth > 4 ) return;
        if ( is_string( $value ) ) {
            $text = trim( preg_replace( '/\\s+/', ' ', wp_strip_all_tags( $value ) ) ?: '' );
            if ( strlen( $text ) >= 8 && strlen( $text ) <= 220 && ! str_starts_with( $text, 'http' ) ) $snippets[] = $text;
            return;
        }
        if ( is_array( $value ) ) foreach ( $value as $child ) self::collect_text( $child, $snippets, $depth + 1 );
    }

    private static function responsive_summary( array $controls ): array {
        $summary = [ 'desktop' => 0, 'tablet' => 0, 'mobile' => 0, 'unknown' => 0, 'explicit_breakpoint_controls' => 0 ];
        foreach ( $controls as $control ) {
            if ( ! is_array( $control ) ) continue;
            $breakpoint = self::breakpoint_from_path( (array) ( $control['setting_path'] ?? [] ) );
            $summary[ $breakpoint ] = ( $summary[ $breakpoint ] ?? 0 ) + 1;
            if ( 'desktop' !== $breakpoint && 'unknown' !== $breakpoint ) $summary['explicit_breakpoint_controls']++;
        }
        return $summary;
    }

    public static function breakpoint_from_path( array $path ): string {
        $key = strtolower( implode( '_', array_map( static fn( $part ) => is_scalar( $part ) ? (string) $part : '', $path ) ) );
        if ( preg_match( '/(^|_)(mobile|mobile_extra)(_|$)/', $key ) ) return 'mobile';
        if ( preg_match( '/(^|_)(tablet|tablet_extra)(_|$)/', $key ) ) return 'tablet';
        return '' !== $key ? 'desktop' : 'unknown';
    }

    private static function blueprint_contract(): array {
        return [
            'authority' => 'ChatGPT creates and maintains the blueprint in conversation; Elementize supplies fresh evidence and guarded execution only.',
            'required_fields' => [ 'page_goal', 'audience', 'conversion_goal', 'visual_direction', 'design_tokens', 'sections', 'responsive_strategy', 'interaction_strategy', 'acceptance_criteria' ],
            'section_fields' => [ 'purpose', 'narrative_role', 'layout_pattern', 'content_requirements', 'component_constraints', 'visual_treatment', 'responsive_behavior', 'interaction_behavior' ],
            'rules' => [
                'Treat the blueprint as page-wide design authority after the user/reference requirements.',
                'Use Pixfort/Elementor templates as implementation primitives, never as independent design authority.',
                'Every inserted section must serve a narrative purpose and inherit one coherent visual system.',
                'Prefer fewer reusable component treatments over section-by-section novelty.',
                'Do not add responsive overrides when inherited behavior already renders correctly.',
            ],
        ];
    }

    private static function reference_contract(): array {
        return [
            'goal' => 'Convert reference evidence into transferable design rules and structure, not a pixel-for-pixel clone.',
            'analyze' => [ 'information_hierarchy', 'section_sequence', 'layout_patterns', 'content_density', 'type_scale', 'spacing_rhythm', 'palette_roles', 'radius_and_border_language', 'image_treatment', 'cta_hierarchy', 'background_rhythm', 'interaction_patterns', 'responsive_transformations' ],
            'separate' => [ 'must_match_structure', 'adaptable_visual_language', 'site_specific_content', 'unsupported_or_unverifiable_behavior' ],
            'rule' => 'When only one viewport is observable, mark responsive conclusions as hypotheses until Elementize QA verifies them.',
        ];
    }

    private static function quality_contract(): array {
        return [
            'visual' => [ 'hierarchy', 'rhythm', 'alignment', 'density', 'contrast', 'consistency', 'composition', 'cta_clarity', 'page_coherence' ],
            'responsive' => [ 'no_horizontal_overflow', 'no_overlap_or_clipping', 'readable_line_lengths', 'logical_stacking', 'touch_target_sizing', 'bounded_whitespace', 'media_scaling' ],
            'interaction' => [ 'links_and_anchors_resolve', 'controls_have_clear_states', 'toggles_do_not_break_layout', 'focus_visibility', 'no_elements_stuck_hidden_after_motion' ],
            'acceptance' => 'A build is not complete from persisted data alone. Required viewports must render cleanly and any relevant interaction probes must pass or have an explicit bounded exception.',
        ];
    }
}

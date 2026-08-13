<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only page-wide coherence evidence for Designer Context.
 * Signals are deterministic observations, never automatic aesthetic verdicts.
 */
final class Elementize_Coherence {
    private const VERSION = '1';
    private const MAX_VISITS = 20000;
    private const MAX_EVIDENCE = 12;

    public static function analyze( array $elements, array $profile ): array {
        $state = self::collect( $elements );
        $systems = [
            'buttons' => self::button_system( $state['buttons'] ),
            'heading_roles' => self::heading_system( $state['headings'] ),
            'icons' => self::icon_system( $state['icons'] ),
            'section_backgrounds' => self::background_system( $state['sections'] ),
            'radii' => self::scale_system( (array) ( $profile['radius_scale'] ?? [] ), 'radius' ),
            'spacing' => self::scale_system( (array) ( $profile['spacing_scale'] ?? [] ), 'spacing' ),
        ];
        $findings = self::findings( $systems, ! empty( $profile['controls_truncated'] ) );
        return [
            'coherence_version' => self::VERSION,
            'read_only' => true,
            'evidence_coverage' => [
                'raw_elements_visited' => (int) $state['visits'],
                'raw_scan_truncated' => (bool) $state['truncated'],
                'design_profile_controls_returned' => count( (array) ( $profile['controls'] ?? [] ) ),
                'design_profile_truncated' => (bool) ( $profile['controls_truncated'] ?? false ),
            ],
            'systems' => $systems,
            'findings' => $findings,
            'finding_count' => count( $findings ),
            'interpretation' => [
                'Signals are evidence for ChatGPT to inspect, not automatic repair instructions.',
                'No finding means no deterministic page-wide drift crossed the conservative threshold; visual QA may still reveal composition problems.',
                'A truncated design profile cannot produce radius/spacing fragmentation findings.',
                'Wrapper padding, position and other layout context are intentionally excluded from button-style comparisons.',
            ],
        ];
    }

    private static function collect( array $elements ): array {
        $state = [ 'visits' => 0, 'truncated' => false, 'buttons' => [], 'headings' => [], 'icons' => [], 'sections' => [] ];
        foreach ( $elements as $index => $element ) {
            if ( ! is_array( $element ) ) continue;
            $settings = is_array( $element['settings'] ?? null ) ? $element['settings'] : [];
            $state['sections'][] = [
                'index' => (int) $index,
                'element_id' => (string) ( $element['id'] ?? '' ),
                'background_color' => self::scalar_setting( $settings, 'background_color' ),
            ];
            self::walk( $element, $state, 0 );
            if ( $state['truncated'] ) break;
        }
        return $state;
    }

    private static function walk( array $element, array &$state, int $depth ): void {
        if ( $depth > 80 || $state['visits'] >= self::MAX_VISITS ) { $state['truncated'] = true; return; }
        $state['visits']++;
        $widget = strtolower( trim( (string) ( $element['widgetType'] ?? '' ) ) );
        $settings = is_array( $element['settings'] ?? null ) ? $element['settings'] : [];
        if ( self::is_button_widget( $widget ) ) $state['buttons'][] = self::button_record( $element, $settings, $widget );
        if ( self::is_heading_widget( $widget ) ) $state['headings'][] = self::heading_record( $element, $settings, $widget );
        $icon = self::icon_record( $element, $settings, $widget );
        if ( null !== $icon ) $state['icons'][] = $icon;
        foreach ( (array) ( $element['elements'] ?? [] ) as $child ) {
            if ( is_array( $child ) ) self::walk( $child, $state, $depth + 1 );
            if ( $state['truncated'] ) return;
        }
    }

    private static function is_button_widget( string $widget ): bool {
        return '' !== $widget && false !== strpos( $widget, 'button' );
    }

    private static function is_heading_widget( string $widget ): bool {
        return '' !== $widget && ( false !== strpos( $widget, 'heading' ) || false !== strpos( $widget, 'highlighted-text' ) );
    }

    private static function button_record( array $element, array $settings, string $widget ): array {
        $style = self::style_subset( $settings, [
            'btn_color', 'btn_size', 'btn_style', 'button_type', 'btn_rounded', 'btn_bold',
            'btn_effect', 'btn_hover_effect', 'background_color', 'text_color', 'border_color', 'border_radius',
        ] );
        if ( ! $style ) $style = [ 'widget_type' => $widget ];
        return [
            'element_id' => (string) ( $element['id'] ?? '' ),
            'widget_type' => $widget,
            'label' => self::label( $settings, [ 'btn_text', 'text', 'title' ] ),
            'style' => $style,
            'fingerprint' => sha1( self::canonical( $style ) ),
        ];
    }

    private static function heading_record( array $element, array $settings, string $widget ): array {
        $role = self::first_scalar( $settings, [ 'html_tag', 'title_tag', 'heading_tag' ] );
        if ( '' === $role ) $role = $widget;
        return [
            'element_id' => (string) ( $element['id'] ?? '' ),
            'widget_type' => $widget,
            'role' => strtolower( $role ),
            'label' => self::label( $settings, [ 'title', 'heading', 'text' ] ),
            'presentation' => self::style_subset( $settings, [
                'title_size', 'heading_size', 'title_color', 'text_color',
                'title_typography_font_size', 'title_typography_font_weight',
            ] ),
        ];
    }

    private static function icon_record( array $element, array $settings, string $widget ): ?array {
        $has_icon = false;
        foreach ( [ 'icon', 'selected_icon', 'icon_type', 'icon_source', 'icon_style' ] as $key ) {
            if ( array_key_exists( $key, $settings ) && self::nonempty( $settings[ $key ] ) ) { $has_icon = true; break; }
        }
        if ( ! $has_icon && false === strpos( $widget, 'icon' ) ) return null;
        $source = '';
        foreach ( [ 'selected_icon', 'icon' ] as $key ) {
            if ( is_array( $settings[ $key ] ?? null ) ) {
                $source = trim( (string) ( $settings[ $key ]['library'] ?? '' ) );
                if ( '' !== $source ) break;
            }
        }
        if ( '' === $source ) $source = self::first_scalar( $settings, [ 'icon_type', 'icon_source', 'icon_style' ] );
        if ( '' === $source ) $source = 'unspecified';
        return [
            'element_id' => (string) ( $element['id'] ?? '' ),
            'widget_type' => $widget,
            'source' => strtolower( $source ),
        ];
    }

    private static function button_system( array $records ): array {
        $groups = [];
        foreach ( $records as $record ) {
            $fingerprint = (string) ( $record['fingerprint'] ?? '' );
            if ( '' === $fingerprint ) continue;
            if ( ! isset( $groups[ $fingerprint ] ) ) $groups[ $fingerprint ] = [ 'count' => 0, 'style' => $record['style'] ?? [], 'examples' => [] ];
            $groups[ $fingerprint ]['count']++;
            if ( count( $groups[ $fingerprint ]['examples'] ) < 4 ) {
                $groups[ $fingerprint ]['examples'][] = [ 'element_id' => (string) ( $record['element_id'] ?? '' ), 'label' => (string) ( $record['label'] ?? '' ) ];
            }
        }
        usort( $groups, static fn( array $a, array $b ): int => $b['count'] <=> $a['count'] );
        $count = count( $records );
        $dominant = (int) ( $groups[0]['count'] ?? 0 );
        return [
            'component_count' => $count,
            'style_group_count' => count( $groups ),
            'dominant_style_ratio' => $count > 0 ? round( $dominant / $count, 3 ) : null,
            'style_groups' => array_slice( array_values( $groups ), 0, self::MAX_EVIDENCE ),
        ];
    }

    private static function heading_system( array $records ): array {
        $roles = [];
        foreach ( $records as $record ) {
            $role = (string) ( $record['role'] ?? 'unknown' );
            if ( ! isset( $roles[ $role ] ) ) $roles[ $role ] = [ 'count' => 0, 'presentations' => [], 'examples' => [] ];
            $roles[ $role ]['count']++;
            $fingerprint = sha1( self::canonical( $record['presentation'] ?? [] ) );
            $roles[ $role ]['presentations'][ $fingerprint ] = ( $roles[ $role ]['presentations'][ $fingerprint ] ?? 0 ) + 1;
            if ( count( $roles[ $role ]['examples'] ) < 4 ) $roles[ $role ]['examples'][] = [ 'element_id' => (string) ( $record['element_id'] ?? '' ), 'label' => (string) ( $record['label'] ?? '' ) ];
        }
        foreach ( $roles as &$role ) { $role['presentation_variant_count'] = count( $role['presentations'] ); unset( $role['presentations'] ); }
        unset( $role );
        uasort( $roles, static fn( array $a, array $b ): int => $b['count'] <=> $a['count'] );
        return [
            'heading_count' => count( $records ),
            'role_count' => count( $roles ),
            'roles' => array_slice( $roles, 0, self::MAX_EVIDENCE, true ),
            'automatic_finding_policy' => 'evidence_only_v1',
        ];
    }

    private static function icon_system( array $records ): array {
        $sources = [];
        foreach ( $records as $record ) {
            $source = (string) ( $record['source'] ?? 'unspecified' );
            $sources[ $source ] = ( $sources[ $source ] ?? 0 ) + 1;
        }
        arsort( $sources );
        $count = count( $records );
        $known = $sources; unset( $known['unspecified'] );
        $known_count = array_sum( $known );
        $dominant = $known ? (int) reset( $known ) : 0;
        return [
            'icon_bearing_component_count' => $count,
            'source_count' => count( $known ),
            'known_source_count' => $known_count,
            'source_evidence_quality' => 0 === $known_count ? 'low' : ( $known_count < $count ? 'partial' : 'high' ),
            'dominant_source_ratio' => $known_count > 0 ? round( $dominant / $known_count, 3 ) : null,
            'sources' => array_slice( $sources, 0, self::MAX_EVIDENCE, true ),
        ];
    }

    private static function background_system( array $sections ): array {
        $sequence = [];
        $longest = 0;
        $current = 0;
        $previous = null;
        foreach ( $sections as $section ) {
            $color = strtolower( trim( (string) ( $section['background_color'] ?? '' ) ) );
            $sequence[] = [ 'index' => (int) ( $section['index'] ?? 0 ), 'element_id' => (string) ( $section['element_id'] ?? '' ), 'background_color' => '' !== $color ? $color : null ];
            if ( '' === $color ) { $previous = null; $current = 0; continue; }
            if ( $color === $previous ) $current++; else { $previous = $color; $current = 1; }
            $longest = max( $longest, $current );
        }
        return [
            'top_level_section_count' => count( $sections ),
            'explicit_background_count' => count( array_filter( $sequence, static fn( array $item ): bool => null !== $item['background_color'] ) ),
            'longest_identical_explicit_run' => $longest,
            'sequence' => array_slice( $sequence, 0, 80 ),
        ];
    }

    private static function scale_system( array $scale, string $kind ): array {
        $values = [];
        $total = 0;
        foreach ( $scale as $item ) {
            if ( ! is_array( $item ) ) continue;
            $count = max( 0, (int) ( $item['count'] ?? 0 ) );
            $total += $count;
            $values[] = [ 'value' => $item['value'] ?? null, 'count' => $count ];
        }
        usort( $values, static fn( array $a, array $b ): int => $b['count'] <=> $a['count'] );
        $dominant = (int) ( $values[0]['count'] ?? 0 );
        return [
            'kind' => $kind,
            'observation_count' => $total,
            'unique_value_count' => count( $values ),
            'dominant_value_ratio' => $total > 0 ? round( $dominant / $total, 3 ) : null,
            'values' => array_slice( $values, 0, self::MAX_EVIDENCE ),
        ];
    }

    private static function findings( array $systems, bool $profile_truncated ): array {
        $findings = [];
        $buttons = (array) ( $systems['buttons'] ?? [] );
        if ( (int) ( $buttons['component_count'] ?? 0 ) >= 3 && (int) ( $buttons['style_group_count'] ?? 0 ) >= 2 && (float) ( $buttons['dominant_style_ratio'] ?? 1 ) < 0.75 ) {
            $findings[] = [ 'type' => 'button_system_fragmentation', 'severity' => 'medium', 'why' => 'Button-like components split across multiple semantic style fingerprints without one dominant treatment.', 'evidence' => $buttons ];
        }
        $icons = (array) ( $systems['icons'] ?? [] );
        if ( (int) ( $icons['known_source_count'] ?? 0 ) >= 6 && (int) ( $icons['source_count'] ?? 0 ) >= 3 && (float) ( $icons['dominant_source_ratio'] ?? 1 ) < 0.70 ) {
            $findings[] = [ 'type' => 'icon_source_fragmentation', 'severity' => 'low', 'why' => 'Icon-bearing components use several source/library families without a dominant family.', 'evidence' => $icons ];
        }
        $backgrounds = (array) ( $systems['section_backgrounds'] ?? [] );
        if ( (int) ( $backgrounds['top_level_section_count'] ?? 0 ) >= 6 && (int) ( $backgrounds['longest_identical_explicit_run'] ?? 0 ) >= 4 ) {
            $findings[] = [ 'type' => 'background_rhythm_monotony_candidate', 'severity' => 'low', 'why' => 'Four or more consecutive top-level sections explicitly repeat the same background color. This is a review candidate, not proof of a defect.', 'evidence' => [ 'longest_identical_explicit_run' => (int) $backgrounds['longest_identical_explicit_run'], 'sequence' => $backgrounds['sequence'] ?? [] ] ];
        }
        if ( ! $profile_truncated ) {
            $radii = (array) ( $systems['radii'] ?? [] );
            if ( (int) ( $radii['observation_count'] ?? 0 ) >= 8 && (int) ( $radii['unique_value_count'] ?? 0 ) >= 4 && (float) ( $radii['dominant_value_ratio'] ?? 1 ) < 0.55 ) {
                $findings[] = [ 'type' => 'radius_system_fragmentation', 'severity' => 'low', 'why' => 'Observed local radius controls are spread across four or more values without a dominant treatment.', 'evidence' => $radii ];
            }
        }
        return $findings;
    }

    private static function style_subset( array $settings, array $keys ): array {
        $out = [];
        foreach ( $keys as $key ) {
            if ( ! array_key_exists( $key, $settings ) || ! self::nonempty( $settings[ $key ] ) ) continue;
            $out[ $key ] = self::bounded_value( $settings[ $key ] );
        }
        ksort( $out );
        return $out;
    }

    private static function bounded_value( $value ) {
        if ( is_scalar( $value ) || null === $value ) return is_string( $value ) ? substr( trim( $value ), 0, 160 ) : $value;
        if ( ! is_array( $value ) ) return null;
        $out = [];
        foreach ( array_slice( $value, 0, 20, true ) as $key => $child ) if ( is_scalar( $child ) || null === $child ) $out[ $key ] = is_string( $child ) ? substr( trim( $child ), 0, 120 ) : $child;
        return $out;
    }

    private static function scalar_setting( array $settings, string $key ): string {
        if ( ! array_key_exists( $key, $settings ) || ! is_scalar( $settings[ $key ] ) ) return '';
        return substr( trim( (string) $settings[ $key ] ), 0, 160 );
    }

    private static function first_scalar( array $settings, array $keys ): string {
        foreach ( $keys as $key ) { $value = self::scalar_setting( $settings, $key ); if ( '' !== $value ) return $value; }
        return '';
    }

    private static function label( array $settings, array $keys ): string {
        $value = self::first_scalar( $settings, $keys );
        if ( '' === $value ) return '';
        $value = function_exists( 'wp_strip_all_tags' ) ? wp_strip_all_tags( $value ) : strip_tags( $value );
        $value = preg_replace( '/\s+/', ' ', $value ) ?: $value;
        return substr( trim( $value ), 0, 120 );
    }

    private static function nonempty( $value ): bool {
        if ( is_string( $value ) ) return '' !== trim( $value );
        if ( is_array( $value ) ) return ! empty( $value );
        return null !== $value && false !== $value;
    }

    private static function canonical( $value ): string {
        if ( is_array( $value ) ) { $copy = $value; ksort( $copy ); $json = function_exists( 'wp_json_encode' ) ? wp_json_encode( $copy ) : json_encode( $copy ); return is_string( $json ) ? $json : ''; }
        if ( is_scalar( $value ) || null === $value ) return (string) $value;
        return '';
    }
}

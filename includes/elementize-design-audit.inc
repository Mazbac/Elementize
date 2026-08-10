<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Audit {
    private const VERSION = '0.8.0';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 27 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 195, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\d+)/design-audit', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'audit' ],
            'permission_callback' => [ self::class, 'can_audit' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function can_audit( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        $id = absint( $request['id'] );
        $page = get_post( $id );
        if ( ! $page || 'page' !== $page->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot inspect this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function audit( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $document = self::document( $id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );

        $global = self::empty_stats();
        $sections = [];
        foreach ( $elements as $index => $node ) {
            if ( ! is_array( $node ) ) continue;
            $top_id = (string) ( $node['id'] ?? 'top-' . $index );
            $sections[ $top_id ] = self::empty_section_stats( $node, $top_id, $index );
            self::walk( [ $node ], $top_id, 0, $global, $sections[ $top_id ] );
        }

        $section_rows = [];
        foreach ( $sections as $section ) $section_rows[] = self::finalize_section( $section );
        $findings = self::findings( $global, $section_rows );
        $blocking = array_values( array_filter( $findings, static fn( $f ) => ! empty( $f['blocks_design_completion_by_default'] ) ) );
        $advisory = array_values( array_filter( $findings, static fn( $f ) => empty( $f['blocks_design_completion_by_default'] ) ) );

        $palette = array_keys( $global['colors'] );
        sort( $palette );
        $font_sizes = array_keys( $global['font_sizes'] );
        sort( $font_sizes, SORT_NUMERIC );
        $spacing_values = array_keys( $global['spacing_values'] );
        sort( $spacing_values, SORT_NUMERIC );
        $button_backgrounds = array_keys( $global['button_backgrounds'] );
        sort( $button_backgrounds );

        $page = get_post( $id );
        return new WP_REST_Response( [
            'design_audit_version' => self::VERSION,
            'read_only' => true,
            'page' => [
                'id' => $id,
                'title' => get_the_title( $id ),
                'status' => (string) $page->post_status,
                'modified_gmt' => (string) $page->post_modified_gmt,
                'permalink' => get_permalink( $id ),
            ],
            'content_hash' => self::hash( $elements ),
            'audit_policy' => [
                'initial_release_advisory_only' => true,
                'note' => '0.8.0 reports measurable design signals conservatively. No design rule blocks completion by default until runtime validation establishes sufficiently low false-positive rates.',
                'visual_probe_remains_source_of_truth_for' => [ 'background imagery', 'perceived whitespace', 'actual visual hierarchy', 'reference-page similarity' ],
            ],
            'summary' => [
                'top_level_count' => count( $section_rows ),
                'total_elements' => $global['total_elements'],
                'widget_count' => $global['widget_count'],
                'explicit_palette_color_count' => count( $palette ),
                'explicit_chromatic_color_count' => self::chromatic_count( $palette ),
                'explicit_base_font_size_count' => count( $font_sizes ),
                'explicit_spacing_value_count' => count( $spacing_values ),
                'button_count' => $global['button_count'],
                'button_with_explicit_background_count' => $global['button_with_background_count'],
                'explicit_button_background_color_count' => count( $button_backgrounds ),
                'same_node_contrast_pair_count' => count( $global['contrast_pairs'] ),
                'finding_count' => count( $findings ),
                'blocking_finding_count' => count( $blocking ),
                'advisory_finding_count' => count( $advisory ),
                'default_design_pass' => 0 === count( $blocking ),
            ],
            'palette' => [
                'explicit_colors' => array_slice( $palette, 0, 40 ),
                'explicit_button_backgrounds' => array_slice( $button_backgrounds, 0, 20 ),
                'note' => 'Only explicit Elementor/Pixfort color values are counted. Global theme tokens, inherited CSS, overlays, gradients, and rendered compositing may not be fully resolved.',
            ],
            'typography' => [
                'explicit_base_font_sizes_px' => array_values( array_map( 'floatval', $font_sizes ) ),
                'observation_count' => count( $global['font_observations'] ),
                'note' => 'Base explicit px font-size controls only; responsive overrides and inherited/global typography are not treated as resolved values.',
            ],
            'spacing' => [
                'explicit_base_spacing_values_px' => array_values( array_map( 'floatval', $spacing_values ) ),
                'observation_count' => count( $global['spacing_observations'] ),
                'note' => 'Base explicit px margin/padding/gap/space values only. Responsive and inherited values are excluded from deterministic conclusions.',
            ],
            'top_level_composition' => $section_rows,
            'contrast_observations' => array_slice( $global['contrast_pairs'], 0, 30 ),
            'findings' => $findings,
            'default_design_pass' => 0 === count( $blocking ),
        ], 200 );
    }

    private static function empty_stats(): array {
        return [
            'total_elements' => 0,
            'widget_count' => 0,
            'button_count' => 0,
            'button_with_background_count' => 0,
            'colors' => [],
            'font_sizes' => [],
            'font_observations' => [],
            'spacing_values' => [],
            'spacing_observations' => [],
            'button_backgrounds' => [],
            'contrast_pairs' => [],
            'small_text' => [],
        ];
    }

    private static function empty_section_stats( array $node, string $top_id, int $index ): array {
        return [
            'top_level_id' => $top_id,
            'index' => $index,
            'root_type' => strtolower( (string) ( $node['elType'] ?? 'unknown' ) ),
            'total_elements' => 0,
            'widget_count' => 0,
            'columns' => 0,
            'containers' => 0,
            'feature_card_widgets' => 0,
            'media_widgets' => 0,
            'button_widgets' => 0,
            'alignments' => [ 'left' => 0, 'center' => 0, 'right' => 0, 'justify' => 0 ],
            'colors' => [],
        ];
    }

    private static function walk( array $nodes, string $top_id, int $depth, array &$global, array &$section ): void {
        foreach ( $nodes as $node ) {
            if ( ! is_array( $node ) ) continue;
            $global['total_elements']++;
            $section['total_elements']++;
            $el_type = strtolower( (string) ( $node['elType'] ?? '' ) );
            if ( 'column' === $el_type ) $section['columns']++;
            if ( 'container' === $el_type ) $section['containers']++;

            $widget = strtolower( (string) ( $node['widgetType'] ?? '' ) );
            $settings = is_array( $node['settings'] ?? null ) ? $node['settings'] : [];
            $element_id = (string) ( $node['id'] ?? '' );
            if ( '' !== $widget ) {
                $global['widget_count']++;
                $section['widget_count']++;
                if ( self::is_button( $widget ) ) {
                    $global['button_count']++;
                    $section['button_widgets']++;
                }
                if ( preg_match( '/feature|card|box/', $widget ) ) $section['feature_card_widgets']++;
                if ( preg_match( '/image|(^|[-_])img([-_]|$)|gallery|picture|photo|video/', $widget ) ) $section['media_widgets']++;
            }

            self::scan_settings( $settings, $widget, $element_id, $top_id, $global, $section );
            self::observe_contrast( $settings, $widget, $element_id, $top_id, $global );
            self::observe_small_text( $settings, $widget, $element_id, $top_id, $global );

            $children = $node['elements'] ?? [];
            if ( is_array( $children ) && $children ) self::walk( $children, $top_id, $depth + 1, $global, $section );
        }
    }

    private static function scan_settings( array $settings, string $widget, string $element_id, string $top_id, array &$global, array &$section, string $prefix = '' ): void {
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            $path = '' === $prefix ? $key_s : $prefix . '.' . $key_s;
            if ( is_array( $value ) ) {
                if ( self::is_base_key( $key_s ) && preg_match( '/font[_-]?size/', $key_s ) ) {
                    $size = self::dimension_size_px( $value );
                    if ( null !== $size ) self::record_font( $size, $path, $widget, $element_id, $top_id, $global );
                }
                if ( self::is_base_key( $key_s ) && preg_match( '/margin|padding|gap|spacing|space/', $key_s ) ) {
                    foreach ( self::dimension_values_px( $value ) as $space ) self::record_spacing( $space, $path, $element_id, $top_id, $global );
                }
                self::scan_settings( $value, $widget, $element_id, $top_id, $global, $section, $path );
                continue;
            }
            if ( ! is_scalar( $value ) ) continue;
            $value_s = trim( (string) $value );
            if ( '' === $value_s ) continue;

            $color = self::normalize_hex( $value_s );
            if ( null !== $color && preg_match( '/color|background|border|fill/', $key_s ) ) {
                $global['colors'][ $color ] = true;
                $section['colors'][ $color ] = true;
                if ( self::is_button( $widget ) && self::is_background_color_key( $path ) ) {
                    $global['button_backgrounds'][ $color ] = true;
                }
            }

            if ( false !== strpos( $key_s, 'align' ) ) {
                $align = strtolower( $value_s );
                if ( isset( $section['alignments'][ $align ] ) ) $section['alignments'][ $align ]++;
            }
            if ( self::is_base_key( $key_s ) && preg_match( '/font[_-]?size/', $key_s ) && is_numeric( $value_s ) ) {
                self::record_font( (float) $value_s, $path, $widget, $element_id, $top_id, $global );
            }
            if ( self::is_base_key( $key_s ) && preg_match( '/margin|padding|gap|spacing|space/', $key_s ) && is_numeric( $value_s ) ) {
                self::record_spacing( (float) $value_s, $path, $element_id, $top_id, $global );
            }
        }

        if ( self::is_button( $widget ) ) {
            $colors = self::flatten_colors( $settings );
            $bg = self::pick_background_color( $colors );
            if ( null !== $bg ) $global['button_with_background_count']++;
        }
    }

    private static function record_font( float $size, string $path, string $widget, string $element_id, string $top_id, array &$global ): void {
        if ( $size <= 0 || $size > 300 ) return;
        $key = self::number_key( $size );
        $global['font_sizes'][ $key ] = true;
        $global['font_observations'][] = [
            'top_level_id' => $top_id,
            'element_id' => $element_id,
            'widget_type' => $widget,
            'setting_path' => $path,
            'size_px' => $size,
        ];
    }

    private static function record_spacing( float $size, string $path, string $element_id, string $top_id, array &$global ): void {
        $size = abs( $size );
        if ( $size > 600 ) return;
        $key = self::number_key( $size );
        $global['spacing_values'][ $key ] = true;
        $global['spacing_observations'][] = [
            'top_level_id' => $top_id,
            'element_id' => $element_id,
            'setting_path' => $path,
            'size_px' => $size,
        ];
    }

    private static function observe_small_text( array $settings, string $widget, string $element_id, string $top_id, array &$global ): void {
        if ( ! preg_match( '/text|paragraph|description|subtitle|badge|feature/', $widget ) || preg_match( '/heading|title|sliding-text/', $widget ) ) return;
        foreach ( self::font_sizes_in_settings( $settings ) as $size ) {
            if ( $size > 0 && $size < 14 ) {
                $global['small_text'][] = [
                    'top_level_id' => $top_id,
                    'element_id' => $element_id,
                    'widget_type' => $widget,
                    'size_px' => $size,
                ];
            }
        }
    }

    private static function observe_contrast( array $settings, string $widget, string $element_id, string $top_id, array &$global ): void {
        $colors = self::flatten_colors( $settings );
        if ( ! $colors ) return;
        $text = self::pick_text_color( $colors, $widget );
        $bg = self::pick_background_color( $colors );
        if ( null === $text || null === $bg || $text === $bg && ! self::valid_six_hex( $text ) ) return;
        if ( ! self::valid_six_hex( $text ) || ! self::valid_six_hex( $bg ) ) return;
        $ratio = self::contrast_ratio( $text, $bg );
        $global['contrast_pairs'][] = [
            'top_level_id' => $top_id,
            'element_id' => $element_id,
            'widget_type' => $widget,
            'text_color' => $text,
            'background_color' => $bg,
            'contrast_ratio' => round( $ratio, 2 ),
            'scope' => 'same_element_explicit_pair',
        ];
    }

    private static function finalize_section( array $section ): array {
        $alignment_evidence = array_sum( array_map( 'intval', $section['alignments'] ) );
        $alignment = self::dominant_alignment( $section['alignments'], $alignment_evidence );
        $complexity = round( $section['widget_count'] + max( 0, $section['total_elements'] - $section['widget_count'] ) * 0.35, 2 );
        $density = $complexity >= 13 ? 'high' : ( $complexity >= 5 ? 'medium' : 'low' );
        $signature = implode( ';', [
            'root=' . $section['root_type'],
            'columns=' . self::bucket( $section['columns'] ),
            'containers=' . self::bucket( $section['containers'] ),
            'cards=' . self::bucket( $section['feature_card_widgets'] ),
            'media=' . self::bucket( $section['media_widgets'] ),
            'cta=' . self::bucket( $section['button_widgets'] ),
            'align=' . $alignment,
            'density=' . $density,
        ] );
        $colors = array_keys( $section['colors'] );
        sort( $colors );
        return [
            'top_level_id' => $section['top_level_id'],
            'index' => $section['index'],
            'root_type' => $section['root_type'],
            'total_elements' => $section['total_elements'],
            'widget_count' => $section['widget_count'],
            'columns' => $section['columns'],
            'containers' => $section['containers'],
            'feature_card_widgets' => $section['feature_card_widgets'],
            'media_widgets' => $section['media_widgets'],
            'button_widgets' => $section['button_widgets'],
            'alignment' => $alignment,
            'alignment_evidence_count' => $alignment_evidence,
            'structural_density' => $density,
            'explicit_color_count' => count( $colors ),
            'explicit_colors' => array_slice( $colors, 0, 20 ),
            'composition_signature' => $signature,
            'composition_hash' => hash( 'sha256', $signature ),
        ];
    }

    private static function findings( array $global, array $sections ): array {
        $findings = [];
        $palette = array_keys( $global['colors'] );
        $chromatic = self::chromatic_count( $palette );
        if ( count( $palette ) >= 12 && $chromatic >= 7 ) {
            $findings[] = self::finding( 'palette.fragmentation', 'warning', 'medium', 'The page contains many explicit colors, including many chromatic colors. Check whether sections have drifted away from one intentional palette.', [
                'explicit_color_count' => count( $palette ),
                'chromatic_color_count' => $chromatic,
            ] );
        }

        $font_sizes = array_keys( $global['font_sizes'] );
        if ( count( $font_sizes ) >= 9 ) {
            $findings[] = self::finding( 'typography.too_many_sizes', 'warning', 'medium', 'Many distinct explicit base font sizes are present. Check whether the page has a coherent heading/body type scale.', [ 'distinct_explicit_font_sizes' => count( $font_sizes ) ] );
        }
        if ( $global['small_text'] ) {
            $findings[] = self::finding( 'typography.small_body_text', 'warning', 'high', 'At least one text-like widget has an explicit base font size below 14px.', [
                'affected_count' => count( $global['small_text'] ),
                'examples' => array_slice( $global['small_text'], 0, 8 ),
            ] );
        }

        $spacing = array_values( array_map( 'floatval', array_keys( $global['spacing_values'] ) ) );
        if ( count( $spacing ) >= 14 ) {
            $findings[] = self::finding( 'spacing.fragmentation', 'info', 'medium', 'Many different explicit spacing values are present. Check whether the page follows a deliberate spacing rhythm instead of unrelated per-section values.', [ 'distinct_explicit_spacing_values' => count( $spacing ) ] );
        }

        $button_backgrounds = array_keys( $global['button_backgrounds'] );
        if ( $global['button_with_background_count'] >= 3 && count( $button_backgrounds ) >= 3 ) {
            $findings[] = self::finding( 'cta.multiple_primary_styles', 'warning', 'medium', 'Buttons use several different explicit background colors. Verify that primary and secondary CTA hierarchy is intentional.', [
                'buttons_with_explicit_background' => $global['button_with_background_count'],
                'distinct_button_backgrounds' => $button_backgrounds,
            ] );
        }

        $bad_contrast = [];
        $borderline_contrast = [];
        foreach ( $global['contrast_pairs'] as $pair ) {
            $ratio = (float) ( $pair['contrast_ratio'] ?? 0 );
            if ( $ratio > 0 && $ratio < 3.0 ) $bad_contrast[] = $pair;
            elseif ( $ratio >= 3.0 && $ratio < 4.5 ) $borderline_contrast[] = $pair;
        }
        if ( $bad_contrast ) {
            $findings[] = self::finding( 'contrast.explicit_pair_below_3', 'warning', 'high', 'An explicit same-element text/background color pair has contrast below 3:1. This is a strong readability warning, but rendered/inherited styling should still be visually verified.', [
                'affected_count' => count( $bad_contrast ),
                'examples' => array_slice( $bad_contrast, 0, 8 ),
            ] );
        }
        if ( $borderline_contrast ) {
            $findings[] = self::finding( 'contrast.explicit_pair_below_4_5', 'info', 'medium', 'Some explicit same-element text/background pairs are below 4.5:1. Verify text size and the rendered result before treating this as an accessibility failure.', [
                'affected_count' => count( $borderline_contrast ),
                'examples' => array_slice( $borderline_contrast, 0, 8 ),
            ] );
        }

        $runs = self::repeated_runs( $sections, 'composition_hash', 3 );
        if ( $runs ) {
            $findings[] = self::finding( 'composition.repeated_consecutive_structure', 'warning', 'medium', 'Three or more consecutive top-level sections share the same coarse composition signature. Check for repetitive page rhythm.', [ 'runs' => $runs ] );
        }
        $center_runs = self::center_runs( $sections, 3 );
        if ( $center_runs ) {
            $findings[] = self::finding( 'composition.centered_section_streak', 'info', 'medium', 'Three or more consecutive sections have confidently detected centered alignment. Check whether the page needs stronger compositional variation.', [ 'runs' => $center_runs ] );
        }

        return $findings;
    }

    private static function finding( string $rule, string $severity, string $confidence, string $message, array $evidence ): array {
        return [
            'rule_id' => $rule,
            'severity' => $severity,
            'confidence' => $confidence,
            'message' => $message,
            'evidence' => $evidence,
            'blocks_design_completion_by_default' => false,
        ];
    }

    private static function repeated_runs( array $rows, string $key, int $minimum ): array {
        $runs = [];
        $start = 0;
        while ( $start < count( $rows ) ) {
            $value = (string) ( $rows[ $start ][ $key ] ?? '' );
            $end = $start + 1;
            while ( $end < count( $rows ) && $value !== '' && (string) ( $rows[ $end ][ $key ] ?? '' ) === $value ) $end++;
            if ( $value !== '' && ( $end - $start ) >= $minimum ) {
                $ids = [];
                for ( $i = $start; $i < $end; $i++ ) $ids[] = $rows[ $i ]['top_level_id'];
                $runs[] = [ 'start_index' => $start, 'length' => $end - $start, 'top_level_ids' => $ids, 'signature' => $rows[ $start ]['composition_signature'] ];
            }
            $start = $end;
        }
        return $runs;
    }

    private static function center_runs( array $rows, int $minimum ): array {
        $runs = [];
        $start = 0;
        while ( $start < count( $rows ) ) {
            $is_center = 'center' === (string) ( $rows[ $start ]['alignment'] ?? '' ) && (int) ( $rows[ $start ]['alignment_evidence_count'] ?? 0 ) >= 2;
            if ( ! $is_center ) { $start++; continue; }
            $end = $start + 1;
            while ( $end < count( $rows ) && 'center' === (string) ( $rows[ $end ]['alignment'] ?? '' ) && (int) ( $rows[ $end ]['alignment_evidence_count'] ?? 0 ) >= 2 ) $end++;
            if ( ( $end - $start ) >= $minimum ) {
                $ids = [];
                for ( $i = $start; $i < $end; $i++ ) $ids[] = $rows[ $i ]['top_level_id'];
                $runs[] = [ 'start_index' => $start, 'length' => $end - $start, 'top_level_ids' => $ids ];
            }
            $start = $end;
        }
        return $runs;
    }

    private static function flatten_colors( array $settings, string $prefix = '' ): array {
        $out = [];
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            $path = '' === $prefix ? $key_s : $prefix . '.' . $key_s;
            if ( is_array( $value ) ) {
                $out = array_merge( $out, self::flatten_colors( $value, $path ) );
                continue;
            }
            if ( ! is_scalar( $value ) ) continue;
            $color = self::normalize_hex( trim( (string) $value ) );
            if ( null !== $color && preg_match( '/color|background|border|fill/', $path ) ) $out[ $path ] = $color;
        }
        return $out;
    }

    private static function pick_text_color( array $colors, string $widget ): ?string {
        foreach ( $colors as $path => $color ) {
            if ( preg_match( '/background|bg[_-]?color|border|fill/', $path ) ) continue;
            if ( preg_match( '/text.*color|title.*color|heading.*color|label.*color|font.*color/', $path ) ) return $color;
        }
        if ( preg_match( '/text|heading|title|badge|button|btn/', $widget ) ) {
            foreach ( $colors as $path => $color ) if ( preg_match( '/(^|\.)color$/', $path ) ) return $color;
        }
        return null;
    }

    private static function pick_background_color( array $colors ): ?string {
        foreach ( $colors as $path => $color ) if ( self::is_background_color_key( $path ) ) return $color;
        return null;
    }

    private static function is_background_color_key( string $path ): bool {
        return (bool) preg_match( '/background.*color|bg[_-]?color|(?:^|\.)(?:background|bg)$/', strtolower( $path ) );
    }

    private static function font_sizes_in_settings( array $settings ): array {
        $out = [];
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            if ( is_array( $value ) ) {
                if ( self::is_base_key( $key_s ) && preg_match( '/font[_-]?size/', $key_s ) ) {
                    $size = self::dimension_size_px( $value );
                    if ( null !== $size ) $out[] = $size;
                }
                $out = array_merge( $out, self::font_sizes_in_settings( $value ) );
            } elseif ( self::is_base_key( $key_s ) && preg_match( '/font[_-]?size/', $key_s ) && is_numeric( $value ) ) {
                $out[] = (float) $value;
            }
        }
        return $out;
    }

    private static function dimension_size_px( array $value ): ?float {
        $unit = strtolower( (string) ( $value['unit'] ?? 'px' ) );
        if ( 'px' !== $unit || ! isset( $value['size'] ) || ! is_numeric( $value['size'] ) ) return null;
        return (float) $value['size'];
    }

    private static function dimension_values_px( array $value ): array {
        $unit = strtolower( (string) ( $value['unit'] ?? 'px' ) );
        if ( 'px' !== $unit ) return [];
        $out = [];
        foreach ( [ 'size', 'top', 'right', 'bottom', 'left', 'column', 'row' ] as $key ) {
            if ( isset( $value[ $key ] ) && is_numeric( $value[ $key ] ) ) $out[] = (float) $value[ $key ];
        }
        return array_values( array_unique( $out ) );
    }

    private static function is_base_key( string $key ): bool {
        return false === strpos( $key, 'mobile' ) && false === strpos( $key, 'tablet' ) && false === strpos( $key, 'widescreen' );
    }

    private static function is_button( string $widget ): bool {
        return (bool) preg_match( '/button|(^|[-_])btn([-_]|$)|(^|[-_])cta([-_]|$)/', $widget );
    }

    private static function dominant_alignment( array $counts, int $evidence ): string {
        if ( $evidence < 2 ) return 'unknown';
        $max = max( $counts );
        if ( $max < 1 ) return 'unknown';
        $leaders = [];
        foreach ( $counts as $key => $count ) if ( $count === $max ) $leaders[] = $key;
        return 1 === count( $leaders ) ? $leaders[0] : 'mixed';
    }

    private static function bucket( int $n ): string {
        if ( $n < 1 ) return '0';
        if ( 1 === $n ) return '1';
        if ( 2 === $n ) return '2';
        return '3+';
    }

    private static function number_key( float $n ): string {
        return rtrim( rtrim( number_format( $n, 2, '.', '' ), '0' ), '.' );
    }

    private static function normalize_hex( string $value ): ?string {
        if ( ! preg_match( '/^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i', $value, $m ) ) return null;
        $hex = strtolower( $m[1] );
        if ( 3 === strlen( $hex ) ) $hex = $hex[0] . $hex[0] . $hex[1] . $hex[1] . $hex[2] . $hex[2];
        if ( 8 === strlen( $hex ) ) {
            if ( 'ff' !== substr( $hex, 6, 2 ) ) return '#' . $hex;
            $hex = substr( $hex, 0, 6 );
        }
        return '#' . $hex;
    }

    private static function valid_six_hex( string $color ): bool {
        return (bool) preg_match( '/^#[0-9a-f]{6}$/i', $color );
    }

    private static function contrast_ratio( string $a, string $b ): float {
        $la = self::luminance( $a );
        $lb = self::luminance( $b );
        $lighter = max( $la, $lb );
        $darker = min( $la, $lb );
        return ( $lighter + 0.05 ) / ( $darker + 0.05 );
    }

    private static function luminance( string $hex ): float {
        $hex = ltrim( $hex, '#' );
        $channels = [];
        foreach ( [ 0, 2, 4 ] as $offset ) {
            $v = hexdec( substr( $hex, $offset, 2 ) ) / 255;
            $channels[] = $v <= 0.04045 ? $v / 12.92 : pow( ( $v + 0.055 ) / 1.055, 2.4 );
        }
        return 0.2126 * $channels[0] + 0.7152 * $channels[1] + 0.0722 * $channels[2];
    }

    private static function chromatic_count( array $colors ): int {
        $count = 0;
        foreach ( $colors as $color ) {
            if ( ! self::valid_six_hex( $color ) ) continue;
            $hex = ltrim( $color, '#' );
            $r = hexdec( substr( $hex, 0, 2 ) );
            $g = hexdec( substr( $hex, 2, 2 ) );
            $b = hexdec( substr( $hex, 4, 2 ) );
            if ( max( $r, $g, $b ) - min( $r, $g, $b ) >= 28 ) $count++;
        }
        return $count;
    }

    private static function hash( array $elements ): string {
        return hash( 'sha256', (string) wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) );
    }

    private static function document( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return new WP_Error( 'elementize_elementor_missing', 'Elementor is not loaded.', [ 'status' => 503 ] );
        try { $document = \Elementor\Plugin::$instance->documents->get( $id ); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_document_unavailable', 'Elementor could not load this page document: ' . $e->getMessage(), [ 'status' => 500 ] ); }
        if ( ! $document ) return new WP_Error( 'elementize_document_unavailable', 'Elementor document is unavailable for this page.', [ 'status' => 404 ] );
        return $document;
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_page_audit'] = true;
        $data['design_page_audit_advisory_only'] = true;
        $data['design_page_audit_palette'] = true;
        $data['design_page_audit_typography'] = true;
        $data['design_page_audit_spacing'] = true;
        $data['design_page_audit_contrast'] = true;
        $data['design_page_audit_cta_consistency'] = true;
        $data['design_page_audit_composition_repetition'] = true;
        $response->set_data( $data );
        return $response;
    }
}

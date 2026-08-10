<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Design_Audit_Calibration {
    private const VERSION = '0.8.1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 215, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( $response->get_status() < 200 || $response->get_status() >= 300 ) return $response;
        $route = $request->get_route();
        if ( '/elementize/v1/status' === $route ) return self::status( $response );
        if ( ! preg_match( '#^/elementize/v1/pages/(\d+)/design-audit$#', $route, $m ) ) return $response;
        return self::calibrate_audit( $response, absint( $m[1] ) );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_page_audit_calibration'] = true;
        $data['design_page_audit_palette_usage'] = true;
        $data['design_page_audit_typography_coverage'] = true;
        $data['design_page_audit_spacing_rhythm'] = true;
        $data['design_page_audit_inherited_solid_contrast'] = true;
        $data['design_page_audit_cta_style_tokens'] = true;
        $data['design_page_audit_visual_truth_required'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function calibrate_audit( WP_REST_Response $response, int $page_id ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $document = self::document( $page_id );
        if ( is_wp_error( $document ) ) return $response;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return $response;

        $stats = self::empty_stats();
        foreach ( $elements as $index => $node ) {
            if ( ! is_array( $node ) ) continue;
            $top_id = (string) ( $node['id'] ?? 'top-' . $index );
            self::walk( [ $node ], $top_id, null, null, false, $stats );
        }

        $palette = self::palette_summary( $stats );
        $typography = self::typography_summary( $stats );
        $spacing = self::spacing_summary( $stats );
        $cta = self::cta_summary( $stats );
        $contrast = self::contrast_summary( $stats );
        $rhythm = self::composition_rhythm( is_array( $data['top_level_composition'] ?? null ) ? $data['top_level_composition'] : [] );

        $findings = [];
        foreach ( (array) ( $data['findings'] ?? [] ) as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            $rule = (string) ( $finding['rule_id'] ?? '' );
            if ( in_array( $rule, [ 'typography.small_body_text', 'spacing.fragmentation', 'palette.fragmentation', 'cta.multiple_primary_styles' ], true ) ) continue;
            $findings[] = $finding;
        }

        if ( $palette['chromatic_base_color_count'] >= 5 && $palette['single_section_chromatic_color_count'] >= 3 ) {
            $findings[] = self::finding(
                'palette.section_accent_drift', 'warning', 'medium',
                'Several chromatic colors appear only in isolated sections. Check whether imported sections have drifted away from one intentional page palette.',
                [
                    'chromatic_base_color_count' => $palette['chromatic_base_color_count'],
                    'single_section_chromatic_color_count' => $palette['single_section_chromatic_color_count'],
                    'single_section_chromatic_colors' => array_slice( $palette['single_section_chromatic_colors'], 0, 10 ),
                ]
            );
        }

        if ( $typography['text_widgets_at_or_below_12px'] > 0 ) {
            $findings[] = self::finding(
                'typography.very_small_text', 'warning', 'high',
                'At least one text-like widget has an explicit base font size of 12px or smaller.',
                [ 'affected_count' => $typography['text_widgets_at_or_below_12px'], 'examples' => array_slice( $stats['very_small_text'], 0, 8 ) ]
            );
        } elseif ( $typography['text_widgets_between_12_and_14px'] > 0 ) {
            $findings[] = self::finding(
                'typography.compact_text', 'info', 'medium',
                'Some text-like widgets use an explicit base size above 12px but below 14px. This can be valid for labels or supporting microcopy; visually verify that it is not primary body text.',
                [ 'affected_count' => $typography['text_widgets_between_12_and_14px'], 'examples' => array_slice( $stats['compact_text'], 0, 8 ) ]
            );
        }

        if ( $spacing['rhythm_observation_count'] >= 20 && $spacing['rounded_scale_value_count'] >= 10 && $spacing['singleton_bucket_ratio'] >= 0.55 ) {
            $findings[] = self::finding(
                'spacing.rhythm_fragmentation', 'info', 'medium',
                'The page uses many one-off spacing buckets. Check whether spacing follows a deliberate scale rather than template-specific values.',
                [
                    'rhythm_observation_count' => $spacing['rhythm_observation_count'],
                    'rounded_scale_value_count' => $spacing['rounded_scale_value_count'],
                    'singleton_bucket_ratio' => $spacing['singleton_bucket_ratio'],
                    'large_layout_offsets_excluded' => $spacing['large_layout_offset_count'],
                ]
            );
        }

        if ( $cta['button_count'] >= 5 && $cta['distinct_style_signature_count'] >= 4 && $cta['dominant_style_share'] < 0.60 ) {
            $findings[] = self::finding(
                'cta.style_fragmentation', 'warning', 'medium',
                'CTA/button widgets use several materially different explicit style-token signatures without one clearly dominant style. Verify primary/secondary CTA hierarchy.',
                [
                    'button_count' => $cta['button_count'],
                    'distinct_style_signature_count' => $cta['distinct_style_signature_count'],
                    'dominant_style_share' => $cta['dominant_style_share'],
                    'style_signatures' => array_slice( $cta['style_signatures'], 0, 10 ),
                ]
            );
        }

        if ( $contrast['below_3_count'] > 0 ) {
            $findings[] = self::finding(
                'contrast.inherited_explicit_pair_below_3', 'warning', 'high',
                'A text-like widget has an explicit text color against a resolvable explicit solid ancestor background below 3:1 contrast.',
                [ 'affected_count' => $contrast['below_3_count'], 'examples' => array_slice( $contrast['below_3'], 0, 8 ) ]
            );
        } elseif ( $contrast['below_4_5_count'] > 0 ) {
            $findings[] = self::finding(
                'contrast.inherited_explicit_pair_below_4_5', 'info', 'medium',
                'Some resolvable explicit text/ancestor-background pairs are below 4.5:1. Verify rendered text size/weight before treating this as an accessibility failure.',
                [ 'affected_count' => $contrast['below_4_5_count'], 'examples' => array_slice( $contrast['below_4_5'], 0, 8 ) ]
            );
        }

        if ( $rhythm['adjacent_identical_pair_count'] >= 2 || $rhythm['max_identical_run'] >= 3 ) {
            $findings[] = self::finding(
                'composition.repetition_pressure', 'info', 'medium',
                'The page contains repeated adjacent coarse section compositions. Visually verify that the repetition creates rhythm rather than monotony.',
                $rhythm
            );
        }

        $blocking = 0;
        foreach ( $findings as $finding ) if ( ! empty( $finding['blocks_design_completion_by_default'] ) ) $blocking++;

        $data['design_audit_version'] = self::VERSION;
        $data['audit_policy']['initial_release_advisory_only'] = true;
        $data['audit_policy']['note'] = '0.8.1 calibrates measurable signals and remains advisory-only. Deterministic checks never replace rendered visual inspection.';
        $data['audit_policy']['visual_probe_remains_source_of_truth_for'] = [ 'background imagery', 'perceived whitespace', 'actual visual hierarchy', 'reference-page similarity', 'gradient/image compositing' ];
        $data['summary']['finding_count'] = count( $findings );
        $data['summary']['blocking_finding_count'] = $blocking;
        $data['summary']['advisory_finding_count'] = count( $findings ) - $blocking;
        $data['summary']['default_design_pass'] = 0 === $blocking;
        $data['summary']['resolved_inherited_solid_contrast_pair_count'] = $contrast['resolved_pair_count'];
        $data['summary']['button_style_signature_count'] = $cta['distinct_style_signature_count'];
        $data['summary']['text_widget_count'] = $typography['text_like_widget_count'];
        $data['summary']['explicit_font_size_text_widget_count'] = $typography['explicit_size_text_widget_count'];
        $data['summary']['font_size_coverage_ratio'] = $typography['explicit_size_coverage_ratio'];
        $data['palette_calibration'] = $palette;
        $data['typography_calibration'] = $typography;
        $data['spacing_calibration'] = $spacing;
        $data['cta_style_calibration'] = $cta;
        $data['inherited_solid_contrast_observations'] = array_slice( $stats['contrast_pairs'], 0, 30 );
        $data['composition_rhythm'] = $rhythm;
        $data['findings'] = $findings;
        $data['default_design_pass'] = 0 === $blocking;
        $data['audit_integrity']['calibrated_signals_0_8_1'] = true;
        $data['audit_integrity']['rendered_visual_inspection_still_required'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function empty_stats(): array {
        return [
            'color_usage' => [],
            'text_like_widget_count' => 0,
            'explicit_size_text_widget_ids' => [],
            'very_small_text' => [],
            'compact_text' => [],
            'spacing' => [],
            'large_offsets' => [],
            'buttons' => [],
            'contrast_pairs' => [],
            'contrast_unresolved_media_background_count' => 0,
        ];
    }

    private static function walk( array $nodes, string $top_id, ?string $inherited_bg, ?string $inherited_text, bool $background_media_unresolved, array &$stats ): void {
        foreach ( $nodes as $node ) {
            if ( ! is_array( $node ) ) continue;
            $settings = is_array( $node['settings'] ?? null ) ? $node['settings'] : [];
            $widget = strtolower( (string) ( $node['widgetType'] ?? '' ) );
            $element_id = (string) ( $node['id'] ?? '' );

            $colors = self::flatten_colors( $settings );
            foreach ( $colors as $path => $color ) self::record_color( $color, $top_id, $path, $stats );

            $local_bg = self::pick_background( $colors );
            $local_text = self::pick_text( $colors, $widget );
            $has_bg_media = $background_media_unresolved || self::has_background_media_or_gradient( $settings );
            $effective_bg = null !== $local_bg ? $local_bg : $inherited_bg;
            $effective_text = null !== $local_text ? $local_text : $inherited_text;

            if ( self::is_text_like( $widget ) ) {
                $stats['text_like_widget_count']++;
                $sizes = self::font_sizes( $settings );
                if ( $sizes ) $stats['explicit_size_text_widget_ids'][ $element_id ] = true;
                foreach ( $sizes as $size ) {
                    if ( $size <= 0 ) continue;
                    $entry = [ 'top_level_id' => $top_id, 'element_id' => $element_id, 'widget_type' => $widget, 'size_px' => $size ];
                    if ( $size <= 12 ) $stats['very_small_text'][] = $entry;
                    elseif ( $size < 14 ) $stats['compact_text'][] = $entry;
                }
                if ( null !== $effective_text && null !== $effective_bg && self::valid_six_hex( $effective_text ) && self::valid_six_hex( $effective_bg ) ) {
                    if ( ! $has_bg_media ) {
                        $stats['contrast_pairs'][] = [
                            'top_level_id' => $top_id,
                            'element_id' => $element_id,
                            'widget_type' => $widget,
                            'text_color' => $effective_text,
                            'background_color' => $effective_bg,
                            'contrast_ratio' => round( self::contrast_ratio( $effective_text, $effective_bg ), 2 ),
                            'scope' => null !== $local_bg ? 'same_element_or_local_background' : 'explicit_ancestor_solid_background',
                        ];
                    } else {
                        $stats['contrast_unresolved_media_background_count']++;
                    }
                }
            }

            foreach ( self::spacing_values( $settings ) as $space ) {
                $abs = abs( $space['value'] );
                if ( $abs > 192 ) $stats['large_offsets'][] = $space + [ 'top_level_id' => $top_id, 'element_id' => $element_id ];
                elseif ( $abs > 0 ) $stats['spacing'][] = $space + [ 'top_level_id' => $top_id, 'element_id' => $element_id ];
            }

            if ( self::is_button( $widget ) ) {
                $signature = self::button_signature( $settings );
                $stats['buttons'][] = [ 'top_level_id' => $top_id, 'element_id' => $element_id, 'widget_type' => $widget, 'style_signature' => $signature ];
            }

            $children = $node['elements'] ?? [];
            if ( is_array( $children ) && $children ) self::walk( $children, $top_id, $effective_bg, $effective_text, $has_bg_media, $stats );
        }
    }

    private static function palette_summary( array $stats ): array {
        $bases = [];
        foreach ( $stats['color_usage'] as $color => $usage ) {
            $base = self::base_six_hex( $color );
            if ( null === $base || ! self::is_chromatic( $base ) ) continue;
            if ( ! isset( $bases[ $base ] ) ) $bases[ $base ] = [ 'observation_count' => 0, 'sections' => [] ];
            $bases[ $base ]['observation_count'] += (int) $usage['observation_count'];
            foreach ( array_keys( $usage['sections'] ) as $section ) $bases[ $base ]['sections'][ $section ] = true;
        }
        $single = [];
        $rows = [];
        foreach ( $bases as $color => $usage ) {
            $section_count = count( $usage['sections'] );
            $row = [ 'color' => $color, 'observation_count' => $usage['observation_count'], 'section_count' => $section_count ];
            $rows[] = $row;
            if ( 1 === $section_count ) $single[] = $row;
        }
        usort( $rows, static fn( $a, $b ) => $b['observation_count'] <=> $a['observation_count'] );
        return [
            'chromatic_base_color_count' => count( $rows ),
            'single_section_chromatic_color_count' => count( $single ),
            'chromatic_color_usage' => $rows,
            'single_section_chromatic_colors' => $single,
            'note' => 'Eight-digit alpha variants are grouped by their six-digit RGB base. Isolated chromatic colors are drift signals, not automatic failures.',
        ];
    }

    private static function typography_summary( array $stats ): array {
        $total = (int) $stats['text_like_widget_count'];
        $explicit = count( $stats['explicit_size_text_widget_ids'] );
        return [
            'text_like_widget_count' => $total,
            'explicit_size_text_widget_count' => $explicit,
            'explicit_size_coverage_ratio' => $total > 0 ? round( $explicit / $total, 3 ) : 0,
            'text_widgets_at_or_below_12px' => count( $stats['very_small_text'] ),
            'text_widgets_between_12_and_14px' => count( $stats['compact_text'] ),
            'note' => 'Low explicit-size coverage means inherited/global typography dominates; in that case the audit cannot safely judge the full type scale from Elementor JSON alone.',
        ];
    }

    private static function spacing_summary( array $stats ): array {
        $buckets = [];
        foreach ( $stats['spacing'] as $row ) {
            $value = abs( (float) $row['value'] );
            $bucket = (int) ( round( $value / 4 ) * 4 );
            if ( ! isset( $buckets[ $bucket ] ) ) $buckets[ $bucket ] = 0;
            $buckets[ $bucket ]++;
        }
        ksort( $buckets, SORT_NUMERIC );
        $singletons = 0;
        foreach ( $buckets as $count ) if ( 1 === $count ) $singletons++;
        $bucket_count = count( $buckets );
        return [
            'rhythm_observation_count' => count( $stats['spacing'] ),
            'rounded_scale_value_count' => $bucket_count,
            'rounded_4px_buckets' => self::assoc_rows( $buckets, 'px', 'count' ),
            'singleton_bucket_count' => $singletons,
            'singleton_bucket_ratio' => $bucket_count > 0 ? round( $singletons / $bucket_count, 3 ) : 0,
            'large_layout_offset_count' => count( $stats['large_offsets'] ),
            'large_layout_offset_examples' => array_slice( $stats['large_offsets'], 0, 8 ),
            'note' => 'Values above 192px are excluded from spacing-rhythm scoring because they are often layout offsets, positioning, or large hero spacing rather than reusable rhythm tokens.',
        ];
    }

    private static function cta_summary( array $stats ): array {
        $counts = [];
        foreach ( $stats['buttons'] as $button ) {
            $sig = (string) $button['style_signature'];
            if ( ! isset( $counts[ $sig ] ) ) $counts[ $sig ] = 0;
            $counts[ $sig ]++;
        }
        arsort( $counts );
        $dominant = $counts ? (int) reset( $counts ) : 0;
        $total = count( $stats['buttons'] );
        return [
            'button_count' => $total,
            'distinct_style_signature_count' => count( $counts ),
            'dominant_style_share' => $total > 0 ? round( $dominant / $total, 3 ) : 0,
            'style_signatures' => self::assoc_rows( $counts, 'signature', 'count' ),
            'note' => 'Signatures use explicit non-content style-related button settings. Theme/global CSS can still make visually different signatures converge, or visually similar signatures diverge.',
        ];
    }

    private static function contrast_summary( array $stats ): array {
        $bad = [];
        $borderline = [];
        foreach ( $stats['contrast_pairs'] as $pair ) {
            $ratio = (float) $pair['contrast_ratio'];
            if ( $ratio < 3 ) $bad[] = $pair;
            elseif ( $ratio < 4.5 ) $borderline[] = $pair;
        }
        return [
            'resolved_pair_count' => count( $stats['contrast_pairs'] ),
            'below_3_count' => count( $bad ),
            'below_4_5_count' => count( $borderline ),
            'below_3' => $bad,
            'below_4_5' => $borderline,
            'unresolved_due_to_background_media_count' => (int) $stats['contrast_unresolved_media_background_count'],
        ];
    }

    private static function composition_rhythm( array $rows ): array {
        $adjacent = 0;
        $max_run = 0;
        $run = 0;
        $prev = null;
        $density = [];
        foreach ( $rows as $row ) {
            $hash = (string) ( $row['composition_hash'] ?? '' );
            $density[] = (string) ( $row['structural_density'] ?? 'unknown' );
            if ( '' !== $hash && $hash === $prev ) {
                $adjacent++;
                $run++;
            } else {
                $run = '' !== $hash ? 1 : 0;
            }
            $max_run = max( $max_run, $run );
            $prev = $hash;
        }
        $unique = [];
        foreach ( $rows as $row ) if ( ! empty( $row['composition_hash'] ) ) $unique[ $row['composition_hash'] ] = true;
        return [
            'section_count' => count( $rows ),
            'unique_composition_count' => count( $unique ),
            'composition_diversity_ratio' => count( $rows ) > 0 ? round( count( $unique ) / count( $rows ), 3 ) : 0,
            'adjacent_identical_pair_count' => $adjacent,
            'max_identical_run' => $max_run,
            'density_sequence' => $density,
        ];
    }

    private static function record_color( string $color, string $top_id, string $path, array &$stats ): void {
        if ( ! isset( $stats['color_usage'][ $color ] ) ) $stats['color_usage'][ $color ] = [ 'observation_count' => 0, 'sections' => [], 'paths' => [] ];
        $stats['color_usage'][ $color ]['observation_count']++;
        $stats['color_usage'][ $color ]['sections'][ $top_id ] = true;
        if ( count( $stats['color_usage'][ $color ]['paths'] ) < 8 ) $stats['color_usage'][ $color ]['paths'][] = $path;
    }

    private static function flatten_colors( array $settings, string $prefix = '' ): array {
        $out = [];
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            $path = '' === $prefix ? $key_s : $prefix . '.' . $key_s;
            if ( is_array( $value ) ) { $out += self::flatten_colors( $value, $path ); continue; }
            if ( ! is_scalar( $value ) ) continue;
            $value_s = trim( (string) $value );
            if ( ! preg_match( '/color|background|border|fill/', $path ) ) continue;
            $color = self::normalize_hex( $value_s );
            if ( null !== $color ) $out[ $path ] = $color;
        }
        return $out;
    }

    private static function pick_background( array $colors ): ?string {
        foreach ( $colors as $path => $color ) {
            if ( ! self::valid_six_hex( $color ) ) continue;
            if ( preg_match( '/background.*color|bg[_-]?color|(?:^|\.)(?:background|bg)$/', strtolower( $path ) ) ) return $color;
        }
        return null;
    }

    private static function pick_text( array $colors, string $widget ): ?string {
        foreach ( $colors as $path => $color ) {
            if ( ! self::valid_six_hex( $color ) || preg_match( '/background|border|fill/', $path ) ) continue;
            if ( preg_match( '/text.*color|title.*color|heading.*color|label.*color|font.*color/', $path ) ) return $color;
        }
        if ( self::is_text_like( $widget ) ) foreach ( $colors as $path => $color ) {
            if ( self::valid_six_hex( $color ) && preg_match( '/(?:^|\.)color$/', $path ) ) return $color;
        }
        return null;
    }

    private static function has_background_media_or_gradient( array $settings, string $prefix = '' ): bool {
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            $path = '' === $prefix ? $key_s : $prefix . '.' . $key_s;
            if ( is_array( $value ) ) {
                if ( self::has_background_media_or_gradient( $value, $path ) ) return true;
                continue;
            }
            if ( ! is_scalar( $value ) ) continue;
            $value_s = strtolower( trim( (string) $value ) );
            if ( '' === $value_s ) continue;
            if ( preg_match( '/background.*(image|video)|gradient/', $path ) && ! in_array( $value_s, [ 'none', '0', 'no', 'false' ], true ) ) return true;
        }
        return false;
    }

    private static function font_sizes( array $settings ): array {
        $out = [];
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            if ( false !== strpos( $key_s, 'mobile' ) || false !== strpos( $key_s, 'tablet' ) || false !== strpos( $key_s, 'widescreen' ) ) continue;
            if ( is_array( $value ) ) {
                if ( preg_match( '/font[_-]?size/', $key_s ) && strtolower( (string) ( $value['unit'] ?? 'px' ) ) === 'px' && isset( $value['size'] ) && is_numeric( $value['size'] ) ) $out[] = (float) $value['size'];
                $out = array_merge( $out, self::font_sizes( $value ) );
            } elseif ( preg_match( '/font[_-]?size/', $key_s ) && is_numeric( $value ) ) {
                $out[] = (float) $value;
            }
        }
        return array_values( array_unique( $out ) );
    }

    private static function spacing_values( array $settings, string $prefix = '' ): array {
        $out = [];
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            $path = '' === $prefix ? $key_s : $prefix . '.' . $key_s;
            if ( false !== strpos( $key_s, 'mobile' ) || false !== strpos( $key_s, 'tablet' ) || false !== strpos( $key_s, 'widescreen' ) ) continue;
            if ( is_array( $value ) ) {
                if ( preg_match( '/margin|padding|gap|spacing|space/', $key_s ) && strtolower( (string) ( $value['unit'] ?? 'px' ) ) === 'px' ) {
                    foreach ( [ 'size', 'top', 'right', 'bottom', 'left', 'column', 'row' ] as $dim ) if ( isset( $value[ $dim ] ) && is_numeric( $value[ $dim ] ) ) $out[] = [ 'path' => $path . '.' . $dim, 'value' => (float) $value[ $dim ] ];
                }
                $out = array_merge( $out, self::spacing_values( $value, $path ) );
            } elseif ( preg_match( '/margin|padding|gap|spacing|space/', $key_s ) && is_numeric( $value ) ) {
                $out[] = [ 'path' => $path, 'value' => (float) $value ];
            }
        }
        return $out;
    }

    private static function button_signature( array $settings ): string {
        $tokens = [];
        self::collect_button_tokens( $settings, '', $tokens );
        if ( ! $tokens ) return 'no-explicit-style-tokens';
        ksort( $tokens );
        $parts = [];
        foreach ( $tokens as $path => $value ) $parts[] = $path . '=' . $value;
        return substr( hash( 'sha256', implode( ';', $parts ) ), 0, 16 );
    }

    private static function collect_button_tokens( array $settings, string $prefix, array &$tokens ): void {
        foreach ( $settings as $key => $value ) {
            $key_s = strtolower( (string) $key );
            $path = '' === $prefix ? $key_s : $prefix . '.' . $key_s;
            if ( preg_match( '/text|label|title|url|link|icon/', $path ) ) continue;
            if ( preg_match( '/hover|active|mobile|tablet|widescreen/', $path ) ) continue;
            if ( is_array( $value ) ) { self::collect_button_tokens( $value, $path, $tokens ); continue; }
            if ( ! is_scalar( $value ) ) continue;
            $value_s = trim( (string) $value );
            if ( '' === $value_s ) continue;
            if ( preg_match( '/style|color|background|size|rounded|radius|outline|effect|gradient|shadow|border|padding/', $path ) ) $tokens[ $path ] = substr( $value_s, 0, 80 );
        }
    }

    private static function is_text_like( string $widget ): bool {
        return '' !== $widget && (bool) preg_match( '/text|heading|title|sliding-text|badge|feature|button|btn|cta/', $widget );
    }

    private static function is_button( string $widget ): bool {
        return (bool) preg_match( '/button|(^|[-_])btn([-_]|$)|(^|[-_])cta([-_]|$)/', $widget );
    }

    private static function normalize_hex( string $value ): ?string {
        if ( ! preg_match( '/^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i', $value, $m ) ) return null;
        $hex = strtolower( $m[1] );
        if ( 3 === strlen( $hex ) ) $hex = $hex[0] . $hex[0] . $hex[1] . $hex[1] . $hex[2] . $hex[2];
        if ( 8 === strlen( $hex ) && 'ff' === substr( $hex, 6, 2 ) ) $hex = substr( $hex, 0, 6 );
        return '#' . $hex;
    }

    private static function base_six_hex( string $color ): ?string {
        if ( preg_match( '/^#([0-9a-f]{6})(?:[0-9a-f]{2})?$/i', $color, $m ) ) return '#' . strtolower( $m[1] );
        return null;
    }

    private static function valid_six_hex( string $color ): bool { return (bool) preg_match( '/^#[0-9a-f]{6}$/i', $color ); }

    private static function is_chromatic( string $color ): bool {
        $hex = ltrim( $color, '#' );
        $r = hexdec( substr( $hex, 0, 2 ) ); $g = hexdec( substr( $hex, 2, 2 ) ); $b = hexdec( substr( $hex, 4, 2 ) );
        return max( $r, $g, $b ) - min( $r, $g, $b ) >= 28;
    }

    private static function contrast_ratio( string $a, string $b ): float {
        $la = self::luminance( $a ); $lb = self::luminance( $b );
        return ( max( $la, $lb ) + 0.05 ) / ( min( $la, $lb ) + 0.05 );
    }

    private static function luminance( string $hex ): float {
        $hex = ltrim( $hex, '#' ); $c = [];
        foreach ( [ 0, 2, 4 ] as $o ) { $v = hexdec( substr( $hex, $o, 2 ) ) / 255; $c[] = $v <= 0.04045 ? $v / 12.92 : pow( ( $v + 0.055 ) / 1.055, 2.4 ); }
        return 0.2126 * $c[0] + 0.7152 * $c[1] + 0.0722 * $c[2];
    }

    private static function assoc_rows( array $assoc, string $key, string $value ): array {
        $rows = [];
        foreach ( $assoc as $k => $v ) $rows[] = [ $key => is_numeric( $k ) ? (int) $k : (string) $k, $value => (int) $v ];
        return $rows;
    }

    private static function finding( string $rule, string $severity, string $confidence, string $message, array $evidence ): array {
        return [ 'rule_id' => $rule, 'severity' => $severity, 'confidence' => $confidence, 'message' => $message, 'evidence' => $evidence, 'blocks_design_completion_by_default' => false ];
    }

    private static function document( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return new WP_Error( 'elementize_elementor_missing' );
        try { $document = \Elementor\Plugin::$instance->documents->get( $id ); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_document_unavailable' ); }
        return $document ?: new WP_Error( 'elementize_document_unavailable' );
    }
}

Elementize_Design_Audit_Calibration::init();
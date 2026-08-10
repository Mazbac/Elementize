<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Rendered_Repair_Correlation {
    private const VERSION = '0.19.0';
    private const NS = 'elementize/v1';
    private const MICRO_TEXT_PX = 13.0;
    private const LARGE_INTERNAL_PADDING_PX = 60.0;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 420, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $repair = is_array( $visual['repair_discovery'] ?? null ) ? $visual['repair_discovery'] : [];
        $metrics = is_array( $visual['render_metrics'] ?? null ) ? $visual['render_metrics'] : [];
        $visual['repair_correlation'] = self::correlate( absint( $m[1] ?? 0 ), $repair, $metrics );
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function correlate( int $id, array $repair, array $metrics ): array {
        $base = [
            'correlation_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'advisory_only' => true,
            'exact_element_ids_required' => true,
            'rendered_metrics_required' => true,
            'fresh_hash_guard' => true,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
            'findings' => [],
        ];
        if ( $id < 1 ) { $base['reason'] = 'A valid page ID is required.'; return $base; }
        if ( empty( $repair['available'] ) ) { $base['reason'] = 'Read-only repair discovery is unavailable.'; return $base; }
        if ( empty( $metrics['available'] ) || ! is_array( $metrics['sections'] ?? null ) ) { $base['reason'] = 'Rendered metrics are unavailable, so repair candidates cannot be correlated to live evidence.'; return $base; }

        $repair_hash = trim( (string) ( $repair['page_content_hash'] ?? '' ) );
        $current_hash = self::content_hash( $id );
        if ( '' === $repair_hash || '' === $current_hash || ! hash_equals( $repair_hash, $current_hash ) ) {
            $base['reason'] = 'The page changed between repair discovery and rendered-evidence correlation. Re-run the completion audit.';
            $base['stage'] = 'stale_hash';
            return $base;
        }

        $index = self::metric_index( $metrics['sections'] );
        $findings = is_array( $repair['findings'] ?? null ) ? $repair['findings'] : [];
        $ready_total = 0;
        $exact_total = 0;
        $supported_total = 0;

        foreach ( $findings as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            $category = strtolower( trim( (string) ( $finding['finding_category'] ?? '' ) ) );
            $tops = array_values( array_unique( array_filter( array_map( 'strval', is_array( $finding['localized_top_level_element_ids'] ?? null ) ? $finding['localized_top_level_element_ids'] : [] ) ) ) );
            $finding_evidence = self::finding_evidence( $category, $tops, $index );
            $candidates_out = [];
            $candidate_counts = [ 'exact' => 0, 'supported' => 0, 'planning_ready' => 0 ];

            foreach ( is_array( $finding['repair_candidates'] ?? null ) ? $finding['repair_candidates'] : [] as $candidate ) {
                if ( ! is_array( $candidate ) ) continue;
                $corr = self::candidate_correlation( $category, $candidate, $index );
                $candidate['rendered_correlation'] = $corr;
                if ( 'exact_element' === (string) ( $corr['evidence_match'] ?? '' ) || 'exact_top_level' === (string) ( $corr['evidence_match'] ?? '' ) ) {
                    $candidate_counts['exact']++;
                    $exact_total++;
                }
                if ( 'supported' === (string) ( $corr['causal_support'] ?? '' ) ) {
                    $candidate_counts['supported']++;
                    $supported_total++;
                }
                if ( ! empty( $corr['bounded_value_planning_ready'] ) ) {
                    $candidate_counts['planning_ready']++;
                    $ready_total++;
                }
                $candidates_out[] = $candidate;
            }

            $base['findings'][] = [
                'finding_index' => absint( $finding['finding_index'] ?? 0 ),
                'finding_category' => $category,
                'localized_top_level_element_ids' => $tops,
                'rendered_finding_evidence' => $finding_evidence,
                'candidate_counts' => $candidate_counts,
                'repair_candidates' => $candidates_out,
                'automatic_write_allowed' => false,
            ];
        }

        $base['available'] = true;
        $base['page_content_hash'] = $current_hash;
        $base['repair_discovery_hash_matches_current'] = true;
        $base['render_metrics_version'] = (string) ( $metrics['render_metrics_version'] ?? '' );
        $base['exact_rendered_candidate_count'] = $exact_total;
        $base['causally_supported_candidate_count'] = $supported_total;
        $base['bounded_value_planning_ready_count'] = $ready_total;
        $base['automatic_write_allowed'] = false;
        $base['note'] = 'Rendered correlation is read-only. A candidate advances toward bounded value planning only when the semantic repair target and the live computed-style evidence match the same exact Elementor element/property. No write is authorized by this object.';
        return $base;
    }

    private static function metric_index( array $sections ): array {
        $out = [ 'sections' => [], 'samples' => [] ];
        foreach ( $sections as $section ) {
            if ( ! is_array( $section ) ) continue;
            $top = trim( (string) ( $section['top_level_element_id'] ?? '' ) );
            if ( '' === $top ) continue;
            $out['sections'][$top] = $section;
            foreach ( [ 'text_samples' => 'text', 'cta_samples' => 'cta', 'media_samples' => 'media' ] as $key => $kind ) {
                foreach ( is_array( $section[$key] ?? null ) ? $section[$key] : [] as $sample ) {
                    if ( ! is_array( $sample ) ) continue;
                    $eid = trim( (string) ( $sample['element_id'] ?? '' ) );
                    if ( '' === $eid ) continue;
                    $sample['_sample_kind'] = $kind;
                    $sample['_top_level_element_id'] = $top;
                    if ( ! isset( $out['samples'][$eid] ) ) $out['samples'][$eid] = [];
                    $out['samples'][$eid][] = $sample;
                }
            }
        }
        return $out;
    }

    private static function finding_evidence( string $category, array $tops, array $index ): array {
        $sections = [];
        $text_sizes = [];
        $cta_signatures = [];
        $media = [];
        $max_internal_padding = 0.0;
        $max_inter_section_gap = 0.0;

        foreach ( $tops as $top ) {
            $section = is_array( $index['sections'][$top] ?? null ) ? $index['sections'][$top] : null;
            if ( ! $section ) continue;
            $padding = is_array( $section['padding'] ?? null ) ? $section['padding'] : [];
            $pad_values = [];
            foreach ( [ 'top', 'right', 'bottom', 'left' ] as $side ) if ( is_numeric( $padding[$side] ?? null ) ) $pad_values[] = (float) $padding[$side];
            if ( $pad_values ) $max_internal_padding = max( $max_internal_padding, max( $pad_values ) );
            foreach ( [ 'gap_before', 'gap_after' ] as $gap_key ) if ( is_numeric( $section[$gap_key] ?? null ) ) $max_inter_section_gap = max( $max_inter_section_gap, (float) $section[$gap_key] );
            $sections[] = [
                'top_level_element_id' => $top,
                'height' => $section['height'] ?? null,
                'padding' => $padding,
                'gap_before' => $section['gap_before'] ?? null,
                'gap_after' => $section['gap_after'] ?? null,
            ];
            foreach ( is_array( $section['text_samples'] ?? null ) ? $section['text_samples'] : [] as $sample ) {
                $size = $sample['style']['font_size'] ?? null;
                if ( is_numeric( $size ) ) $text_sizes[] = [ 'element_id' => (string) ( $sample['element_id'] ?? '' ), 'font_size' => (float) $size, 'text' => self::clip( (string) ( $sample['text'] ?? '' ), 120 ) ];
            }
            foreach ( is_array( $section['cta_samples'] ?? null ) ? $section['cta_samples'] : [] as $sample ) {
                $style = is_array( $sample['style'] ?? null ) ? $sample['style'] : [];
                $signature = [
                    'element_id' => (string) ( $sample['element_id'] ?? '' ),
                    'text' => self::clip( (string) ( $sample['text'] ?? '' ), 100 ),
                    'font_size' => $style['font_size'] ?? null,
                    'background_color' => (string) ( $style['background_color'] ?? '' ),
                    'color' => (string) ( $style['color'] ?? '' ),
                    'border_radius' => (string) ( $style['border_radius'] ?? '' ),
                    'padding_top' => $style['padding_top'] ?? null,
                    'padding_right' => $style['padding_right'] ?? null,
                    'padding_bottom' => $style['padding_bottom'] ?? null,
                    'padding_left' => $style['padding_left'] ?? null,
                ];
                $cta_signatures[] = $signature;
            }
            foreach ( is_array( $section['media_samples'] ?? null ) ? $section['media_samples'] : [] as $sample ) {
                $media[] = [
                    'element_id' => (string) ( $sample['element_id'] ?? '' ),
                    'src_file' => (string) ( $sample['src_file'] ?? '' ),
                    'width' => $sample['width'] ?? null,
                    'height' => $sample['height'] ?? null,
                    'object_fit' => (string) ( $sample['object_fit'] ?? '' ),
                ];
            }
        }

        $out = [
            'section_metrics' => $sections,
            'max_internal_padding_px' => round( $max_internal_padding, 2 ),
            'max_inter_section_gap_px' => round( $max_inter_section_gap, 2 ),
        ];
        if ( self::contains_any( $category, [ 'typograph', 'readab', 'legib', 'hierarchy' ] ) && $text_sizes ) {
            $sizes = array_map( static fn( $x ) => (float) $x['font_size'], $text_sizes );
            $out['text_size_range_px'] = [ 'min' => min( $sizes ), 'max' => max( $sizes ) ];
            $out['micro_text_samples'] = array_values( array_filter( $text_sizes, static fn( $x ) => (float) $x['font_size'] <= self::MICRO_TEXT_PX ) );
        }
        if ( str_contains( $category, 'cta' ) ) {
            $out['cta_samples'] = $cta_signatures;
            $tokens = [];
            foreach ( $cta_signatures as $sig ) {
                $token = wp_json_encode( [ $sig['font_size'], $sig['background_color'], $sig['color'], $sig['border_radius'], $sig['padding_top'], $sig['padding_right'], $sig['padding_bottom'], $sig['padding_left'] ] );
                if ( is_string( $token ) ) $tokens[$token] = true;
            }
            $out['distinct_cta_style_signature_count'] = count( $tokens );
        }
        if ( self::contains_any( $category, [ 'imagery', 'image', 'media' ] ) ) $out['media_samples'] = $media;
        if ( self::contains_any( $category, [ 'whitespace', 'spacing', 'rhythm', 'balance', 'flow' ] ) ) {
            $out['inter_section_gap_is_primary_signal'] = $max_inter_section_gap > 24;
            $out['large_internal_padding_detected'] = $max_internal_padding >= self::LARGE_INTERNAL_PADDING_PX;
        }
        return $out;
    }

    private static function candidate_correlation( string $finding, array $candidate, array $index ): array {
        $type = strtolower( trim( (string) ( $candidate['candidate_type'] ?? '' ) ) );
        $category = strtolower( trim( (string) ( $candidate['category'] ?? '' ) ) );
        $kind = strtolower( trim( (string) ( $candidate['kind'] ?? '' ) ) );
        $element_id = trim( (string) ( $candidate['element_id'] ?? '' ) );
        $top = trim( (string) ( $candidate['top_level_element_id'] ?? '' ) );
        $path_text = strtolower( implode( '.', array_map( 'strval', is_array( $candidate['setting_path'] ?? null ) ? $candidate['setting_path'] : [] ) ) );
        $control_key = strtolower( trim( (string) ( $candidate['control_key'] ?? '' ) ) );
        $control_text = trim( $control_key . ' ' . $path_text );
        $semantic = strtolower( (string) ( $candidate['actionability'] ?? 'weak' ) );
        $semantic_planning = ! empty( $candidate['value_planning_ready'] );
        $scope = strtolower( (string) ( $candidate['responsive_scope'] ?? 'base' ) );

        $samples = is_array( $index['samples'][$element_id] ?? null ) ? $index['samples'][$element_id] : [];
        $section = is_array( $index['sections'][$top] ?? null ) ? $index['sections'][$top] : null;
        $exact_top = '' !== $element_id && $element_id === $top && $section;
        $match = $samples ? 'exact_element' : ( $exact_top ? 'exact_top_level' : ( $section ? 'section_context_only' : 'none' ) );
        $observed = [];
        $property_supported = false;
        $property = '';

        if ( 'design_control' === $type && 'spacing' === $category ) {
            $property = self::spacing_property( $control_text );
            if ( '' !== $property ) {
                if ( $exact_top ) {
                    $observed = self::section_spacing_evidence( $section, $property );
                    $property_supported = isset( $observed['rendered_value_px'] ) && is_numeric( $observed['rendered_value_px'] );
                } elseif ( $samples ) {
                    $observed = self::sample_style_evidence( $samples, $property );
                    $property_supported = ! empty( $observed['matches'] );
                }
            }
        } elseif ( 'design_control' === $type && 'typography' === $category ) {
            if ( preg_match( '/font[_-]?size/', $control_text ) ) $property = 'font_size';
            elseif ( preg_match( '/line[_-]?height/', $control_text ) ) $property = 'line_height';
            if ( '' !== $property && $samples ) {
                $observed = self::sample_style_evidence( $samples, $property );
                $property_supported = ! empty( $observed['matches'] );
            }
        } elseif ( 'design_control' === $type && 'border_radius' === $category ) {
            $property = 'border_radius';
            if ( $samples ) {
                $observed = self::sample_style_evidence( $samples, $property );
                $property_supported = ! empty( $observed['matches'] );
            }
        } elseif ( 'visual_setting' === $type && 'media' === $kind ) {
            $property = 'media_source';
            foreach ( $samples as $sample ) {
                if ( 'media' !== (string) ( $sample['_sample_kind'] ?? '' ) ) continue;
                $observed['matches'][] = [
                    'src_file' => (string) ( $sample['src_file'] ?? '' ),
                    'width' => $sample['width'] ?? null,
                    'height' => $sample['height'] ?? null,
                    'object_fit' => (string) ( $sample['object_fit'] ?? '' ),
                ];
            }
            $property_supported = ! empty( $observed['matches'] );
        } elseif ( 'visual_setting' === $type && 'color' === $kind ) {
            $property = str_contains( $control_text, 'background' ) ? 'background_color' : 'color';
            if ( $samples ) {
                $observed = self::sample_style_evidence( $samples, $property );
                $property_supported = ! empty( $observed['matches'] );
            }
        }

        $causal = 'unmeasured';
        if ( $property_supported && 'direct' === $semantic ) $causal = 'supported';
        elseif ( $property_supported && in_array( $semantic, [ 'supporting', 'weak' ], true ) ) $causal = 'context_only';
        elseif ( in_array( $match, [ 'exact_element', 'exact_top_level' ], true ) ) $causal = 'exact_element_but_property_unmeasured';
        elseif ( 'section_context_only' === $match ) $causal = 'section_context_only';

        $planning_ready = false;
        $block = 'Rendered evidence does not support bounded value planning for this candidate.';
        if ( $semantic_planning && 'direct' === $semantic && 'supported' === $causal && 'base' === $scope && 'design_control' === $type && in_array( $category, [ 'spacing', 'typography' ], true ) ) {
            $planning_ready = true;
            $block = 'No write is authorized yet. This exact base-scope reversible design control has sufficient rendered evidence to enter a separate bounded value-planning step.';
        } elseif ( $semantic_planning && 'base' !== $scope ) {
            $block = 'First bounded experiments require base-scope controls so desktop rendered evidence is not confused with a responsive override.';
        } elseif ( 'direct' === $semantic && ! $property_supported ) {
            $block = 'The candidate is semantically direct, but the exact affected rendered property was not measured on this element.';
        } elseif ( 'direct' !== $semantic ) {
            $block = 'Only semantically direct candidates can enter bounded value planning.';
        }

        return [
            'evidence_match' => $match,
            'rendered_property' => $property,
            'rendered_evidence' => $observed,
            'causal_support' => $causal,
            'bounded_value_planning_ready' => $planning_ready,
            'automatic_write_allowed' => false,
            'planning_or_write_block_reason' => $block,
        ];
    }

    private static function spacing_property( string $text ): string {
        foreach ( [ 'padding_top', 'padding_right', 'padding_bottom', 'padding_left', 'margin_top', 'margin_right', 'margin_bottom', 'margin_left' ] as $property ) {
            $needle = str_replace( '_', '[-_.]?', $property );
            if ( preg_match( '/' . $needle . '/', $text ) ) return $property;
        }
        if ( preg_match( '/(^|[^a-z])padding([^a-z]|$)/', $text ) ) return 'padding';
        if ( preg_match( '/(^|[^a-z])margin([^a-z]|$)/', $text ) ) return 'margin';
        return '';
    }

    private static function section_spacing_evidence( array $section, string $property ): array {
        if ( in_array( $property, [ 'padding', 'margin' ], true ) ) {
            return [ 'box' => is_array( $section[$property] ?? null ) ? $section[$property] : [] ];
        }
        if ( preg_match( '/^(padding|margin)_(top|right|bottom|left)$/', $property, $m ) ) {
            $box = is_array( $section[$m[1]] ?? null ) ? $section[$m[1]] : [];
            $value = $box[$m[2]] ?? null;
            return [ 'side' => $m[2], 'rendered_value_px' => $value ];
        }
        return [];
    }

    private static function sample_style_evidence( array $samples, string $property ): array {
        $out = [ 'matches' => [] ];
        foreach ( $samples as $sample ) {
            $style = is_array( $sample['style'] ?? null ) ? $sample['style'] : [];
            if ( ! array_key_exists( $property, $style ) ) continue;
            $out['matches'][] = [
                'sample_kind' => (string) ( $sample['_sample_kind'] ?? '' ),
                'top_level_element_id' => (string) ( $sample['_top_level_element_id'] ?? '' ),
                'text' => self::clip( (string) ( $sample['text'] ?? '' ), 100 ),
                'rendered_value' => $style[$property],
            ];
        }
        return $out;
    }

    private static function content_hash( int $id ): string {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return '';
        try {
            $doc = \Elementor\Plugin::$instance->documents->get( $id );
            $elements = $doc ? $doc->get_elements_data() : null;
            if ( ! is_array( $elements ) ) return '';
            $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
            return hash( 'sha256', false === $json ? '' : $json );
        } catch ( Throwable $ignored ) {
            return '';
        }
    }

    private static function contains_any( string $haystack, array $needles ): bool {
        foreach ( $needles as $needle ) if ( str_contains( $haystack, $needle ) ) return true;
        return false;
    }

    private static function clip( string $value, int $max ): string {
        return function_exists( 'mb_substr' ) ? mb_substr( $value, 0, $max ) : substr( $value, 0, $max );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_repair_correlation'] = true;
        $data['rendered_visual_repair_correlation_read_only'] = true;
        $data['rendered_visual_repair_correlation_exact_element_ids'] = true;
        $data['rendered_visual_repair_correlation_hash_guard'] = true;
        $data['rendered_visual_repair_correlation_computed_style_evidence'] = true;
        $data['rendered_visual_repair_correlation_direct_target_requires_exact_evidence'] = true;
        $data['rendered_visual_repair_correlation_base_scope_first_experiments'] = true;
        $data['rendered_visual_repair_value_planning_automatic_write_allowed'] = false;
        $data['rendered_visual_repair_correlation_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

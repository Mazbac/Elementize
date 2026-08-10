<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Rendered_Observations {
    private const VERSION = '0.21.0';
    private const NS = 'elementize/v1';
    private const MICRO_TEXT_PX = 13.0;
    private const LARGE_PADDING_PX = 60.0;
    private const MAX_SAMPLES = 12;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 460, 3 );
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
        $metrics = is_array( $visual['render_metrics'] ?? null ) ? $visual['render_metrics'] : [];
        $critique = is_array( $visual['critique'] ?? null ) ? $visual['critique'] : [];
        $visual['render_observations'] = self::observe( $metrics, $critique );
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function observe( array $metrics, array $critique ): array {
        $base = [
            'observation_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'advisory_only' => true,
            'deterministic_measurements_only' => true,
            'defects_asserted' => false,
            'direct_repair_candidates_created' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
            'observations' => [],
        ];
        if ( empty( $metrics['available'] ) || ! is_array( $metrics['sections'] ?? null ) ) {
            $base['reason'] = 'Rendered metrics are unavailable.';
            return $base;
        }

        $micro = [];
        $cta = [];
        $padding = [];
        $max_gap = 0.0;

        foreach ( $metrics['sections'] as $section ) {
            if ( ! is_array( $section ) ) continue;
            $top = trim( (string) ( $section['top_level_element_id'] ?? '' ) );
            foreach ( [ 'gap_before', 'gap_after' ] as $gap_key ) {
                if ( is_numeric( $section[$gap_key] ?? null ) ) $max_gap = max( $max_gap, (float) $section[$gap_key] );
            }
            $box = is_array( $section['padding'] ?? null ) ? $section['padding'] : [];
            foreach ( [ 'top', 'bottom', 'right', 'left' ] as $side ) {
                if ( ! is_numeric( $box[$side] ?? null ) ) continue;
                $value = (float) $box[$side];
                if ( $value >= self::LARGE_PADDING_PX ) {
                    $padding[] = [
                        'top_level_element_id' => $top,
                        'side' => $side,
                        'rendered_value_px' => round( $value, 2 ),
                        'section_height_px' => is_numeric( $section['height'] ?? null ) ? round( (float) $section['height'], 2 ) : null,
                    ];
                }
            }

            foreach ( is_array( $section['text_samples'] ?? null ) ? $section['text_samples'] : [] as $sample ) {
                if ( ! is_array( $sample ) ) continue;
                $style = is_array( $sample['style'] ?? null ) ? $sample['style'] : [];
                if ( ! is_numeric( $style['font_size'] ?? null ) ) continue;
                $size = (float) $style['font_size'];
                if ( $size > self::MICRO_TEXT_PX ) continue;
                if ( count( $micro ) >= self::MAX_SAMPLES ) continue;
                $micro[] = [
                    'top_level_element_id' => $top,
                    'element_id' => (string) ( $sample['element_id'] ?? '' ),
                    'font_size_px' => round( $size, 2 ),
                    'line_height' => $style['line_height'] ?? null,
                    'text' => self::clip( (string) ( $sample['text'] ?? '' ), 120 ),
                ];
            }

            foreach ( is_array( $section['cta_samples'] ?? null ) ? $section['cta_samples'] : [] as $sample ) {
                if ( ! is_array( $sample ) ) continue;
                $style = is_array( $sample['style'] ?? null ) ? $sample['style'] : [];
                if ( count( $cta ) >= self::MAX_SAMPLES ) continue;
                $signature = [
                    'font_size' => $style['font_size'] ?? null,
                    'background_color' => (string) ( $style['background_color'] ?? '' ),
                    'color' => (string) ( $style['color'] ?? '' ),
                    'border_radius' => (string) ( $style['border_radius'] ?? '' ),
                    'padding_top' => $style['padding_top'] ?? null,
                    'padding_right' => $style['padding_right'] ?? null,
                    'padding_bottom' => $style['padding_bottom'] ?? null,
                    'padding_left' => $style['padding_left'] ?? null,
                ];
                $cta[] = [
                    'top_level_element_id' => $top,
                    'element_id' => (string) ( $sample['element_id'] ?? '' ),
                    'text' => self::clip( (string) ( $sample['text'] ?? '' ), 100 ),
                    'signature' => $signature,
                    '_token' => (string) wp_json_encode( $signature, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ),
                ];
            }
        }

        $observations = [];
        if ( $micro ) {
            $observations[] = [
                'type' => 'micro_text_present',
                'measured' => true,
                'threshold_px' => self::MICRO_TEXT_PX,
                'sample_count' => count( $micro ),
                'samples' => $micro,
                'visual_convergence' => self::critique_mentions( $critique, [ 'typograph', 'readab', 'legib', 'small font', 'font size', 'text size' ] ),
                'defect_asserted' => false,
                'direct_repair_candidate_created' => false,
                'note' => 'This only records that visible text at or below the threshold exists. Microtext may be intentional; a defect is not inferred from size alone.',
            ];
        }

        if ( $cta ) {
            $tokens = [];
            foreach ( $cta as $sample ) $tokens[ (string) $sample['_token'] ] = true;
            if ( count( $tokens ) > 1 ) {
                $clean = [];
                foreach ( $cta as $sample ) { unset( $sample['_token'] ); $clean[] = $sample; }
                $observations[] = [
                    'type' => 'cta_style_divergence_present',
                    'measured' => true,
                    'cta_sample_count' => count( $clean ),
                    'distinct_style_signature_count' => count( $tokens ),
                    'samples' => $clean,
                    'visual_convergence' => self::critique_mentions( $critique, [ 'cta', 'call-to-action', 'call to action', 'button', 'consisten' ] ),
                    'defect_asserted' => false,
                    'direct_repair_candidate_created' => false,
                    'note' => 'Different rendered CTA style signatures are factual. They are not automatically a defect because primary and secondary actions may intentionally differ.',
                ];
            }
        }

        if ( $padding ) {
            $observations[] = [
                'type' => 'large_internal_padding_present',
                'measured' => true,
                'threshold_px' => self::LARGE_PADDING_PX,
                'max_inter_section_gap_px' => round( $max_gap, 2 ),
                'samples' => array_slice( $padding, 0, self::MAX_SAMPLES ),
                'visual_convergence' => self::critique_mentions( $critique, [ 'whitespace', 'spacing', 'rhythm', 'balance', 'flow', 'disjoint' ] ),
                'defect_asserted' => false,
                'direct_repair_candidate_created' => false,
                'note' => 'Large internal padding is reported separately from inter-section gaps. It may be intentional and is not a repair target without converging visual evidence plus exact guarded control mapping.',
            ];
        }

        $converged = 0;
        foreach ( $observations as $observation ) if ( ! empty( $observation['visual_convergence'] ) ) $converged++;
        $base['available'] = true;
        $base['render_metrics_version'] = (string) ( $metrics['render_metrics_version'] ?? '' );
        $base['observation_count'] = count( $observations );
        $base['visually_converged_observation_count'] = $converged;
        $base['observations'] = $observations;
        $base['automatic_write_allowed'] = false;
        $base['note'] = 'Rendered observations are deterministic measurements, not defect declarations. They may be used by a later convergence layer only when the visual critique independently supports the same issue and an exact guarded control can be mapped.';
        return $base;
    }

    private static function critique_mentions( array $critique, array $needles ): bool {
        $parts = [];
        $parts[] = (string) ( $critique['overall_assessment'] ?? '' );
        $parts[] = (string) ( $critique['summary'] ?? '' );
        foreach ( is_array( $critique['dimensions'] ?? null ) ? $critique['dimensions'] : [] as $name => $dimension ) {
            $parts[] = (string) $name;
            if ( is_array( $dimension ) ) {
                $parts[] = (string) ( $dimension['evidence'] ?? '' );
                $parts[] = (string) ( $dimension['recommendation'] ?? '' );
            }
        }
        foreach ( is_array( $critique['findings'] ?? null ) ? $critique['findings'] : [] as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            $parts[] = (string) ( $finding['category'] ?? '' );
            $parts[] = (string) ( $finding['evidence'] ?? '' );
            $parts[] = (string) ( $finding['recommendation'] ?? '' );
        }
        foreach ( is_array( $critique['priority_actions'] ?? null ) ? $critique['priority_actions'] : [] as $action ) $parts[] = (string) $action;
        $text = strtolower( implode( ' ', $parts ) );
        foreach ( $needles as $needle ) if ( str_contains( $text, strtolower( $needle ) ) ) return true;
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
        $data['rendered_visual_observations'] = true;
        $data['rendered_visual_observations_read_only'] = true;
        $data['rendered_visual_observations_deterministic_measurements_only'] = true;
        $data['rendered_visual_observations_microtext'] = true;
        $data['rendered_visual_observations_cta_style_divergence'] = true;
        $data['rendered_visual_observations_large_internal_padding'] = true;
        $data['rendered_visual_observations_visual_convergence_signal'] = true;
        $data['rendered_visual_observations_defects_asserted'] = false;
        $data['rendered_visual_observations_direct_repair_candidates_created'] = false;
        $data['rendered_visual_observations_automatic_write_allowed'] = false;
        $data['rendered_visual_observations_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

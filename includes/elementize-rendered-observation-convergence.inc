<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observations_Hardening', false ) ) return;

final class Elementize_Rendered_Observation_Convergence {
    private const VERSION = '0.22.0';
    private const NS = 'elementize/v1';
    private const MAX_TARGETS = 6;
    private const MAX_CONTROLS_PER_TOP = 150;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 480, 3 );
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
        $observations = is_array( $visual['render_observations'] ?? null ) ? $visual['render_observations'] : [];
        $visual['repair_convergence'] = self::converge( absint( $m[1] ?? 0 ), $observations );
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function converge( int $id, array $observations ): array {
        $base = [
            'convergence_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'advisory_only' => true,
            'automatic_write_allowed' => false,
            'defects_asserted' => false,
            'promotion_requires_rendered_measurement' => true,
            'promotion_requires_visual_convergence' => true,
            'promotion_requires_exact_guarded_control' => true,
            'promotion_requires_same_element_and_property' => true,
            'promoted_targets' => [],
            'blocked_observations' => [],
            'action_slot_cost' => 0,
        ];
        if ( $id < 1 ) { $base['reason'] = 'A valid page ID is required.'; return $base; }
        if ( empty( $observations['available'] ) || ! is_array( $observations['observations'] ?? null ) ) {
            $base['reason'] = 'Fresh rendered observations are required before convergence-to-control mapping.';
            return $base;
        }

        $elements = self::elements( $id );
        if ( ! is_array( $elements ) ) { $base['reason'] = 'Fresh Elementor page data could not be read.'; return $base; }
        $hash = self::hash( $elements );
        $cache = [];
        $mapped = 0;
        $promoted = [];
        $blocked = [];

        foreach ( $observations['observations'] as $observation ) {
            if ( ! is_array( $observation ) || empty( $observation['measured'] ) ) continue;
            $type = (string) ( $observation['type'] ?? '' );
            $visual_convergence = ! empty( $observation['visual_convergence'] );

            if ( 'cta_style_divergence_present' === $type ) {
                $blocked[] = [
                    'observation_type' => $type,
                    'measured' => true,
                    'visual_convergence' => $visual_convergence,
                    'reason' => 'CTA style divergence is measurable, but a reference CTA/style must be selected before any exact style control can be promoted for normalization.',
                    'requires_reference_style' => true,
                ];
                continue;
            }

            foreach ( is_array( $observation['samples'] ?? null ) ? $observation['samples'] : [] as $sample ) {
                if ( ! is_array( $sample ) ) continue;
                $top = trim( (string) ( $sample['top_level_element_id'] ?? '' ) );
                if ( '' === $top ) continue;
                if ( ! isset( $cache[$top] ) ) {
                    $read = self::design_controls_for_top( $id, $top, $hash );
                    if ( is_wp_error( $read ) ) {
                        $base['reason'] = self::safe_error( $read, 'Fresh design controls could not be read.' );
                        $base['stage'] = 'design_controls';
                        return $base;
                    }
                    $cache[$top] = $read;
                }
                $controls = $cache[$top];
                $match = null;
                $property = '';
                $rendered_value = null;
                $component = null;

                if ( 'large_internal_padding_present' === $type ) {
                    $element = $top;
                    $component = strtolower( trim( (string) ( $sample['side'] ?? '' ) ) );
                    $rendered_value = is_numeric( $sample['rendered_value_px'] ?? null ) ? (float) $sample['rendered_value_px'] : null;
                    $property = 'padding';
                    $match = self::match_padding_control( $controls, $element, $component, $rendered_value );
                } elseif ( 'micro_text_present' === $type ) {
                    $element = trim( (string) ( $sample['element_id'] ?? '' ) );
                    $rendered_value = is_numeric( $sample['font_size_px'] ?? null ) ? (float) $sample['font_size_px'] : null;
                    $property = 'font_size';
                    $match = self::match_font_size_control( $controls, $element, $rendered_value );
                } else {
                    continue;
                }

                if ( ! is_array( $match ) ) continue;
                $mapped++;
                if ( ! $visual_convergence ) continue;
                if ( count( $promoted ) >= self::MAX_TARGETS ) continue;

                $identity = [
                    'page_content_hash' => $hash,
                    'observation_type' => $type,
                    'top_level_element_id' => $top,
                    'element_id' => (string) ( $match['element_id'] ?? '' ),
                    'setting_path' => is_array( $match['setting_path'] ?? null ) ? array_values( $match['setting_path'] ) : [],
                    'control_fingerprint' => (string) ( $match['control_fingerprint'] ?? '' ),
                    'rendered_property' => $property,
                    'rendered_value' => $rendered_value,
                    'component' => $component,
                ];
                $target_id = substr( hash( 'sha256', (string) wp_json_encode( $identity, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) ), 0, 20 );
                $promoted[] = [
                    'target_id' => $target_id,
                    'promotion_status' => 'promoted_read_only',
                    'observation_type' => $type,
                    'top_level_element_id' => $top,
                    'element_id' => (string) ( $match['element_id'] ?? '' ),
                    'widget_type' => $match['widget_type'] ?? null,
                    'category' => (string) ( $match['category'] ?? '' ),
                    'control_key' => (string) ( $match['control_key'] ?? '' ),
                    'setting_path' => is_array( $match['setting_path'] ?? null ) ? array_values( $match['setting_path'] ) : [],
                    'responsive_scope' => (string) ( $match['responsive_scope'] ?? 'base' ),
                    'current_value' => $match['value'] ?? ( $match['current_value'] ?? null ),
                    'control_fingerprint' => (string) ( $match['control_fingerprint'] ?? '' ),
                    'writer_route' => 'POST /' . self::NS . '/pages/{id}/design-settings',
                    'rendered_property' => $property,
                    'rendered_value' => $rendered_value,
                    'matched_component' => $component,
                    'visual_convergence' => true,
                    'exact_control_match' => true,
                    'eligible_for_bounded_planning_review' => in_array( (string) ( $match['category'] ?? '' ), [ 'spacing', 'typography' ], true ),
                    'defect_asserted' => false,
                    'automatic_write_allowed' => false,
                    'fresh_exact_read_required_before_any_write' => true,
                    'promotion_basis' => 'Deterministic rendered measurement + independent visual convergence + exact same-element guarded design control with matching current rendered property.',
                ];
            }
        }

        $base['available'] = true;
        $base['page_content_hash'] = $hash;
        $base['observation_count'] = count( $observations['observations'] );
        $base['mapped_exact_control_count'] = $mapped;
        $base['promoted_target_count'] = count( $promoted );
        $base['promoted_targets'] = $promoted;
        $base['blocked_observations'] = array_slice( $blocked, 0, 4 );
        $base['automatic_write_allowed'] = false;
        $base['note'] = $promoted
            ? 'One or more deterministic observations also have independent visual convergence and an exact guarded same-property control. These are read-only promoted targets, not approved edits. A later bounded planner may review them after a fresh exact read.'
            : 'No observation satisfied all three promotion gates: deterministic measurement, independent visual convergence, and exact guarded same-element/property control mapping.';
        return $base;
    }

    private static function match_padding_control( array $controls, string $element, string $side, ?float $rendered ): ?array {
        if ( ! in_array( $side, [ 'top', 'right', 'bottom', 'left' ], true ) || null === $rendered ) return null;
        foreach ( $controls as $control ) {
            if ( ! self::eligible_control( $control, $element, 'spacing' ) ) continue;
            $key = strtolower( (string) ( $control['control_key'] ?? '' ) );
            $path = strtolower( implode( '.', array_map( 'strval', is_array( $control['setting_path'] ?? null ) ? $control['setting_path'] : [] ) ) );
            if ( ! str_contains( $key . ' ' . $path, 'padding' ) ) continue;
            $value = $control['value'] ?? ( $control['current_value'] ?? null );
            if ( ! is_array( $value ) || 'px' !== strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) || ! is_numeric( $value[$side] ?? null ) ) continue;
            if ( abs( (float) $value[$side] - $rendered ) > 1.0 ) continue;
            return $control;
        }
        return null;
    }

    private static function match_font_size_control( array $controls, string $element, ?float $rendered ): ?array {
        if ( '' === $element || null === $rendered ) return null;
        foreach ( $controls as $control ) {
            if ( ! self::eligible_control( $control, $element, 'typography' ) ) continue;
            $key = strtolower( (string) ( $control['control_key'] ?? '' ) );
            $path = strtolower( implode( '.', array_map( 'strval', is_array( $control['setting_path'] ?? null ) ? $control['setting_path'] : [] ) ) );
            if ( ! preg_match( '/font[_-]?size/', $key . ' ' . $path ) ) continue;
            $value = $control['value'] ?? ( $control['current_value'] ?? null );
            $current = null;
            if ( is_array( $value ) && 'px' === strtolower( trim( (string) ( $value['unit'] ?? '' ) ) ) && is_numeric( $value['size'] ?? null ) ) $current = (float) $value['size'];
            elseif ( is_numeric( $value ) ) $current = (float) $value;
            if ( null === $current || abs( $current - $rendered ) > 1.0 ) continue;
            return $control;
        }
        return null;
    }

    private static function eligible_control( $control, string $element, string $category ): bool {
        if ( ! is_array( $control ) ) return false;
        if ( (string) ( $control['element_id'] ?? '' ) !== $element ) return false;
        if ( strtolower( (string) ( $control['category'] ?? '' ) ) !== $category ) return false;
        if ( 'base' !== strtolower( (string) ( $control['responsive_scope'] ?? 'base' ) ) ) return false;
        if ( empty( $control['writable_now'] ) ) return false;
        if ( '' === trim( (string) ( $control['control_fingerprint'] ?? '' ) ) ) return false;
        return true;
    }

    private static function design_controls_for_top( int $id, string $top, string $hash ) {
        $out = [];
        $offset = 0;
        while ( count( $out ) < self::MAX_CONTROLS_PER_TOP ) {
            $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/design-settings' );
            foreach ( [ 'top_level_id' => $top, 'offset' => $offset, 'limit' => 75, 'compact' => true ] as $key => $value ) $request->set_param( $key, $value );
            $response = rest_do_request( $request );
            if ( is_wp_error( $response ) ) return $response;
            if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_convergence_design_read_failed', 'Fresh design controls could not be read.' );
            $data = $response->get_data();
            if ( ! is_array( $data ) ) return new WP_Error( 'elementize_convergence_design_invalid', 'Fresh design-control read returned invalid data.' );
            $read_hash = trim( (string) ( $data['content_hash'] ?? '' ) );
            if ( '' !== $read_hash && ! hash_equals( $hash, $read_hash ) ) return new WP_Error( 'elementize_convergence_stale_hash', 'The page changed while observation controls were being mapped.' );
            foreach ( is_array( $data['controls'] ?? null ) ? $data['controls'] : [] as $control ) $out[] = $control;
            $next = $data['next_offset'] ?? null;
            if ( null === $next || ! is_numeric( $next ) || absint( $next ) <= $offset ) break;
            $offset = absint( $next );
        }
        return array_slice( $out, 0, self::MAX_CONTROLS_PER_TOP );
    }

    private static function elements( int $id ): ?array {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return null;
        try {
            $doc = \Elementor\Plugin::$instance->documents->get( $id );
            $elements = $doc ? $doc->get_elements_data() : null;
            return is_array( $elements ) ? $elements : null;
        } catch ( Throwable $ignored ) { return null; }
    }

    private static function hash( array $elements ): string {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return hash( 'sha256', false === $json ? '' : $json );
    }

    private static function safe_error( $error, string $fallback ): string {
        if ( is_wp_error( $error ) ) {
            $message = trim( (string) $error->get_error_message() );
            if ( '' !== $message ) return function_exists( 'mb_substr' ) ? mb_substr( $message, 0, 400 ) : substr( $message, 0, 400 );
        }
        return $fallback;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_observation_convergence'] = true;
        $data['rendered_visual_observation_convergence_read_only'] = true;
        $data['rendered_visual_observation_convergence_same_element_property'] = true;
        $data['rendered_visual_observation_convergence_fresh_hash_guard'] = true;
        $data['rendered_visual_observation_convergence_exact_guarded_controls'] = true;
        $data['rendered_visual_observation_convergence_cta_reference_required'] = true;
        $data['rendered_visual_observation_convergence_defects_asserted'] = false;
        $data['rendered_visual_observation_convergence_automatic_write_allowed'] = false;
        $data['rendered_visual_observation_convergence_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observation_Convergence', false ) ) return;

final class Elementize_Convergence_Exact_Element_Recovery {
    private const VERSION = '0.23.4';
    private const NS = 'elementize/v1';
    private const MAX_TARGETS = 6;

    public static function init(): void {
        // Base convergence runs at 480. Recover exact per-element typography controls
        // immediately afterwards, before focused candidate selection begins at 483.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 481, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . self::NS . '/pages/(\d+)/completion-audit$#', $request->get_route(), $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $observations = is_array( $visual['render_observations'] ?? null ) ? $visual['render_observations'] : [];
        if ( empty( $conv['available'] ) || empty( $observations['available'] ) ) return $response;

        $page_id = absint( $m[1] ?? 0 );
        $hash = trim( (string) ( $conv['page_content_hash'] ?? '' ) );
        $original = is_array( $conv['promoted_targets'] ?? null ) ? $conv['promoted_targets'] : [];
        $mapped_before = absint( $conv['mapped_exact_control_count'] ?? 0 );
        $existing = self::existing_keys( $original );
        $recovered = [];
        $reads = 0;
        $micro_samples = 0;
        $failure = '';

        foreach ( is_array( $observations['observations'] ?? null ) ? $observations['observations'] : [] as $observation ) {
            if ( ! is_array( $observation ) || 'micro_text_present' !== (string) ( $observation['type'] ?? '' ) ) continue;
            if ( empty( $observation['measured'] ) || empty( $observation['visual_convergence'] ) ) continue;

            foreach ( is_array( $observation['samples'] ?? null ) ? $observation['samples'] : [] as $sample ) {
                if ( ! is_array( $sample ) ) continue;
                $micro_samples++;
                $top = trim( (string) ( $sample['top_level_element_id'] ?? '' ) );
                $element = trim( (string) ( $sample['element_id'] ?? '' ) );
                $rendered = is_numeric( $sample['font_size_px'] ?? null ) ? (float) $sample['font_size_px'] : null;
                if ( '' === $top || '' === $element || null === $rendered ) continue;

                $key = self::target_key( 'micro_text_present', $element, 'font_size', null );
                if ( isset( $existing[$key] ) ) continue;

                $read = self::design_controls_for_element( $page_id, $element, $hash );
                $reads++;
                if ( is_wp_error( $read ) ) {
                    $failure = self::safe_error( $read, 'Exact per-element design-control recovery failed.' );
                    break 2;
                }

                $match = self::match_font_size_control( $read, $element, $rendered );
                if ( ! is_array( $match ) ) continue;

                $identity = [
                    'page_content_hash' => $hash,
                    'observation_type' => 'micro_text_present',
                    'top_level_element_id' => $top,
                    'element_id' => (string) ( $match['element_id'] ?? '' ),
                    'setting_path' => is_array( $match['setting_path'] ?? null ) ? array_values( $match['setting_path'] ) : [],
                    'control_fingerprint' => (string) ( $match['control_fingerprint'] ?? '' ),
                    'rendered_property' => 'font_size',
                    'rendered_value' => $rendered,
                    'component' => null,
                ];
                $target_id = substr( hash( 'sha256', (string) wp_json_encode( $identity, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) ), 0, 20 );

                $recovered[] = [
                    'target_id' => $target_id,
                    'promotion_status' => 'promoted_read_only',
                    'observation_type' => 'micro_text_present',
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
                    'rendered_property' => 'font_size',
                    'rendered_value' => $rendered,
                    'matched_component' => null,
                    'visual_convergence' => true,
                    'exact_control_match' => true,
                    'eligible_for_bounded_planning_review' => true,
                    'defect_asserted' => false,
                    'automatic_write_allowed' => false,
                    'fresh_exact_read_required_before_any_write' => true,
                    'promotion_basis' => 'Deterministic rendered measurement + independent visual convergence + exact same-element guarded typography control recovered through a bounded element_id read.',
                    'exact_element_recovery' => true,
                ];
                $existing[$key] = true;
            }
        }

        $micro_existing = [];
        $other_existing = [];
        foreach ( $original as $target ) {
            if ( ! is_array( $target ) ) continue;
            if ( 'micro_text_present' === (string) ( $target['observation_type'] ?? '' ) ) $micro_existing[] = $target;
            else $other_existing[] = $target;
        }

        // Microtext is the first deterministic observation family. If its exact controls
        // had not been truncated by the top-level control scan, these targets would have
        // been encountered before padding. Reconstruct that intended ordering, then keep
        // the existing six-target budget unchanged.
        $ordered = array_merge( $micro_existing, $recovered, $other_existing );
        $final = array_slice( $ordered, 0, self::MAX_TARGETS );

        if ( $recovered ) {
            $conv['promoted_targets'] = $final;
            $conv['promoted_target_count'] = count( $final );
            $conv['mapped_exact_control_count'] = $mapped_before + count( $recovered );
        }

        $visual['repair_convergence'] = $conv;
        $visual['exact_element_convergence_recovery'] = [
            'version' => self::VERSION,
            'available' => '' === $failure,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'microtext_sample_count' => $micro_samples,
            'exact_element_read_count' => $reads,
            'recovered_exact_target_count' => count( $recovered ),
            'mapped_exact_control_count_before' => $mapped_before,
            'mapped_exact_control_count_after' => absint( $conv['mapped_exact_control_count'] ?? $mapped_before ),
            'promoted_target_count_before' => count( $original ),
            'promoted_target_count_after' => count( $final ),
            'target_budget_unchanged' => true,
            'target_budget' => self::MAX_TARGETS,
            'recovered_observation_types' => array_values( array_unique( array_map( static fn ( array $target ): string => (string) ( $target['observation_type'] ?? '' ), $recovered ) ) ),
            'failure_reason' => $failure ?: null,
            'action_slot_cost' => 0,
            'note' => 'Exact microtext controls are recovered with bounded element_id design-setting reads when the broader top-level control cache omits them. This is read-only mapping recovery; no write authority is granted.',
        ];
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function design_controls_for_element( int $id, string $element, string $hash ) {
        $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/design-settings' );
        foreach ( [ 'element_id' => $element, 'offset' => 0, 'limit' => 300 ] as $key => $value ) $request->set_param( $key, $value );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_exact_element_design_read_failed', 'Fresh exact-element design controls could not be read.' );
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return new WP_Error( 'elementize_exact_element_design_invalid', 'Fresh exact-element design-control read returned invalid data.' );
        $read_hash = trim( (string) ( $data['content_hash'] ?? '' ) );
        if ( '' !== $hash && '' !== $read_hash && ! hash_equals( $hash, $read_hash ) ) return new WP_Error( 'elementize_exact_element_stale_hash', 'The page changed while exact-element controls were being recovered.' );
        return is_array( $data['controls'] ?? null ) ? $data['controls'] : [];
    }

    private static function match_font_size_control( array $controls, string $element, float $rendered ): ?array {
        foreach ( $controls as $control ) {
            if ( ! is_array( $control ) ) continue;
            if ( (string) ( $control['element_id'] ?? '' ) !== $element ) continue;
            if ( 'typography' !== strtolower( (string) ( $control['category'] ?? '' ) ) ) continue;
            if ( 'base' !== strtolower( (string) ( $control['responsive_scope'] ?? 'base' ) ) ) continue;
            if ( empty( $control['writable_now'] ) ) continue;
            if ( '' === trim( (string) ( $control['control_fingerprint'] ?? '' ) ) ) continue;
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

    private static function existing_keys( array $targets ): array {
        $out = [];
        foreach ( $targets as $target ) {
            if ( ! is_array( $target ) ) continue;
            $out[ self::target_key(
                (string) ( $target['observation_type'] ?? '' ),
                (string) ( $target['element_id'] ?? '' ),
                (string) ( $target['rendered_property'] ?? '' ),
                $target['matched_component'] ?? null
            ) ] = true;
        }
        return $out;
    }

    private static function target_key( string $type, string $element, string $property, $component ): string {
        return strtolower( trim( $type ) ) . '|' . trim( $element ) . '|' . strtolower( trim( $property ) ) . '|' . strtolower( trim( (string) $component ) );
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
}

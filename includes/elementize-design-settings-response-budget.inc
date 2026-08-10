<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Settings_Response_Budget {
    private const VERSION = '0.10.1';
    private const NS = 'elementize/v1';
    private const MAX_GPT_CONTROLS = 75;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 245, 3 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/\d+/design-settings$#', $route ) ) return $response;
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;

        $summary_only = filter_var( $request->get_param( 'summary_only' ), FILTER_VALIDATE_BOOLEAN );
        $compact_param = $request->get_param( 'compact' );
        $compact = null === $compact_param ? true : filter_var( $compact_param, FILTER_VALIDATE_BOOLEAN );

        $controls = is_array( $data['controls'] ?? null ) ? array_values( $data['controls'] ) : [];
        $requested_count = count( $controls );
        $offset = isset( $data['filters']['offset'] ) ? absint( $data['filters']['offset'] ) : 0;
        $filtered_total = isset( $data['summary']['filtered_control_count'] ) ? absint( $data['summary']['filtered_control_count'] ) : $requested_count;

        if ( $summary_only ) {
            $controls = [];
            $next_offset = null;
        } else {
            if ( count( $controls ) > self::MAX_GPT_CONTROLS ) $controls = array_slice( $controls, 0, self::MAX_GPT_CONTROLS );
            if ( $compact ) $controls = array_map( [ self::class, 'compact_control' ], $controls );
            $next_offset = ( $offset + count( $controls ) < $filtered_total ) ? $offset + count( $controls ) : null;
        }

        $data['design_settings_version'] = self::VERSION;
        $data['response_budget'] = [
            'gpt_safe_response' => true,
            'compact_default' => true,
            'compact' => $compact,
            'summary_only' => $summary_only,
            'max_controls_per_response' => self::MAX_GPT_CONTROLS,
            'controls_before_response_budget' => $requested_count,
            'controls_returned' => count( $controls ),
            'guidance' => 'Start with summary_only=true or compact=true. Then filter by category/top_level_id/element_id/responsive_scope and follow next_offset instead of requesting the whole page at once.',
        ];
        if ( isset( $data['summary'] ) && is_array( $data['summary'] ) ) {
            $data['summary']['returned_control_count'] = count( $controls );
        }
        if ( isset( $data['filters'] ) && is_array( $data['filters'] ) ) {
            $data['filters']['compact'] = $compact;
            $data['filters']['summary_only'] = $summary_only;
        }
        $data['controls'] = $controls;
        $data['next_offset'] = $next_offset;
        $response->set_data( $data );
        return $response;
    }

    public static function compact_control( $control ): array {
        if ( ! is_array( $control ) ) return [];
        $keys = [
            'top_level_id', 'element_id', 'widget_type', 'category', 'control_key', 'base_control_key',
            'setting_path', 'responsive_scope', 'source', 'value_shape', 'unit', 'value',
            'writer_candidate', 'control_fingerprint',
        ];
        $out = [];
        foreach ( $keys as $key ) if ( array_key_exists( $key, $control ) ) $out[ $key ] = $control[ $key ];
        if ( isset( $control['source'] ) && 'explicit' !== $control['source'] && array_key_exists( 'source_reference', $control ) ) {
            $out['source_reference'] = $control['source_reference'];
        }
        return $out;
    }

    private static function status( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_settings_response_budget'] = true;
        $data['design_settings_compact_default'] = true;
        $data['design_settings_summary_only'] = true;
        $data['design_settings_gpt_max_controls_per_response'] = self::MAX_GPT_CONTROLS;
        $response->set_data( $data );
        return $response;
    }
}

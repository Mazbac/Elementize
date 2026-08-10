<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observation_Convergence', false ) ) return;

final class Elementize_Focused_Candidate_Diversity {
    private const VERSION = '0.23.2';
    private const NS = 'elementize/v1';

    public static function init(): void {
        // Base convergence runs at 480 and focused verification at 485.
        // Reorder only the read-only broad target list between them so the bounded
        // focused pass does not spend every slot on one observation family.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 482, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . self::NS . '/pages/(\\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $targets = is_array( $conv['promoted_targets'] ?? null ) ? $conv['promoted_targets'] : [];

        $diag = [
            'version' => self::VERSION,
            'available' => ! empty( $targets ),
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'target_count' => count( $targets ),
            'observation_types_before' => self::types( $targets ),
            'reordered' => false,
            'priority_observation_types' => [ 'large_internal_padding_present', 'micro_text_present' ],
            'action_slot_cost' => 0,
        ];

        if ( count( $targets ) > 1 ) {
            $ordered = self::diverse_order( $targets );
            if ( self::ids( $ordered ) !== self::ids( $targets ) ) {
                $conv['promoted_targets'] = $ordered;
                $visual['repair_convergence'] = $conv;
                $diag['reordered'] = true;
            }
            $diag['observation_types_after'] = self::types( $ordered );
        } else {
            $diag['observation_types_after'] = $diag['observation_types_before'];
        }

        $diag['note'] = 'Only target order is changed so the bounded focused visual verifier can sample more than one deterministic observation family. Target membership, measurements, hashes, fingerprints, planning eligibility, and write authority are unchanged.';
        $visual['focused_candidate_diversity'] = $diag;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function diverse_order( array $targets ): array {
        $priority = [ 'large_internal_padding_present', 'micro_text_present' ];
        $selected = [];
        $selected_ids = [];

        foreach ( $priority as $type ) {
            foreach ( $targets as $target ) {
                if ( ! is_array( $target ) ) continue;
                if ( $type !== strtolower( trim( (string) ( $target['observation_type'] ?? '' ) ) ) ) continue;
                $key = self::key( $target );
                if ( isset( $selected_ids[$key] ) ) continue;
                $selected[] = $target;
                $selected_ids[$key] = true;
                break;
            }
        }

        foreach ( $targets as $target ) {
            if ( ! is_array( $target ) ) continue;
            $key = self::key( $target );
            if ( isset( $selected_ids[$key] ) ) continue;
            $selected[] = $target;
            $selected_ids[$key] = true;
        }
        return $selected;
    }

    private static function key( array $target ): string {
        $id = trim( (string) ( $target['target_id'] ?? '' ) );
        if ( '' !== $id ) return 'id:' . $id;
        return 'fallback:' . md5( wp_json_encode( [
            $target['observation_type'] ?? '', $target['top_level_element_id'] ?? '',
            $target['element_id'] ?? '', $target['matched_component'] ?? '',
        ] ) ?: '' );
    }

    private static function ids( array $targets ): array {
        return array_map( static function ( $target ): string {
            return is_array( $target ) ? self::key( $target ) : '';
        }, $targets );
    }

    private static function types( array $targets ): array {
        $out = [];
        foreach ( $targets as $target ) {
            if ( ! is_array( $target ) ) continue;
            $type = strtolower( trim( (string) ( $target['observation_type'] ?? '' ) ) );
            if ( '' !== $type ) $out[] = $type;
        }
        return array_values( array_unique( $out ) );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }
}

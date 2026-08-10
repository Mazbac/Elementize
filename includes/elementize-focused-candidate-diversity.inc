<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Rendered_Observation_Convergence', false ) ) return;

final class Elementize_Focused_Candidate_Diversity {
    private const VERSION = '0.23.3';
    private const NS = 'elementize/v1';
    private const MAX_FOCUSED_CANDIDATES = 4;
    private const STASH_KEY = '_elementize_focused_diversity_original_targets';
    private const META_KEY = '_elementize_focused_diversity_meta';

    public static function init(): void {
        // Base convergence runs at 480 and focused verification at 485.
        // Temporarily narrow promoted_targets to a diverse bounded set only for the
        // focused verifier, then restore the exact original list before section
        // hardening (490). This avoids changing final convergence membership.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'before_focused' ], 483, 3 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_focused' ], 487, 3 );
    }

    public static function before_focused( $response, $handler, WP_REST_Request $request ) {
        if ( ! self::eligible_request( $response, $request ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $targets = is_array( $conv['promoted_targets'] ?? null ) ? $conv['promoted_targets'] : [];
        if ( ! $targets ) return $response;

        $selected = self::diverse_limit( $targets, self::MAX_FOCUSED_CANDIDATES );
        $visual[self::STASH_KEY] = $targets;
        $visual[self::META_KEY] = [
            'version' => self::VERSION,
            'target_count' => count( $targets ),
            'selected_count' => count( $selected ),
            'observation_types_before' => self::types( $targets ),
            'observation_types_selected' => self::types( $selected ),
            'selected_target_ids' => self::ids( $selected ),
        ];

        $conv['promoted_targets'] = $selected;
        $visual['repair_convergence'] = $conv;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    public static function after_focused( $response, $handler, WP_REST_Request $request ) {
        if ( ! self::eligible_request( $response, $request ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];
        $original = is_array( $visual[self::STASH_KEY] ?? null ) ? $visual[self::STASH_KEY] : [];
        $meta = is_array( $visual[self::META_KEY] ?? null ) ? $visual[self::META_KEY] : [];
        if ( ! $original ) return $response;

        $conv = is_array( $visual['repair_convergence'] ?? null ) ? $visual['repair_convergence'] : [];
        $conv['promoted_targets'] = $original;
        $visual['repair_convergence'] = $conv;
        unset( $visual[self::STASH_KEY], $visual[self::META_KEY] );

        $visual['focused_candidate_diversity'] = [
            'version' => self::VERSION,
            'available' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'target_count' => (int) ( $meta['target_count'] ?? count( $original ) ),
            'selected_count' => (int) ( $meta['selected_count'] ?? 0 ),
            'candidate_limit' => self::MAX_FOCUSED_CANDIDATES,
            'observation_types_before' => is_array( $meta['observation_types_before'] ?? null ) ? $meta['observation_types_before'] : self::types( $original ),
            'observation_types_selected' => is_array( $meta['observation_types_selected'] ?? null ) ? $meta['observation_types_selected'] : [],
            'selected_target_ids' => is_array( $meta['selected_target_ids'] ?? null ) ? $meta['selected_target_ids'] : [],
            'temporary_substitution_restored' => true,
            'final_convergence_membership_changed' => false,
            'measurements_changed' => false,
            'hashes_or_fingerprints_changed' => false,
            'planning_eligibility_changed' => false,
            'action_slot_cost' => 0,
            'note' => 'A diverse bounded target subset was exposed only during the focused verifier callback window. The exact original convergence target list was restored before section hardening, so final convergence evidence and write authority are unchanged.',
        ];

        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function diverse_limit( array $targets, int $limit ): array {
        if ( $limit < 1 || count( $targets ) <= $limit ) return $targets;

        $priority = [ 'large_internal_padding_present', 'micro_text_present' ];
        $selected = [];
        $seen = [];

        // Reserve one slot per supported observation family when available.
        foreach ( $priority as $type ) {
            foreach ( $targets as $target ) {
                if ( ! is_array( $target ) ) continue;
                if ( $type !== strtolower( trim( (string) ( $target['observation_type'] ?? '' ) ) ) ) continue;
                $key = self::key( $target );
                if ( isset( $seen[$key] ) ) continue;
                $selected[] = $target;
                $seen[$key] = true;
                break;
            }
        }

        // Fill the remaining budget in the original convergence order.
        foreach ( $targets as $target ) {
            if ( count( $selected ) >= $limit ) break;
            if ( ! is_array( $target ) ) continue;
            $key = self::key( $target );
            if ( isset( $seen[$key] ) ) continue;
            $selected[] = $target;
            $seen[$key] = true;
        }

        return array_slice( $selected, 0, $limit );
    }

    private static function eligible_request( $response, WP_REST_Request $request ): bool {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return false;
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return false;
        if ( ! preg_match( '#^/' . self::NS . '/pages/(\d+)/completion-audit$#', $request->get_route() ) ) return false;
        if ( $response->get_status() >= 400 ) return false;
        return self::truthy( $request->get_param( 'include_visual' ) );
    }

    private static function key( array $target ): string {
        $id = trim( (string) ( $target['target_id'] ?? '' ) );
        if ( '' !== $id ) return 'id:' . $id;
        return 'fallback:' . md5( wp_json_encode( [
            $target['observation_type'] ?? '',
            $target['top_level_element_id'] ?? '',
            $target['element_id'] ?? '',
            $target['matched_component'] ?? '',
        ] ) ?: '' );
    }

    private static function ids( array $targets ): array {
        $out = [];
        foreach ( $targets as $target ) {
            if ( ! is_array( $target ) ) continue;
            $out[] = (string) ( $target['target_id'] ?? '' );
        }
        return array_values( array_filter( $out, static fn ( string $id ): bool => '' !== $id ) );
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

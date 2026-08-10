<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Judgment', false ) ) return;

/**
 * Exact-state last-known-good grounded-context stability for contextual A/B judgment.
 *
 * Local visual reassessment can be nondeterministic even at temperature 0. A fresh
 * completion audit may therefore fail a model-facing grounding contract for an
 * unchanged Elementor page even though a prior audit already produced and cached a
 * fully accepted grounded context for that exact page state.
 *
 * This pass never weakens grounding acceptance. It runs only after the base A/B
 * context decorator and both fresh-source recovery layers have left context
 * unavailable. It may reuse only an unexpired transient whose page ID and exact
 * _elementor_data SHA-256 match the current page and whose compact section ledger is
 * sequential and transition-valid. Only already-grounded source types are accepted.
 *
 * Read-only: this class never mutates Elementor or page state.
 */
final class Elementize_Aesthetic_AB_Context_Cache_Stability {
    private const VERSION = '0.27.5-k1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 577, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $page_id = absint( $m[1] ?? 0 );
        if ( $page_id < 1 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) ) return $response;
        $visual = $data['visual'];
        $base = is_array( $visual['aesthetic_ab_context'] ?? null ) ? $visual['aesthetic_ab_context'] : [];
        if ( ! empty( $base['available'] ) ) return $response;

        $state_hash = self::page_state_hash( $page_id );
        $diag = [
            'version' => self::VERSION,
            'attempted' => true,
            'succeeded' => false,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'requires_exact_page_state_hash' => true,
            'current_page_state_hash' => $state_hash,
        ];

        if ( '' === $state_hash ) {
            $diag['reason'] = 'The current Elementor page state could not be fingerprinted for exact-state cache recovery.';
            return self::attach_failure( $response, $data, $visual, $base, $diag );
        }

        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) ) {
            $diag['reason'] = 'No unexpired previously accepted grounded A/B context exists for the exact current page state.';
            return self::attach_failure( $response, $data, $visual, $base, $diag );
        }

        $validation = self::validate_snapshot( $snapshot, $page_id, $state_hash );
        $diag['validation'] = $validation;
        if ( empty( $validation['valid'] ) ) {
            $diag['reason'] = 'The previously cached A/B context did not satisfy exact-state grounded snapshot validation.';
            return self::attach_failure( $response, $data, $visual, $base, $diag );
        }

        $diag['succeeded'] = true;
        $diag['reason'] = null;
        $diag['cached_grounding_source'] = (string) ( $snapshot['grounding_source'] ?? '' );
        $diag['cached_captured_gmt'] = (string) ( $snapshot['captured_gmt'] ?? '' );
        $diag['marker_count'] = count( (array) ( $snapshot['section_ledger'] ?? [] ) );
        $diag['policy_note'] = 'A prior accepted grounded context was reused only because its exact Elementor state hash still matches. No failed fresh model judgment was promoted or trusted.';

        $base['version'] = (string) ( $base['version'] ?? '0.27.0-a1' );
        $base['available'] = true;
        $base['read_only'] = true;
        $base['writes_performed'] = false;
        $base['automatic_write_allowed'] = false;
        $base['action_slot_cost'] = 0;
        $base['page_id'] = $page_id;
        $base['page_state_hash'] = $state_hash;
        $base['captured_gmt'] = (string) ( $snapshot['captured_gmt'] ?? '' );
        $base['grounding_source'] = (string) ( $snapshot['grounding_source'] ?? '' );
        $base['marker_count'] = count( (array) ( $snapshot['section_ledger'] ?? [] ) );
        $base['context_evidence_complete'] = true;
        $base['reused_last_known_good_exact_state_context'] = true;
        $base['context_cache_stability'] = $diag;
        $base['note'] = 'Fresh audit grounding was unstable, so Elementize reused an unexpired previously accepted grounded context for the exact unchanged Elementor state.';
        unset( $base['reason'] );

        $visual['aesthetic_ab_context'] = $base;
        $data['visual'] = $visual;
        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_ab_context_ready'] = true;
        $data['summary']['aesthetic_ab_context_reused_from_exact_state_cache'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function attach_failure( WP_REST_Response $response, array $data, array $visual, array $base, array $diag ): WP_REST_Response {
        $base['context_cache_stability'] = $diag;
        $visual['aesthetic_ab_context'] = $base;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function validate_snapshot( array $snapshot, int $page_id, string $state_hash ): array {
        $source = (string) ( $snapshot['grounding_source'] ?? '' );
        $allowed_sources = [
            'fact_conditioned_grounded_reassessment',
            'fact_conditioned_grounded_reassessment_context_hardening',
            'deterministic_semantic_grounding_context_stabilizer',
            'semantic_grounding',
        ];
        $rows = is_array( $snapshot['section_ledger'] ?? null ) ? array_values( $snapshot['section_ledger'] ) : [];
        $sequential = ! empty( $rows );
        $transitions_valid = ! empty( $rows );

        foreach ( $rows as $index => $row ) {
            if ( ! is_array( $row ) ) { $sequential = false; $transitions_valid = false; break; }
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            if ( 'S' . ( $index + 1 ) !== $marker ) $sequential = false;
            $transition = strtolower( trim( (string) ( $row['transition_from_previous'] ?? '' ) ) );
            $ok = 0 === $index ? 'n/a' === $transition : in_array( $transition, [ 'smooth', 'neutral', 'abrupt', 'repetitive' ], true );
            if ( ! $ok ) $transitions_valid = false;
        }

        $checks = [
            'snapshot_page_id_matches' => absint( $snapshot['page_id'] ?? 0 ) === $page_id,
            'snapshot_state_hash_matches' => isset( $snapshot['page_state_hash'] ) && is_string( $snapshot['page_state_hash'] ) && hash_equals( $state_hash, $snapshot['page_state_hash'] ),
            'grounding_source_allowed' => in_array( $source, $allowed_sources, true ),
            'section_ledger_present' => ! empty( $rows ),
            'section_markers_sequential' => $sequential,
            'section_transitions_valid' => $transitions_valid,
        ];
        $checks['valid'] = ! in_array( false, array_values( $checks ), true );
        return $checks;
    }

    private static function page_state_hash( int $page_id ): string {
        $raw = get_post_meta( $page_id, '_elementor_data', true );
        if ( is_string( $raw ) ) return hash( 'sha256', $raw );
        $json = wp_json_encode( $raw, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) ? hash( 'sha256', $json ) : '';
    }

    private static function cache_key( int $page_id, string $hash ): string {
        return 'elementize_ab_' . $page_id . '_' . substr( preg_replace( '/[^a-f0-9]/i', '', $hash ), 0, 24 );
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_ab_context_cache_stability_version'] = self::VERSION;
        $data['aesthetic_ab_context_cache_stability'] = true;
        $data['aesthetic_ab_context_cache_stability_read_only'] = true;
        $data['aesthetic_ab_context_cache_stability_exact_state_only'] = true;
        $data['aesthetic_ab_context_cache_stability_action_slot_cost'] = 0;
        $data['aesthetic_ab_context_cache_stability_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

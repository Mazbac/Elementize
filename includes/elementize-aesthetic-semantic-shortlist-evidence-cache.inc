<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Semantic_Shortlist_Hardening', false ) ) return;

/**
 * Exact-state cache for the final hardened semantic shortlist.
 * Read-only evidence bridge between the shortlist request and later visual ranking.
 */
final class Elementize_Aesthetic_Semantic_Shortlist_Evidence_Cache {
    private const VERSION = '0.28.2-c1';
    private const NS = 'elementize/v1';
    private const TTL = 900;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 640, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/templates' !== $route || 'GET' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'semantic_shortlist' ) ) ) return $response;

        $data = $response->get_data();
        $shortlist = is_array( $data['semantic_shortlist'] ?? null ) ? $data['semantic_shortlist'] : [];
        if ( empty( $shortlist['available'] ) || empty( $shortlist['usable_for_visual_ranking'] ) ) return $response;
        if ( empty( $shortlist['read_only'] ) || ! empty( $shortlist['writes_performed'] ) || ! empty( $shortlist['automatic_write_allowed'] ) ) return $response;

        $page_id = absint( $shortlist['page_id'] ?? $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) ( $shortlist['target_marker'] ?? $request->get_param( 'target_marker' ) ) ) );
        $hash = strtolower( trim( (string) ( $shortlist['page_state_hash'] ?? '' ) ) );
        $candidates = array_values( array_filter( (array) ( $shortlist['candidates'] ?? [] ), 'is_array' ) );
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) || ! preg_match( '/^[a-f0-9]{64}$/', $hash ) || count( $candidates ) < 2 || count( $candidates ) > 4 ) return $response;

        $rows = [];
        foreach ( $candidates as $candidate ) {
            $id = sanitize_key( (string) ( $candidate['template_id'] ?? '' ) );
            $score = absint( $candidate['semantic_fit_score'] ?? 0 );
            if ( '' === $id || $score < 1 || $score > 100 || empty( $candidate['eligible'] ) ) continue;
            $rows[] = [
                'template_id' => $id,
                'semantic_fit_score' => $score,
                'structural_match' => sanitize_key( (string) ( $candidate['structural_match'] ?? '' ) ),
                'dependency_risk' => sanitize_key( (string) ( $candidate['dependency_risk'] ?? '' ) ),
                'role_requirements_met' => ! empty( $candidate['role_requirements_met'] ),
            ];
        }
        if ( count( $rows ) < 2 || count( $rows ) !== count( $candidates ) ) return $response;

        $evidence = [
            'version' => self::VERSION,
            'source_shortlist_version' => (string) ( $shortlist['version'] ?? '' ),
            'page_id' => $page_id,
            'target_marker' => $marker,
            'page_state_hash' => $hash,
            'target_role' => sanitize_key( (string) ( $shortlist['target_role'] ?? '' ) ),
            'candidates' => $rows,
            'captured_gmt' => gmdate( 'c' ),
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
        ];
        set_transient( self::cache_key( $page_id, $hash, $marker ), $evidence, self::TTL );

        $shortlist['semantic_evidence_cache'] = [
            'version' => self::VERSION,
            'cached' => true,
            'ttl_seconds' => self::TTL,
            'candidate_count' => count( $rows ),
            'exact_state_keyed' => true,
            'read_only' => true,
        ];
        $data['semantic_shortlist'] = $shortlist;
        $response->set_data( $data );
        return $response;
    }

    public static function get( int $page_id, string $hash, string $marker ): ?array {
        $value = get_transient( self::cache_key( $page_id, strtolower( $hash ), strtoupper( $marker ) ) );
        return is_array( $value ) ? $value : null;
    }

    private static function cache_key( int $page_id, string $hash, string $marker ): string {
        return 'elementize_semantic_shortlist_' . $page_id . '_' . substr( preg_replace( '/[^a-f0-9]/i', '', $hash ), 0, 20 ) . '_' . strtolower( preg_replace( '/[^a-z0-9]/i', '', $marker ) );
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_semantic_evidence_cache_version'] = self::VERSION;
        $data['aesthetic_semantic_evidence_cache'] = true;
        $data['aesthetic_semantic_evidence_cache_ttl_seconds'] = self::TTL;
        $data['aesthetic_semantic_evidence_cache_exact_state_keyed'] = true;
        $data['aesthetic_semantic_evidence_cache_read_only'] = true;
        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Visual_Repair_Discovery {
    private const VERSION = '0.16.0';
    private const NS = 'elementize/v1';
    private const MAX_ELIGIBLE_FINDINGS = 6;
    private const MAX_CANDIDATES_PER_FINDING = 10;
    private const MAX_DESIGN_CONTROLS_PER_SECTION = 150;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 360, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual']['localization']['available'] ) ) return $response;
        $localized = is_array( $data['visual']['localization']['localized_findings'] ?? null ) ? $data['visual']['localization']['localized_findings'] : [];
        $eligible = [];
        foreach ( $localized as $item ) {
            if ( ! is_array( $item ) || empty( $item['usable_for_repair_discovery'] ) || 'localized' !== (string) ( $item['localization_status'] ?? '' ) ) continue;
            $eligible[] = $item;
            if ( count( $eligible ) >= self::MAX_ELIGIBLE_FINDINGS ) break;
        }
        $data['visual']['repair_discovery'] = self::discover( absint( $m[1] ?? 0 ), $eligible );
        $response->set_data( $data );
        return $response;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function discover( int $id, array $eligible ): array {
        $base = [
            'repair_discovery_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'advisory_only' => true,
            'localized_findings_only' => true,
            'medium_high_confidence_only' => true,
            'exact_existing_targets_only' => true,
            'fresh_read_required_before_any_write' => true,
            'candidate_limit_per_finding' => self::MAX_CANDIDATES_PER_FINDING,
            'eligible_finding_count' => count( $eligible ),
            'findings' => [],
        ];
        if ( ! $eligible ) {
            $base['available'] = true;
            $base['note'] = 'No localization-valid medium/high-confidence findings were eligible for repair discovery.';
            return $base;
        }

        $elements = self::elements( $id );
        if ( ! is_array( $elements ) ) { $base['reason'] = 'Fresh Elementor page data could not be read for repair discovery.'; return $base; }
        $hash = self::hash( $elements );
        $element_to_top = [];
        self::map_element_tree( $elements, '', $element_to_top );

        $visual = self::rest_get( '/' . self::NS . '/pages/' . $id . '/visual-settings', [ 'active' => true, 'writable' => true, 'limit' => 200, 'compact' => true ] );
        if ( is_wp_error( $visual ) ) { $base['reason'] = self::safe_error( $visual, 'Fresh visual settings could not be read.' ); $base['stage'] = 'visual_settings'; return $base; }
        if ( '' !== (string) ( $visual['content_hash'] ?? '' ) && ! hash_equals( $hash, (string) $visual['content_hash'] ) ) { $base['reason'] = 'The page changed while repair candidates were being discovered. Re-run the completion audit.'; $base['stage'] = 'stale_visual_settings'; return $base; }

        $links = self::rest_get( '/' . self::NS . '/pages/' . $id . '/links' );
        if ( is_wp_error( $links ) ) { $base['reason'] = self::safe_error( $links, 'Fresh page links could not be read.' ); $base['stage'] = 'links'; return $base; }
        if ( '' !== (string) ( $links['content_hash'] ?? '' ) && ! hash_equals( $hash, (string) $links['content_hash'] ) ) { $base['reason'] = 'The page changed while link targets were being discovered. Re-run the completion audit.'; $base['stage'] = 'stale_links'; return $base; }

        $design_cache = [];
        $all_visual = is_array( $visual['visual_items'] ?? null ) ? $visual['visual_items'] : [];
        $all_links = is_array( $links['links'] ?? null ) ? $links['links'] : [];
        $total_candidates = 0;

        foreach ( $eligible as $finding ) {
            $category = strtolower( trim( (string) ( $finding['finding_category'] ?? '' ) ) );
            $tops = array_values( array_unique( array_filter( array_map( 'strval', is_array( $finding['top_level_element_ids'] ?? null ) ? $finding['top_level_element_ids'] : [] ) ) ) );
            $candidates = [];

            foreach ( $tops as $top ) {
                if ( ! isset( $design_cache[$top] ) ) {
                    $read = self::design_controls_for_top( $id, $top, $hash );
                    if ( is_wp_error( $read ) ) { $design_cache[$top] = $read; }
                    else $design_cache[$top] = $read;
                }
                $controls = $design_cache[$top];
                if ( is_wp_error( $controls ) ) continue;
                foreach ( $controls as $control ) {
                    if ( ! is_array( $control ) || empty( $control['writable_now'] ) ) continue;
                    $score = self::design_match_score( $category, $control );
                    if ( $score < 1 ) continue;
                    $candidates[] = self::design_candidate( $control, $top, $score );
                }
            }

            foreach ( $all_visual as $item ) {
                if ( ! is_array( $item ) || empty( $item['writable'] ) || empty( $item['active'] ) ) continue;
                $element_id = (string) ( $item['element_id'] ?? '' );
                $top = (string) ( $element_to_top[$element_id] ?? '' );
                if ( '' === $top || ! in_array( $top, $tops, true ) ) continue;
                $score = self::visual_match_score( $category, $item );
                if ( $score < 1 ) continue;
                $candidates[] = self::visual_candidate( $item, $top, $score );
            }

            if ( self::finding_can_use_link_target( $category ) ) {
                foreach ( $all_links as $link ) {
                    if ( ! is_array( $link ) ) continue;
                    $top = (string) ( $link['top_level_element_id'] ?? '' );
                    if ( '' === $top || ! in_array( $top, $tops, true ) ) continue;
                    if ( empty( $link['cta_like'] ) && ! str_contains( $category, 'link' ) && ! str_contains( $category, 'destination' ) ) continue;
                    $candidates[] = self::link_candidate( $link, $top );
                }
            }

            $candidates = self::dedupe_and_sort( $candidates );
            $candidates = array_slice( $candidates, 0, self::MAX_CANDIDATES_PER_FINDING );
            $total_candidates += count( $candidates );
            $base['findings'][] = [
                'finding_index' => absint( $finding['finding_index'] ?? 0 ),
                'finding_category' => $category,
                'localization_confidence' => (string) ( $finding['confidence'] ?? '' ),
                'localized_top_level_element_ids' => $tops,
                'candidate_count' => count( $candidates ),
                'repair_candidates' => $candidates,
                'candidate_note' => $candidates ? 'These are plausible existing writable targets inside the localized section(s), not approved edits.' : 'No current guarded writer target plausibly matched this localized finding.',
            ];
        }

        $base['available'] = true;
        $base['page_content_hash'] = $hash;
        $base['source_hashes_match'] = true;
        $base['total_candidate_count'] = $total_candidates;
        $base['writer_routes'] = [
            'design' => 'POST /' . self::NS . '/pages/{id}/design-settings',
            'visual' => 'POST /' . self::NS . '/pages/{id}/visual-settings',
            'links' => 'POST /' . self::NS . '/pages/{id}/links',
        ];
        $base['note'] = 'Repair discovery is read-only. Re-read the exact target immediately before any write and preserve all existing hash, expected-value, fingerprint, revision, verification, and rollback guards.';
        return $base;
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

    private static function map_element_tree( array $nodes, string $top, array &$map ): void {
        foreach ( $nodes as $node ) {
            if ( ! is_array( $node ) ) continue;
            $id = (string) ( $node['id'] ?? '' );
            $resolved_top = '' !== $top ? $top : $id;
            if ( '' !== $id ) $map[$id] = $resolved_top;
            if ( ! empty( $node['elements'] ) && is_array( $node['elements'] ) ) self::map_element_tree( $node['elements'], $resolved_top, $map );
        }
    }

    private static function rest_get( string $route, array $params = [] ) {
        $request = new WP_REST_Request( 'GET', $route );
        foreach ( $params as $key => $value ) $request->set_param( $key, $value );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) {
            $message = $response instanceof WP_REST_Response ? (string) ( $response->get_data()['message'] ?? 'Internal read failed.' ) : 'Internal read failed.';
            return new WP_Error( 'elementize_repair_discovery_read_failed', $message );
        }
        $data = $response->get_data();
        return is_array( $data ) ? $data : new WP_Error( 'elementize_repair_discovery_invalid_read', 'Internal read returned invalid data.' );
    }

    private static function design_controls_for_top( int $id, string $top, string $hash ) {
        $out = [];
        $offset = 0;
        while ( count( $out ) < self::MAX_DESIGN_CONTROLS_PER_SECTION ) {
            $data = self::rest_get( '/' . self::NS . '/pages/' . $id . '/design-settings', [ 'top_level_id' => $top, 'offset' => $offset, 'limit' => 75, 'compact' => true ] );
            if ( is_wp_error( $data ) ) return $data;
            $read_hash = (string) ( $data['content_hash'] ?? '' );
            if ( '' !== $read_hash && ! hash_equals( $hash, $read_hash ) ) return new WP_Error( 'elementize_repair_discovery_stale_design', 'The page changed while design controls were being discovered.' );
            foreach ( is_array( $data['controls'] ?? null ) ? $data['controls'] : [] as $control ) $out[] = $control;
            $next = $data['next_offset'] ?? null;
            if ( null === $next || ! is_numeric( $next ) || absint( $next ) <= $offset ) break;
            $offset = absint( $next );
        }
        return array_slice( $out, 0, self::MAX_DESIGN_CONTROLS_PER_SECTION );
    }

    private static function design_match_score( string $finding, array $control ): int {
        $cat = (string) ( $control['category'] ?? '' );
        $widget = strtolower( (string) ( $control['widget_type'] ?? '' ) );
        $path = strtolower( implode( '.', array_map( 'strval', is_array( $control['setting_path'] ?? null ) ? $control['setting_path'] : [] ) ) );
        $button_like = (bool) preg_match( '/button|btn|cta|call[-_ ]?to[-_ ]?action/', $widget . ' ' . $path );
        $heading_like = (bool) preg_match( '/heading|headline|title/', $widget . ' ' . $path );

        if ( str_contains( $finding, 'typograph' ) || str_contains( $finding, 'readab' ) || str_contains( $finding, 'legib' ) ) return 'typography' === $cat ? 9 + ( $heading_like ? 1 : 0 ) : 0;
        if ( str_contains( $finding, 'whitespace' ) || str_contains( $finding, 'spacing' ) || str_contains( $finding, 'rhythm' ) ) return 'spacing' === $cat ? 9 : 0;
        if ( str_contains( $finding, 'hierarchy' ) ) {
            if ( 'typography' === $cat ) return 8 + ( $heading_like ? 2 : 0 );
            if ( 'spacing' === $cat ) return 6;
            if ( 'alignment' === $cat ) return 5;
            return 0;
        }
        if ( str_contains( $finding, 'cta' ) ) {
            if ( ! $button_like ) return 0;
            if ( 'border_radius' === $cat ) return 8;
            if ( 'spacing' === $cat ) return 7;
            if ( 'typography' === $cat ) return 7;
            if ( 'alignment' === $cat ) return 6;
            return 0;
        }
        if ( str_contains( $finding, 'section' ) || str_contains( $finding, 'balance' ) || str_contains( $finding, 'flow' ) ) {
            if ( 'spacing' === $cat ) return 8;
            if ( 'alignment' === $cat ) return 6;
        }
        if ( str_contains( $finding, 'cohesion' ) || str_contains( $finding, 'consisten' ) ) {
            if ( 'border_radius' === $cat ) return 5;
            if ( 'spacing' === $cat ) return 4;
        }
        return 0;
    }

    private static function visual_match_score( string $finding, array $item ): int {
        $kind = strtolower( (string) ( $item['kind'] ?? '' ) );
        $widget = strtolower( (string) ( $item['widget_type'] ?? '' ) );
        $path = strtolower( implode( '.', array_map( 'strval', is_array( $item['setting_path'] ?? null ) ? $item['setting_path'] : [] ) ) );
        $button_like = (bool) preg_match( '/button|btn|cta|call[-_ ]?to[-_ ]?action/', $widget . ' ' . $path );
        if ( ( str_contains( $finding, 'imagery' ) || str_contains( $finding, 'image' ) || str_contains( $finding, 'media' ) ) && 'media' === $kind ) return 10;
        if ( str_contains( $finding, 'cta' ) && 'color' === $kind && $button_like ) return 9;
        if ( ( str_contains( $finding, 'cohesion' ) || str_contains( $finding, 'color' ) || str_contains( $finding, 'brand' ) ) && 'color' === $kind ) return 7;
        if ( str_contains( $finding, 'hierarchy' ) && 'color' === $kind ) return 4;
        return 0;
    }

    private static function finding_can_use_link_target( string $finding ): bool {
        return str_contains( $finding, 'destination' ) || str_contains( $finding, 'link' ) || str_contains( $finding, 'navigation' );
    }

    private static function design_candidate( array $control, string $top, int $score ): array {
        return [
            'candidate_type' => 'design_control',
            'score' => $score,
            'top_level_element_id' => $top,
            'element_id' => (string) ( $control['element_id'] ?? '' ),
            'widget_type' => $control['widget_type'] ?? null,
            'category' => (string) ( $control['category'] ?? '' ),
            'control_key' => (string) ( $control['control_key'] ?? '' ),
            'setting_path' => is_array( $control['setting_path'] ?? null ) ? array_values( $control['setting_path'] ) : [],
            'responsive_scope' => (string) ( $control['responsive_scope'] ?? 'base' ),
            'current_value' => $control['value'] ?? null,
            'control_fingerprint' => (string) ( $control['control_fingerprint'] ?? '' ),
            'writable_now' => true,
            'writer_route' => 'POST /' . self::NS . '/pages/{id}/design-settings',
            'why_plausible' => 'Existing guarded design control inside the localized section matches this finding category.',
        ];
    }

    private static function visual_candidate( array $item, string $top, int $score ): array {
        return [
            'candidate_type' => 'visual_setting',
            'score' => $score,
            'top_level_element_id' => $top,
            'element_id' => (string) ( $item['element_id'] ?? '' ),
            'widget_type' => $item['widget_type'] ?? null,
            'kind' => (string) ( $item['kind'] ?? '' ),
            'setting_path' => is_array( $item['setting_path'] ?? null ) ? array_values( $item['setting_path'] ) : [],
            'current_value' => $item['value'] ?? null,
            'active' => true,
            'writable' => true,
            'writer_route' => 'POST /' . self::NS . '/pages/{id}/visual-settings',
            'why_plausible' => 'Existing guarded visual target inside the localized section matches this finding category.',
        ];
    }

    private static function link_candidate( array $link, string $top ): array {
        return [
            'candidate_type' => 'link_target',
            'score' => 6,
            'top_level_element_id' => $top,
            'element_id' => (string) ( $link['element_id'] ?? '' ),
            'widget_type' => $link['widget_type'] ?? null,
            'setting_path' => is_array( $link['setting_path'] ?? null ) ? array_values( $link['setting_path'] ) : [],
            'current_value' => (string) ( $link['current_value'] ?? $link['value'] ?? '' ),
            'cta_like' => (bool) ( $link['cta_like'] ?? false ),
            'writer_route' => 'POST /' . self::NS . '/pages/{id}/links',
            'why_plausible' => 'Existing guarded link target inside the localized section matches a link/destination finding.',
        ];
    }

    private static function dedupe_and_sort( array $items ): array {
        $seen = []; $out = [];
        foreach ( $items as $item ) {
            if ( ! is_array( $item ) ) continue;
            $key = (string) ( $item['candidate_type'] ?? '' ) . '|' . (string) ( $item['element_id'] ?? '' ) . '|' . wp_json_encode( $item['setting_path'] ?? [] );
            if ( isset( $seen[$key] ) ) continue;
            $seen[$key] = true;
            $out[] = $item;
        }
        usort( $out, static function( $a, $b ) {
            $score = (int) ( $b['score'] ?? 0 ) <=> (int) ( $a['score'] ?? 0 );
            if ( 0 !== $score ) return $score;
            return strcmp( (string) ( $a['element_id'] ?? '' ), (string) ( $b['element_id'] ?? '' ) );
        } );
        return $out;
    }

    private static function safe_error( WP_Error $error, string $fallback ): string {
        $message = trim( (string) $error->get_error_message() );
        return self::clip( '' !== $message ? $message : $fallback, 500 );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\s+/', ' ', $value ) ?? $value );
        return strlen( $value ) <= $max ? $value : substr( $value, 0, max( 0, $max - 1 ) ) . '…';
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_repair_discovery'] = true;
        $data['rendered_visual_repair_discovery_read_only'] = true;
        $data['rendered_visual_repair_discovery_localized_findings_only'] = true;
        $data['rendered_visual_repair_discovery_medium_high_only'] = true;
        $data['rendered_visual_repair_discovery_exact_existing_targets'] = true;
        $data['rendered_visual_repair_discovery_fresh_hash_crosscheck'] = true;
        $data['rendered_visual_repair_discovery_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

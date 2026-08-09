<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Intelligence {
    private const VERSION = '0.7.0';
    private const NS = 'elementize/v1';
    private const META_USAGE = '_elementize_pixfort_template_usage_v1';
    private const OPT_HISTORY_STARTED = 'elementize_design_history_started_gmt';
    private const USER_INSPECTIONS = '_elementize_design_visual_inspections_v1';
    private const USAGE_TRANSIENT = 'elementize_design_usage_index_v1';
    private const MAX_HISTORY_PAGES = 50;
    private const MAX_INSPECTIONS = 100;

    public static function init(): void {
        if ( '' === (string) get_option( self::OPT_HISTORY_STARTED, '' ) ) {
            add_option( self::OPT_HISTORY_STARTED, gmdate( 'Y-m-d H:i:s' ), '', false );
        }
        add_action( 'rest_api_init', [ self::class, 'routes' ], 25 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 160, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pixfort/catalogue-summary', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'catalogue_summary' ],
            'permission_callback' => [ self::class, 'can_manage_pixfort' ],
        ] );
    }

    public static function can_manage_pixfort() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_design_forbidden', 'Design-intelligence catalogue access requires an administrator.', [ 'status' => 403 ] );
        return true;
    }

    public static function catalogue_summary(): WP_REST_Response {
        $catalogue = self::catalogue();
        if ( is_wp_error( $catalogue ) ) return new WP_REST_Response( [
            'code' => $catalogue->get_error_code(),
            'message' => $catalogue->get_error_message(),
        ], (int) ( $catalogue->get_error_data()['status'] ?? 500 ) );

        $sections = self::type_summary( $catalogue, 'section' );
        $pages = self::type_summary( $catalogue, 'page' );
        $families = [];
        foreach ( $catalogue as $item ) {
            $family = (string) $item['design_family'];
            if ( ! isset( $families[ $family ] ) ) $families[ $family ] = 0;
            $families[ $family ]++;
        }
        arsort( $families );

        $usage = self::usage_index();
        $recent = array_values( $usage['templates'] );
        usort( $recent, static function( array $a, array $b ): int {
            $cmp = (int) $b['active_page_count'] <=> (int) $a['active_page_count'];
            if ( 0 !== $cmp ) return $cmp;
            return (int) $b['historical_insertions'] <=> (int) $a['historical_insertions'];
        } );
        $recent = array_slice( $recent, 0, 20 );

        return new WP_REST_Response( [
            'design_intelligence_version' => self::VERSION,
            'history_started_gmt' => (string) get_option( self::OPT_HISTORY_STARTED, '' ),
            'catalogue' => [
                'total_templates' => count( $catalogue ),
                'section' => $sections,
                'page' => $pages,
                'unique_families' => count( $families ),
                'largest_families' => self::assoc_rows( array_slice( $families, 0, 25, true ), 'family', 'count' ),
            ],
            'tracked_usage_scope' => [
                'pages_scanned' => $usage['pages_scanned'],
                'max_pages' => self::MAX_HISTORY_PAGES,
                'note' => 'Usage history begins when Design Intelligence is installed. It is a soft novelty signal, not a prohibition on reuse.',
            ],
            'recently_used_templates' => $recent,
            'recent_visual_inspections' => self::recent_inspections(),
            'selection_guidance' => [
                'Use this summary before substantial new-page template selection.',
                'Search multiple relevant categories or query angles instead of accepting the first plausible result.',
                'Prefer a visually diverse shortlist across design_family values.',
                'Treat repeated usage as a soft penalty unless the repeated template is clearly the best fit.',
                'Use visual probes for important section choices before insertion.',
            ],
        ], 200 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $status = $response->get_status();
        if ( $status < 200 || $status >= 300 ) return $response;

        if ( '/elementize/v1/status' === $route ) return self::augment_status( $response );
        if ( '/elementize/v1/pixfort/templates' === $route ) return self::augment_template_search( $response );
        if ( '/elementize/v1/pixfort/visual-probe' === $route ) return self::record_visual_probe( $response, $request );
        if ( preg_match( '#^/elementize/v1/pages/(\d+)/pixfort/insert$#', $route, $m ) ) return self::record_insert( $response, $request, absint( $m[1] ) );
        if ( preg_match( '#^/elementize/v1/pages/(\d+)/elements/remove$#', $route, $m ) ) return self::record_remove( $response, absint( $m[1] ) );
        return $response;
    }

    private static function augment_status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_intelligence'] = true;
        $data['design_catalogue_summary'] = true;
        $data['design_template_family_signals'] = true;
        $data['design_template_usage_tracking'] = true;
        $data['design_visual_inspection_tracking'] = true;
        $data['design_history_started_gmt'] = (string) get_option( self::OPT_HISTORY_STARTED, '' );
        $response->set_data( $data );
        return $response;
    }

    private static function augment_template_search( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) || ! isset( $data['templates'] ) || ! is_array( $data['templates'] ) ) return $response;
        $usage = self::usage_index();
        $families = [];
        $previously_used = 0;

        foreach ( $data['templates'] as &$template ) {
            if ( ! is_array( $template ) ) continue;
            $kind = (string) ( $template['kind'] ?? 'section' );
            $id = (string) ( $template['id'] ?? '' );
            $title = (string) ( $template['title'] ?? $id );
            $family = self::family_signature( $kind, $title, $id );
            $families[ $family ] = true;
            $u = $usage['templates'][ $id ] ?? self::empty_usage( $id );
            if ( (int) $u['historical_insertions'] > 0 ) $previously_used++;
            $template['design_family'] = $family;
            $template['design_usage'] = $u;
            $template['design_novelty'] = self::novelty( $u );
        }
        unset( $template );

        $returned = count( $data['templates'] );
        $data['design_intelligence'] = [
            'history_started_gmt' => (string) get_option( self::OPT_HISTORY_STARTED, '' ),
            'returned_template_count' => $returned,
            'returned_unique_family_count' => count( $families ),
            'returned_family_duplicate_count' => max( 0, $returned - count( $families ) ),
            'returned_previously_used_count' => $previously_used,
            'guidance' => 'For important section choices, build a shortlist across materially different design_family values and visually inspect serious candidates before insertion.',
        ];
        $response->set_data( $data );
        return $response;
    }

    private static function record_visual_probe( WP_REST_Response $response, WP_REST_Request $request ): WP_REST_Response {
        $ids = $request->get_param( 'template_ids' );
        if ( ! is_array( $ids ) ) return $response;
        $catalogue = self::catalogue();
        $by_id = [];
        if ( is_array( $catalogue ) ) foreach ( $catalogue as $item ) $by_id[ (string) $item['id'] ] = $item;

        $history = get_user_meta( get_current_user_id(), self::USER_INSPECTIONS, true );
        if ( ! is_array( $history ) ) $history = [];
        $now = gmdate( 'Y-m-d H:i:s' );
        $recorded = [];
        foreach ( array_values( array_unique( array_filter( array_map( 'strval', $ids ) ) ) ) as $id ) {
            $item = $by_id[ $id ] ?? null;
            $entry = [
                'template_id' => $id,
                'design_family' => is_array( $item ) ? (string) $item['design_family'] : null,
                'inspected_gmt' => $now,
            ];
            $history[] = $entry;
            $recorded[] = $entry;
        }
        if ( count( $history ) > self::MAX_INSPECTIONS ) $history = array_slice( $history, -self::MAX_INSPECTIONS );
        update_user_meta( get_current_user_id(), self::USER_INSPECTIONS, $history );

        $data = $response->get_data();
        if ( is_array( $data ) ) {
            $data['design_intelligence'] = [
                'inspection_recorded' => true,
                'inspected_candidates' => $recorded,
                'unique_families_inspected' => count( array_unique( array_filter( array_column( $recorded, 'design_family' ) ) ) ),
            ];
            $response->set_data( $data );
        }
        return $response;
    }

    private static function record_insert( WP_REST_Response $response, WP_REST_Request $request, int $page_id ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['saved'] ) ) return $response;
        $template_id = sanitize_key( (string) $request->get_param( 'template_id' ) );
        if ( '' === $template_id || $page_id < 1 ) return $response;

        $usage = self::page_usage( $page_id );
        $usage['occurrences'][] = [
            'template_id' => $template_id,
            'top_level_ids' => array_values( array_filter( array_map( 'strval', (array) ( $data['inserted_top_level_ids'] ?? [] ) ) ) ),
            'inserted_gmt' => gmdate( 'Y-m-d H:i:s' ),
            'removed_gmt' => null,
        ];
        update_post_meta( $page_id, self::META_USAGE, $usage );
        delete_transient( self::USAGE_TRANSIENT );

        $u = self::usage_for_template_on_page( $usage, $template_id );
        $data['design_intelligence'] = [
            'template_usage_recorded' => true,
            'template_id' => $template_id,
            'current_page_active_usage_count' => $u['active_count'],
            'current_page_historical_insertions' => $u['historical_count'],
        ];
        $response->set_data( $data );
        return $response;
    }

    private static function record_remove( WP_REST_Response $response, int $page_id ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['saved'] ) ) return $response;
        $removed_id = (string) ( $data['removed_top_level_id'] ?? '' );
        if ( '' === $removed_id || $page_id < 1 ) return $response;

        $usage = self::page_usage( $page_id );
        $changed = false;
        foreach ( $usage['occurrences'] as &$occurrence ) {
            if ( ! is_array( $occurrence ) || ! empty( $occurrence['removed_gmt'] ) ) continue;
            if ( in_array( $removed_id, (array) ( $occurrence['top_level_ids'] ?? [] ), true ) ) {
                $occurrence['removed_gmt'] = gmdate( 'Y-m-d H:i:s' );
                $changed = true;
            }
        }
        unset( $occurrence );
        if ( $changed ) {
            update_post_meta( $page_id, self::META_USAGE, $usage );
            delete_transient( self::USAGE_TRANSIENT );
        }
        $data['design_intelligence'] = [ 'template_usage_removal_reconciled' => $changed ];
        $response->set_data( $data );
        return $response;
    }

    private static function catalogue() {
        if ( ! function_exists( 'pixfort_elementor_library_data' ) ) {
            $loader = trailingslashit( get_template_directory() ) . 'inc/demo-content/elementor/loader.php';
            if ( file_exists( $loader ) ) require_once $loader;
        }
        if ( ! function_exists( 'pixfort_elementor_library_data' ) ) return new WP_Error( 'elementize_pixfort_library_unavailable', 'The active theme does not expose the Pixfort Elementor template library.', [ 'status' => 503 ] );
        try { $raw = pixfort_elementor_library_data(); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_pixfort_library_failed', 'Pixfort could not load its Elementor template catalogue: ' . $e->getMessage(), [ 'status' => 500 ] ); }
        if ( ! is_array( $raw ) ) return new WP_Error( 'elementize_pixfort_library_invalid', 'Pixfort returned an invalid template catalogue.', [ 'status' => 500 ] );

        $out = [];
        self::append_catalogue( $out, $raw['sections'] ?? [], 'section' );
        self::append_catalogue( $out, $raw['pages'] ?? [], 'page' );
        return array_values( $out );
    }

    private static function append_catalogue( array &$out, $source, string $kind ): void {
        if ( ! is_array( $source ) ) return;
        foreach ( $source as $row ) {
            if ( ! is_array( $row ) || empty( $row['id'] ) ) continue;
            $id = (string) $row['id'];
            $key = $kind . '|' . $id;
            $categories = [];
            foreach ( (array) ( $row['categories'] ?? [] ) as $cat ) if ( is_string( $cat ) && '' !== trim( $cat ) ) $categories[] = strtolower( trim( $cat ) );
            if ( isset( $out[ $key ] ) ) {
                $out[ $key ]['categories'] = array_values( array_unique( array_merge( $out[ $key ]['categories'], $categories ) ) );
                continue;
            }
            $title = (string) ( $row['title'] ?? $id );
            $out[ $key ] = [
                'id' => $id,
                'kind' => $kind,
                'title' => $title,
                'subtype' => strtolower( trim( (string) ( $row['subtype'] ?? '' ) ) ),
                'categories' => array_values( array_unique( $categories ) ),
                'design_family' => self::family_signature( $kind, $title, $id ),
            ];
        }
    }

    private static function type_summary( array $catalogue, string $kind ): array {
        $categories = [];
        $subtypes = [];
        $families = [];
        $total = 0;
        foreach ( $catalogue as $item ) {
            if ( (string) $item['kind'] !== $kind ) continue;
            $total++;
            foreach ( (array) $item['categories'] as $category ) {
                if ( ! isset( $categories[ $category ] ) ) $categories[ $category ] = 0;
                $categories[ $category ]++;
            }
            $subtype = (string) $item['subtype'];
            if ( '' !== $subtype ) {
                if ( ! isset( $subtypes[ $subtype ] ) ) $subtypes[ $subtype ] = 0;
                $subtypes[ $subtype ]++;
            }
            $families[ (string) $item['design_family'] ] = true;
        }
        arsort( $categories );
        arsort( $subtypes );
        return [
            'total' => $total,
            'unique_families' => count( $families ),
            'categories' => self::assoc_rows( $categories, 'id', 'count' ),
            'subtypes' => self::assoc_rows( $subtypes, 'id', 'count' ),
        ];
    }

    private static function family_signature( string $kind, string $title, string $id ): string {
        $source = '' !== trim( $title ) ? $title : $id;
        $source = strtolower( html_entity_decode( wp_strip_all_tags( $source ), ENT_QUOTES | ENT_HTML5, 'UTF-8' ) );
        $source = preg_replace( '/[_-]+/', ' ', $source );
        $source = preg_replace( '/\s+(?:v(?:ersion)?\s*)?\d+\s*$/i', '', (string) $source );
        $source = preg_replace( '/\s+/', ' ', trim( (string) $source ) );
        $slug = sanitize_title( $source );
        if ( '' === $slug ) $slug = sanitize_title( $id );
        return $kind . '|' . $slug;
    }

    private static function page_usage( int $page_id ): array {
        $usage = get_post_meta( $page_id, self::META_USAGE, true );
        if ( ! is_array( $usage ) ) $usage = [];
        if ( ! isset( $usage['version'] ) ) $usage['version'] = 1;
        if ( ! isset( $usage['occurrences'] ) || ! is_array( $usage['occurrences'] ) ) $usage['occurrences'] = [];
        return $usage;
    }

    private static function usage_for_template_on_page( array $usage, string $template_id ): array {
        $active = 0;
        $historical = 0;
        foreach ( (array) ( $usage['occurrences'] ?? [] ) as $occurrence ) {
            if ( ! is_array( $occurrence ) || (string) ( $occurrence['template_id'] ?? '' ) !== $template_id ) continue;
            $historical++;
            if ( empty( $occurrence['removed_gmt'] ) ) $active++;
        }
        return [ 'active_count' => $active, 'historical_count' => $historical ];
    }

    private static function usage_index(): array {
        $cached = get_transient( self::USAGE_TRANSIENT );
        if ( is_array( $cached ) && isset( $cached['templates'] ) ) return $cached;

        $query = new WP_Query( [
            'post_type' => 'page',
            'post_status' => 'any',
            'posts_per_page' => self::MAX_HISTORY_PAGES,
            'orderby' => 'modified',
            'order' => 'DESC',
            'meta_key' => self::META_USAGE,
            'fields' => 'ids',
            'no_found_rows' => true,
        ] );
        $index = [];
        $pages_scanned = 0;
        foreach ( (array) $query->posts as $page_id ) {
            $page_id = absint( $page_id );
            if ( $page_id < 1 ) continue;
            $pages_scanned++;
            $usage = self::page_usage( $page_id );
            $active_seen = [];
            foreach ( (array) $usage['occurrences'] as $occurrence ) {
                if ( ! is_array( $occurrence ) ) continue;
                $template_id = (string) ( $occurrence['template_id'] ?? '' );
                if ( '' === $template_id ) continue;
                if ( ! isset( $index[ $template_id ] ) ) $index[ $template_id ] = self::empty_usage( $template_id );
                $index[ $template_id ]['historical_insertions']++;
                $inserted = (string) ( $occurrence['inserted_gmt'] ?? '' );
                if ( $inserted > (string) $index[ $template_id ]['last_used_gmt'] ) {
                    $index[ $template_id ]['last_used_gmt'] = $inserted;
                    $index[ $template_id ]['last_used_page_id'] = $page_id;
                    $index[ $template_id ]['last_used_page_title'] = get_the_title( $page_id );
                }
                if ( empty( $occurrence['removed_gmt'] ) ) {
                    $index[ $template_id ]['active_occurrence_count']++;
                    $active_seen[ $template_id ] = true;
                }
            }
            foreach ( array_keys( $active_seen ) as $template_id ) $index[ $template_id ]['active_page_count']++;
        }
        $out = [ 'pages_scanned' => $pages_scanned, 'templates' => $index ];
        set_transient( self::USAGE_TRANSIENT, $out, 5 * MINUTE_IN_SECONDS );
        return $out;
    }

    private static function empty_usage( string $template_id ): array {
        return [
            'template_id' => $template_id,
            'active_page_count' => 0,
            'active_occurrence_count' => 0,
            'historical_insertions' => 0,
            'last_used_page_id' => null,
            'last_used_page_title' => null,
            'last_used_gmt' => null,
        ];
    }

    private static function novelty( array $usage ): string {
        $historical = (int) ( $usage['historical_insertions'] ?? 0 );
        $pages = (int) ( $usage['active_page_count'] ?? 0 );
        if ( 0 === $historical ) return 'unseen_in_tracked_history';
        if ( $pages <= 1 && $historical <= 2 ) return 'lightly_used';
        if ( $pages >= 4 || $historical >= 6 ) return 'frequently_reused';
        return 'reused';
    }

    private static function recent_inspections(): array {
        $history = get_user_meta( get_current_user_id(), self::USER_INSPECTIONS, true );
        if ( ! is_array( $history ) ) return [];
        return array_values( array_slice( $history, -20 ) );
    }

    private static function assoc_rows( array $values, string $key_name, string $value_name ): array {
        $rows = [];
        foreach ( $values as $key => $value ) $rows[] = [ $key_name => (string) $key, $value_name => (int) $value ];
        return $rows;
    }
}

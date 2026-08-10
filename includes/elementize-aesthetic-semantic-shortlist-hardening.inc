<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Semantic_Shortlist', false ) ) return;

/**
 * Role-specific structural hardening for semantic Pixfort shortlisting.
 *
 * 0.28.0 proved that boolean role-presence alone is too permissive: a pricing,
 * numbers, or feature-heavy section can contain heading/text/CTA/image and still
 * look superficially hero-compatible. This layer keeps catalogue metadata limited
 * to discovery, then re-inspects a better discovery pool and scores actual Elementor
 * structure by count distance, complexity distance, dependency risk, and role-specific
 * structural exclusions against the exact target structure already captured by the
 * base semantic shortlist.
 *
 * Read-only. It never inserts/replaces a section and never authorizes a write.
 */
final class Elementize_Aesthetic_Semantic_Shortlist_Hardening {
    private const VERSION = '0.28.1-h1';
    private const NS = 'elementize/v1';
    private const CATALOGUE_PAGE_SIZE = 100;
    private const MAX_CATALOGUE_PAGES = 8;
    private const DISCOVERY_POOL_LIMIT = 32;
    private const STRUCTURE_BATCH_LIMIT = 8;
    private const MAX_SHORTLIST = 4;
    private const MIN_VISUAL_RANKING = 2;

    public static function init(): void {
        // Base semantic shortlist runs at 620. Harden its result afterward.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 630, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/templates' !== $route || 'GET' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'semantic_shortlist' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['semantic_shortlist'] ?? null ) ) return $response;
        $base = $data['semantic_shortlist'];
        if ( empty( $base['available'] ) ) return $response;

        $role = sanitize_key( (string) ( $base['target_role'] ?? '' ) );
        $target = is_array( $base['target_structure'] ?? null ) ? $base['target_structure'] : [];
        $page_id = absint( $base['page_id'] ?? $request->get_param( 'context_page_id' ) );
        if ( $page_id < 1 || ! $target ) return $response;

        // 0.28.1 focuses on the runtime-proven failure mode first. Other roles keep
        // the conservative 0.28.0 behavior until each receives its own acceptance test.
        if ( 'hero' !== $role ) {
            $base['semantic_shortlist_hardening'] = [
                'version' => self::VERSION,
                'applied' => false,
                'reason' => 'Role-specific count/complexity hardening is currently enabled for hero targets only.',
                'read_only' => true,
                'writes_performed' => false,
                'automatic_write_allowed' => false,
            ];
            $data['semantic_shortlist'] = $base;
            $response->set_data( $data );
            return $response;
        }

        $catalogue = self::all_sections();
        if ( is_wp_error( $catalogue ) ) return self::fail_closed( $response, $data, $base, $catalogue->get_error_message() );

        $discovery = self::hero_discovery_pool( $catalogue );
        if ( count( $discovery ) < self::MIN_VISUAL_RANKING ) {
            return self::fail_closed( $response, $data, $base, 'Hero discovery produced fewer than two candidates after role-mismatch filtering.' );
        }

        $ids = array_values( array_map( static fn ( array $row ): string => (string) $row['id'], $discovery ) );
        $inspected = self::inspect( $page_id, $ids );
        if ( is_wp_error( $inspected ) ) return self::fail_closed( $response, $data, $base, $inspected->get_error_message() );

        $meta = [];
        foreach ( $catalogue as $row ) $meta[ (string) ( $row['id'] ?? '' ) ] = $row;

        $ranked = [];
        foreach ( $inspected as $template ) {
            if ( ! is_array( $template ) || empty( $template['ok'] ) ) continue;
            $id = sanitize_key( (string) ( $template['template_id'] ?? '' ) );
            if ( '' === $id ) continue;
            $score = self::hero_structure_score( $template, $target );
            $m = is_array( $meta[ $id ] ?? null ) ? $meta[ $id ] : [];
            $ranked[] = [
                'template_id' => $id,
                'title' => (string) ( $m['title'] ?? $template['title'] ?? $id ),
                'semantic_role' => 'hero',
                'semantic_fit_score' => $score['score'],
                'eligible' => $score['eligible'],
                'structural_match' => $score['structural_match'],
                'role_requirements_met' => $score['requirements_met'],
                'dependency_risk' => $score['dependency_risk'],
                'reason' => $score['reason'],
                'categories' => array_values( array_slice( (array) ( $m['categories'] ?? [] ), 0, 8 ) ),
                'subtype' => $m['subtype'] ?? null,
                'dominant_alignment' => (string) ( $template['dominant_alignment'] ?? 'unknown' ),
                'approximate_density' => (string) ( $template['approximate_density'] ?? 'unknown' ),
                'image_prominence' => (string) ( $template['image_prominence'] ?? 'unknown' ),
                'role_counts' => is_array( $template['role_counts'] ?? null ) ? $template['role_counts'] : [],
                'widget_count' => self::widget_count( $template ),
                'dependency_indicators' => is_array( $template['dependency_indicators'] ?? null ) ? $template['dependency_indicators'] : [],
                'structural_family_hash' => (string) ( $template['structural_family_hash'] ?? '' ),
                'hardening_diagnostics' => $score['diagnostics'],
            ];
        }

        usort( $ranked, static function ( array $a, array $b ): int {
            if ( (int) $a['semantic_fit_score'] === (int) $b['semantic_fit_score'] ) return strcmp( (string) $a['template_id'], (string) $b['template_id'] );
            return (int) $b['semantic_fit_score'] <=> (int) $a['semantic_fit_score'];
        } );

        $limit = absint( $base['shortlist_limit'] ?? self::MAX_SHORTLIST );
        $limit = min( self::MAX_SHORTLIST, max( self::MIN_VISUAL_RANKING, $limit ) );
        $shortlist = [];
        $families = [];
        foreach ( $ranked as $row ) {
            if ( empty( $row['eligible'] ) ) continue;
            $family = (string) $row['structural_family_hash'];
            if ( '' !== $family && isset( $families[ $family ] ) ) continue;
            $shortlist[] = $row;
            if ( '' !== $family ) $families[ $family ] = true;
            if ( count( $shortlist ) >= $limit ) break;
        }
        // Fill only with other *eligible* candidates; never fall back to rejected ones.
        if ( count( $shortlist ) < $limit ) {
            foreach ( $ranked as $row ) {
                if ( empty( $row['eligible'] ) ) continue;
                $exists = false;
                foreach ( $shortlist as $selected ) if ( $selected['template_id'] === $row['template_id'] ) { $exists = true; break; }
                if ( $exists ) continue;
                $shortlist[] = $row;
                if ( count( $shortlist ) >= $limit ) break;
            }
        }

        $shortlist_ids = array_values( array_map( static fn ( array $row ): string => (string) $row['template_id'], $shortlist ) );
        $usable = count( $shortlist_ids ) >= self::MIN_VISUAL_RANKING;

        $base['base_version'] = (string) ( $base['version'] ?? '' );
        $base['version'] = self::VERSION;
        $base['metadata_discovery_count'] = count( $discovery );
        $base['structurally_inspected_count'] = count( $inspected );
        $base['shortlist_count'] = count( $shortlist );
        $base['template_ids'] = $shortlist_ids;
        $base['candidates'] = $shortlist;
        $base['usable_for_visual_ranking'] = $usable;
        $base['catalogue_metadata_policy'] = 'Catalogue metadata is discovery-only. 0.28.1 applies role-specific negative discovery terms only to prevent specialized families from crowding out plausible heroes; final eligibility uses inspected Elementor structure, count distance, complexity distance, and dependency risk.';
        $base['selection_policy'] = $usable
            ? 'These are structure-grounded hero candidates. Pass them to contextual visual ranking; this still does not authorize insertion or replacement.'
            : 'The hardened hero shortlist contains fewer than two structurally eligible candidates. Do not fall back to weaker metadata-selected candidates and do not insert automatically.';
        $base['semantic_shortlist_hardening'] = [
            'version' => self::VERSION,
            'applied' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'count_distance_grounded' => true,
            'complexity_distance_grounded' => true,
            'role_specific_specialized_structure_guard' => true,
            'fail_closed_without_two_candidates' => true,
            'discovery_pool_limit' => self::DISCOVERY_POOL_LIMIT,
            'eligible_count' => count( array_filter( $ranked, static fn ( array $row ): bool => ! empty( $row['eligible'] ) ) ),
            'rejected_count' => count( array_filter( $ranked, static fn ( array $row ): bool => empty( $row['eligible'] ) ) ),
        ];

        $data['semantic_shortlist'] = $base;
        $response->set_data( $data );
        return $response;
    }

    private static function hero_discovery_pool( array $catalogue ): array {
        $ranked = [];
        foreach ( $catalogue as $row ) {
            if ( ! is_array( $row ) ) continue;
            $text = strtolower( implode( ' ', [
                (string) ( $row['id'] ?? '' ),
                (string) ( $row['title'] ?? '' ),
                (string) ( $row['subtype'] ?? '' ),
                implode( ' ', array_map( 'strval', (array) ( $row['categories'] ?? [] ) ) ),
            ] ) );
            $score = 0;
            foreach ( [ 'hero' => 45, 'home-intro' => 35, 'home intro' => 35, 'intro-left' => 28, 'intro-center' => 28, 'intro-video' => 24, 'intro' => 18, 'header' => 12, 'banner' => 10 ] as $token => $points ) {
                if ( false !== strpos( $text, $token ) ) $score += $points;
            }
            // Discovery-only penalties: these terms strongly indicate specialized
            // content families that should not monopolize a hero candidate pool.
            foreach ( [
                'pricing' => 65, 'price' => 45, 'case-stud' => 55, 'case stud' => 55,
                'feature' => 35, 'progress' => 45, 'table' => 50, 'numbers' => 32,
                'number' => 25, 'counter' => 35, 'stats' => 30, 'statistics' => 30,
                'testimonial' => 45, 'review' => 35, 'faq' => 55, 'accordion' => 45,
                'contact' => 40, 'form' => 40, 'newsletter' => 45, 'team' => 35,
                'blog' => 40, 'post' => 30, 'portfolio' => 35, 'products' => 35,
                'clients-marquee' => 30, 'client-marquee' => 30, 'logos-marquee' => 30,
                'marquee-numbers' => 40, 'solutions-intro-numbers' => 35,
            ] as $token => $points ) {
                if ( false !== strpos( $text, $token ) ) $score -= $points;
            }
            $ranked[] = [ 'row' => $row, 'score' => $score ];
        }
        usort( $ranked, static function ( array $a, array $b ): int {
            if ( $a['score'] === $b['score'] ) return strcmp( (string) ( $a['row']['id'] ?? '' ), (string) ( $b['row']['id'] ?? '' ) );
            return $b['score'] <=> $a['score'];
        } );

        $pool = [];
        foreach ( $ranked as $item ) {
            if ( (int) $item['score'] < 8 && count( $pool ) >= 16 ) break;
            $pool[] = $item['row'];
            if ( count( $pool ) >= self::DISCOVERY_POOL_LIMIT ) break;
        }
        return $pool;
    }

    private static function hero_structure_score( array $template, array $target ): array {
        $roles = is_array( $template['role_counts'] ?? null ) ? $template['role_counts'] : [];
        $target_roles = is_array( $target['role_counts'] ?? null ) ? $target['role_counts'] : [];
        $candidate_widgets = is_array( $template['widget_type_counts'] ?? null ) ? $template['widget_type_counts'] : [];
        $target_widgets = is_array( $target['widget_types'] ?? null ) ? $target['widget_types'] : [];

        $heading = absint( $roles['heading'] ?? 0 );
        $text = absint( $roles['text'] ?? 0 );
        $button = absint( $roles['button_cta'] ?? 0 );
        $form = absint( $roles['form'] ?? 0 );
        $shortcode = absint( $roles['shortcode'] ?? 0 );
        $target_heading = absint( $target_roles['heading'] ?? 0 );
        $target_text = absint( $target_roles['text'] ?? 0 );
        $target_button = absint( $target_roles['button_cta'] ?? 0 );

        $requirements_met = true;
        $reasons = [];
        if ( $heading < 1 ) { $requirements_met = false; $reasons[] = 'missing hero heading'; }
        if ( $target_text > 0 && $text < 1 ) { $requirements_met = false; $reasons[] = 'missing body copy present in target hero'; }
        if ( $target_button > 0 && $button < 1 ) { $requirements_met = false; $reasons[] = 'missing CTA present in target hero'; }
        if ( $form > 0 || $shortcode > 0 ) { $requirements_met = false; $reasons[] = 'adds form/shortcode dependency absent from ordinary hero structure'; }

        $specialized_hits = [];
        $specialized_pattern = '/pricing|price[-_]?table|progress|counter|statistic|accordion|faq|testimonial|review|posts|portfolio|products|product[-_]?grid|case[-_]?stud|timeline/i';
        foreach ( array_keys( $candidate_widgets ) as $widget ) {
            if ( preg_match( $specialized_pattern, (string) $widget ) ) $specialized_hits[] = (string) $widget;
        }
        $target_specialized = false;
        foreach ( array_keys( $target_widgets ) as $widget ) if ( preg_match( $specialized_pattern, (string) $widget ) ) { $target_specialized = true; break; }
        if ( $specialized_hits && ! $target_specialized ) {
            $requirements_met = false;
            $reasons[] = 'contains specialized non-hero widget structure absent from target';
        }

        $score = 100;
        $role_weights = [
            'heading' => 9,
            'text' => 7,
            'button_cta' => 10,
            'image' => 7,
            'icon' => 4,
            'form' => 14,
            'shortcode' => 14,
        ];
        $count_distance_penalty = 0;
        foreach ( $role_weights as $key => $weight ) {
            $a = absint( $target_roles[ $key ] ?? 0 );
            $b = absint( $roles[ $key ] ?? 0 );
            $delta = abs( $a - $b );
            $count_distance_penalty += min( 18, $delta * $weight );
        }
        $count_distance_penalty = min( 45, $count_distance_penalty );
        $score -= $count_distance_penalty;

        $target_widget_count = max( 1, absint( $target['widget_count'] ?? 0 ) );
        $candidate_widget_count = max( 1, self::widget_count( $template ) );
        $allowed_excess = max( 3, (int) ceil( $target_widget_count * 0.55 ) );
        $excess = max( 0, $candidate_widget_count - $target_widget_count - $allowed_excess );
        $deficit = max( 0, $target_widget_count - $candidate_widget_count - 3 );
        $complexity_penalty = min( 32, ( $excess * 3 ) + ( $deficit * 2 ) );

        $target_elements = max( 1, absint( $target['total_elements'] ?? 0 ) );
        $candidate_elements = max( 1, absint( $template['total_elements'] ?? 0 ) );
        if ( $candidate_elements > (int) ceil( $target_elements * 2.1 ) ) $complexity_penalty += 12;
        $complexity_penalty = min( 40, $complexity_penalty );
        $score -= $complexity_penalty;

        $alignment_penalty = 0;
        $target_alignment = strtolower( (string) ( $target['dominant_alignment'] ?? 'unknown' ) );
        $candidate_alignment = strtolower( (string) ( $template['dominant_alignment'] ?? 'unknown' ) );
        if ( ! in_array( $target_alignment, [ '', 'unknown', 'mixed' ], true ) && ! in_array( $candidate_alignment, [ '', 'unknown', 'mixed' ], true ) && $target_alignment !== $candidate_alignment ) {
            $alignment_penalty = 5;
            $score -= $alignment_penalty;
        }

        $dependency_risk = 'low';
        $dependency_penalty = 0;
        if ( $form > 0 || $shortcode > 0 ) {
            $dependency_risk = 'high';
            $dependency_penalty = 24;
        } elseif ( ! empty( $template['dependency_indicators']['widgets'] ) ) {
            $dependency_risk = 'medium';
            $dependency_penalty = 8;
        }
        $score -= $dependency_penalty;

        $specialized_penalty = min( 35, count( $specialized_hits ) * 18 );
        $score -= $specialized_penalty;
        $score = max( 0, min( 100, $score ) );

        if ( $count_distance_penalty <= 14 ) $reasons[] = 'role counts closely match target hero';
        elseif ( $count_distance_penalty <= 28 ) $reasons[] = 'role counts moderately match target hero';
        else $reasons[] = 'role counts diverge materially from target hero';
        if ( 0 === $complexity_penalty ) $reasons[] = 'section complexity is close to target hero';
        elseif ( $complexity_penalty >= 20 ) $reasons[] = 'section complexity materially exceeds/undershoots target hero';

        $eligible = $requirements_met && $score >= 62 && 'high' !== $dependency_risk;
        return [
            'score' => $score,
            'eligible' => $eligible,
            'requirements_met' => $requirements_met,
            'dependency_risk' => $dependency_risk,
            'structural_match' => $score >= 84 ? 'strong' : ( $score >= 72 ? 'good' : ( $score >= 62 ? 'acceptable' : 'weak' ) ),
            'reason' => self::clip( implode( '; ', array_values( array_unique( $reasons ) ) ), 500 ),
            'diagnostics' => [
                'count_distance_penalty' => $count_distance_penalty,
                'complexity_penalty' => $complexity_penalty,
                'alignment_penalty' => $alignment_penalty,
                'dependency_penalty' => $dependency_penalty,
                'specialized_structure_penalty' => $specialized_penalty,
                'specialized_widget_hits' => array_values( array_slice( $specialized_hits, 0, 10 ) ),
                'target_widget_count' => $target_widget_count,
                'candidate_widget_count' => $candidate_widget_count,
                'target_total_elements' => $target_elements,
                'candidate_total_elements' => $candidate_elements,
            ],
        ];
    }

    private static function widget_count( array $template ): int {
        $sum = 0;
        foreach ( is_array( $template['widget_type_counts'] ?? null ) ? $template['widget_type_counts'] : [] as $count ) $sum += absint( $count );
        return $sum;
    }

    private static function inspect( int $page_id, array $ids ) {
        $out = [];
        foreach ( array_chunk( array_values( array_unique( $ids ) ), self::STRUCTURE_BATCH_LIMIT ) as $batch ) {
            $inner = new WP_REST_Request( 'POST', '/' . self::NS . '/pages/' . $page_id . '/pixfort/template-structure' );
            $inner->set_param( 'id', $page_id );
            $inner->set_param( 'template_ids', $batch );
            $response = rest_do_request( $inner );
            if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) {
                return new WP_Error( 'elementize_semantic_shortlist_hardening_structure_failed', 'Pixfort structure inspection failed during hardened hero shortlisting.' );
            }
            $data = $response->get_data();
            if ( ! is_array( $data ) ) continue;
            foreach ( is_array( $data['templates'] ?? null ) ? $data['templates'] : [] as $template ) if ( is_array( $template ) ) $out[] = $template;
        }
        return $out;
    }

    private static function all_sections() {
        $all = [];
        $pages = 1;
        for ( $page = 1; $page <= self::MAX_CATALOGUE_PAGES && $page <= $pages; $page++ ) {
            $inner = new WP_REST_Request( 'GET', '/' . self::NS . '/pixfort/templates' );
            $inner->set_param( 'type', 'section' );
            $inner->set_param( 'page', $page );
            $inner->set_param( 'per_page', self::CATALOGUE_PAGE_SIZE );
            $response = rest_do_request( $inner );
            if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) {
                return new WP_Error( 'elementize_semantic_shortlist_hardening_catalogue_failed', 'Pixfort catalogue could not be read during hardened hero discovery.' );
            }
            $data = $response->get_data();
            if ( ! is_array( $data ) ) break;
            $pages = min( self::MAX_CATALOGUE_PAGES, max( 1, absint( $data['total_pages'] ?? 1 ) ) );
            foreach ( is_array( $data['templates'] ?? null ) ? $data['templates'] : [] as $row ) {
                if ( ! is_array( $row ) || 'section' !== (string) ( $row['kind'] ?? '' ) ) continue;
                $id = sanitize_key( (string) ( $row['id'] ?? '' ) );
                if ( '' !== $id ) $all[ $id ] = $row;
            }
        }
        return array_values( $all );
    }

    private static function fail_closed( WP_REST_Response $response, array $data, array $base, string $reason ): WP_REST_Response {
        $base['base_version'] = (string) ( $base['version'] ?? '' );
        $base['version'] = self::VERSION;
        $base['template_ids'] = [];
        $base['candidates'] = [];
        $base['shortlist_count'] = 0;
        $base['usable_for_visual_ranking'] = false;
        $base['selection_policy'] = 'Hardened hero shortlisting failed closed. Do not reuse the weaker base shortlist and do not insert automatically.';
        $base['semantic_shortlist_hardening'] = [
            'version' => self::VERSION,
            'applied' => true,
            'failed_closed' => true,
            'reason' => self::clip( $reason, 500 ),
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
        ];
        $data['semantic_shortlist'] = $base;
        $response->set_data( $data );
        return $response;
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\s+/u', ' ', $value ) ?? $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) $data = [];
        // Override the public shortlist version so the existing harness can prove the
        // hardened layer is active without adding another GPT action or endpoint.
        $data['aesthetic_semantic_shortlist_base_version'] = (string) ( $data['aesthetic_semantic_shortlist_version'] ?? '' );
        $data['aesthetic_semantic_shortlist_version'] = self::VERSION;
        $data['aesthetic_semantic_shortlist_hardening_version'] = self::VERSION;
        $data['aesthetic_semantic_shortlist_count_distance_grounded'] = true;
        $data['aesthetic_semantic_shortlist_complexity_distance_grounded'] = true;
        $data['aesthetic_semantic_shortlist_fail_closed'] = true;
        $data['aesthetic_semantic_shortlist_action_slot_cost'] = 0;
        $data['aesthetic_semantic_shortlist_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

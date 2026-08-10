<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Template_Structure', false ) ) return;

/**
 * Read-only semantic Pixfort candidate shortlisting.
 *
 * Catalogue metadata is used only for broad discovery. Candidate eligibility and
 * ranking are then based on real Elementor/Pixfort structure inspected through the
 * existing template-structure route and compared with the exact current target
 * section structure. This layer does not inspect candidate thumbnails, does not
 * insert or replace sections, and does not authorize a visual winner.
 */
final class Elementize_Aesthetic_Semantic_Shortlist {
    private const VERSION = '0.28.0-s1';
    private const NS = 'elementize/v1';
    private const CATALOGUE_PAGE_SIZE = 100;
    private const MAX_CATALOGUE_PAGES = 8;
    private const DISCOVERY_POOL_LIMIT = 16;
    private const STRUCTURE_BATCH_LIMIT = 8;
    private const DEFAULT_SHORTLIST_LIMIT = 4;
    private const MIN_SHORTLIST_LIMIT = 2;
    private const MAX_SHORTLIST_LIMIT = 4;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 620, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/templates' !== $route || 'GET' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'semantic_shortlist' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;

        $result = self::build_shortlist( $request );
        if ( is_wp_error( $result ) ) {
            $data['semantic_shortlist'] = [
                'version' => self::VERSION,
                'available' => false,
                'read_only' => true,
                'writes_performed' => false,
                'automatic_write_allowed' => false,
                'action_slot_cost' => 0,
                'requires_visual_ranking' => true,
                'reason' => self::clip( $result->get_error_message(), 500 ),
            ];
        } else {
            $data['semantic_shortlist'] = $result;
        }
        $response->set_data( $data );
        return $response;
    }

    private static function build_shortlist( WP_REST_Request $request ) {
        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        $limit = absint( $request->get_param( 'shortlist_limit' ) );
        if ( $limit < self::MIN_SHORTLIST_LIMIT ) $limit = self::DEFAULT_SHORTLIST_LIMIT;
        $limit = min( self::MAX_SHORTLIST_LIMIT, max( self::MIN_SHORTLIST_LIMIT, $limit ) );

        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) ) {
            return new WP_Error( 'elementize_semantic_shortlist_context_invalid', 'semantic_shortlist requires a valid context_page_id and target_marker.' );
        }
        $post = get_post( $page_id );
        if ( ! $post || 'page' !== $post->post_type || ! current_user_can( 'edit_post', $page_id ) ) {
            return new WP_Error( 'elementize_semantic_shortlist_page_unavailable', 'The contextual page is unavailable or not editable.' );
        }

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_semantic_shortlist_hash_unavailable', 'The current Elementor page state could not be fingerprinted.' );
        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) {
            return new WP_Error( 'elementize_semantic_shortlist_context_stale', 'The exact-state grounded A/B context is unavailable or stale. Refresh completion-audit with include_visual=true first.' );
        }

        $target_ledger = self::ledger_row( $snapshot, $marker );
        if ( is_wp_error( $target_ledger ) ) return $target_ledger;
        $target_element_id = trim( (string) ( $target_ledger['top_level_element_id'] ?? '' ) );
        if ( '' === $target_element_id ) return new WP_Error( 'elementize_semantic_shortlist_target_id_missing', 'The grounded target marker does not expose a top-level Elementor element ID.' );

        $elementor = self::elementor_data( $page_id );
        if ( is_wp_error( $elementor ) ) return $elementor;
        $target_node = self::find_top_level( $elementor, $target_element_id );
        if ( null === $target_node ) return new WP_Error( 'elementize_semantic_shortlist_target_missing', 'The grounded target top-level element is not present in the exact current Elementor state.' );

        $target_structure = self::summarize_nodes( [ $target_node ] );
        $role = self::classify_target_role( $marker, $target_structure );

        $catalogue = self::all_section_catalogue();
        if ( is_wp_error( $catalogue ) ) return $catalogue;
        if ( count( $catalogue ) < 2 ) return new WP_Error( 'elementize_semantic_shortlist_catalogue_empty', 'The Pixfort section catalogue contains fewer than two usable candidates.' );

        $discovery = self::discovery_pool( $catalogue, $role['role'] );
        if ( count( $discovery ) < 2 ) return new WP_Error( 'elementize_semantic_shortlist_discovery_empty', 'Broad catalogue discovery did not produce enough candidates for structural inspection.' );

        $template_ids = array_values( array_map( static fn ( array $row ): string => (string) $row['id'], $discovery ) );
        $inspected = self::inspect_structures( $page_id, $template_ids );
        if ( is_wp_error( $inspected ) ) return $inspected;

        $meta_by_id = [];
        foreach ( $catalogue as $row ) $meta_by_id[ (string) $row['id'] ] = $row;

        $ranked = [];
        foreach ( $inspected as $template ) {
            if ( ! is_array( $template ) || empty( $template['ok'] ) ) continue;
            $template_id = sanitize_key( (string) ( $template['template_id'] ?? '' ) );
            if ( '' === $template_id ) continue;
            $scored = self::semantic_score( $template, $target_structure, $role['role'] );
            $meta = is_array( $meta_by_id[ $template_id ] ?? null ) ? $meta_by_id[ $template_id ] : [];
            $ranked[] = [
                'template_id' => $template_id,
                'title' => (string) ( $meta['title'] ?? $template['title'] ?? $template_id ),
                'semantic_role' => $role['role'],
                'semantic_fit_score' => $scored['score'],
                'eligible' => $scored['eligible'],
                'structural_match' => $scored['structural_match'],
                'role_requirements_met' => $scored['requirements_met'],
                'dependency_risk' => $scored['dependency_risk'],
                'reason' => $scored['reason'],
                'categories' => array_values( array_slice( (array) ( $meta['categories'] ?? [] ), 0, 8 ) ),
                'subtype' => $meta['subtype'] ?? null,
                'dominant_alignment' => (string) ( $template['dominant_alignment'] ?? 'unknown' ),
                'approximate_density' => (string) ( $template['approximate_density'] ?? 'unknown' ),
                'image_prominence' => (string) ( $template['image_prominence'] ?? 'unknown' ),
                'role_counts' => is_array( $template['role_counts'] ?? null ) ? $template['role_counts'] : [],
                'dependency_indicators' => is_array( $template['dependency_indicators'] ?? null ) ? $template['dependency_indicators'] : [],
                'structural_family_hash' => (string) ( $template['structural_family_hash'] ?? '' ),
            ];
        }

        usort( $ranked, static function ( array $a, array $b ): int {
            if ( (int) $a['semantic_fit_score'] === (int) $b['semantic_fit_score'] ) return strcmp( (string) $a['template_id'], (string) $b['template_id'] );
            return (int) $b['semantic_fit_score'] <=> (int) $a['semantic_fit_score'];
        } );

        $shortlist = [];
        $family_seen = [];
        foreach ( $ranked as $row ) {
            if ( empty( $row['eligible'] ) ) continue;
            $family = (string) $row['structural_family_hash'];
            if ( '' !== $family && isset( $family_seen[ $family ] ) ) continue;
            $shortlist[] = $row;
            if ( '' !== $family ) $family_seen[ $family ] = true;
            if ( count( $shortlist ) >= $limit ) break;
        }
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
        $usable = count( $shortlist_ids ) >= 2;

        return [
            'version' => self::VERSION,
            'available' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
            'page_id' => $page_id,
            'target_marker' => $marker,
            'page_state_hash' => $state_hash,
            'context_grounding_source' => (string) ( $snapshot['grounding_source'] ?? '' ),
            'target_top_level_element_id' => $target_element_id,
            'target_role' => $role['role'],
            'target_role_confidence' => $role['confidence'],
            'target_role_reason' => $role['reason'],
            'target_structure' => $target_structure,
            'catalogue_candidate_count' => count( $catalogue ),
            'metadata_discovery_count' => count( $discovery ),
            'structurally_inspected_count' => count( $inspected ),
            'shortlist_limit' => $limit,
            'shortlist_count' => count( $shortlist ),
            'template_ids' => $shortlist_ids,
            'candidates' => $shortlist,
            'usable_for_visual_ranking' => $usable,
            'requires_visual_ranking' => true,
            'requires_post_insert_visual_verification' => true,
            'catalogue_metadata_policy' => 'Catalogue title/category/subtype metadata is used only for broad discovery. Eligibility is based on actual Elementor/Pixfort structure inspection, not demo copy or thumbnail metadata.',
            'selection_policy' => $usable
                ? 'These candidates are a semantic/structural shortlist only. Pass template_ids to the existing contextual visual-probe ranking before any guarded insertion decision.'
                : 'The semantic shortlist is too small for comparative visual ranking. Do not insert automatically.',
        ];
    }

    private static function all_section_catalogue() {
        $all = [];
        $pages = 1;
        for ( $page = 1; $page <= self::MAX_CATALOGUE_PAGES && $page <= $pages; $page++ ) {
            $inner = new WP_REST_Request( 'GET', '/' . self::NS . '/pixfort/templates' );
            $inner->set_param( 'type', 'section' );
            $inner->set_param( 'page', $page );
            $inner->set_param( 'per_page', self::CATALOGUE_PAGE_SIZE );
            $response = rest_do_request( $inner );
            if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) {
                return new WP_Error( 'elementize_semantic_shortlist_catalogue_failed', 'The Pixfort section catalogue could not be read during semantic discovery.' );
            }
            $data = $response->get_data();
            if ( ! is_array( $data ) ) break;
            $pages = min( self::MAX_CATALOGUE_PAGES, max( 1, absint( $data['total_pages'] ?? 1 ) ) );
            foreach ( is_array( $data['templates'] ?? null ) ? $data['templates'] : [] as $row ) {
                if ( ! is_array( $row ) || 'section' !== (string) ( $row['kind'] ?? '' ) ) continue;
                $id = sanitize_key( (string) ( $row['id'] ?? '' ) );
                if ( '' === $id ) continue;
                $all[ $id ] = $row;
            }
        }
        return array_values( $all );
    }

    private static function discovery_pool( array $catalogue, string $role ): array {
        $scored = [];
        foreach ( $catalogue as $row ) {
            if ( ! is_array( $row ) ) continue;
            $text = strtolower( implode( ' ', [
                (string) ( $row['id'] ?? '' ),
                (string) ( $row['title'] ?? '' ),
                (string) ( $row['subtype'] ?? '' ),
                implode( ' ', array_map( 'strval', (array) ( $row['categories'] ?? [] ) ) ),
            ] ) );
            $scored[] = [ 'row' => $row, 'score' => self::metadata_role_score( $text, $role ) ];
        }
        usort( $scored, static function ( array $a, array $b ): int {
            if ( $a['score'] === $b['score'] ) return strcmp( (string) ( $a['row']['id'] ?? '' ), (string) ( $b['row']['id'] ?? '' ) );
            return $b['score'] <=> $a['score'];
        } );

        $pool = [];
        foreach ( $scored as $item ) {
            if ( $item['score'] <= 0 && count( $pool ) >= 8 ) break;
            $pool[] = $item['row'];
            if ( count( $pool ) >= self::DISCOVERY_POOL_LIMIT ) break;
        }
        if ( count( $pool ) < 8 ) {
            foreach ( $scored as $item ) {
                $id = (string) ( $item['row']['id'] ?? '' );
                $exists = false;
                foreach ( $pool as $p ) if ( (string) ( $p['id'] ?? '' ) === $id ) { $exists = true; break; }
                if ( $exists ) continue;
                $pool[] = $item['row'];
                if ( count( $pool ) >= min( self::DISCOVERY_POOL_LIMIT, count( $catalogue ) ) ) break;
            }
        }
        return $pool;
    }

    private static function metadata_role_score( string $text, string $role ): int {
        $tokens = [
            'hero' => [ 'hero', 'intro', 'header', 'banner' ],
            'lead_capture' => [ 'contact', 'form', 'newsletter', 'subscribe', 'booking', 'appointment', 'lead' ],
            'feature_grid' => [ 'feature', 'features', 'service', 'services', 'benefit', 'benefits', 'icon', 'boxes' ],
            'split_media_feature' => [ 'feature', 'content', 'showcase', 'image', 'about' ],
            'cta' => [ 'cta', 'call-to-action', 'action', 'contact' ],
            'content' => [ 'content', 'about', 'text' ],
        ];
        $score = 0;
        foreach ( $tokens[ $role ] ?? [] as $token ) if ( false !== strpos( $text, $token ) ) $score += 10;
        return $score;
    }

    private static function inspect_structures( int $page_id, array $template_ids ) {
        $out = [];
        foreach ( array_chunk( array_values( array_unique( $template_ids ) ), self::STRUCTURE_BATCH_LIMIT ) as $batch ) {
            $inner = new WP_REST_Request( 'POST', '/' . self::NS . '/pages/' . $page_id . '/pixfort/template-structure' );
            $inner->set_param( 'id', $page_id );
            $inner->set_param( 'template_ids', $batch );
            $response = rest_do_request( $inner );
            if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) {
                return new WP_Error( 'elementize_semantic_shortlist_structure_failed', 'Pixfort template structure inspection failed during semantic shortlisting.' );
            }
            $data = $response->get_data();
            if ( ! is_array( $data ) ) continue;
            foreach ( is_array( $data['templates'] ?? null ) ? $data['templates'] : [] as $template ) if ( is_array( $template ) ) $out[] = $template;
        }
        return $out;
    }

    private static function semantic_score( array $template, array $target, string $role ): array {
        $roles = is_array( $template['role_counts'] ?? null ) ? $template['role_counts'] : [];
        $heading = absint( $roles['heading'] ?? 0 );
        $text = absint( $roles['text'] ?? 0 );
        $button = absint( $roles['button_cta'] ?? 0 );
        $image = absint( $roles['image'] ?? 0 );
        $icon = absint( $roles['icon'] ?? 0 );
        $form = absint( $roles['form'] ?? 0 );
        $shortcode = absint( $roles['shortcode'] ?? 0 );

        $score = 20;
        $requirements_met = true;
        $reasons = [];
        switch ( $role ) {
            case 'hero':
                if ( $heading >= 1 ) { $score += 25; $reasons[] = 'has heading'; } else { $requirements_met = false; $reasons[] = 'missing heading'; }
                if ( $button >= 1 ) { $score += 20; $reasons[] = 'has CTA'; } else { $score -= 8; $reasons[] = 'no CTA'; }
                if ( $text >= 1 ) $score += 10;
                if ( $image >= 1 ) $score += 8;
                if ( $form > 0 ) $score -= 12;
                break;
            case 'lead_capture':
                if ( $form >= 1 ) { $score += 38; $reasons[] = 'has form'; } else { $requirements_met = false; $reasons[] = 'missing form'; }
                if ( $heading >= 1 ) $score += 16;
                if ( $text >= 1 ) $score += 10;
                if ( $button >= 1 ) $score += 8;
                break;
            case 'feature_grid':
                if ( $heading >= 2 || $icon >= 3 ) { $score += 30; $reasons[] = 'supports repeated feature items'; } else { $requirements_met = false; $reasons[] = 'insufficient repeated feature structure'; }
                if ( $icon >= 3 ) $score += 18;
                if ( $text >= 2 ) $score += 12;
                if ( $image > 2 ) $score -= 5;
                break;
            case 'split_media_feature':
                if ( $image >= 1 ) { $score += 28; $reasons[] = 'has media'; } else { $requirements_met = false; $reasons[] = 'missing media'; }
                if ( $heading >= 1 ) $score += 18;
                if ( $text >= 1 ) $score += 14;
                if ( $button >= 1 ) $score += 7;
                break;
            case 'cta':
                if ( $button >= 1 ) { $score += 32; $reasons[] = 'has CTA'; } else { $requirements_met = false; $reasons[] = 'missing CTA'; }
                if ( $heading >= 1 ) $score += 20;
                if ( $text >= 1 ) $score += 10;
                if ( $form > 0 ) $score -= 10;
                break;
            default:
                if ( $heading >= 1 ) $score += 22;
                if ( $text >= 1 ) $score += 24;
                if ( 0 === $heading && 0 === $text ) $requirements_met = false;
                if ( $image >= 1 ) $score += 8;
                break;
        }

        $target_roles = is_array( $target['role_counts'] ?? null ) ? $target['role_counts'] : [];
        $presence_keys = [ 'heading', 'text', 'button_cta', 'image', 'icon', 'form' ];
        $same_presence = 0;
        foreach ( $presence_keys as $key ) {
            $a = absint( $target_roles[ $key ] ?? 0 ) > 0;
            $b = absint( $roles[ $key ] ?? 0 ) > 0;
            if ( $a === $b ) $same_presence++;
        }
        $presence_similarity = (int) round( 15 * ( $same_presence / count( $presence_keys ) ) );
        $score += $presence_similarity;

        $target_form = absint( $target_roles['form'] ?? 0 ) > 0;
        $target_shortcode = absint( $target_roles['shortcode'] ?? 0 ) > 0;
        $dependency_risk = 'low';
        if ( ( $form > 0 && ! $target_form ) || ( $shortcode > 0 && ! $target_shortcode ) ) {
            $score -= 18;
            $dependency_risk = 'high';
            $reasons[] = 'adds dependency-heavy structure absent from target';
        } elseif ( ! empty( $template['dependency_indicators']['widgets'] ) ) {
            $score -= 6;
            $dependency_risk = 'medium';
        }

        $score = max( 0, min( 100, $score ) );
        $eligible = $requirements_met && $score >= 55 && 'high' !== $dependency_risk;
        return [
            'score' => $score,
            'eligible' => $eligible,
            'requirements_met' => $requirements_met,
            'dependency_risk' => $dependency_risk,
            'structural_match' => $score >= 80 ? 'strong' : ( $score >= 65 ? 'good' : ( $score >= 55 ? 'acceptable' : 'weak' ) ),
            'reason' => self::clip( implode( '; ', array_values( array_unique( $reasons ) ) ), 420 ),
        ];
    }

    private static function classify_target_role( string $marker, array $structure ): array {
        $roles = is_array( $structure['role_counts'] ?? null ) ? $structure['role_counts'] : [];
        $heading = absint( $roles['heading'] ?? 0 );
        $text = absint( $roles['text'] ?? 0 );
        $button = absint( $roles['button_cta'] ?? 0 );
        $image = absint( $roles['image'] ?? 0 );
        $icon = absint( $roles['icon'] ?? 0 );
        $form = absint( $roles['form'] ?? 0 );

        if ( $form > 0 ) return [ 'role' => 'lead_capture', 'confidence' => 'high', 'reason' => 'The current target contains a form-like widget.' ];
        if ( 'S1' === $marker && $heading > 0 && $button > 0 ) return [ 'role' => 'hero', 'confidence' => 'high', 'reason' => 'The first section contains both heading and CTA structure.' ];
        if ( $icon >= 3 && ( $heading >= 2 || $text >= 2 ) ) return [ 'role' => 'feature_grid', 'confidence' => 'high', 'reason' => 'The target contains repeated icon/content structure.' ];
        if ( $image > 0 && $heading > 0 && $text > 0 ) return [ 'role' => 'split_media_feature', 'confidence' => 'medium', 'reason' => 'The target combines media with heading and body copy.' ];
        if ( $button > 0 && $heading > 0 && $image === 0 ) return [ 'role' => 'cta', 'confidence' => 'medium', 'reason' => 'The target is primarily heading/copy plus CTA without media.' ];
        return [ 'role' => 'content', 'confidence' => 'medium', 'reason' => 'The target is best described conservatively as a general content section.' ];
    }

    private static function summarize_nodes( array $nodes ): array {
        $stats = [
            'total_elements' => 0,
            'widget_count' => 0,
            'role_counts' => [ 'heading' => 0, 'text' => 0, 'button_cta' => 0, 'image' => 0, 'icon' => 0, 'form' => 0, 'shortcode' => 0 ],
            'widget_types' => [],
            'alignment_counts' => [ 'left' => 0, 'center' => 0, 'right' => 0, 'justify' => 0 ],
        ];
        self::walk_nodes( $nodes, $stats );
        arsort( $stats['widget_types'] );
        return [
            'total_elements' => $stats['total_elements'],
            'widget_count' => $stats['widget_count'],
            'role_counts' => $stats['role_counts'],
            'dominant_alignment' => self::dominant_alignment( $stats['alignment_counts'] ),
            'widget_types' => array_slice( $stats['widget_types'], 0, 12, true ),
        ];
    }

    private static function walk_nodes( array $nodes, array &$stats ): void {
        foreach ( $nodes as $node ) {
            if ( ! is_array( $node ) ) continue;
            $stats['total_elements']++;
            $widget = strtolower( trim( (string) ( $node['widgetType'] ?? '' ) ) );
            $settings = is_array( $node['settings'] ?? null ) ? $node['settings'] : [];
            if ( '' !== $widget ) {
                $stats['widget_count']++;
                $stats['widget_types'][ $widget ] = ( $stats['widget_types'][ $widget ] ?? 0 ) + 1;
                if ( preg_match( '/(^|[-_])(heading|title|headline|sliding-text)([-_]|$)/', $widget ) ) $stats['role_counts']['heading']++;
                if ( 'text-editor' === $widget || preg_match( '/(^|[-_])text([-_]|$)/', $widget ) ) $stats['role_counts']['text']++;
                if ( preg_match( '/button|(^|[-_])btn([-_]|$)|(^|[-_])cta([-_]|$)/', $widget ) ) $stats['role_counts']['button_cta']++;
                if ( preg_match( '/image|(^|[-_])img([-_]|$)|gallery/', $widget ) ) $stats['role_counts']['image']++;
                if ( preg_match( '/icon/', $widget ) ) $stats['role_counts']['icon']++;
                if ( preg_match( '/form|contact|newsletter|subscribe|wpforms|gravity/', $widget ) ) $stats['role_counts']['form']++;
                if ( preg_match( '/shortcode/', $widget ) ) $stats['role_counts']['shortcode']++;
            }
            foreach ( $settings as $key => $value ) {
                $key_l = strtolower( (string) $key );
                if ( is_scalar( $value ) && false !== strpos( $key_l, 'align' ) ) {
                    $align = strtolower( trim( (string) $value ) );
                    if ( isset( $stats['alignment_counts'][ $align ] ) ) $stats['alignment_counts'][ $align ]++;
                }
            }
            $children = is_array( $node['elements'] ?? null ) ? $node['elements'] : [];
            if ( $children ) self::walk_nodes( $children, $stats );
        }
    }

    private static function dominant_alignment( array $counts ): string {
        $max = max( $counts );
        if ( $max < 1 ) return 'unknown';
        $leaders = [];
        foreach ( $counts as $key => $count ) if ( $count === $max ) $leaders[] = $key;
        return 1 === count( $leaders ) ? $leaders[0] : 'mixed';
    }

    private static function ledger_row( array $snapshot, string $marker ) {
        foreach ( array_values( array_filter( (array) ( $snapshot['section_ledger'] ?? [] ), 'is_array' ) ) as $row ) {
            if ( $marker === strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) ) ) return $row;
        }
        return new WP_Error( 'elementize_semantic_shortlist_marker_missing', 'The requested target marker is missing from the grounded section ledger.' );
    }

    private static function elementor_data( int $page_id ) {
        $raw = get_post_meta( $page_id, '_elementor_data', true );
        if ( is_array( $raw ) ) return array_values( $raw );
        if ( ! is_string( $raw ) || '' === trim( $raw ) ) return new WP_Error( 'elementize_semantic_shortlist_elementor_missing', 'The page has no inspectable Elementor data.' );
        $decoded = json_decode( $raw, true );
        return is_array( $decoded ) ? array_values( $decoded ) : new WP_Error( 'elementize_semantic_shortlist_elementor_invalid', 'The page Elementor data is not valid JSON.' );
    }

    private static function find_top_level( array $nodes, string $id ): ?array {
        foreach ( $nodes as $node ) if ( is_array( $node ) && $id === (string) ( $node['id'] ?? '' ) ) return $node;
        return null;
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
        $data['aesthetic_semantic_shortlist_version'] = self::VERSION;
        $data['aesthetic_semantic_shortlist'] = true;
        $data['aesthetic_semantic_shortlist_read_only'] = true;
        $data['aesthetic_semantic_shortlist_metadata_discovery_only'] = true;
        $data['aesthetic_semantic_shortlist_structure_grounded'] = true;
        $data['aesthetic_semantic_shortlist_max_candidates'] = self::MAX_SHORTLIST_LIMIT;
        $data['aesthetic_semantic_shortlist_action_slot_cost'] = 0;
        $data['aesthetic_semantic_shortlist_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

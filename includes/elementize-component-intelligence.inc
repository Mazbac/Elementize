<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only component/template ranking for senior-designer planning.
 * The server scores grounded implementation fit; ChatGPT remains the design authority.
 */
final class Elementize_Component_Intelligence {
    private const VERSION = '3';
    private const MAX_INDEX_ITEMS = 500;
    private const MAX_METADATA_SHORTLIST = 16;
    private const MAX_RETURN = 8;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 28 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/component-candidates', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'rank' ],
            'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
            'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
        ] );
    }
    public static function rank( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $input = self::request_input( $request );
        if ( is_wp_error( $input ) ) return $input;

        $profile = Elementize_Design::profile_for_elements( $loaded['elements'], '', 700 );
        $page_tokens = self::page_token_sets( $profile );
        $indexed = self::template_index();
        if ( is_wp_error( $indexed ) ) return $indexed;

        $metadata = [];
        foreach ( $indexed as $item ) {
            if ( $input['provider'] !== 'all' && $input['provider'] !== (string) ( $item['provider'] ?? '' ) ) continue;
            if ( ! in_array( (string) ( $item['type'] ?? '' ), [ 'section', 'container' ], true ) ) continue;
            $metadata[] = [ 'item' => $item, 'score' => self::metadata_score( $item, $input ) ];
        }
        usort( $metadata, static fn( array $a, array $b ): int => $b['score'] <=> $a['score'] );
        $metadata = array_slice( $metadata, 0, self::MAX_METADATA_SHORTLIST );

        $ranked = [];
        $skipped = [];
        foreach ( $metadata as $entry ) {
            $item = $entry['item'];
            $source = sanitize_key( (string) ( $item['source_id'] ?? '' ) );
            $template = sanitize_text_field( (string) ( $item['template_id'] ?? '' ) );
            if ( '' === $source || '' === $template ) continue;
            $template_data = Elementize_Templates::load_template( $source, $template );
            if ( is_wp_error( $template_data ) ) {
                $skipped[] = [
                    'source_id' => $source,
                    'template_id' => $template,
                    'title' => (string) ( $item['title'] ?? '' ),
                    'reason' => sanitize_text_field( $template_data->get_error_message() ),
                ];
                continue;
            }
            $ranked[] = self::score_candidate( $item, $template_data, $input, $page_tokens, (int) $entry['score'] );
        }

        usort( $ranked, static function( array $a, array $b ): int {
            $score = (int) $b['score'] <=> (int) $a['score'];
            if ( 0 !== $score ) return $score;
            return strcasecmp( (string) $a['title'], (string) $b['title'] );
        } );

        $state = Elementize_Capabilities::state();
        return new WP_REST_Response( [
            'component_intelligence_version' => self::VERSION,
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'capability_revision' => (int) ( $state['revision'] ?? 0 ),
            'read_only' => true,
            'request' => [
                'purpose' => $input['purpose'],
                'contract' => $input['contract'],
                'provider' => $input['provider'],
                'keywords' => $input['keywords'],
                'limit' => $input['limit'],
                'blueprint_grounded' => $input['blueprint_grounded'],
            ],
            'blueprint_grounding' => $input['blueprint_context'],
            'scoring_contract' => self::scoring_contract(),
            'index_items_considered' => count( $indexed ),
            'metadata_shortlist_count' => count( $metadata ),
            'candidate_count' => min( count( $ranked ), $input['limit'] ),
            'candidates' => array_slice( $ranked, 0, $input['limit'] ),
            'skipped_inspections' => array_slice( $skipped, 0, 12 ),
        ], 200 );
    }

    private static function request_input( WP_REST_Request $request ) {
        $blueprint_context = null;
        $blueprint_raw = $request->get_param( 'blueprint' );
        if ( is_array( $blueprint_raw ) ) {
            $selected_id = sanitize_key( (string) $request->get_param( 'selected_section_id' ) );
            if ( '' === $selected_id ) return new WP_Error( 'elementize_blueprint_section_required', 'selected_section_id is required when blueprint is supplied.', [ 'status' => 400 ] );
            $expected = strtolower( trim( sanitize_text_field( (string) $request->get_param( 'expected_blueprint_fingerprint' ) ) ) );
            $validated = Elementize_Blueprint::normalize( $blueprint_raw, $selected_id, $expected );
            if ( is_wp_error( $validated ) ) return $validated;
            $section = (array) ( $validated['selected_section'] ?? [] );
            $purpose = (string) ( $section['purpose'] ?? '' );
            $contract = (string) ( $section['contract'] ?? 'generic' );
            $keywords = (array) ( $section['keywords'] ?? [] );
            $semantic_context = trim( implode( ' ', array_merge( [ $purpose, (string) ( $section['narrative_role'] ?? '' ), (string) ( $section['layout_pattern'] ?? '' ) ], (array) ( $section['component_constraints'] ?? [] ) ) ) );
            $blueprint_context = [
                'blueprint_version' => (string) ( $validated['blueprint_version'] ?? '' ),
                'blueprint_fingerprint' => (string) ( $validated['fingerprint'] ?? '' ),
                'selected_section_id' => $selected_id,
                'selected_section' => $section,
                'acceptance_plan' => (array) ( $validated['normalized']['acceptance_plan'] ?? [] ),
            ];
        } else {
            $purpose = trim( sanitize_textarea_field( (string) $request->get_param( 'purpose' ) ) );
            if ( '' === $purpose || self::length( $purpose ) > 800 ) return new WP_Error( 'elementize_component_purpose_required', 'purpose is required when blueprint is not supplied and must be at most 800 characters.', [ 'status' => 400 ] );
            $contract = sanitize_key( (string) $request->get_param( 'contract' ) );
            if ( '' === $contract ) $contract = 'generic';
            $keywords = $request->get_param( 'keywords' );
            $semantic_context = $purpose;
        }
        $contracts = [ 'feature_grid', 'content_split', 'testimonial_grid', 'cta', 'generic' ];
        if ( ! in_array( $contract, $contracts, true ) ) return new WP_Error( 'elementize_component_contract_invalid', 'Unsupported component contract.', [ 'status' => 400 ] );
        $provider = sanitize_key( (string) $request->get_param( 'provider' ) );
        if ( '' === $provider ) $provider = 'all';
        if ( ! in_array( $provider, [ 'all', 'pixfort', 'local' ], true ) ) return new WP_Error( 'elementize_component_provider_invalid', 'provider must be all, pixfort, or local.', [ 'status' => 400 ] );

        $clean = [];
        foreach ( array_slice( (array) $keywords, 0, 12 ) as $value ) {
            if ( ! is_scalar( $value ) ) continue;
            $value = trim( sanitize_text_field( (string) $value ) );
            if ( '' !== $value && self::length( $value ) <= 80 ) $clean[] = $value;
        }
        $limit = absint( $request->get_param( 'limit' ) );
        if ( $limit < 1 ) $limit = 5;
        $limit = min( self::MAX_RETURN, $limit );
        return [
            'purpose' => $purpose,
            'contract' => $contract,
            'provider' => $provider,
            'keywords' => array_values( array_unique( $clean ) ),
            'semantic_context' => $semantic_context,
            'blueprint_grounded' => null !== $blueprint_context,
            'blueprint_context' => $blueprint_context,
            'limit' => $limit,
        ];
    }

    private static function template_index() {
        $items = [];
        for ( $page = 1; $page <= 5 && count( $items ) < self::MAX_INDEX_ITEMS; $page++ ) {
            $r = new WP_REST_Request( 'GET', '/' . Elementize_Core::NS . '/templates' );
            $r->set_param( 'q', '' );
            $r->set_param( 'type', 'all' );
            $r->set_param( 'provider', 'all' );
            $r->set_param( 'page', $page );
            $r->set_param( 'per_page', 100 );
            $response = Elementize_Templates::search( $r );
            if ( is_wp_error( $response ) ) return $response;
            $data = $response instanceof WP_REST_Response ? $response->get_data() : $response;
            if ( ! is_array( $data ) ) break;
            $batch = is_array( $data['items'] ?? null ) ? $data['items'] : [];
            foreach ( $batch as $item ) if ( is_array( $item ) ) $items[] = $item;
            if ( count( $batch ) < 100 ) break;
        }
        return array_slice( $items, 0, self::MAX_INDEX_ITEMS );
    }

    private static function metadata_score( array $item, array $input ): int {
        $tokens = self::tokens( implode( ' ', array_merge( [ $input['semantic_context'] ?? $input['purpose'], $input['contract'] ], $input['keywords'] ) ) );
        $haystack = self::lower( implode( ' ', [
            (string) ( $item['title'] ?? '' ),
            (string) ( $item['subtype'] ?? '' ),
            (string) ( $item['type'] ?? '' ),
            implode( ' ', (array) ( $item['keywords'] ?? [] ) ),
        ] ) );
        $hits = 0;
        foreach ( $tokens as $token ) if ( false !== strpos( $haystack, $token ) ) $hits++;
        $score = min( 15, $hits * 3 );
        foreach ( self::contract_terms( $input['contract'] ) as $term ) {
            if ( false !== strpos( $haystack, $term ) ) { $score += 5; break; }
        }
        if ( 'pixfort' === (string) ( $item['provider'] ?? '' ) ) $score += 2;
        return min( 20, $score );
    }

    private static function contract_terms( string $contract ): array {
        return match ( $contract ) {
            'testimonial_grid' => [ 'testimonial', 'review', 'reviews', 'quote', 'client' ],
            'feature_grid' => [ 'feature', 'features', 'service', 'services', 'card', 'cards', 'solution', 'solutions' ],
            'cta' => [ 'cta', 'call-to-action', 'action', 'contact' ],
            'content_split' => [ 'content', 'about', 'intro', 'image', 'showcase', 'story' ],
            default => [],
        };
    }

    private static function score_candidate( array $item, array $template, array $input, array $page_tokens, int $metadata_score ): array {
        $elements = (array) ( $template['elements'] ?? [] );
        $summary = Elementize_Templates::structure_summary( $elements );
        $fields = Elementize_Content::discover_items( $elements );
        $controls = Elementize_Design::controls_for_template( $elements, 500 );
        $families = self::widget_families( (array) ( $summary['widgets'] ?? [] ) );

        $structure = self::structure_score( $input['contract'], $families, $summary );
        $design = self::design_score( $controls, $page_tokens );
        $responsive = self::responsive_score( $controls, $summary );
        $safety = self::safety_score( $summary );
        $efficiency = self::edit_efficiency_score( count( $fields ), count( $controls ), (int) ( $summary['element_count'] ?? 0 ) );
        $total = min( 100, $metadata_score + $structure + $design + $responsive + $safety + $efficiency['score'] );

        return [
            'score' => $total,
            'source_id' => (string) ( $item['source_id'] ?? '' ),
            'template_id' => (string) ( $item['template_id'] ?? '' ),
            'provider' => (string) ( $item['provider'] ?? '' ),
            'title' => (string) ( $item['title'] ?? '' ),
            'type' => (string) ( $item['type'] ?? '' ),
            'subscores' => [
                'semantic_metadata' => $metadata_score,
                'structural_fit' => $structure,
                'design_compatibility' => $design,
                'responsive_confidence' => $responsive,
                'dependency_safety' => $safety,
                'edit_efficiency' => $efficiency['score'],
            ],
            'estimated_normalization_cost' => $efficiency['cost'],
            'evidence' => [
                'root_count' => (int) ( $summary['root_count'] ?? 0 ),
                'element_count' => (int) ( $summary['element_count'] ?? 0 ),
                'widget_count' => (int) ( $summary['widget_count'] ?? 0 ),
                'max_depth' => (int) ( $summary['max_depth'] ?? 0 ),
                'widget_families' => $families,
                'editable_content_fields' => count( $fields ),
                'design_controls' => count( $controls ),
                'explicit_responsive_controls' => self::responsive_control_count( $controls ),
                'dynamic_values' => (int) ( $summary['dynamic_values'] ?? 0 ),
                'global_style_values' => (int) ( $summary['global_style_values'] ?? 0 ),
            ],
            'recommendation' => self::recommendation( $total, $efficiency['cost'], $safety, $responsive ),
        ];
    }

    public static function widget_families( array $widgets ): array {
        $families = [];
        foreach ( $widgets as $widget => $count ) {
            $family = self::widget_family( (string) $widget );
            $families[ $family ] = ( $families[ $family ] ?? 0 ) + max( 1, (int) $count );
        }
        arsort( $families );
        return $families;
    }

    private static function widget_family( string $widget ): string {
        $w = strtolower( $widget );
        $map = [
            'testimonial' => [ 'testimonial', 'review', 'quote' ],
            'accordion' => [ 'accordion', 'faq', 'toggle' ],
            'pricing' => [ 'pricing', 'price' ],
            'feature' => [ 'feature', 'fancybox', 'card', 'icon-box', 'service' ],
            'button' => [ 'button', 'btn' ],
            'heading' => [ 'heading', 'title', 'highlighted-text' ],
            'image' => [ 'image', 'img', 'photo', 'gallery' ],
            'text' => [ 'text', 'paragraph', 'editor' ],
            'form' => [ 'form', 'input' ],
            'tabs' => [ 'tab' ],
            'video' => [ 'video' ],
            'counter' => [ 'counter', 'number' ],
            'logo' => [ 'logo' ],
            'icon' => [ 'icon' ],
        ];
        foreach ( $map as $family => $needles ) {
            foreach ( $needles as $needle ) if ( false !== strpos( $w, $needle ) ) return $family;
        }
        return 'other';
    }

    public static function structure_score( string $contract, array $families, array $summary ): int {
        $count = static fn( string $family ): int => (int) ( $families[ $family ] ?? 0 );
        $score = 0;
        if ( 'feature_grid' === $contract ) {
            $score += min( 16, $count( 'feature' ) * 4 );
            if ( $count( 'heading' ) > 0 ) $score += 7;
            if ( $count( 'text' ) > 0 ) $score += 3;
        } elseif ( 'content_split' === $contract ) {
            foreach ( [ 'heading' => 8, 'text' => 7, 'image' => 8, 'button' => 4 ] as $family => $points ) if ( $count( $family ) > 0 ) $score += $points;
        } elseif ( 'testimonial_grid' === $contract ) {
            $score += min( 18, $count( 'testimonial' ) * 6 );
            if ( $count( 'heading' ) > 0 ) $score += 7;
        } elseif ( 'cta' === $contract ) {
            if ( $count( 'heading' ) > 0 ) $score += 10;
            if ( $count( 'button' ) > 0 ) $score += 10;
            if ( $count( 'text' ) > 0 ) $score += 6;
            if ( $count( 'feature' ) > 2 ) $score -= min( 8, $count( 'feature' ) * 2 );
        } else {
            $score = 18;
            if ( $count( 'heading' ) > 0 ) $score += 4;
            if ( $count( 'text' ) > 0 ) $score += 3;
        }
        $elements = (int) ( $summary['element_count'] ?? 0 );
        $depth = (int) ( $summary['max_depth'] ?? 0 );
        if ( $elements > 90 ) $score -= 5;
        if ( $depth > 10 ) $score -= 4;
        return max( 0, min( 30, $score ) );
    }

    private static function design_score( array $controls, array $page_tokens ): int {
        $considered = 0;
        $matched = 0;
        $portable = 0;
        foreach ( $controls as $control ) {
            if ( ! is_array( $control ) ) continue;
            $category = (string) ( $control['category'] ?? '' );
            if ( 'pixfort_theme_color' === $category ) { $considered++; $portable++; continue; }
            if ( ! in_array( $category, [ 'color', 'spacing', 'radius', 'typography' ], true ) ) continue;
            $considered++;
            $value = self::canonical( $control['value'] ?? null );
            if ( '' !== $value && isset( $page_tokens[ $category ][ $value ] ) ) $matched++;
        }
        if ( 0 === $considered ) return 12;
        $ratio = ( $matched + ( $portable * 0.8 ) ) / $considered;
        return max( 4, min( 20, (int) round( 6 + 14 * $ratio ) ) );
    }

    private static function responsive_score( array $controls, array $summary ): int {
        $explicit = self::responsive_control_count( $controls );
        $elements = (int) ( $summary['element_count'] ?? 0 );
        $depth = (int) ( $summary['max_depth'] ?? 0 );
        $score = 6 + min( 3, intdiv( $explicit, 4 ) );
        if ( $elements > 90 ) $score -= 2;
        if ( $depth > 10 ) $score -= 2;
        return max( 2, min( 10, $score ) );
    }

    private static function safety_score( array $summary ): int {
        if ( ! empty( $summary['embedded_template_references'] ) ) return 0;
        $score = 10;
        $score -= min( 4, (int) ( $summary['dynamic_values'] ?? 0 ) * 2 );
        $score -= min( 3, (int) ( $summary['global_style_values'] ?? 0 ) );
        return max( 0, $score );
    }

    private static function edit_efficiency_score( int $fields, int $controls, int $elements ): array {
        $cost_units = $fields + (int) ceil( $controls / 3 ) + (int) ceil( $elements / 2 );
        if ( $cost_units <= 35 ) return [ 'score' => 10, 'cost' => 'low' ];
        if ( $cost_units <= 70 ) return [ 'score' => 8, 'cost' => 'low_medium' ];
        if ( $cost_units <= 120 ) return [ 'score' => 6, 'cost' => 'medium' ];
        if ( $cost_units <= 190 ) return [ 'score' => 3, 'cost' => 'high' ];
        return [ 'score' => 1, 'cost' => 'very_high' ];
    }
    private static function responsive_control_count( array $controls ): int {
        $count = 0;
        foreach ( $controls as $control ) {
            if ( ! is_array( $control ) ) continue;
            $breakpoint = Elementize_Designer::breakpoint_from_path( (array) ( $control['setting_path'] ?? [] ) );
            if ( in_array( $breakpoint, [ 'tablet', 'mobile' ], true ) ) $count++;
        }
        return $count;
    }

    private static function page_token_sets( array $profile ): array {
        return [
            'color' => self::value_set( $profile['palette'] ?? [] ),
            'spacing' => self::value_set( $profile['spacing_scale'] ?? [] ),
            'radius' => self::value_set( $profile['radius_scale'] ?? [] ),
            'typography' => self::value_set( $profile['typography_tokens'] ?? [] ),
        ];
    }

    private static function value_set( $value ): array {
        $set = [];
        self::collect_values( $value, $set, 0 );
        return $set;
    }
    private static function collect_values( $value, array &$set, int $depth ): void {
        if ( $depth > 8 ) return;
        if ( is_scalar( $value ) || null === $value ) {
            $key = self::canonical( $value );
            if ( '' !== $key ) $set[ $key ] = true;
            return;
        }
        if ( is_array( $value ) ) foreach ( $value as $child ) self::collect_values( $child, $set, $depth + 1 );
    }

    private static function recommendation( int $score, string $cost, int $safety, int $responsive ): string {
        if ( $safety < 6 ) return 'reject_dependency_or_state_risk';
        if ( $score >= 82 && in_array( $cost, [ 'low', 'low_medium', 'medium' ], true ) ) return 'strong_candidate';
        if ( $score >= 68 && $responsive >= 6 ) return 'inspect_and_compare';
        if ( $score >= 55 ) return 'possible_but_normalization_heavy';
        return 'weak_fit';
    }

    private static function scoring_contract(): array {
        return [
            'total' => 100,
            'weights' => [
                'semantic_metadata' => 20,
                'structural_fit' => 30,
                'design_compatibility' => 20,
                'responsive_confidence' => 10,
                'dependency_safety' => 10,
                'edit_efficiency' => 10,
            ],
            'interpretation' => [
                'Higher score means lower expected normalization effort for this page and purpose.',
                'Scores are deterministic planning evidence, not aesthetic truth.',
                'ChatGPT must still inspect the chosen template before insertion and verify the rendered result.',
                'A high score never bypasses Creative Control or any mutation guard.',
            ],
        ];
    }

    private static function tokens( string $value ): array {
        $value = self::lower( preg_replace( '/[^A-Za-z0-9 _-]+/', ' ', $value ) ?: $value );
        $parts = preg_split( '/[\s_-]+/', $value ) ?: [];
        $stop = [ 'the', 'and', 'for', 'with', 'that', 'this', 'from', 'into', 'section', 'page', 'block', 'generic' ];
        return array_values( array_unique( array_filter( $parts, static fn( string $part ): bool => strlen( $part ) >= 3 && ! in_array( $part, $stop, true ) ) ) );
    }

    private static function canonical( $value ): string {
        if ( is_string( $value ) ) return self::lower( trim( $value ) );
        if ( is_scalar( $value ) || null === $value ) return self::lower( wp_json_encode( $value ) ?: '' );
        if ( is_array( $value ) ) {
            $encoded = wp_json_encode( $value );
            return is_string( $encoded ) ? self::lower( $encoded ) : '';
        }
        return '';
    }

    private static function lower( string $value ): string {
        return function_exists( 'mb_strtolower' ) ? mb_strtolower( $value, 'UTF-8' ) : strtolower( $value );
    }

    private static function length( string $value ): int {
        return function_exists( 'mb_strlen' ) ? mb_strlen( $value, 'UTF-8' ) : strlen( $value );
    }
}

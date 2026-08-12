<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Design {
    private const MAX_CONTROLS = 1200;
    private const MAX_VISITS = 24000;
    private const MAX_DEPTH = 80;
    private const MAX_PATH = 30;
    private const MAX_GLOBAL_REFERENCES = 160;
    private const MAX_SITE_TOKENS = 48;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 26 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/design-profile', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'read' ],
            'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'element_id' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'limit' => [ 'type' => 'integer', 'default' => 300, 'minimum' => 1, 'maximum' => self::MAX_CONTROLS, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function read( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $element_id = trim( (string) $request->get_param( 'element_id' ) );
        $limit = min( self::MAX_CONTROLS, max( 1, absint( $request->get_param( 'limit' ) ) ) );
        $profile = self::profile_for_elements( $loaded['elements'], $element_id, $limit );
        $state = Elementize_Capabilities::state();
        return new WP_REST_Response( [
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'editing_profile' => $state['profile'],
            'creative_control_enabled' => $state['creative_enabled'],
            'creative_scope_page_id' => $state['scope_page_id'],
            'capability_revision' => $state['revision'],
            'design_profile' => $profile,
            'creative_guidelines' => self::guidelines(),
        ], 200 );
    }

    public static function profile_for_elements( array $elements, string $element_filter = '', int $limit = 300 ): array {
        $controls = [];
        $state = [ 'visits' => 0 ];
        self::collect_elements( $elements, $controls, $state, $element_filter, 0, $limit );
        $palette = [];
        $spacing = [];
        $radii = [];
        $typography = [];
        foreach ( $controls as $control ) {
            $category = (string) $control['category'];
            $value = $control['value'];
            if ( 'color' === $category ) self::count_color_values( $value, $palette );
            if ( in_array( $category, [ 'spacing', 'size' ], true ) ) self::count_numeric_values( $value, $spacing );
            if ( 'radius' === $category ) self::count_numeric_values( $value, $radii );
            if ( 'typography' === $category ) self::count_typography_value( $control, $typography );
        }

        $site_tokens = self::active_kit_tokens();
        $global_references = self::global_references_for_elements( $elements, $element_filter );
        $resolved_globals = self::resolve_global_references( $global_references, $site_tokens );
        foreach ( (array) ( $resolved_globals['colors'] ?? [] ) as $reference ) {
            $value = (string) ( $reference['value'] ?? '' );
            if ( '' !== $value ) self::count_color_values( $value, $palette );
        }
        foreach ( (array) ( $resolved_globals['typography'] ?? [] ) as $reference ) {
            foreach ( (array) ( $reference['values'] ?? [] ) as $key => $value ) {
                if ( is_scalar( $value ) && '' !== trim( (string) $value ) ) {
                    $token = sanitize_key( (string) $key ) . '=' . trim( (string) $value );
                    $typography[ $token ] = ( $typography[ $token ] ?? 0 ) + 1;
                }
            }
        }

        arsort( $palette );
        arsort( $spacing );
        arsort( $radii );
        arsort( $typography );

        $page_palette = self::count_list( $palette, 16 );
        $page_typography = self::count_list( $typography, 16 );
        return [
            'source' => 'observed_current_page',
            'controls_returned' => count( $controls ),
            'controls_truncated' => count( $controls ) >= $limit || (int) $state['visits'] >= self::MAX_VISITS,
            'palette' => $page_palette,
            'spacing_scale' => self::count_list( $spacing, 16 ),
            'radius_scale' => self::count_list( $radii, 12 ),
            'typography_tokens' => $page_typography,
            'controls' => $controls,
            'global_references' => $resolved_globals,
            'site_design_tokens' => $site_tokens,
            'normalization_candidates' => self::normalization_candidates( $page_palette, $page_typography, $site_tokens ),
        ];
    }

    public static function controls_for_template( array $elements, int $limit = 500 ): array {
        $profile = self::profile_for_elements( $elements, '', $limit );
        return (array) ( $profile['controls'] ?? [] );
    }

    public static function classify_control( array $path, $value ) {
        if ( ! $path || count( $path ) > self::MAX_PATH ) return false;
        foreach ( $path as $part ) if ( '__globals__' === (string) $part ) return false;
        $key = strtolower( (string) end( $path ) );
        if ( '' === $key || str_starts_with( $key, '_' ) ) return false;
        if ( is_string( $value ) && self::dynamic( $value ) ) return false;
        if ( preg_match( '/(^|_)(color|background_color|border_color|text_color|icon_color|fill_color|stroke_color)(_|$)/', $key ) ) {
            // Pixfort frequently stores semantic theme selectors such as
            // gradient-primary, heading-default, body-default, primary and white
            // in settings whose keys contain "color". Those are not literal CSS
            // color fields and must not be treated as writable CSS controls.
            return is_string( $value ) && self::safe_color( $value ) ? 'color' : false;
        }
        if ( preg_match( '/(^|_)(padding|margin|gap|row_gap|column_gap|space_between)(_|$)/', $key ) ) return 'spacing';
        if ( preg_match( '/(^|_)(border_radius|radius)(_|$)/', $key ) ) return 'radius';
        if ( preg_match( '/(^|_)(text_align|align|alignment|justify_content|align_items|content_position|vertical_align)(_|$)/', $key ) ) return 'alignment';
        if ( preg_match( '/(^|_)(font_size|line_height|letter_spacing|font_family|font_weight|text_transform|font_style)(_|$)/', $key ) ) return 'typography';
        if ( preg_match( '/(^|_)(width|max_width|min_width|height|min_height|max_height|size)(_|$)/', $key ) ) return 'size';
        return false;
    }

    public static function validate_control_value( string $category, $value, $current ) {
        if ( gettype( $value ) !== gettype( $current ) ) {
            if ( ! ( is_int( $current ) && is_float( $value ) ) && ! ( is_float( $current ) && is_int( $value ) ) ) {
                return new WP_Error( 'elementize_design_type_mismatch', 'The new design value must use the same value type as the existing Elementor control.', [ 'status' => 400 ] );
            }
        }
        if ( 'color' === $category ) {
            if ( ! is_string( $current ) || ! self::safe_color( $current ) ) {
                return new WP_Error( 'elementize_design_color_semantic_token', 'This setting contains a theme color token rather than a literal CSS color and is not writable as a local CSS color.', [ 'status' => 400 ] );
            }
            if ( ! is_string( $value ) || ! self::safe_color( $value ) ) {
                return new WP_Error( 'elementize_design_color_invalid', 'The design color is not an allowed CSS color value.', [ 'status' => 400 ] );
            }
            return trim( $value );
        }
        if ( 'alignment' === $category ) {
            if ( ! is_string( $value ) || ! in_array( strtolower( trim( $value ) ), [ 'left','center','right','justify','start','end','flex-start','flex-end','space-between','space-around','space-evenly','stretch','baseline','top','middle','bottom' ], true ) ) {
                return new WP_Error( 'elementize_design_alignment_invalid', 'The requested alignment is not in the supported alignment set.', [ 'status' => 400 ] );
            }
            return trim( $value );
        }
        if ( in_array( $category, [ 'spacing', 'radius', 'size', 'typography' ], true ) ) {
            if ( ! self::bounded_css_value( $value, 0 ) ) return new WP_Error( 'elementize_design_value_invalid', 'The requested design control value is outside the safe bounded Elementor value shape.', [ 'status' => 400 ] );
            return $value;
        }
        return new WP_Error( 'elementize_design_control_invalid', 'This Elementor setting is not an approved creative design control.', [ 'status' => 400 ] );
    }

    public static function canonical_json( $value ): string {
        $json = wp_json_encode( $value, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) ? $json : '';
    }

    public static function decode_json_value( string $json, bool &$ok ) {
        $value = json_decode( $json, true );
        $ok = JSON_ERROR_NONE === json_last_error();
        return $value;
    }

    public static function guidelines(): array {
        return [
            'principle' => 'Use templates as structural building blocks, then normalize them to the visual language already observed on the target page. Prefer page-observed values first. When local page controls are sparse because Elementor globals provide the design language, use exact read-only active-kit tokens returned in normalization_candidates rather than inventing values. Never mutate global references.',
            'complexity_budget' => [
                'max_creative_operations_per_transaction' => 50,
                'max_nested_element_depth' => 80,
                'max_primary_cta_styles_per_section' => 1,
                'max_card_treatments_per_repeated_group' => 1,
                'max_icon_styles_per_repeated_group' => 1,
                'prefer_existing_spacing_scale' => true,
                'prefer_existing_palette' => true,
            ],
            'section_contracts' => [
                'feature_grid' => [ 'heading_groups' => '1', 'cards' => '3-8', 'cta' => '0-1', 'card_style' => 'consistent', 'icon_style' => 'consistent' ],
                'content_split' => [ 'heading_groups' => '1', 'primary_visuals' => '0-1', 'cta' => '0-2', 'columns' => '2' ],
                'testimonial_grid' => [ 'heading_groups' => '1', 'testimonials' => '3-6', 'card_style' => 'consistent' ],
                'cta' => [ 'headlines' => '1', 'supporting_paragraphs' => '0-1', 'primary_cta' => '1', 'secondary_cta' => '0-1' ],
                'generic' => [ 'rule' => 'Keep hierarchy simple and remove template content that does not serve the user request.' ],
            ],
        ];
    }

    private static function collect_elements( array $elements, array &$controls, array &$state, string $filter, int $depth, int $limit ): void {
        if ( $depth > self::MAX_DEPTH || count( $controls ) >= $limit ) return;
        foreach ( $elements as $element ) {
            if ( count( $controls ) >= $limit || (int) $state['visits'] >= self::MAX_VISITS ) return;
            if ( ! is_array( $element ) ) continue;
            $id = trim( (string) ( $element['id'] ?? '' ) );
            if ( '' !== $id && ( '' === $filter || $filter === $id ) && is_array( $element['settings'] ?? null ) ) self::collect_settings( $element['settings'], [], $id, (string) ( $element['widgetType'] ?? '' ), $controls, $state, $limit );
            if ( '' === $filter || $filter !== $id ) if ( is_array( $element['elements'] ?? null ) ) self::collect_elements( $element['elements'], $controls, $state, $filter, $depth + 1, $limit );
        }
    }

    private static function collect_settings( $value, array $path, string $id, string $widget, array &$controls, array &$state, int $limit ): void {
        if ( count( $controls ) >= $limit || count( $path ) > self::MAX_PATH || (int) $state['visits'] >= self::MAX_VISITS ) return;
        $state['visits'] = (int) $state['visits'] + 1;
        if ( is_array( $value ) ) {
            $category = self::classify_control( $path, $value );
            if ( false !== $category ) { $controls[] = self::control( $category, $id, $widget, $path, $value ); return; }
            foreach ( $value as $key => $child ) {
                self::collect_settings( $child, array_merge( $path, [ is_int( $key ) ? $key : (string) $key ] ), $id, $widget, $controls, $state, $limit );
                if ( count( $controls ) >= $limit || (int) $state['visits'] >= self::MAX_VISITS ) return;
            }
            return;
        }
        $category = self::classify_control( $path, $value );
        if ( false !== $category ) $controls[] = self::control( $category, $id, $widget, $path, $value );
    }

    private static function control( string $category, string $id, string $widget, array $path, $value ): array {
        return [
            'category' => $category,
            'element_id' => $id,
            'widget_type' => $widget,
            'setting_path' => array_values( $path ),
            'setting_key' => (string) end( $path ),
            'value' => $value,
            'value_json' => self::canonical_json( $value ),
            'value_semantics' => 'color' === $category ? 'literal_css_color' : 'bounded_elementor_value',
            'writable_in_creative_mode' => true,
        ];
    }

    private static function global_references_for_elements( array $elements, string $filter = '' ): array {
        $references = [];
        self::collect_global_references_elements( $elements, $filter, $references, 0 );
        return array_slice( $references, 0, self::MAX_GLOBAL_REFERENCES );
    }

    private static function collect_global_references_elements( array $elements, string $filter, array &$references, int $depth ): void {
        if ( $depth > self::MAX_DEPTH || count( $references ) >= self::MAX_GLOBAL_REFERENCES ) return;
        foreach ( $elements as $element ) {
            if ( count( $references ) >= self::MAX_GLOBAL_REFERENCES ) return;
            if ( ! is_array( $element ) ) continue;
            $id = trim( (string) ( $element['id'] ?? '' ) );
            $matches = '' === $filter || $filter === $id;
            if ( $matches && is_array( $element['settings'] ?? null ) ) {
                self::collect_global_references_settings( $element['settings'], $id, (string) ( $element['widgetType'] ?? '' ), $references, 0 );
            }
            if ( '' === $filter || $filter !== $id ) {
                if ( is_array( $element['elements'] ?? null ) ) self::collect_global_references_elements( $element['elements'], $filter, $references, $depth + 1 );
            }
        }
    }

    private static function collect_global_references_settings( $value, string $id, string $widget, array &$references, int $depth ): void {
        if ( ! is_array( $value ) || $depth > 30 || count( $references ) >= self::MAX_GLOBAL_REFERENCES ) return;
        foreach ( $value as $key => $child ) {
            if ( '__globals__' === (string) $key && is_array( $child ) ) {
                foreach ( $child as $setting_key => $reference ) {
                    if ( count( $references ) >= self::MAX_GLOBAL_REFERENCES ) return;
                    if ( ! is_string( $reference ) ) continue;
                    $parsed = self::parse_global_reference( $reference );
                    if ( ! $parsed ) continue;
                    $references[] = [
                        'element_id' => $id,
                        'widget_type' => $widget,
                        'setting_key' => sanitize_text_field( (string) $setting_key ),
                        'reference' => $reference,
                        'token_type' => $parsed['type'],
                        'token_id' => $parsed['id'],
                    ];
                }
                continue;
            }
            if ( is_array( $child ) ) self::collect_global_references_settings( $child, $id, $widget, $references, $depth + 1 );
        }
    }

    private static function parse_global_reference( string $reference ): ?array {
        if ( ! preg_match( '#^globals/(colors|typography)\?id=([A-Za-z0-9_-]{1,80})$#', trim( $reference ), $matches ) ) return null;
        return [ 'type' => 'colors' === $matches[1] ? 'color' : 'typography', 'id' => (string) $matches[2] ];
    }

    private static function active_kit_tokens(): array {
        $result = [
            'source' => 'elementor_active_kit_read_only',
            'available' => false,
            'kit_id' => 0,
            'palette' => [],
            'typography' => [],
            'writable' => false,
        ];
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->kits_manager ) ) return $result;
        $manager = \Elementor\Plugin::$instance->kits_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_current_settings' ) ) return $result;
        try {
            if ( method_exists( $manager, 'get_active_id' ) ) $result['kit_id'] = absint( $manager->get_active_id() );
            $system_colors = $manager->get_current_settings( 'system_colors' );
            $custom_colors = $manager->get_current_settings( 'custom_colors' );
            $system_typography = $manager->get_current_settings( 'system_typography' );
            $custom_typography = $manager->get_current_settings( 'custom_typography' );
        } catch ( Throwable $ignored ) {
            return $result;
        }

        foreach ( [ 'system' => $system_colors, 'custom' => $custom_colors ] as $scope => $items ) {
            foreach ( is_array( $items ) ? $items : [] as $item ) {
                if ( count( $result['palette'] ) >= self::MAX_SITE_TOKENS ) break 2;
                if ( ! is_array( $item ) ) continue;
                $token_id = sanitize_key( (string) ( $item['_id'] ?? '' ) );
                $color = trim( (string) ( $item['color'] ?? '' ) );
                if ( '' === $token_id || ! self::safe_color( $color ) ) continue;
                $result['palette'][] = [
                    'token_id' => $token_id,
                    'title' => sanitize_text_field( (string) ( $item['title'] ?? $token_id ) ),
                    'value' => $color,
                    'scope' => $scope,
                    'reference' => 'globals/colors?id=' . $token_id,
                    'read_only' => true,
                ];
            }
        }

        foreach ( [ 'system' => $system_typography, 'custom' => $custom_typography ] as $scope => $items ) {
            foreach ( is_array( $items ) ? $items : [] as $item ) {
                if ( count( $result['typography'] ) >= self::MAX_SITE_TOKENS ) break 2;
                if ( ! is_array( $item ) ) continue;
                $token_id = sanitize_key( (string) ( $item['_id'] ?? '' ) );
                if ( '' === $token_id ) continue;
                $values = self::typography_token_values( $item );
                if ( ! $values ) continue;
                $result['typography'][] = [
                    'token_id' => $token_id,
                    'title' => sanitize_text_field( (string) ( $item['title'] ?? $token_id ) ),
                    'values' => $values,
                    'scope' => $scope,
                    'reference' => 'globals/typography?id=' . $token_id,
                    'read_only' => true,
                ];
            }
        }
        $result['available'] = ! empty( $result['palette'] ) || ! empty( $result['typography'] );
        return $result;
    }

    private static function typography_token_values( array $item ): array {
        $allowed = [
            'typography_font_family' => 'font_family',
            'typography_font_size' => 'font_size',
            'typography_font_weight' => 'font_weight',
            'typography_text_transform' => 'text_transform',
            'typography_font_style' => 'font_style',
            'typography_line_height' => 'line_height',
            'typography_letter_spacing' => 'letter_spacing',
            'typography_word_spacing' => 'word_spacing',
        ];
        $values = [];
        foreach ( $allowed as $source_key => $target_key ) {
            if ( ! array_key_exists( $source_key, $item ) ) continue;
            $value = $item[ $source_key ];
            if ( '' === $value || null === $value ) continue;
            if ( ! self::bounded_css_value( $value, 0 ) ) continue;
            $values[ $target_key ] = $value;
        }
        return $values;
    }

    private static function resolve_global_references( array $references, array $site_tokens ): array {
        $color_index = [];
        foreach ( (array) ( $site_tokens['palette'] ?? [] ) as $token ) {
            if ( is_array( $token ) && '' !== (string) ( $token['token_id'] ?? '' ) ) $color_index[ (string) $token['token_id'] ] = $token;
        }
        $typography_index = [];
        foreach ( (array) ( $site_tokens['typography'] ?? [] ) as $token ) {
            if ( is_array( $token ) && '' !== (string) ( $token['token_id'] ?? '' ) ) $typography_index[ (string) $token['token_id'] ] = $token;
        }

        $resolved = [ 'count' => count( $references ), 'colors' => [], 'typography' => [], 'unresolved' => [] ];
        foreach ( $references as $reference ) {
            if ( ! is_array( $reference ) ) continue;
            $type = (string) ( $reference['token_type'] ?? '' );
            $token_id = (string) ( $reference['token_id'] ?? '' );
            $token = 'color' === $type ? ( $color_index[ $token_id ] ?? null ) : ( $typography_index[ $token_id ] ?? null );
            if ( ! is_array( $token ) ) {
                $resolved['unresolved'][] = $reference;
                continue;
            }
            $entry = array_merge( $reference, [
                'title' => (string) ( $token['title'] ?? $token_id ),
                'scope' => (string) ( $token['scope'] ?? '' ),
                'read_only' => true,
            ] );
            if ( 'color' === $type ) {
                $entry['value'] = (string) ( $token['value'] ?? '' );
                $resolved['colors'][] = $entry;
            } else {
                $entry['values'] = (array) ( $token['values'] ?? [] );
                $resolved['typography'][] = $entry;
            }
        }
        return $resolved;
    }

    private static function normalization_candidates( array $page_palette, array $page_typography, array $site_tokens ): array {
        $site_palette = [];
        foreach ( array_slice( (array) ( $site_tokens['palette'] ?? [] ), 0, 16 ) as $token ) {
            if ( ! is_array( $token ) ) continue;
            $site_palette[] = [
                'value' => (string) ( $token['value'] ?? '' ),
                'title' => (string) ( $token['title'] ?? '' ),
                'token_id' => (string) ( $token['token_id'] ?? '' ),
                'reference' => (string) ( $token['reference'] ?? '' ),
                'read_only_source' => true,
            ];
        }
        $site_typography = [];
        foreach ( array_slice( (array) ( $site_tokens['typography'] ?? [] ), 0, 12 ) as $token ) {
            if ( ! is_array( $token ) ) continue;
            $site_typography[] = [
                'title' => (string) ( $token['title'] ?? '' ),
                'token_id' => (string) ( $token['token_id'] ?? '' ),
                'values' => (array) ( $token['values'] ?? [] ),
                'reference' => (string) ( $token['reference'] ?? '' ),
                'read_only_source' => true,
            ];
        }
        return [
            'palette_source' => $page_palette ? 'current_page_explicit_or_resolved_global' : ( $site_palette ? 'active_elementor_kit_read_only_fallback' : 'none' ),
            'palette' => $page_palette ?: $site_palette,
            'typography_source' => $page_typography ? 'current_page_explicit_or_resolved_global' : ( $site_typography ? 'active_elementor_kit_read_only_fallback' : 'none' ),
            'typography' => $page_typography ?: $site_typography,
            'rule' => 'Values here are candidates for local writable controls only. A read-only global/site token may supply an exact value, but its global reference must never be mutated.',
        ];
    }

    private static function safe_color( string $value ): bool {
        return '' !== $value && strlen( $value ) <= 128 && (bool) preg_match( '/^(#[0-9a-f]{3,8}|rgba?\([^\r\n]{1,90}\)|hsla?\([^\r\n]{1,90}\)|transparent|currentColor|var\(--[A-Za-z0-9_-]+\))$/i', trim( $value ) );
    }

    private static function dynamic( string $value ): bool {
        $trimmed = ltrim( $value );
        return str_starts_with( $trimmed, '[elementor-tag' ) || false !== strpos( $trimmed, '{{' );
    }

    private static function bounded_css_value( $value, int $depth ): bool {
        if ( $depth > 5 ) return false;
        if ( is_int( $value ) || is_float( $value ) ) return abs( (float) $value ) <= 100000;
        if ( is_bool( $value ) || null === $value ) return true;
        if ( is_string( $value ) ) return strlen( $value ) <= 200 && ! self::dynamic( $value ) && false === strpos( $value, '<' ) && false === strpos( $value, '>' );
        if ( ! is_array( $value ) || count( $value ) > 24 ) return false;
        foreach ( $value as $key => $child ) {
            if ( is_string( $key ) && strlen( $key ) > 80 ) return false;
            if ( ! self::bounded_css_value( $child, $depth + 1 ) ) return false;
        }
        return true;
    }

    private static function count_color_values( $value, array &$counts ): void {
        if ( is_string( $value ) && preg_match( '/^(#[0-9a-f]{3,8}|rgba?\([^\r\n]+\)|hsla?\([^\r\n]+\)|transparent)$/i', trim( $value ) ) ) {
            $key = strtolower( trim( $value ) );
            $counts[ $key ] = ( $counts[ $key ] ?? 0 ) + 1;
        }
        if ( is_array( $value ) ) foreach ( $value as $child ) self::count_color_values( $child, $counts );
    }

    private static function count_numeric_values( $value, array &$counts ): void {
        if ( is_int( $value ) || is_float( $value ) ) { $key = (string) round( (float) $value, 3 ); $counts[ $key ] = ( $counts[ $key ] ?? 0 ) + 1; return; }
        if ( is_string( $value ) && preg_match( '/^-?\d+(?:\.\d+)?(?:px|em|rem|%|vh|vw)?$/i', trim( $value ) ) ) { $key = strtolower( trim( $value ) ); $counts[ $key ] = ( $counts[ $key ] ?? 0 ) + 1; return; }
        if ( is_array( $value ) ) foreach ( $value as $key => $child ) if ( ! in_array( (string) $key, [ 'unit', 'isLinked' ], true ) ) self::count_numeric_values( $child, $counts );
    }

    private static function count_typography_value( array $control, array &$counts ): void {
        $key = (string) ( $control['setting_key'] ?? '' );
        $value = $control['value'] ?? null;
        if ( is_scalar( $value ) && '' !== trim( (string) $value ) ) { $token = $key . '=' . trim( (string) $value ); $counts[ $token ] = ( $counts[ $token ] ?? 0 ) + 1; }
    }

    private static function count_list( array $counts, int $limit ): array {
        $out = [];
        foreach ( array_slice( $counts, 0, $limit, true ) as $value => $count ) $out[] = [ 'value' => (string) $value, 'count' => (int) $count ];
        return $out;
    }
}

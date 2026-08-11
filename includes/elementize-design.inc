<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Design {
    private const MAX_CONTROLS = 1200;
    private const MAX_VISITS = 24000;
    private const MAX_DEPTH = 80;
    private const MAX_PATH = 30;

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
        arsort( $palette );
        arsort( $spacing );
        arsort( $radii );
        arsort( $typography );
        return [
            'source' => 'observed_current_page',
            'controls_returned' => count( $controls ),
            'controls_truncated' => count( $controls ) >= $limit || (int) $state['visits'] >= self::MAX_VISITS,
            'palette' => self::count_list( $palette, 16 ),
            'spacing_scale' => self::count_list( $spacing, 16 ),
            'radius_scale' => self::count_list( $radii, 12 ),
            'typography_tokens' => self::count_list( $typography, 16 ),
            'controls' => $controls,
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
        if ( preg_match( '/(^|_)(color|background_color|border_color|text_color|icon_color|fill_color|stroke_color)(_|$)/', $key ) ) return 'color';
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
            if ( ! is_string( $value ) || strlen( $value ) > 128 || ! preg_match( '/^(#[0-9a-f]{3,8}|rgba?\([^\r\n]{1,90}\)|hsla?\([^\r\n]{1,90}\)|transparent|currentColor|var\(--[A-Za-z0-9_-]+\))$/i', trim( $value ) ) ) {
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
            'principle' => 'Use templates as structural building blocks, then normalize them to the visual language already observed on the target page. Prefer existing tokens over inventing new ones.',
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
        return [ 'category' => $category, 'element_id' => $id, 'widget_type' => $widget, 'setting_path' => array_values( $path ), 'setting_key' => (string) end( $path ), 'value' => $value, 'value_json' => self::canonical_json( $value ), 'writable_in_creative_mode' => true ];
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

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Guarded Pixfort repeater-item semantic text color controls.
 *
 * Some Pixfort widgets expose a repeater switch (`has_color`) plus a semantic
 * selector (`item_color`). Dormant items omit both keys, so normal recursive
 * design discovery cannot expose the control. This adapter derives the exact
 * fields/options from the installed Elementor widget schema and exposes one
 * synthetic item-scoped design control without permitting arbitrary repeater
 * mutation.
 */
final class Elementize_Pixfort_Repeater_Colors {
    private const MAX_SYNTHETIC_CONTROLS = 80;
    private const MAX_OPTIONS = 80;
    private static array $meta_cache = [];

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'augment_design_profile' ], 12, 3 );
    }

    public static function augment_design_profile( $response, array $handler, WP_REST_Request $request ) {
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/(\d+)/design-profile$#', (string) $request->get_route(), $m ) ) return $response;
        if ( is_wp_error( $response ) ) return $response;
        $rest = rest_ensure_response( $response );
        $data = $rest->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['design_profile'] ?? null ) ) return $response;

        $page_id = absint( $m[1] );
        $loaded = Elementize_Core::elements( $page_id );
        if ( is_wp_error( $loaded ) || ! is_array( $loaded['elements'] ?? null ) ) return $response;
        $filter_id = trim( (string) $request->get_param( 'element_id' ) );
        $synthetic = [];
        self::collect_elements( $loaded['elements'], $filter_id, $synthetic, 0 );
        if ( ! $synthetic ) return $response;

        $controls = is_array( $data['design_profile']['controls'] ?? null ) ? $data['design_profile']['controls'] : [];
        foreach ( $synthetic as $control ) {
            if ( count( $controls ) >= 1200 ) break;
            $controls[] = $control;
        }
        $data['design_profile']['controls'] = $controls;
        $data['design_profile']['controls_returned'] = count( $controls );
        $data['design_profile']['pixfort_repeater_color_controls'] = count( $synthetic );
        $rest->set_data( $data );
        return $rest;
    }

    public static function apply_style( array &$elements, string $element_id, array $path, $expected, $new, array &$changes ) {
        if ( count( $path ) !== 3 || ! is_string( $path[0] ) || ! is_int( $path[1] ) || 'item_color' !== (string) $path[2] ) return null;
        $node = Elementize_Tree::find_node( $elements, $element_id );
        if ( ! is_array( $node ) ) return new WP_Error( 'elementize_design_target_missing', 'The repeater design target no longer exists.', [ 'status' => 409 ] );
        $widget = sanitize_key( (string) ( $node['widgetType'] ?? '' ) );
        $repeater_key = sanitize_key( (string) $path[0] );
        $meta = self::control_meta( $widget, $repeater_key );
        if ( ! is_array( $meta ) ) return null;
        if ( ! is_string( $new ) || '' === trim( $new ) ) return new WP_Error( 'elementize_pixfort_repeater_color_invalid', 'Pixfort repeater color requires an exact installed selector token.', [ 'status' => 400 ] );
        $new = trim( $new );
        if ( ! in_array( $new, $meta['writable_values'], true ) ) return new WP_Error( 'elementize_pixfort_repeater_color_not_allowed', 'The installed Pixfort repeater control does not allow that color selector.', [ 'status' => 400 ] );

        if ( in_array( $new, [ 'primary', 'secondary' ], true ) && class_exists( 'Elementize_Pixfort_Theme_Tokens' ) ) {
            $roles = Elementize_Pixfort_Theme_Tokens::theme_roles();
            if ( ! isset( $roles[ $new ] ) || empty( $roles[ $new ]['authoritative'] ) ) {
                return new WP_Error( 'elementize_pixfort_repeater_color_unresolved', 'The requested Pixfort Primary/Secondary role is not grounded in the active Pixfort theme options.', [ 'status' => 400 ] );
            }
        }

        $item_index = (int) $path[1];
        $old_color = null;
        $old_activation = null;
        $result = Elementize_Tree::mutate_setting( $elements, $element_id, [ $repeater_key ], static function( &$items ) use ( $item_index, $expected, $new, $meta, &$old_color, &$old_activation ) {
            if ( ! is_array( $items ) || ! array_key_exists( $item_index, $items ) || ! is_array( $items[ $item_index ] ) ) {
                return new WP_Error( 'elementize_pixfort_repeater_item_changed', 'The Pixfort repeater item changed after the design read.', [ 'status' => 409 ] );
            }
            $current_color = array_key_exists( 'item_color', $items[ $item_index ] ) ? (string) $items[ $item_index ]['item_color'] : '';
            if ( Elementize_Design::canonical_json( $current_color ) !== Elementize_Design::canonical_json( $expected ) ) {
                return new WP_Error( 'elementize_design_value_changed', 'The repeater item color changed after the design read.', [ 'status' => 409 ] );
            }
            $old_color = $current_color;
            $old_activation = array_key_exists( 'has_color', $items[ $item_index ] ) ? (string) $items[ $item_index ]['has_color'] : '';
            $items[ $item_index ]['has_color'] = (string) $meta['activation_value'];
            $items[ $item_index ]['item_color'] = $new;
            return true;
        } );
        if ( is_wp_error( $result ) ) return $result;
        if ( true !== $result ) return new WP_Error( 'elementize_pixfort_repeater_item_missing', 'The Pixfort repeater item no longer exists.', [ 'status' => 409 ] );

        $changes[] = [
            'kind' => 'design_pixfort_repeater_theme_color',
            'element_id' => $element_id,
            'setting_path' => [ $repeater_key, $item_index, 'item_color' ],
            'old_value' => $old_color,
            'new_value' => $new,
        ];
        if ( (string) $old_activation !== (string) $meta['activation_value'] ) {
            $changes[] = [
                'kind' => 'design_pixfort_repeater_color_activation',
                'element_id' => $element_id,
                'setting_path' => [ $repeater_key, $item_index, 'has_color' ],
                'old_value' => $old_activation,
                'new_value' => (string) $meta['activation_value'],
            ];
        }
        return [ 'category' => 'pixfort_repeater_theme_color', 'new' => $new ];
    }
    private static function collect_elements( array $elements, string $filter_id, array &$out, int $depth ): void {
        if ( $depth > 80 || count( $out ) >= self::MAX_SYNTHETIC_CONTROLS ) return;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = trim( (string) ( $element['id'] ?? '' ) );
            $matches = '' === $filter_id || $filter_id === $id;
            if ( $matches ) self::collect_element_controls( $element, $out );
            if ( ( '' === $filter_id || $filter_id !== $id ) && is_array( $element['elements'] ?? null ) ) {
                self::collect_elements( $element['elements'], $filter_id, $out, $depth + 1 );
            }
            if ( count( $out ) >= self::MAX_SYNTHETIC_CONTROLS ) return;
        }
    }

    private static function collect_element_controls( array $element, array &$out ): void {
        $widget = sanitize_key( (string) ( $element['widgetType'] ?? '' ) );
        $id = trim( (string) ( $element['id'] ?? '' ) );
        $settings = is_array( $element['settings'] ?? null ) ? $element['settings'] : [];
        if ( '' === $id || ! str_starts_with( $widget, 'pix-' ) ) return;
        foreach ( $settings as $repeater_key => $items ) {
            if ( ! is_string( $repeater_key ) || ! is_array( $items ) ) continue;
            $meta = self::control_meta( $widget, sanitize_key( $repeater_key ) );
            if ( ! is_array( $meta ) ) continue;
            foreach ( $items as $index => $item ) {
                if ( ! is_int( $index ) || ! is_array( $item ) || count( $out ) >= self::MAX_SYNTHETIC_CONTROLS ) continue;
                $out[] = self::public_control( $id, $widget, $repeater_key, $index, $item, $meta );
            }
        }
    }
    private static function public_control( string $id, string $widget, string $repeater_key, int $index, array $item, array $meta ): array {
        $current = array_key_exists( 'item_color', $item ) ? (string) $item['item_color'] : '';
        $activation = array_key_exists( 'has_color', $item ) ? (string) $item['has_color'] : '';
        $normalization = [];
        foreach ( $meta['options'] as $value => $label ) {
            if ( ! in_array( (string) $value, $meta['writable_values'], true ) ) continue;
            $normalization[] = [
                'value' => (string) $value,
                'label' => (string) $label,
                'selector_source' => 'installed_elementor_pixfort_repeater_control',
                'read_only_source' => true,
            ];
        }
        return [
            'category' => 'pixfort_repeater_theme_color',
            'element_id' => $id,
            'widget_type' => $widget,
            'setting_path' => [ $repeater_key, $index, 'item_color' ],
            'setting_key' => 'item_color',
            'value' => $current,
            'value_json' => Elementize_Design::canonical_json( $current ),
            'value_semantics' => 'pixfort_theme_token',
            'writable_in_creative_mode' => true,
            'token_source' => 'installed_elementor_pixfort_repeater_control',
            'normalization_options' => $normalization,
            'companion_setting_path' => [ $repeater_key, $index, 'has_color' ],
            'companion_current_value' => $activation,
            'companion_required_value' => (string) $meta['activation_value'],
            'repeater_item_id' => sanitize_text_field( (string) ( $item['_id'] ?? '' ) ),
            'repeater_item_text' => sanitize_text_field( substr( (string) ( $item['text'] ?? '' ), 0, 240 ) ),
        ];
    }
    private static function control_meta( string $widget, string $repeater_key ): ?array {
        $cache_key = $widget . '|' . $repeater_key;
        if ( array_key_exists( $cache_key, self::$meta_cache ) ) return self::$meta_cache[ $cache_key ];
        self::$meta_cache[ $cache_key ] = null;
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->widgets_manager ) ) return null;
        $manager = \Elementor\Plugin::$instance->widgets_manager;
        try { $instance = $manager->get_widget_types( $widget ); } catch ( Throwable $ignored ) { $instance = null; }
        if ( ! is_object( $instance ) || ! method_exists( $instance, 'get_controls' ) ) return null;
        try { $controls = $instance->get_controls(); } catch ( Throwable $ignored ) { $controls = []; }
        $repeater = is_array( $controls[ $repeater_key ] ?? null ) ? $controls[ $repeater_key ] : null;
        if ( ! is_array( $repeater ) || 'repeater' !== sanitize_key( (string) ( $repeater['type'] ?? '' ) ) ) return null;
        $fields = is_array( $repeater['fields'] ?? null ) ? $repeater['fields'] : [];
        $activation = is_array( $fields['has_color'] ?? null ) ? $fields['has_color'] : null;
        $color = is_array( $fields['item_color'] ?? null ) ? $fields['item_color'] : null;
        if ( ! is_array( $activation ) || ! is_array( $color ) ) return null;
        if ( 'switcher' !== sanitize_key( (string) ( $activation['type'] ?? '' ) ) || 'select' !== sanitize_key( (string) ( $color['type'] ?? '' ) ) ) return null;
        $activation_value = trim( (string) ( $activation['return_value'] ?? '' ) );
        if ( '' === $activation_value ) return null;
        $options = self::flatten_options( $color['options'] ?? null );
        if ( ! $options ) $options = self::flatten_options( $color['groups'] ?? null );
        if ( ! $options ) return null;
        $writable = [];
        foreach ( $options as $value => $label ) {
            if ( in_array( (string) $value, [ 'custom', 'custom-gradient' ], true ) ) continue;
            $writable[] = (string) $value;
        }
        self::$meta_cache[ $cache_key ] = [
            'activation_value' => $activation_value,
            'options' => $options,
            'writable_values' => $writable,
        ];
        return self::$meta_cache[ $cache_key ];
    }

    private static function flatten_options( $raw, int $depth = 0 ): array {
        if ( ! is_array( $raw ) || $depth > 3 ) return [];
        $out = [];
        foreach ( $raw as $value => $entry ) {
            if ( count( $out ) >= self::MAX_OPTIONS ) break;
            if ( is_array( $entry ) ) {
                if ( is_array( $entry['options'] ?? null ) ) {
                    foreach ( self::flatten_options( $entry['options'], $depth + 1 ) as $nested_value => $label ) {
                        if ( count( $out ) >= self::MAX_OPTIONS ) break;
                        $out[ $nested_value ] = $label;
                    }
                } else {
                    foreach ( self::flatten_options( $entry, $depth + 1 ) as $nested_value => $label ) {
                        if ( count( $out ) >= self::MAX_OPTIONS ) break;
                        $out[ $nested_value ] = $label;
                    }
                }
                continue;
            }
            if ( ! is_scalar( $value ) || ! is_scalar( $entry ) ) continue;
            $value = (string) $value;
            if ( '' === $value || ! preg_match( '/^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/', $value ) ) continue;
            $out[ $value ] = sanitize_text_field( (string) $entry );
        }
        return $out;
    }
}

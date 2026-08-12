<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only adapter for Pixfort semantic color selectors.
 *
 * Pixfort widgets commonly store values such as `primary`, `gradient-primary`,
 * `heading-default` and `body-default` in settings whose names contain `color`.
 * Those values are CSS/theme selector tokens, not literal CSS colors. This
 * adapter never invents such tokens: it discovers exact allowed values from the
 * currently registered Elementor widget controls and only exposes primary or
 * secondary normalization targets when the active Elementor Kit also exposes
 * Pixfort's corresponding Theme Primary/Secondary color token.
 */
final class Elementize_Pixfort_Theme_Tokens {
    private const MAX_OPTIONS = 80;
    private const MAX_WIDGETS = 300;
    private static array $meta_cache = [];
    private static ?array $theme_roles = null;
    private static ?array $widget_types = null;

    public static function control_meta( string $widget_type, string $setting_key, $current_value ): ?array {
        $widget_type = sanitize_key( $widget_type );
        $setting_key = sanitize_key( $setting_key );
        if ( ! str_starts_with( $widget_type, 'pix-' ) || '' === $setting_key || ! self::looks_like_color_key( $setting_key ) ) return null;
        if ( ! is_string( $current_value ) || '' === trim( $current_value ) || self::literal_css_color( $current_value ) ) return null;

        $cache_key = $widget_type . '|' . $setting_key;
        if ( ! array_key_exists( $cache_key, self::$meta_cache ) ) {
            self::$meta_cache[ $cache_key ] = self::read_control_meta( $widget_type, $setting_key );
        }
        $meta = self::$meta_cache[ $cache_key ];
        if ( ! is_array( $meta ) || ! isset( $meta['options'][ $current_value ] ) ) return null;

        $roles = self::theme_roles();
        $normalization = [];
        foreach ( [ 'primary', 'secondary' ] as $token ) {
            if ( ! isset( $meta['options'][ $token ] ) || ! isset( $roles[ $token ] ) ) continue;
            $normalization[] = [
                'value' => $token,
                'label' => (string) $meta['options'][ $token ],
                'theme_role' => $token,
                'resolves_to' => (string) $roles[ $token ]['value'],
                'site_token_id' => (string) $roles[ $token ]['token_id'],
                'site_token_title' => (string) $roles[ $token ]['title'],
                'read_only_source' => true,
            ];
        }
        if ( ! $normalization ) return null;

        $allowed = [];
        foreach ( array_slice( $meta['options'], 0, self::MAX_OPTIONS, true ) as $value => $label ) {
            if ( 'custom' === $value ) continue;
            $allowed[] = [ 'value' => (string) $value, 'label' => (string) $label ];
        }

        return [
            'source' => 'installed_elementor_pixfort_widget_control',
            'widget_type' => $widget_type,
            'setting_key' => $setting_key,
            'current_value' => $current_value,
            'current_label' => (string) ( $meta['options'][ $current_value ] ?? $current_value ),
            'current_theme_role' => self::theme_role_for_token( $current_value ),
            'allowed_values' => $allowed,
            'normalization_options' => $normalization,
        ];
    }

    public static function is_control( string $widget_type, string $setting_key, $current_value ): bool {
        return is_array( self::control_meta( $widget_type, $setting_key, $current_value ) );
    }

    /**
     * Writer-side fallback. The design writer historically receives only the
     * setting path/current value, not the widget type. Keep this fail-closed by
     * requiring an exact installed Pixfort color control with the same setting
     * key/current token, and only allow normalization to primary/secondary when
     * the active Kit exposes the corresponding Pixfort Theme token.
     */
    public static function validate_any_transition( string $setting_key, $current_value, $new_value ) {
        $setting_key = sanitize_key( $setting_key );
        if ( ! self::looks_like_color_key( $setting_key ) || ! is_string( $current_value ) || ! is_string( $new_value ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_invalid', 'Pixfort theme-color normalization requires string selector tokens on a color setting.', [ 'status' => 400 ] );
        }
        $new_value = trim( $new_value );
        if ( ! in_array( $new_value, [ 'primary', 'secondary' ], true ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_target_forbidden', 'Pixfort semantic color normalization is currently limited to the installed theme Primary or Secondary selector.', [ 'status' => 400 ] );
        }
        $roles = self::theme_roles();
        if ( ! isset( $roles[ $new_value ] ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_unresolved', 'The requested Pixfort theme role is not grounded in the active Elementor Kit.', [ 'status' => 400 ] );
        }

        foreach ( self::pixfort_widget_types() as $widget_type ) {
            $meta = self::control_meta( $widget_type, $setting_key, $current_value );
            if ( ! is_array( $meta ) ) continue;
            foreach ( (array) $meta['normalization_options'] as $option ) {
                if ( is_array( $option ) && (string) ( $option['value'] ?? '' ) === $new_value ) return $new_value;
            }
        }
        return new WP_Error( 'elementize_pixfort_theme_token_not_allowed', 'The installed Pixfort control does not allow that semantic color-token transition.', [ 'status' => 400 ] );
    }

    public static function theme_roles(): array {
        if ( null !== self::$theme_roles ) return self::$theme_roles;
        self::$theme_roles = [];
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->kits_manager ) ) return self::$theme_roles;
        $manager = \Elementor\Plugin::$instance->kits_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_current_settings' ) ) return self::$theme_roles;
        try { $colors = $manager->get_current_settings( 'custom_colors' ); } catch ( Throwable $ignored ) { $colors = []; }
        foreach ( is_array( $colors ) ? $colors : [] as $item ) {
            if ( ! is_array( $item ) ) continue;
            $id = sanitize_key( (string) ( $item['_id'] ?? '' ) );
            $role = 'pixprimarycolor' === $id ? 'primary' : ( 'pixsecondarycolor' === $id ? 'secondary' : '' );
            if ( '' === $role ) continue;
            $value = trim( (string) ( $item['color'] ?? '' ) );
            if ( ! self::literal_css_color( $value ) ) continue;
            self::$theme_roles[ $role ] = [
                'token_id' => $id,
                'title' => sanitize_text_field( (string) ( $item['title'] ?? ( 'Theme ' . ucfirst( $role ) . ' Color' ) ) ),
                'value' => $value,
                'read_only' => true,
            ];
        }
        return self::$theme_roles;
    }

    private static function read_control_meta( string $widget_type, string $setting_key ): ?array {
        $widget = self::widget_instance( $widget_type );
        if ( ! is_object( $widget ) || ! method_exists( $widget, 'get_controls' ) ) return null;
        try { $controls = $widget->get_controls(); } catch ( Throwable $ignored ) { return null; }
        if ( ! is_array( $controls ) || ! is_array( $controls[ $setting_key ] ?? null ) ) return null;
        $control = $controls[ $setting_key ];
        $type = sanitize_key( (string) ( $control['type'] ?? '' ) );
        if ( ! in_array( $type, [ 'select', 'select2' ], true ) || ! is_array( $control['options'] ?? null ) ) return null;

        $options = [];
        foreach ( array_slice( $control['options'], 0, self::MAX_OPTIONS, true ) as $value => $label ) {
            if ( ! is_scalar( $value ) || ! is_scalar( $label ) ) continue;
            $value = (string) $value;
            if ( '' === $value || strlen( $value ) > 80 || ! preg_match( '/^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/', $value ) ) continue;
            $options[ $value ] = sanitize_text_field( (string) $label );
        }
        return $options ? [ 'type' => $type, 'options' => $options ] : null;
    }

    private static function widget_instance( string $widget_type ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->widgets_manager ) ) return null;
        $manager = \Elementor\Plugin::$instance->widgets_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_widget_types' ) ) return null;
        try {
            $widget = $manager->get_widget_types( $widget_type );
            if ( is_object( $widget ) ) return $widget;
            $all = $manager->get_widget_types();
            return is_array( $all ) && is_object( $all[ $widget_type ] ?? null ) ? $all[ $widget_type ] : null;
        } catch ( Throwable $ignored ) {
            return null;
        }
    }

    private static function pixfort_widget_types(): array {
        if ( null !== self::$widget_types ) return self::$widget_types;
        self::$widget_types = [];
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->widgets_manager ) ) return self::$widget_types;
        $manager = \Elementor\Plugin::$instance->widgets_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_widget_types' ) ) return self::$widget_types;
        try { $all = $manager->get_widget_types(); } catch ( Throwable $ignored ) { $all = []; }
        foreach ( array_slice( is_array( $all ) ? $all : [], 0, self::MAX_WIDGETS, true ) as $name => $widget ) {
            $name = sanitize_key( (string) $name );
            if ( str_starts_with( $name, 'pix-' ) && is_object( $widget ) ) self::$widget_types[] = $name;
        }
        return self::$widget_types;
    }

    private static function looks_like_color_key( string $key ): bool {
        return (bool) preg_match( '/(^|_)(color|background_color|border_color|text_color|icon_color|fill_color|stroke_color)(_|$)/', strtolower( $key ) );
    }

    private static function theme_role_for_token( string $token ): string {
        if ( 'primary' === $token || str_starts_with( $token, 'primary-' ) || str_starts_with( $token, 'gradient-primary' ) ) return 'primary';
        if ( 'secondary' === $token || str_starts_with( $token, 'secondary-' ) || str_starts_with( $token, 'gradient-secondary' ) ) return 'secondary';
        return '';
    }

    private static function literal_css_color( string $value ): bool {
        return '' !== trim( $value ) && strlen( $value ) <= 128 && (bool) preg_match( '/^(#[0-9a-f]{3,8}|rgba?\([^\r\n]{1,90}\)|hsla?\([^\r\n]{1,90}\)|transparent|currentColor|var\(--[A-Za-z0-9_-]+\))$/i', trim( $value ) );
    }
}

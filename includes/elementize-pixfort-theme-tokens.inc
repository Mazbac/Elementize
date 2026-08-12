<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only adapter for Pixfort semantic color selectors.
 *
 * Pixfort widgets store selector tokens such as `primary`, `gradient-primary`,
 * `heading-default` and `body-default` in settings whose names contain `color`.
 * They are not literal CSS colors. The installed Elementor widget control is the
 * authority for allowed selector values. For Primary/Secondary, Pixfort's own
 * theme options are the authority for the rendered color; matching Elementor Kit
 * `pixprimarycolor`/`pixsecondarycolor` entries are returned only as corroboration.
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
            if ( ! isset( $meta['options'][ $token ] ) || ! isset( $roles[ $token ] ) || empty( $roles[ $token ]['authoritative'] ) ) continue;
            $role = $roles[ $token ];
            $option = [
                'value' => $token,
                'label' => (string) $meta['options'][ $token ],
                'theme_role' => $token,
                'resolves_to' => (string) $role['value'],
                'resolution_source' => (string) $role['source'],
                'pixfort_option_key' => (string) $role['option_key'],
                'read_only_source' => true,
            ];
            if ( ! empty( $role['site_token_id'] ) ) {
                $option['site_token_id'] = (string) $role['site_token_id'];
                $option['site_token_title'] = (string) $role['site_token_title'];
                $option['site_token_scope'] = (string) $role['site_token_scope'];
                $option['site_token_value'] = (string) $role['site_token_value'];
                $option['site_token_matches_render_value'] = (bool) $role['site_token_matches_render_value'];
            }
            $normalization[] = $option;
        }

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
            'normalization_writable' => ! empty( $normalization ),
            'theme_roles_detected' => array_values( array_keys( $roles ) ),
        ];
    }

    public static function is_control( string $widget_type, string $setting_key, $current_value ): bool {
        return is_array( self::control_meta( $widget_type, $setting_key, $current_value ) );
    }

    public static function is_any_control( string $setting_key, $current_value ): bool {
        $setting_key = sanitize_key( $setting_key );
        if ( '' === $setting_key || ! is_string( $current_value ) ) return false;
        foreach ( self::pixfort_widget_types() as $widget_type ) {
            if ( self::is_control( $widget_type, $setting_key, $current_value ) ) return true;
        }
        return false;
    }

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
        if ( ! isset( $roles[ $new_value ] ) || empty( $roles[ $new_value ]['authoritative'] ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_unresolved', 'The requested Pixfort theme role is not grounded in the active Pixfort theme options.', [ 'status' => 400 ] );
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

        $kit_roles = self::elementor_kit_roles();
        $role_options = [
            'primary' => 'opt-primary-color',
            'secondary' => 'opt-secondary-color',
        ];
        foreach ( $role_options as $role => $option_key ) {
            $value = '';
            if ( function_exists( 'pix_plugin_get_option' ) ) {
                try { $candidate = pix_plugin_get_option( $option_key ); } catch ( Throwable $ignored ) { $candidate = ''; }
                if ( is_scalar( $candidate ) ) $value = trim( (string) $candidate );
            }
            if ( ! self::literal_css_color( $value ) ) continue;

            $entry = [
                'value' => $value,
                'source' => 'pixfort_theme_option',
                'option_key' => $option_key,
                'authoritative' => true,
                'read_only' => true,
            ];
            if ( isset( $kit_roles[ $role ] ) ) {
                $kit = $kit_roles[ $role ];
                $entry['site_token_id'] = (string) $kit['token_id'];
                $entry['site_token_title'] = (string) $kit['title'];
                $entry['site_token_scope'] = (string) $kit['scope'];
                $entry['site_token_value'] = (string) $kit['value'];
                $entry['site_token_matches_render_value'] = 0 === strcasecmp( $value, (string) $kit['value'] );
            }
            self::$theme_roles[ $role ] = $entry;
        }
        return self::$theme_roles;
    }

    private static function elementor_kit_roles(): array {
        $roles = [];
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->kits_manager ) ) return $roles;
        $manager = \Elementor\Plugin::$instance->kits_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_current_settings' ) ) return $roles;
        try {
            $groups = [
                'system' => $manager->get_current_settings( 'system_colors' ),
                'custom' => $manager->get_current_settings( 'custom_colors' ),
            ];
        } catch ( Throwable $ignored ) {
            return $roles;
        }
        foreach ( $groups as $scope => $colors ) {
            foreach ( is_array( $colors ) ? $colors : [] as $item ) {
                if ( ! is_array( $item ) ) continue;
                $id = sanitize_key( (string) ( $item['_id'] ?? '' ) );
                $role = 'pixprimarycolor' === $id ? 'primary' : ( 'pixsecondarycolor' === $id ? 'secondary' : '' );
                if ( '' === $role ) continue;
                $value = trim( (string) ( $item['color'] ?? '' ) );
                if ( ! self::literal_css_color( $value ) ) continue;
                $roles[ $role ] = [
                    'token_id' => $id,
                    'title' => sanitize_text_field( (string) ( $item['title'] ?? $id ) ),
                    'value' => $value,
                    'scope' => sanitize_key( (string) $scope ),
                    'read_only' => true,
                ];
            }
        }
        return $roles;
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

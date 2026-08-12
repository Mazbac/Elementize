<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only adapter for Pixfort semantic color selectors.
 *
 * Pixfort widgets store selector tokens such as `primary`, `gradient-primary`,
 * `heading-default` and `body-default` in settings whose names contain `color`.
 * They are not literal CSS colors. The installed Elementor widget control is the
 * authority for allowed selector values and writable exact selector transitions.
 * For Primary/Secondary, Pixfort's own theme options remain the authority for the
 * rendered color; matching Elementor Kit `pixprimarycolor`/`pixsecondarycolor`
 * entries are returned only as corroboration.
 */
final class Elementize_Pixfort_Theme_Tokens {
    private const MAX_OPTIONS = 80;
    private const MAX_WIDGETS = 300;
    private const MAX_DIAGNOSTIC_KEYS = 12;
    private static array $meta_cache = [];
    private static ?array $theme_roles = null;
    private static ?array $widget_types = null;
    private static array $diagnostics = [
        'attempts' => 0,
        'recognized' => 0,
        'widget_unavailable' => 0,
        'control_unavailable' => 0,
        'options_unavailable' => 0,
        'current_value_not_allowed' => 0,
        'keys' => [],
    ];

    public static function control_meta( string $widget_type, string $setting_key, $current_value ): ?array {
        $widget_type = sanitize_key( $widget_type );
        $setting_key = sanitize_key( $setting_key );
        if ( ! str_starts_with( $widget_type, 'pix-' ) || '' === $setting_key || ! self::looks_like_color_key( $setting_key ) ) return null;
        if ( ! is_string( $current_value ) || '' === trim( $current_value ) || self::literal_css_color( $current_value ) ) return null;

        self::$diagnostics['attempts']++;
        self::remember_diagnostic_key( $widget_type, $setting_key, $current_value );

        $cache_key = $widget_type . '|' . $setting_key;
        if ( ! array_key_exists( $cache_key, self::$meta_cache ) ) {
            self::$meta_cache[ $cache_key ] = self::read_control_meta( $widget_type, $setting_key );
        }
        $meta = self::$meta_cache[ $cache_key ];
        if ( ! is_array( $meta ) ) return null;
        if ( ! array_key_exists( $current_value, $meta['options'] ) ) {
            self::$diagnostics['current_value_not_allowed']++;
            return null;
        }

        $roles = self::theme_roles();
        $allowed = [];
        $normalization = [];
        foreach ( array_slice( $meta['options'], 0, self::MAX_OPTIONS, true ) as $value => $label ) {
            $value = (string) $value;
            $label = (string) $label;
            if ( 'custom' === $value ) continue;

            $allowed[] = [ 'value' => $value, 'label' => $label ];
            $option = [
                'value' => $value,
                'label' => $label,
                'selector_source' => 'installed_elementor_pixfort_widget_control',
                'read_only_source' => true,
            ];

            $theme_role = self::theme_role_for_token( $value );
            if ( '' !== $theme_role ) $option['theme_role'] = $theme_role;
            if ( in_array( $value, [ 'primary', 'secondary' ], true ) && isset( $roles[ $value ] ) && ! empty( $roles[ $value ]['authoritative'] ) ) {
                $role = $roles[ $value ];
                $option['resolves_to'] = (string) $role['value'];
                $option['resolution_source'] = (string) $role['source'];
                $option['pixfort_option_key'] = (string) $role['option_key'];
                if ( ! empty( $role['site_token_id'] ) ) {
                    $option['site_token_id'] = (string) $role['site_token_id'];
                    $option['site_token_title'] = (string) $role['site_token_title'];
                    $option['site_token_scope'] = (string) $role['site_token_scope'];
                    $option['site_token_value'] = (string) $role['site_token_value'];
                    $option['site_token_matches_render_value'] = (bool) $role['site_token_matches_render_value'];
                }
            }
            $normalization[] = $option;
        }

        self::$diagnostics['recognized']++;
        return [
            'source' => 'installed_elementor_pixfort_widget_control',
            'control_lookup_source' => (string) ( $meta['lookup_source'] ?? '' ),
            'control_type' => (string) ( $meta['type'] ?? '' ),
            'widget_type' => $widget_type,
            'setting_key' => $setting_key,
            'current_value' => $current_value,
            'current_label' => (string) ( $meta['options'][ $current_value ] ?? $current_value ),
            'current_theme_role' => self::theme_role_for_token( $current_value ),
            'allowed_values' => $allowed,
            'writable_values' => $allowed,
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

    public static function validate_transition( string $widget_type, string $setting_key, $current_value, $new_value ) {
        $widget_type = sanitize_key( $widget_type );
        $setting_key = sanitize_key( $setting_key );
        if ( ! str_starts_with( $widget_type, 'pix-' ) || ! self::looks_like_color_key( $setting_key ) || ! is_string( $current_value ) || ! is_string( $new_value ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_invalid', 'Pixfort theme-color normalization requires an exact Pixfort widget, color selector setting, and string token values.', [ 'status' => 400 ] );
        }
        $new_value = trim( $new_value );
        if ( '' === $new_value || 'custom' === $new_value ) {
            return new WP_Error( 'elementize_pixfort_theme_token_target_forbidden', 'The Pixfort custom color selector is not writable through semantic token normalization.', [ 'status' => 400 ] );
        }

        $meta = self::control_meta( $widget_type, $setting_key, $current_value );
        if ( ! is_array( $meta ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_control_unverified', 'The exact installed Pixfort widget control could not be verified for this semantic color setting.', [ 'status' => 400 ] );
        }

        $matched = false;
        foreach ( (array) $meta['normalization_options'] as $option ) {
            if ( is_array( $option ) && (string) ( $option['value'] ?? '' ) === $new_value ) {
                $matched = true;
                break;
            }
        }
        if ( ! $matched ) {
            return new WP_Error( 'elementize_pixfort_theme_token_not_allowed', 'The exact installed Pixfort widget control does not allow that semantic color-token transition.', [ 'status' => 400 ] );
        }

        if ( in_array( $new_value, [ 'primary', 'secondary' ], true ) ) {
            $roles = self::theme_roles();
            if ( ! isset( $roles[ $new_value ] ) || empty( $roles[ $new_value ]['authoritative'] ) ) {
                return new WP_Error( 'elementize_pixfort_theme_token_unresolved', 'The requested Pixfort theme role is not grounded in the active Pixfort theme options.', [ 'status' => 400 ] );
            }
        }
        return $new_value;
    }

    public static function validate_any_transition( string $setting_key, $current_value, $new_value ) {
        foreach ( self::pixfort_widget_types() as $widget_type ) {
            $validated = self::validate_transition( $widget_type, $setting_key, $current_value, $new_value );
            if ( ! is_wp_error( $validated ) ) return $validated;
        }
        return new WP_Error( 'elementize_pixfort_theme_token_not_allowed', 'No exact installed Pixfort widget control allowed that semantic color-token transition.', [ 'status' => 400 ] );
    }

    public static function diagnostics(): array {
        return [
            'attempts' => (int) self::$diagnostics['attempts'],
            'recognized' => (int) self::$diagnostics['recognized'],
            'widget_unavailable' => (int) self::$diagnostics['widget_unavailable'],
            'control_unavailable' => (int) self::$diagnostics['control_unavailable'],
            'options_unavailable' => (int) self::$diagnostics['options_unavailable'],
            'current_value_not_allowed' => (int) self::$diagnostics['current_value_not_allowed'],
            'sample_keys' => array_values( self::$diagnostics['keys'] ),
        ];
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
        if ( ! is_object( $widget ) || ! method_exists( $widget, 'get_controls' ) ) {
            self::$diagnostics['widget_unavailable']++;
            return null;
        }

        $control = null;
        $lookup_source = '';
        try {
            $candidate = $widget->get_controls( $setting_key );
            if ( is_array( $candidate ) ) {
                $control = $candidate;
                $lookup_source = 'get_controls_by_id';
            }
        } catch ( Throwable $ignored ) {
            $control = null;
        }
        if ( ! is_array( $control ) ) {
            try { $controls = $widget->get_controls(); } catch ( Throwable $ignored ) { $controls = []; }
            if ( is_array( $controls ) && is_array( $controls[ $setting_key ] ?? null ) ) {
                $control = $controls[ $setting_key ];
                $lookup_source = 'get_controls_map';
            }
        }
        if ( ! is_array( $control ) ) {
            self::$diagnostics['control_unavailable']++;
            return null;
        }

        $options = self::flatten_options( $control['options'] ?? null );
        if ( ! $options && is_array( $control['groups'] ?? null ) ) {
            foreach ( $control['groups'] as $group ) {
                if ( ! is_array( $group ) ) continue;
                foreach ( self::flatten_options( $group['options'] ?? null ) as $value => $label ) {
                    if ( count( $options ) >= self::MAX_OPTIONS ) break 2;
                    $options[ $value ] = $label;
                }
            }
            if ( $options ) $lookup_source .= '+grouped_options';
        }
        if ( ! $options ) {
            self::$diagnostics['options_unavailable']++;
            return null;
        }
        return [
            'type' => sanitize_key( (string) ( $control['type'] ?? '' ) ),
            'lookup_source' => $lookup_source,
            'options' => $options,
        ];
    }

    private static function flatten_options( $raw, int $depth = 0 ): array {
        if ( ! is_array( $raw ) || $depth > 2 ) return [];
        $options = [];
        foreach ( $raw as $value => $label ) {
            if ( count( $options ) >= self::MAX_OPTIONS ) break;
            if ( is_array( $label ) ) {
                foreach ( self::flatten_options( $label, $depth + 1 ) as $nested_value => $nested_label ) {
                    if ( count( $options ) >= self::MAX_OPTIONS ) break;
                    $options[ $nested_value ] = $nested_label;
                }
                continue;
            }
            if ( ! is_scalar( $value ) || ! is_scalar( $label ) ) continue;
            $value = (string) $value;
            if ( '' === $value || strlen( $value ) > 80 || ! preg_match( '/^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/', $value ) ) continue;
            $options[ $value ] = sanitize_text_field( (string) $label );
        }
        return $options;
    }

    private static function widget_instance( string $widget_type ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->widgets_manager ) ) return null;
        $manager = \Elementor\Plugin::$instance->widgets_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_widget_types' ) ) return null;
        try {
            $widget = $manager->get_widget_types( $widget_type );
            if ( is_object( $widget ) ) return $widget;
            $all = $manager->get_widget_types();
            if ( ! is_array( $all ) ) return null;
            if ( is_object( $all[ $widget_type ] ?? null ) ) return $all[ $widget_type ];
            foreach ( $all as $candidate ) {
                if ( ! is_object( $candidate ) || ! method_exists( $candidate, 'get_name' ) ) continue;
                try { $name = sanitize_key( (string) $candidate->get_name() ); } catch ( Throwable $ignored ) { continue; }
                if ( $name === $widget_type ) return $candidate;
            }
        } catch ( Throwable $ignored ) {
            return null;
        }
        return null;
    }

    private static function pixfort_widget_types(): array {
        if ( null !== self::$widget_types ) return self::$widget_types;
        self::$widget_types = [];
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->widgets_manager ) ) return self::$widget_types;
        $manager = \Elementor\Plugin::$instance->widgets_manager;
        if ( ! is_object( $manager ) || ! method_exists( $manager, 'get_widget_types' ) ) return self::$widget_types;
        try { $all = $manager->get_widget_types(); } catch ( Throwable $ignored ) { $all = []; }
        foreach ( array_slice( is_array( $all ) ? $all : [], 0, self::MAX_WIDGETS, true ) as $name => $widget ) {
            if ( ! is_object( $widget ) ) continue;
            $resolved = sanitize_key( (string) $name );
            if ( method_exists( $widget, 'get_name' ) ) {
                try { $candidate = sanitize_key( (string) $widget->get_name() ); } catch ( Throwable $ignored ) { $candidate = ''; }
                if ( '' !== $candidate ) $resolved = $candidate;
            }
            if ( str_starts_with( $resolved, 'pix-' ) && ! in_array( $resolved, self::$widget_types, true ) ) self::$widget_types[] = $resolved;
        }
        return self::$widget_types;
    }

    private static function remember_diagnostic_key( string $widget_type, string $setting_key, string $current_value ): void {
        $key = $widget_type . '|' . $setting_key . '|' . $current_value;
        if ( isset( self::$diagnostics['keys'][ $key ] ) ) return;
        if ( count( self::$diagnostics['keys'] ) >= self::MAX_DIAGNOSTIC_KEYS ) return;
        self::$diagnostics['keys'][ $key ] = [
            'widget_type' => $widget_type,
            'setting_key' => $setting_key,
            'current_value' => $current_value,
        ];
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

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Visual_Writes', false ) ) return;

final class Elementize_Visual_Rendering {
    private const VERSION = '0.3.2';
    private const NS = 'elementize/v1';

    public static function init(): void {
        remove_action( 'admin_menu', [ Elementize_Visual_Writes::class, 'menu' ] );
        add_action( 'admin_menu', [ self::class, 'menu' ] );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 20, 3 );
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', 'elementize', [ self::class, 'admin' ], 'dashicons-admin-tools', 58 );
    }

    public static function admin(): void {
        ob_start();
        Elementize_Visual_Writes::admin();
        $html = (string) ob_get_clean();
        $html = str_replace( 'Version 0.3.0', 'Version ' . self::VERSION, $html );
        $html = str_replace(
            '0.3.0 adds guarded writes for explicit CSS colors, Pixfort icon values, and WordPress image attachments. Global style references, dynamic values, divider colors, and icon behavior/layout settings remain protected.',
            '0.3.2 keeps guarded visual writes, filters color writability by active control state, and regenerates the affected Elementor post CSS after successful visual writes. Global style references, dynamic values, divider colors, and inactive custom-color fields remain protected.',
            $html
        );
        echo $html; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) {
            return self::annotate_status( $response );
        }

        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/visual-settings$#', $route, $matches ) ) {
            return $response;
        }

        $page_id = absint( $matches[1] ?? 0 );
        if ( $page_id < 1 ) return $response;

        $method = strtoupper( $request->get_method() );
        if ( 'GET' === $method ) return self::annotate_visual_settings( $response, $page_id );
        if ( in_array( $method, [ 'POST', 'PUT', 'PATCH' ], true ) ) return self::refresh_css_after_write( $response, $page_id );
        return $response;
    }

    private static function annotate_status( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['visual_color_active_state'] = true;
        $data['visual_css_regeneration'] = 'per-post';
        $response->set_data( $data );
        return $response;
    }

    private static function annotate_visual_settings( $response, int $page_id ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual_items'] ) || ! is_array( $data['visual_items'] ) ) return $response;

        $elements = self::elements( $page_id );
        if ( ! is_array( $elements ) ) return $response;

        $active_color_count = 0;
        $writable_active_color_count = 0;
        foreach ( $data['visual_items'] as &$item ) {
            if ( ! is_array( $item ) ) continue;
            $kind = (string) ( $item['kind'] ?? '' );
            if ( 'color' !== $kind ) {
                $item['active'] = true;
                continue;
            }

            $element_id = (string) ( $item['element_id'] ?? '' );
            $path = isset( $item['setting_path'] ) && is_array( $item['setting_path'] ) ? $item['setting_path'] : [];
            $active = self::color_is_active( $elements, $element_id, $path );
            $item['active'] = $active;
            if ( $active ) $active_color_count++;
            if ( array_key_exists( 'writable', $item ) ) $item['writable'] = (bool) $item['writable'] && $active;
            if ( ! empty( $item['writable'] ) ) $writable_active_color_count++;
        }
        unset( $item );

        $data['active_color_count'] = $active_color_count;
        $data['writable_active_color_count'] = $writable_active_color_count;
        $data['color_writable_semantics'] = 'guarded-and-active';
        $data['css_regeneration_after_visual_write'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function refresh_css_after_write( $response, int $page_id ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( $response->get_status() >= 400 ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['saved'] ) ) return $response;

        try {
            if ( ! class_exists( '\Elementor\Core\Files\CSS\Post' ) ) {
                $data['css_regenerated'] = false;
                $data['css_regeneration_error'] = 'Elementor post CSS class is unavailable.';
            } else {
                $css = new \Elementor\Core\Files\CSS\Post( $page_id );
                $css->update();
                $data['css_regenerated'] = true;
            }
        } catch ( Throwable $exception ) {
            $data['css_regenerated'] = false;
            $data['css_regeneration_error'] = $exception->getMessage();
        }

        $response->set_data( $data );
        return $response;
    }

    private static function elements( int $page_id ): ?array {
        if ( ! class_exists( '\Elementor\Plugin' ) ) return null;
        try {
            $document = \Elementor\Plugin::$instance->documents->get( $page_id );
            if ( ! $document ) return null;
            $elements = $document->get_elements_data();
            return is_array( $elements ) ? $elements : null;
        } catch ( Throwable $exception ) {
            return null;
        }
    }

    private static function color_is_active( array $elements, string $element_id, array $path ): bool {
        if ( '' === $element_id || ! $path ) return false;
        foreach ( $path as $segment ) {
            if ( is_string( $segment ) && '__globals__' === strtolower( $segment ) ) return false;
        }

        $settings = self::find_element_settings( $elements, $element_id );
        if ( ! is_array( $settings ) ) return false;

        $key = self::last_path_key( $path );
        if ( '' === $key ) return false;
        if ( preg_match( '/^[bt]_[123]_color$/i', $key ) || str_contains( $key, 'moving_divider' ) ) return false;

        if ( 1 === count( $path ) ) {
            if ( 'background_color' === $key ) return 'classic' === self::setting_string( $settings, 'background_background' );
            if ( 'background_color_b' === $key ) return 'gradient' === self::setting_string( $settings, 'background_background' );
            if ( 'background_hover_color' === $key ) return 'classic' === self::setting_string( $settings, 'background_hover_background' );
            if ( 'background_hover_color_b' === $key ) return 'gradient' === self::setting_string( $settings, 'background_hover_background' );
            if ( 'background_overlay_color' === $key ) return 'classic' === self::setting_string( $settings, 'background_overlay_background' );
            if ( 'background_overlay_color_b' === $key ) return 'gradient' === self::setting_string( $settings, 'background_overlay_background' );
            if ( 'border_color' === $key ) {
                $border = self::setting_string( $settings, 'border_border' );
                return '' !== $border && 'none' !== $border;
            }
            if ( 'color' === $key ) return true;
        }

        $found = false;
        $parent = self::get_path_value( $settings, array_slice( $path, 0, -1 ), $found );
        if ( ! $found || ! is_array( $parent ) ) return false;

        if ( str_ends_with( $key, '_custom_color' ) ) {
            $mode_key = substr( $key, 0, -strlen( '_custom_color' ) ) . '_color';
            $mode = isset( $parent[ $mode_key ] ) && is_string( $parent[ $mode_key ] ) ? strtolower( trim( $parent[ $mode_key ] ) ) : '';
            return in_array( $mode, [ 'custom', 'custom-color', 'custom_color' ], true );
        }

        if ( 'highlight_color' === $key ) {
            $highlighted = isset( $parent['is_highlighted'] ) && is_string( $parent['is_highlighted'] ) ? strtolower( trim( $parent['is_highlighted'] ) ) : '';
            return '' !== $highlighted && ! in_array( $highlighted, [ 'no', 'none', 'image' ], true );
        }

        return false;
    }

    private static function find_element_settings( array $elements, string $element_id ): ?array {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                return isset( $element['settings'] ) && is_array( $element['settings'] ) ? $element['settings'] : null;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $settings = self::find_element_settings( $element['elements'], $element_id );
                if ( is_array( $settings ) ) return $settings;
            }
        }
        return null;
    }

    private static function get_path_value( array $settings, array $path, bool &$found ) {
        $cursor = $settings;
        foreach ( $path as $segment ) {
            if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) {
                $found = false;
                return null;
            }
            $cursor = $cursor[ $segment ];
        }
        $found = true;
        return $cursor;
    }

    private static function setting_string( array $settings, string $key ): string {
        return isset( $settings[ $key ] ) && is_string( $settings[ $key ] ) ? strtolower( trim( $settings[ $key ] ) ) : '';
    }

    private static function last_path_key( array $path ): string {
        for ( $index = count( $path ) - 1; $index >= 0; $index-- ) {
            if ( is_string( $path[ $index ] ) ) return strtolower( $path[ $index ] );
        }
        return '';
    }
}

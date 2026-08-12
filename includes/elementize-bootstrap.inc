<?php
if ( ! defined( 'ABSPATH' ) ) exit;

require_once __DIR__ . '/elementize-core.inc';
require_once __DIR__ . '/elementize-pixfort-icons.inc';
require_once __DIR__ . '/elementize-pixfort-transport.inc';
require_once __DIR__ . '/elementize-pixfort-theme-tokens.inc';
require_once __DIR__ . '/elementize-content.inc';
require_once __DIR__ . '/elementize-context.inc';
require_once __DIR__ . '/elementize-capabilities.inc';
require_once __DIR__ . '/elementize-design.inc';
require_once __DIR__ . '/elementize-tree.inc';
require_once __DIR__ . '/elementize-templates.inc';
require_once __DIR__ . '/elementize-creative.inc';
require_once __DIR__ . '/elementize-visual-qa.inc';
require_once __DIR__ . '/elementize-media-library.inc';
require_once __DIR__ . '/elementize-media-import.inc';
require_once __DIR__ . '/elementize-activity.inc';
require_once __DIR__ . '/elementize-connections.inc';
require_once __DIR__ . '/elementize-onboarding.inc';

/**
 * Exact-widget preflight for Pixfort semantic theme-color writes.
 *
 * Elementize_Creative still performs the authoritative transaction, stale-state,
 * expected-value and persisted-save verification. This preflight closes a narrower
 * safety gap: a semantic color token must be validated against the exact Pixfort
 * widget type targeted by the operation, never merely against some installed
 * Pixfort widget that happens to expose a setting with the same key.
 *
 * Inserted-template aliases are resolved against the exact pinned template snapshot,
 * so insert + semantic-style plans remain atomic and do not need a separate write.
 */
final class Elementize_Creative_Style_Preflight {
    private const MAX_OPERATIONS = 50;
    private const MAX_PATH = 30;

    public static function before_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( null !== $response ) return $response;
        $route = (string) $request->get_route();
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/\\d+/creative$#', $route ) ) return $response;

        $operations = $request->get_param( 'operations' );
        if ( ! is_array( $operations ) || ! $operations || count( $operations ) > self::MAX_OPERATIONS ) return $response;
        if ( ! self::contains_semantic_style_candidate( $operations ) ) return $response;

        $page_id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $page_id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $elements = is_array( $loaded['elements'] ?? null ) ? $loaded['elements'] : [];
        $aliases = [];

        foreach ( array_values( $operations ) as $index => $operation ) {
            if ( ! is_array( $operation ) ) continue;
            $type = sanitize_key( (string) ( $operation['type'] ?? '' ) );

            if ( 'insert_template' === $type ) {
                $registered = self::register_insert_alias( $operation, $aliases, $index );
                if ( is_wp_error( $registered ) ) return $registered;
                continue;
            }

            if ( 'duplicate_element' === $type ) {
                self::register_duplicate_alias( $operation, $aliases, $elements );
                continue;
            }

            if ( 'style' !== $type ) continue;
            $guarded = self::guard_style( $operation, $aliases, $elements, $index );
            if ( is_wp_error( $guarded ) ) return $guarded;
        }

        return $response;
    }

    private static function contains_semantic_style_candidate( array $operations ): bool {
        foreach ( $operations as $operation ) {
            if ( ! is_array( $operation ) || 'style' !== sanitize_key( (string) ( $operation['type'] ?? '' ) ) ) continue;
            $path = $operation['setting_path'] ?? null;
            if ( ! is_array( $path ) || ! $path ) continue;
            $key = strtolower( (string) end( $path ) );
            if ( ! self::looks_like_color_key( $key ) ) continue;
            if ( ! is_string( $operation['expected_json'] ?? null ) ) continue;
            $ok = false;
            $expected = Elementize_Design::decode_json_value( (string) $operation['expected_json'], $ok );
            if ( $ok && is_string( $expected ) && '' !== trim( $expected ) && ! self::literal_css_color( $expected ) ) return true;
        }
        return false;
    }

    private static function register_insert_alias( array $operation, array &$aliases, int $index ) {
        $alias = sanitize_key( (string) ( $operation['alias'] ?? '' ) );
        $source_id = sanitize_key( (string) ( $operation['source_id'] ?? '' ) );
        $template_id = sanitize_text_field( (string) ( $operation['template_id'] ?? '' ) );
        if ( '' === $alias || '' === $source_id || '' === $template_id || isset( $aliases[ $alias ] ) ) return true;

        $template = Elementize_Templates::load_template( $source_id, $template_id );
        if ( is_wp_error( $template ) ) return $template;
        $roots = is_array( $template['elements'] ?? null ) ? $template['elements'] : [];
        $map = self::widget_map( $roots );
        $root_ids = array_values( array_filter( array_map( static fn( $node ): string => is_array( $node ) ? (string) ( $node['id'] ?? '' ) : '', $roots ) ) );
        $direct = '';
        if ( 1 === count( $root_ids ) && isset( $map[ $root_ids[0] ] ) ) $direct = (string) $map[ $root_ids[0] ];
        $aliases[ $alias ] = [ 'direct_widget' => $direct, 'map' => $map ];
        return true;
    }

    private static function register_duplicate_alias( array $operation, array &$aliases, array $elements ): void {
        $alias = sanitize_key( (string) ( $operation['alias'] ?? '' ) );
        if ( '' === $alias || isset( $aliases[ $alias ] ) || ! is_string( $operation['element_ref'] ?? null ) ) return;
        $ref = (string) $operation['element_ref'];
        $widget = self::widget_for_ref( $ref, $aliases, $elements );
        if ( is_wp_error( $widget ) || '' === (string) $widget ) return;

        $map = [];
        if ( false === strpos( $ref, ':' ) && ! isset( $aliases[ $ref ] ) ) {
            $node = Elementize_Tree::find_node( $elements, $ref );
            if ( is_array( $node ) ) $map = self::widget_map( [ $node ] );
        }
        $aliases[ $alias ] = [ 'direct_widget' => (string) $widget, 'map' => $map ];
    }

    private static function guard_style( array $operation, array $aliases, array $elements, int $index ) {
        $path = self::valid_path( $operation['setting_path'] ?? null );
        if ( is_wp_error( $path ) ) return $path;
        $key = strtolower( (string) end( $path ) );
        if ( ! self::looks_like_color_key( $key ) ) return true;
        if ( ! is_string( $operation['expected_json'] ?? null ) || ! is_string( $operation['value_json'] ?? null ) ) return true;

        $expected_ok = false;
        $expected = Elementize_Design::decode_json_value( (string) $operation['expected_json'], $expected_ok );
        $new_ok = false;
        $new = Elementize_Design::decode_json_value( (string) $operation['value_json'], $new_ok );
        if ( ! $expected_ok || ! $new_ok || ! is_string( $expected ) || self::literal_css_color( $expected ) ) return true;

        if ( ! is_string( $operation['element_ref'] ?? null ) || '' === trim( (string) $operation['element_ref'] ) ) {
            return self::error( $index, 'Pixfort semantic theme-color writes require an exact element reference.' );
        }
        $widget = self::widget_for_ref( (string) $operation['element_ref'], $aliases, $elements );
        if ( is_wp_error( $widget ) ) return $widget;
        $widget = sanitize_key( (string) $widget );
        if ( '' === $widget ) return self::error( $index, 'The exact target widget type could not be resolved for this semantic color write.' );

        $category = Elementize_Design::classify_control( $path, $expected, $widget );
        if ( ! is_string( $category ) || ! str_starts_with( $category, 'pixfort_theme_color:' ) ) {
            return new WP_Error(
                'elementize_pixfort_theme_token_control_unverified',
                'The exact target widget does not expose this semantic Pixfort theme-color control.',
                [ 'status' => 400, 'operation_index' => $index, 'widget_type' => $widget, 'setting_key' => $key ]
            );
        }
        if ( ! class_exists( 'Elementize_Pixfort_Theme_Tokens' ) ) {
            return new WP_Error( 'elementize_pixfort_theme_token_unavailable', 'Pixfort semantic theme-color validation is unavailable.', [ 'status' => 400, 'operation_index' => $index ] );
        }

        $validated = Elementize_Pixfort_Theme_Tokens::validate_transition( $widget, $key, $expected, $new );
        if ( is_wp_error( $validated ) ) return $validated;
        return true;
    }

    private static function widget_for_ref( string $ref, array $aliases, array $elements ) {
        $ref = trim( $ref );
        if ( '' === $ref || strlen( $ref ) > 160 ) return new WP_Error( 'elementize_creative_ref_invalid', 'Element reference is missing or too long.', [ 'status' => 400 ] );

        if ( false === strpos( $ref, ':' ) ) {
            if ( isset( $aliases[ $ref ] ) ) return (string) ( $aliases[ $ref ]['direct_widget'] ?? '' );
            $node = Elementize_Tree::find_node( $elements, $ref );
            if ( ! is_array( $node ) ) return new WP_Error( 'elementize_creative_ref_unknown', 'The semantic style target could not be resolved from the current page.', [ 'status' => 400, 'element_ref' => $ref ] );
            return (string) ( $node['widgetType'] ?? '' );
        }

        [ $alias, $old_id ] = array_pad( explode( ':', $ref, 2 ), 2, '' );
        $alias = sanitize_key( $alias );
        if ( '' === $alias || '' === $old_id || ! isset( $aliases[ $alias ]['map'][ $old_id ] ) ) {
            return new WP_Error( 'elementize_creative_ref_unknown', 'The semantic style target is not available from a preceding inserted or duplicated alias.', [ 'status' => 400, 'element_ref' => $ref ] );
        }
        return (string) $aliases[ $alias ]['map'][ $old_id ];
    }

    private static function widget_map( array $elements ): array {
        $map = [];
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            if ( '' !== $id ) $map[ $id ] = sanitize_key( (string) ( $element['widgetType'] ?? '' ) );
            if ( is_array( $element['elements'] ?? null ) ) $map += self::widget_map( $element['elements'] );
        }
        return $map;
    }

    private static function valid_path( $path ) {
        if ( ! is_array( $path ) || ! $path || count( $path ) > self::MAX_PATH ) return new WP_Error( 'elementize_creative_path_invalid', 'Semantic style setting_path is invalid.', [ 'status' => 400 ] );
        $out = [];
        foreach ( $path as $part ) {
            if ( ! is_string( $part ) && ! is_int( $part ) ) return new WP_Error( 'elementize_creative_path_invalid', 'Semantic style path segments must be strings or integers.', [ 'status' => 400 ] );
            if ( '__globals__' === (string) $part ) return new WP_Error( 'elementize_creative_global_style_read_only', 'Elementor global style references remain read-only.', [ 'status' => 400 ] );
            $out[] = $part;
        }
        return $out;
    }

    private static function looks_like_color_key( string $key ): bool {
        return (bool) preg_match( '/(^|_)(color|background_color|border_color|text_color|icon_color|fill_color|stroke_color)(_|$)/', strtolower( $key ) );
    }

    private static function literal_css_color( string $value ): bool {
        $value = trim( $value );
        return '' !== $value && strlen( $value ) <= 128 && (bool) preg_match( '/^(#[0-9a-f]{3,8}|rgba?\\([^\\r\\n]{1,90}\\)|hsla?\\([^\\r\\n]{1,90}\\)|transparent|currentColor|var\\(--[A-Za-z0-9_-]+\\))$/i', $value );
    }

    private static function error( int $index, string $message ): WP_Error {
        return new WP_Error( 'elementize_creative_semantic_style_preflight_failed', 'Invalid creative semantic style operation at index ' . $index . ': ' . $message, [ 'status' => 400, 'operation_index' => $index ] );
    }
}

add_filter( 'rest_request_before_callbacks', [ Elementize_Creative_Style_Preflight::class, 'before_callbacks' ], 12, 3 );

Elementize_Core::init();
Elementize_Pixfort_Icons::init();
Elementize_Pixfort_Transport::init();
Elementize_Content::init();
Elementize_Context::init();
Elementize_Capabilities::init();
Elementize_Design::init();
Elementize_Templates::init();
Elementize_Creative::init();
Elementize_Visual_QA::init();
Elementize_Media_Library::init();
Elementize_Media_Import::init();
Elementize_Activity::init();
Elementize_Connections::init();
Elementize_Onboarding::init();
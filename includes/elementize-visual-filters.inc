<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Visual_Rendering', false ) ) return;

final class Elementize_Visual_Filters {
    private const VERSION = '0.4.2';
    private const NS = 'elementize/v1';
    private const MAX_LIMIT = 200;
    private const COMPACT_DEFAULT_LIMIT = 20;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 60, 3 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) {
            return self::annotate_status( $response );
        }

        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/\d+/visual-settings$#', $route ) ) return $response;

        return self::filter_visual_settings( $response, $request );
    }

    private static function annotate_status( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['visual_settings_filters'] = [ 'kind', 'active', 'writable', 'element_id', 'offset', 'limit', 'compact' ];
        $data['visual_settings_filter_max_limit'] = self::MAX_LIMIT;
        $data['visual_settings_compact_default_limit'] = self::COMPACT_DEFAULT_LIMIT;
        $response->set_data( $data );
        return $response;
    }

    private static function filter_visual_settings( $response, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! isset( $data['visual_items'] ) || ! is_array( $data['visual_items'] ) ) return $response;

        $kind = strtolower( trim( (string) $request->get_param( 'kind' ) ) );
        $allowed_kinds = [ 'all', 'color', 'icon', 'media', 'global', 'dynamic' ];
        if ( '' !== $kind && ! in_array( $kind, $allowed_kinds, true ) ) {
            return new WP_Error( 'elementize_invalid_visual_kind_filter', 'kind must be all, color, icon, media, global, or dynamic.', [ 'status' => 400 ] );
        }

        $active = self::bool_filter( $request->get_param( 'active' ), 'active' );
        if ( is_wp_error( $active ) ) return $active;
        $writable = self::bool_filter( $request->get_param( 'writable' ), 'writable' );
        if ( is_wp_error( $writable ) ) return $writable;
        $compact = self::bool_filter( $request->get_param( 'compact' ), 'compact' );
        if ( is_wp_error( $compact ) ) return $compact;
        if ( null === $compact ) $compact = false;

        $element_id = trim( (string) $request->get_param( 'element_id' ) );
        if ( strlen( $element_id ) > 100 ) {
            return new WP_Error( 'elementize_invalid_visual_element_filter', 'element_id is too long.', [ 'status' => 400 ] );
        }

        $offset_raw = $request->get_param( 'offset' );
        $limit_raw = $request->get_param( 'limit' );
        $offset = null === $offset_raw || '' === $offset_raw ? 0 : max( 0, absint( $offset_raw ) );
        if ( null === $limit_raw || '' === $limit_raw ) {
            $limit = $compact ? self::COMPACT_DEFAULT_LIMIT : null;
        } else {
            $limit = absint( $limit_raw );
        }
        if ( null !== $limit && ( $limit < 1 || $limit > self::MAX_LIMIT ) ) {
            return new WP_Error( 'elementize_invalid_visual_limit', 'limit must be between 1 and ' . self::MAX_LIMIT . '.', [ 'status' => 400 ] );
        }

        $items = array_values( $data['visual_items'] );
        $total = count( $items );
        $filtered = [];

        foreach ( $items as $item ) {
            if ( ! is_array( $item ) ) continue;
            if ( '' !== $kind && 'all' !== $kind && strtolower( (string) ( $item['kind'] ?? '' ) ) !== $kind ) continue;
            if ( null !== $active && (bool) ( $item['active'] ?? false ) !== $active ) continue;
            if ( null !== $writable && (bool) ( $item['writable'] ?? false ) !== $writable ) continue;
            if ( '' !== $element_id && (string) ( $item['element_id'] ?? '' ) !== $element_id ) continue;
            $filtered[] = $item;
        }

        $matched = count( $filtered );
        if ( $offset > 0 || null !== $limit ) {
            $filtered = array_slice( $filtered, $offset, $limit );
        }

        if ( $compact ) {
            $filtered = array_values( array_map( [ self::class, 'compact_item' ], $filtered ) );
        }

        $returned = count( $filtered );
        $filters = [
            'kind' => '' === $kind ? null : $kind,
            'active' => $active,
            'writable' => $writable,
            'element_id' => '' === $element_id ? null : $element_id,
            'offset' => $offset,
            'limit' => $limit,
            'compact' => $compact,
        ];
        $truncated = ( $offset + $returned ) < $matched;
        $next_offset = $truncated ? $offset + $returned : null;

        if ( $compact ) {
            $kind_counts = [ 'color' => 0, 'icon' => 0, 'media' => 0, 'global' => 0, 'dynamic' => 0 ];
            foreach ( $filtered as $item ) {
                $item_kind = strtolower( (string) ( $item['kind'] ?? '' ) );
                if ( array_key_exists( $item_kind, $kind_counts ) ) $kind_counts[ $item_kind ]++;
            }
            $response->set_data( [
                'content_hash' => (string) ( $data['content_hash'] ?? '' ),
                'visual_item_total' => $total,
                'visual_item_matched' => $matched,
                'visual_item_count' => $returned,
                'visual_items_truncated' => $truncated,
                'next_offset' => $next_offset,
                'returned_kind_counts' => $kind_counts,
                'visual_filters' => $filters,
                'visual_items' => $filtered,
            ] );
            return $response;
        }

        $data['visual_items'] = $filtered;
        $data['visual_item_total'] = $total;
        $data['visual_item_matched'] = $matched;
        $data['visual_item_count'] = $returned;
        $data['visual_filters'] = $filters;
        $data['visual_items_truncated'] = $truncated;
        $data['next_offset'] = $next_offset;
        $response->set_data( $data );
        return $response;
    }

    private static function compact_item( array $item ): array {
        $kind = (string) ( $item['kind'] ?? '' );
        $value = $item['value'] ?? null;
        if ( 'media' === $kind && is_array( $value ) ) {
            $url = isset( $value['url'] ) && is_string( $value['url'] ) ? $value['url'] : '';
            $path = '' !== $url ? (string) wp_parse_url( $url, PHP_URL_PATH ) : '';
            $compact_value = [ 'id' => absint( $value['id'] ?? 0 ) ];
            if ( '' !== $path ) $compact_value['file'] = wp_basename( $path );
            if ( isset( $value['source'] ) && is_string( $value['source'] ) && '' !== $value['source'] ) {
                $compact_value['source'] = $value['source'];
            }
            $value = $compact_value;
        }

        $compact = [
            'kind' => $kind,
            'element_id' => (string) ( $item['element_id'] ?? '' ),
            'setting_path' => isset( $item['setting_path'] ) && is_array( $item['setting_path'] ) ? array_values( $item['setting_path'] ) : [],
            'value' => $value,
            'active' => (bool) ( $item['active'] ?? false ),
            'writable' => (bool) ( $item['writable'] ?? false ),
        ];
        if ( isset( $item['widget_type'] ) && is_string( $item['widget_type'] ) && '' !== $item['widget_type'] ) {
            $compact['widget_type'] = $item['widget_type'];
        }
        return $compact;
    }

    private static function bool_filter( $raw, string $name ) {
        if ( null === $raw || '' === $raw ) return null;
        if ( is_bool( $raw ) ) return $raw;
        if ( is_int( $raw ) ) {
            if ( 1 === $raw ) return true;
            if ( 0 === $raw ) return false;
        }
        if ( is_string( $raw ) ) {
            $value = strtolower( trim( $raw ) );
            if ( in_array( $value, [ '1', 'true', 'yes' ], true ) ) return true;
            if ( in_array( $value, [ '0', 'false', 'no' ], true ) ) return false;
        }
        return new WP_Error( 'elementize_invalid_visual_' . $name . '_filter', $name . ' must be true or false.', [ 'status' => 400 ] );
    }
}

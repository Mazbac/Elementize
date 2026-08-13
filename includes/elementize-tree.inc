<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Tree {
    public const MAX_DEPTH = 80;
    public const MAX_ELEMENTS = 10000;

    public static function reserved_ids( array $elements ): array {
        $ids = self::element_ids( $elements );
        self::collect_repeater_ids_existing( $elements, $ids, 0 );
        return $ids;
    }

    public static function element_ids( array $elements ): array {
        $ids = [];
        self::collect_element_ids( $elements, $ids, 0 );
        return $ids;
    }

    public static function clone_with_fresh_ids( array $elements, array &$used_ids, array &$map ): array {
        self::collect_clone_map( $elements, $map, $used_ids, 0 );
        return self::remap_element_list( $elements, $map, 0 );
    }

    public static function find_node( array $elements, string $id, int $depth = 0 ): ?array {
        if ( $depth > self::MAX_DEPTH ) return null;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( (string) ( $element['id'] ?? '' ) === $id ) return $element;
            if ( is_array( $element['elements'] ?? null ) ) {
                $found = self::find_node( $element['elements'], $id, $depth + 1 );
                if ( is_array( $found ) ) return $found;
            }
        }
        return null;
    }

    public static function insert_nodes( array &$elements, string $anchor_id, array $nodes, string $position, int $depth = 0 ) {
        if ( 0 === $depth && 'page_start' === $position ) { array_splice( $elements, 0, 0, $nodes ); return true; }
        if ( 0 === $depth && 'page_end' === $position ) { array_splice( $elements, count( $elements ), 0, $nodes ); return true; }
        if ( '' === $anchor_id ) return new WP_Error( 'elementize_creative_anchor_required', 'A valid anchor_ref is required unless position is page_start or page_end.', [ 'status' => 400 ] );
        if ( $depth > self::MAX_DEPTH ) return false;
        for ( $i = 0; $i < count( $elements ); $i++ ) {
            if ( ! is_array( $elements[ $i ] ) ) continue;
            if ( (string) ( $elements[ $i ]['id'] ?? '' ) === $anchor_id ) {
                if ( 'before' === $position ) { array_splice( $elements, $i, 0, $nodes ); return true; }
                if ( 'after' === $position ) { array_splice( $elements, $i + 1, 0, $nodes ); return true; }
                if ( in_array( $position, [ 'inside_start', 'inside_end' ], true ) ) {
                    if ( ! array_key_exists( 'elements', $elements[ $i ] ) || ! is_array( $elements[ $i ]['elements'] ) ) return new WP_Error( 'elementize_creative_anchor_not_container', 'The selected anchor cannot contain child elements.', [ 'status' => 400 ] );
                    $offset = 'inside_start' === $position ? 0 : count( $elements[ $i ]['elements'] );
                    array_splice( $elements[ $i ]['elements'], $offset, 0, $nodes );
                    return true;
                }
                return new WP_Error( 'elementize_creative_position_invalid', 'position must be before, after, inside_start, inside_end, page_start, or page_end.', [ 'status' => 400 ] );
            }
            if ( is_array( $elements[ $i ]['elements'] ?? null ) ) {
                $result = self::insert_nodes( $elements[ $i ]['elements'], $anchor_id, $nodes, $position, $depth + 1 );
                if ( true === $result || is_wp_error( $result ) ) return $result;
            }
        }
        return false;
    }

    public static function remove_node( array &$elements, string $id, &$removed, string &$parent_id, int &$index, string $parent = '', int $depth = 0 ): bool {
        if ( $depth > self::MAX_DEPTH ) return false;
        for ( $i = 0; $i < count( $elements ); $i++ ) {
            if ( ! is_array( $elements[ $i ] ) ) continue;
            if ( (string) ( $elements[ $i ]['id'] ?? '' ) === $id ) {
                $removed = $elements[ $i ];
                $parent_id = $parent;
                $index = $i;
                array_splice( $elements, $i, 1 );
                return true;
            }
            if ( is_array( $elements[ $i ]['elements'] ?? null ) ) {
                $current = (string) ( $elements[ $i ]['id'] ?? '' );
                if ( self::remove_node( $elements[ $i ]['elements'], $id, $removed, $parent_id, $index, $current, $depth + 1 ) ) return true;
            }
        }
        return false;
    }

    public static function reorder_children( array &$elements, string $parent_id, array $ordered_ids, int $depth = 0 ) {
        if ( '' === $parent_id ) return self::reorder_array( $elements, $ordered_ids );
        if ( $depth > self::MAX_DEPTH ) return false;
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( (string) ( $element['id'] ?? '' ) === $parent_id ) {
                if ( ! is_array( $element['elements'] ?? null ) ) return new WP_Error( 'elementize_reorder_parent_invalid', 'The reorder parent has no child element list.', [ 'status' => 400 ] );
                return self::reorder_array( $element['elements'], $ordered_ids );
            }
            if ( is_array( $element['elements'] ?? null ) ) {
                $result = self::reorder_children( $element['elements'], $parent_id, $ordered_ids, $depth + 1 );
                if ( false !== $result ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    public static function mutate_setting( array &$elements, string $id, array $path, callable $callback, int $depth = 0 ) {
        if ( $depth > self::MAX_DEPTH ) return false;
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( (string) ( $element['id'] ?? '' ) === $id ) {
                if ( ! is_array( $element['settings'] ?? null ) ) return new WP_Error( 'elementize_setting_not_found', 'The target element has no settings.', [ 'status' => 409 ] );
                $cursor =& $element['settings'];
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_setting_not_found', 'The requested setting no longer exists.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }
                return $callback( $cursor );
            }
            if ( is_array( $element['elements'] ?? null ) ) {
                $result = self::mutate_setting( $element['elements'], $id, $path, $callback, $depth + 1 );
                if ( false !== $result ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    public static function validate( array $elements ) {
        if ( ! $elements ) return new WP_Error( 'elementize_creative_empty_page', 'Creative Control will not save an empty Elementor page.', [ 'status' => 400 ] );
        $seen = [];
        $count = 0;
        $walk = function( array $nodes, int $depth ) use ( &$walk, &$seen, &$count ) {
            if ( $depth > self::MAX_DEPTH ) return new WP_Error( 'elementize_creative_depth_limit', 'The resulting Elementor tree is too deeply nested.', [ 'status' => 400 ] );
            foreach ( $nodes as $node ) {
                if ( ! is_array( $node ) ) return new WP_Error( 'elementize_creative_tree_invalid', 'The resulting Elementor tree contains an invalid element.', [ 'status' => 400 ] );
                $count++;
                if ( $count > self::MAX_ELEMENTS ) return new WP_Error( 'elementize_creative_element_limit', 'The resulting Elementor page exceeds the Elementize element limit.', [ 'status' => 400 ] );
                $id = trim( (string) ( $node['id'] ?? '' ) );
                if ( '' === $id ) return new WP_Error( 'elementize_creative_element_id_missing', 'Every Elementor element must keep a non-empty ID.', [ 'status' => 400 ] );
                if ( isset( $seen[ $id ] ) ) return new WP_Error( 'elementize_creative_duplicate_id', 'The resulting Elementor tree contains a duplicate element ID.', [ 'status' => 400, 'element_id' => $id ] );
                $seen[ $id ] = true;
                if ( isset( $node['elements'] ) && ! is_array( $node['elements'] ) ) return new WP_Error( 'elementize_creative_children_invalid', 'An Elementor element has an invalid child list.', [ 'status' => 400, 'element_id' => $id ] );
                if ( is_array( $node['elements'] ?? null ) ) {
                    $result = $walk( $node['elements'], $depth + 1 );
                    if ( is_wp_error( $result ) ) return $result;
                }
            }
            return true;
        };
        return $walk( $elements, 0 );
    }

    private static function reorder_array( array &$children, array $ordered_ids ) {
        $current_ids = [];
        foreach ( $children as $child ) if ( is_array( $child ) ) $current_ids[] = (string) ( $child['id'] ?? '' );
        if ( count( $current_ids ) !== count( $ordered_ids ) || array_diff( $current_ids, $ordered_ids ) || array_diff( $ordered_ids, $current_ids ) ) {
            return new WP_Error( 'elementize_reorder_set_mismatch', 'ordered_refs must contain every current child exactly once. Refresh structure before reordering.', [ 'status' => 409, 'current_child_ids' => $current_ids ] );
        }
        $by_id = [];
        foreach ( $children as $child ) if ( is_array( $child ) ) $by_id[ (string) $child['id'] ] = $child;
        $children = array_map( static fn( string $id ) => $by_id[ $id ], $ordered_ids );
        return true;
    }

    private static function collect_element_ids( array $elements, array &$ids, int $depth ): void {
        if ( $depth > self::MAX_DEPTH ) return;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            if ( '' !== $id ) $ids[ $id ] = true;
            if ( is_array( $element['elements'] ?? null ) ) self::collect_element_ids( $element['elements'], $ids, $depth + 1 );
        }
    }

    private static function collect_repeater_ids_existing( $value, array &$ids, int $depth ): void {
        if ( ! is_array( $value ) || $depth > 50 ) return;
        foreach ( $value as $key => $child ) {
            if ( '_id' === (string) $key && is_string( $child ) && '' !== $child ) $ids[ $child ] = true;
            if ( is_array( $child ) ) self::collect_repeater_ids_existing( $child, $ids, $depth + 1 );
        }
    }

    private static function collect_clone_map( array $elements, array &$map, array &$used_ids, int $depth ): void {
        if ( $depth > self::MAX_DEPTH ) return;
        foreach ( $elements as $node ) {
            if ( ! is_array( $node ) ) continue;
            $looks_like_element = isset( $node['elType'] ) || isset( $node['widgetType'] ) || array_key_exists( 'elements', $node );
            $id = $looks_like_element && is_string( $node['id'] ?? null ) ? trim( $node['id'] ) : '';
            if ( '' !== $id && preg_match( '/^[A-Za-z0-9_-]{4,40}$/', $id ) && ! isset( $map[ $id ] ) ) $map[ $id ] = self::fresh_id( $used_ids );
            self::collect_repeater_clone_map( $node['settings'] ?? [], $map, $used_ids, 0 );
            if ( is_array( $node['elements'] ?? null ) ) self::collect_clone_map( $node['elements'], $map, $used_ids, $depth + 1 );
            foreach ( $node as $key => $child ) {
                if ( in_array( (string) $key, [ 'id', 'settings', 'elements' ], true ) ) continue;
                if ( is_array( $child ) ) self::collect_repeater_clone_map( $child, $map, $used_ids, 0 );
            }
        }
    }

    private static function collect_repeater_clone_map( $value, array &$map, array &$used_ids, int $depth ): void {
        if ( ! is_array( $value ) || $depth > 30 ) return;
        foreach ( $value as $key => $child ) {
            if ( '_id' === (string) $key && is_string( $child ) && preg_match( '/^[A-Za-z0-9_-]{4,40}$/', $child ) && ! isset( $map[ $child ] ) ) $map[ $child ] = self::fresh_id( $used_ids );
            if ( is_array( $child ) ) self::collect_repeater_clone_map( $child, $map, $used_ids, $depth + 1 );
        }
    }

    private static function remap_element_list( array $elements, array $map, int $depth ): array {
        if ( $depth > self::MAX_DEPTH ) return [];
        $out = [];
        foreach ( $elements as $node ) {
            if ( ! is_array( $node ) ) continue;
            $copy = $node;
            $id = is_string( $copy['id'] ?? null ) ? $copy['id'] : '';
            if ( '' !== $id && isset( $map[ $id ] ) ) $copy['id'] = $map[ $id ];
            if ( array_key_exists( 'settings', $copy ) ) $copy['settings'] = self::remap_value( $copy['settings'], $map, 'settings', 0 );
            if ( is_array( $copy['elements'] ?? null ) ) $copy['elements'] = self::remap_element_list( $copy['elements'], $map, $depth + 1 );
            foreach ( $copy as $key => $child ) {
                if ( in_array( (string) $key, [ 'id', 'settings', 'elements' ], true ) ) continue;
                if ( is_array( $child ) ) $copy[ $key ] = self::remap_value( $child, $map, (string) $key, 0 );
            }
            $out[] = $copy;
        }
        return $out;
    }

    private static function remap_value( $value, array $map, string $key, int $depth ) {
        if ( $depth > 40 ) return $value;
        if ( is_array( $value ) ) {
            $out = [];
            foreach ( $value as $child_key => $child ) $out[ $child_key ] = self::remap_value( $child, $map, (string) $child_key, $depth + 1 );
            return $out;
        }
        if ( ! is_string( $value ) ) return $value;
        if ( '_id' === $key && isset( $map[ $value ] ) ) return $map[ $value ];
        if ( preg_match( '/(^|_)(element|element_id|target_id|anchor_id|container_id|parent_id|trigger_id)$/i', $key ) && isset( $map[ $value ] ) ) return $map[ $value ];
        foreach ( $map as $old => $new ) {
            $value = str_replace( 'elementor-element-' . $old, 'elementor-element-' . $new, $value );
            $value = str_replace( '#' . $old, '#' . $new, $value );
            $value = str_replace( '[data-id="' . $old . '"]', '[data-id="' . $new . '"]', $value );
            $value = str_replace( "[data-id='" . $old . "']", "[data-id='" . $new . "']", $value );
        }
        return $value;
    }

    private static function fresh_id( array &$used ): string {
        do { $id = substr( md5( wp_generate_uuid4() . microtime( true ) . wp_rand() ), 0, 8 ); } while ( isset( $used[ $id ] ) );
        $used[ $id ] = true;
        return $id;
    }
}

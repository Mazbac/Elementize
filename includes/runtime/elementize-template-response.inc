<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Keep template inspection responses usable by ChatGPT Actions.
 *
 * The template loader may legitimately discover hundreds of writable fields and
 * design controls. Returning every one of them can exceed the Actions response
 * budget even though Pixfort loaded the template successfully. This filter keeps
 * the structural truth intact while bounding the two large detail arrays.
 *
 * Design controls are visibility-prioritized rather than returned in raw tree
 * traversal order. This makes the bounded slice useful for Creative Control:
 * repeated feature/card/button/badge controls and direct visible color/style
 * controls are surfaced ahead of low-signal container/decorative settings.
 */
final class Elementize_Template_Response {
    private const FIELD_LIMIT = 40;
    private const CONTROL_LIMIT = 40;

    public static function init(): void {
        add_filter( 'rest_post_dispatch', [ self::class, 'compact' ], 100, 3 );
    }

    public static function compact( $result, $server, $request ) {
        if ( ! $request instanceof WP_REST_Request ) return $result;
        $route = (string) $request->get_route();
        if ( ! preg_match( '#^/elementize/v1/templates/[^/]+/[^/]+$#', $route ) ) return $result;
        if ( is_wp_error( $result ) ) return $result;

        $response = rest_ensure_response( $result );
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return $result;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $result;

        $fields = is_array( $data['editable_content_fields'] ?? null ) ? array_values( $data['editable_content_fields'] ) : [];
        $controls = is_array( $data['design_controls'] ?? null ) ? array_values( $data['design_controls'] ) : [];
        $selected_controls = self::prioritize_controls( $controls );
        $selection = self::selection_summary( $selected_controls, $controls );

        $data['inspection_payload'] = [
            'bounded_for_actions' => true,
            'editable_content_fields_available' => count( $fields ),
            'editable_content_fields_returned' => min( count( $fields ), self::FIELD_LIMIT ),
            'editable_content_fields_truncated' => count( $fields ) > self::FIELD_LIMIT,
            'design_controls_available' => count( $controls ),
            'design_controls_returned' => count( $selected_controls ),
            'design_controls_truncated' => count( $controls ) > self::CONTROL_LIMIT,
            'design_control_selection' => $selection,
        ];
        $data['editable_content_fields'] = array_slice( $fields, 0, self::FIELD_LIMIT );
        $data['design_controls'] = $selected_controls;

        $response->set_data( $data );
        return $response;
    }

    private static function prioritize_controls( array $controls ): array {
        if ( count( $controls ) <= self::CONTROL_LIMIT ) return array_values( $controls );

        $widget_counts = [];
        foreach ( $controls as $control ) {
            if ( ! is_array( $control ) ) continue;
            $widget = sanitize_key( (string) ( $control['widget_type'] ?? '' ) );
            if ( '' !== $widget ) $widget_counts[ $widget ] = ( $widget_counts[ $widget ] ?? 0 ) + 1;
        }

        $ranked = [];
        foreach ( array_values( $controls ) as $index => $control ) {
            if ( ! is_array( $control ) ) continue;
            $ranked[] = [
                'control' => $control,
                'index' => (int) $index,
                'score' => self::visibility_score( $control, $widget_counts ),
            ];
        }

        usort( $ranked, static function( array $a, array $b ): int {
            $score_compare = (int) $b['score'] <=> (int) $a['score'];
            return 0 !== $score_compare ? $score_compare : ( (int) $a['index'] <=> (int) $b['index'] );
        } );

        return array_values( array_map(
            static fn( array $item ): array => $item['control'],
            array_slice( $ranked, 0, self::CONTROL_LIMIT )
        ) );
    }

    private static function visibility_score( array $control, array $widget_counts ): int {
        $category = sanitize_key( (string) ( $control['category'] ?? '' ) );
        $widget = sanitize_key( (string) ( $control['widget_type'] ?? '' ) );
        $key = strtolower( (string) ( $control['setting_key'] ?? '' ) );
        $path_parts = is_array( $control['setting_path'] ?? null ) ? $control['setting_path'] : [];
        $path = strtolower( implode( '.', array_map( static fn( $part ): string => (string) $part, $path_parts ) ) );
        $value = $control['value'] ?? null;

        $score = 0;
        $category_scores = [
            'color' => 90,
            'radius' => 50,
            'typography' => 45,
            'spacing' => 35,
            'alignment' => 25,
            'size' => 20,
        ];
        $score += (int) ( $category_scores[ $category ] ?? 0 );

        $widget_scores = [
            'pix-feature' => 70,
            'pix-button' => 68,
            'pix-badge' => 65,
            'pix-fancybox' => 62,
            'pix-card' => 62,
            'pix-heading' => 45,
            'pix-highlighted-text' => 38,
            'pix-text' => 32,
            'divider' => 22,
        ];
        $score += (int) ( $widget_scores[ $widget ] ?? ( '' !== $widget ? 18 : -30 ) );

        if ( isset( $widget_counts[ $widget ] ) && $widget_counts[ $widget ] > 1 ) {
            $score += min( 24, (int) $widget_counts[ $widget ] * 2 );
        }

        $direct_keys = [
            'icon_color' => 55,
            'background_color' => 52,
            'text_color' => 50,
            'item_color' => 44,
            'border_color' => 42,
            'fill_color' => 42,
            'stroke_color' => 42,
            'color' => 34,
            'border_radius' => 24,
            'radius' => 24,
        ];
        foreach ( $direct_keys as $needle => $points ) {
            if ( $key === $needle || str_ends_with( $key, '_' . $needle ) || false !== strpos( $path, $needle ) ) {
                $score += $points;
                break;
            }
        }

        if ( is_string( $value ) && preg_match( '/^(#[0-9a-f]{3,8}|rgba?\([^\r\n]+\)|hsla?\([^\r\n]+\)|transparent|currentColor|var\(--[A-Za-z0-9_-]+\))$/i', trim( $value ) ) ) {
            $score += 22;
        }
        if ( is_string( $value ) && '' === trim( $value ) ) $score -= 18;
        if ( null === $value ) $score -= 18;

        if ( false !== strpos( $path, 'highlight_color' ) ) $score -= 100;
        if ( preg_match( '/(^|\.)(b_[123]_color|moving[^.]*|divider[^.]*)(\.|$)/', $path ) ) $score -= 45;
        if ( false !== strpos( $path, 'hover' ) ) $score -= 12;
        if ( false !== strpos( $path, 'responsive' ) ) $score -= 8;

        return $score;
    }

    private static function selection_summary( array $selected, array $all ): array {
        $categories = [];
        $widgets = [];
        foreach ( $selected as $control ) {
            if ( ! is_array( $control ) ) continue;
            $category = sanitize_key( (string) ( $control['category'] ?? '' ) );
            $widget = sanitize_key( (string) ( $control['widget_type'] ?? '' ) );
            if ( '' !== $category ) $categories[ $category ] = ( $categories[ $category ] ?? 0 ) + 1;
            if ( '' !== $widget ) $widgets[ $widget ] = ( $widgets[ $widget ] ?? 0 ) + 1;
        }
        arsort( $categories );
        arsort( $widgets );

        return [
            'strategy' => count( $all ) > self::CONTROL_LIMIT ? 'visibility_priority_v1' : 'all_controls_fit',
            'total_available' => count( $all ),
            'returned' => count( $selected ),
            'omitted' => max( 0, count( $all ) - count( $selected ) ),
            'returned_categories' => $categories,
            'returned_widget_types' => array_slice( $widgets, 0, 20, true ),
            'note' => 'Selection prioritizes direct visible component styles and repeated feature/card/button/badge widgets; exact controls remain unchanged and must still be grounded before mutation.',
        ];
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Keep template inspection responses usable by ChatGPT Actions.
 *
 * The template loader may legitimately discover hundreds of writable fields and
 * design controls. Returning every one of them can exceed the Actions response
 * budget even though Pixfort loaded the template successfully. This filter keeps
 * the structural truth intact while bounding the two large detail arrays.
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

        $data['inspection_payload'] = [
            'bounded_for_actions' => true,
            'editable_content_fields_available' => count( $fields ),
            'editable_content_fields_returned' => min( count( $fields ), self::FIELD_LIMIT ),
            'editable_content_fields_truncated' => count( $fields ) > self::FIELD_LIMIT,
            'design_controls_available' => count( $controls ),
            'design_controls_returned' => min( count( $controls ), self::CONTROL_LIMIT ),
            'design_controls_truncated' => count( $controls ) > self::CONTROL_LIMIT,
        ];
        $data['editable_content_fields'] = array_slice( $fields, 0, self::FIELD_LIMIT );
        $data['design_controls'] = array_slice( $controls, 0, self::CONTROL_LIMIT );

        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Template_Structure {
    private const VERSION = '0.7.0';
    private const NS = 'elementize/v1';
    private const MAX_TEMPLATES = 8;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 26 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 170, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\d+)/pixfort/template-structure', [
            'methods' => WP_REST_Server::CREATABLE,
            'callback' => [ self::class, 'inspect' ],
            'permission_callback' => [ self::class, 'can_inspect' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'template_ids' => [ 'type' => 'array', 'required' => true, 'items' => [ 'type' => 'string' ] ],
            ],
        ] );
    }

    public static function can_inspect( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_structure_inspection_forbidden', 'Pixfort structure inspection requires an administrator.', [ 'status' => 403 ] );
        $page_id = absint( $request['id'] );
        $page = get_post( $page_id );
        if ( ! $page || 'page' !== $page->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $page_id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function inspect( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $raw = $request->get_param( 'template_ids' );
        if ( ! is_array( $raw ) || ! $raw ) return new WP_Error( 'elementize_template_structure_required', 'template_ids must contain at least one Pixfort section ID.', [ 'status' => 400 ] );

        $ids = [];
        foreach ( $raw as $value ) {
            if ( ! is_string( $value ) ) return new WP_Error( 'elementize_template_structure_invalid_id', 'Each template_ids item must be a string.', [ 'status' => 400 ] );
            $id = sanitize_key( $value );
            if ( '' !== $id && ! in_array( $id, $ids, true ) ) $ids[] = $id;
        }
        if ( ! $ids ) return new WP_Error( 'elementize_template_structure_required', 'No usable Pixfort template IDs were supplied.', [ 'status' => 400 ] );
        if ( count( $ids ) > self::MAX_TEMPLATES ) return new WP_Error( 'elementize_template_structure_too_many', 'Inspect at most ' . self::MAX_TEMPLATES . ' Pixfort templates per request.', [ 'status' => 400 ] );

        $ready = self::load_source();
        if ( is_wp_error( $ready ) ) return $ready;

        $results = [];
        add_filter( 'pixfort_el_remote_get_args', [ self::class, 'remote_headers' ], 100 );
        try {
            $source = new \Elementor\TemplateLibrary\Source_Pixfort();
            foreach ( $ids as $template_id ) {
                try {
                    $template = $source->get_data( [ 'template_id' => $template_id, 'editor_post_id' => $page_id ] );
                } catch ( Throwable $e ) {
                    $results[] = [
                        'template_id' => $template_id,
                        'ok' => false,
                        'error' => 'Pixfort could not prepare this template for read-only inspection: ' . $e->getMessage(),
                    ];
                    continue;
                }
                if ( is_wp_error( $template ) ) {
                    $results[] = [ 'template_id' => $template_id, 'ok' => false, 'error' => $template->get_error_message() ];
                    continue;
                }
                if ( ! is_array( $template ) || empty( $template['content'] ) || ! is_array( $template['content'] ) ) {
                    $results[] = [ 'template_id' => $template_id, 'ok' => false, 'error' => 'Pixfort returned no inspectable Elementor content.' ];
                    continue;
                }
                $results[] = self::describe( $template_id, (string) ( $template['title'] ?? $template_id ), array_values( $template['content'] ) );
            }
        } finally {
            remove_filter( 'pixfort_el_remote_get_args', [ self::class, 'remote_headers' ], 100 );
        }

        $ok_count = 0;
        foreach ( $results as $result ) if ( ! empty( $result['ok'] ) ) $ok_count++;
        return new WP_REST_Response( [
            'read_only' => true,
            'page_id' => $page_id,
            'requested_count' => count( $ids ),
            'inspected_count' => $ok_count,
            'max_templates_per_request' => self::MAX_TEMPLATES,
            'templates' => $results,
        ], 200 );
    }

    private static function describe( string $template_id, string $title, array $content ): array {
        $stats = [
            'total_elements' => 0,
            'max_depth' => 0,
            'element_types' => [],
            'widget_types' => [],
            'role_counts' => [ 'heading' => 0, 'text' => 0, 'button_cta' => 0, 'image' => 0, 'icon' => 0, 'form' => 0, 'shortcode' => 0 ],
            'alignment_counts' => [ 'left' => 0, 'center' => 0, 'right' => 0, 'justify' => 0 ],
            'colors' => [],
            'dependency_widgets' => [],
            'widget_count' => 0,
        ];
        self::walk( $content, 0, $stats );

        arsort( $stats['widget_types'] );
        arsort( $stats['element_types'] );
        $top_level_shape = [];
        foreach ( $content as $node ) {
            if ( ! is_array( $node ) ) continue;
            $type = (string) ( $node['elType'] ?? 'unknown' );
            $children = is_array( $node['elements'] ?? null ) ? count( $node['elements'] ) : 0;
            $top_level_shape[] = $type . ':' . $children;
        }

        $alignment = self::dominant_alignment( $stats['alignment_counts'] );
        $image_count = (int) $stats['role_counts']['image'];
        $widget_count = max( 1, (int) $stats['widget_count'] );
        $image_ratio = $image_count / $widget_count;
        $image_prominence = 0 === $image_count ? 'none' : ( $image_ratio >= 0.35 ? 'high' : ( $image_ratio >= 0.15 ? 'medium' : 'low' ) );
        $per_top = $stats['widget_count'] / max( 1, count( $content ) );
        $density = $per_top >= 12 ? 'high' : ( $per_top >= 6 ? 'medium' : 'low' );
        $dependency_widgets = array_values( array_unique( $stats['dependency_widgets'] ) );
        sort( $dependency_widgets );
        $colors = array_keys( $stats['colors'] );
        sort( $colors );
        $colors = array_slice( $colors, 0, 20 );

        $signature_parts = [
            'top=' . implode( ',', $top_level_shape ),
            'widgets=' . self::compact_counts( $stats['widget_types'], 8 ),
            'roles=' . self::compact_counts( $stats['role_counts'], 7 ),
            'align=' . $alignment,
            'density=' . $density,
            'images=' . $image_prominence,
        ];
        $signature = implode( ';', $signature_parts );

        return [
            'template_id' => $template_id,
            'title' => $title,
            'ok' => true,
            'top_level_count' => count( $content ),
            'top_level_shape' => $top_level_shape,
            'total_elements' => $stats['total_elements'],
            'max_depth' => $stats['max_depth'],
            'element_type_counts' => $stats['element_types'],
            'widget_type_counts' => $stats['widget_types'],
            'role_counts' => $stats['role_counts'],
            'dominant_alignment' => $alignment,
            'alignment_counts' => $stats['alignment_counts'],
            'image_prominence' => $image_prominence,
            'approximate_density' => $density,
            'color_hints' => $colors,
            'dependency_indicators' => [
                'has_form_like_widget' => (int) $stats['role_counts']['form'] > 0,
                'has_shortcode_like_widget' => (int) $stats['role_counts']['shortcode'] > 0,
                'widgets' => $dependency_widgets,
            ],
            'structural_signature' => $signature,
            'structural_family_hash' => hash( 'sha256', $signature ),
            'heuristic_note' => 'Role, density, alignment, image-prominence, and dependency classifications are conservative heuristics derived from Elementor/Pixfort structure. Raw widget counts are included for verification.',
        ];
    }

    private static function walk( array $nodes, int $depth, array &$stats ): void {
        foreach ( $nodes as $node ) {
            if ( ! is_array( $node ) ) continue;
            $stats['total_elements']++;
            $stats['max_depth'] = max( $stats['max_depth'], $depth );
            $element_type = strtolower( (string) ( $node['elType'] ?? 'unknown' ) );
            if ( ! isset( $stats['element_types'][ $element_type ] ) ) $stats['element_types'][ $element_type ] = 0;
            $stats['element_types'][ $element_type ]++;

            $widget_type = strtolower( (string) ( $node['widgetType'] ?? '' ) );
            $settings = is_array( $node['settings'] ?? null ) ? $node['settings'] : [];
            if ( '' !== $widget_type ) {
                $stats['widget_count']++;
                if ( ! isset( $stats['widget_types'][ $widget_type ] ) ) $stats['widget_types'][ $widget_type ] = 0;
                $stats['widget_types'][ $widget_type ]++;
                self::classify_widget( $widget_type, $settings, $stats );
            }
            self::scan_settings( $settings, $stats );
            $children = $node['elements'] ?? [];
            if ( is_array( $children ) && $children ) self::walk( $children, $depth + 1, $stats );
        }
    }

    private static function classify_widget( string $widget, array $settings, array &$stats ): void {
        if ( preg_match( '/(^|[-_])(heading|title|headline|sliding-text)([-_]|$)/', $widget ) ) $stats['role_counts']['heading']++;
        if ( 'text-editor' === $widget || preg_match( '/(^|[-_])text([-_]|$)/', $widget ) ) $stats['role_counts']['text']++;
        if ( preg_match( '/button|(^|[-_])btn([-_]|$)|(^|[-_])cta([-_]|$)/', $widget ) ) $stats['role_counts']['button_cta']++;
        if ( preg_match( '/image|(^|[-_])img([-_]|$)|gallery/', $widget ) ) $stats['role_counts']['image']++;
        if ( preg_match( '/icon/', $widget ) ) $stats['role_counts']['icon']++;
        if ( preg_match( '/form|contact|newsletter|subscribe|wpforms|gravity/', $widget ) ) {
            $stats['role_counts']['form']++;
            $stats['dependency_widgets'][] = $widget;
        }
        if ( preg_match( '/shortcode/', $widget ) ) {
            $stats['role_counts']['shortcode']++;
            $stats['dependency_widgets'][] = $widget;
        }
        if ( preg_match( '/slider|carousel|posts|loop|feed|portfolio|products/', $widget ) ) $stats['dependency_widgets'][] = $widget;

        foreach ( $settings as $key => $value ) {
            $key = strtolower( (string) $key );
            if ( is_scalar( $value ) && '' !== trim( (string) $value ) && preg_match( '/(^|_)btn(_|$).*text|button.*text|cta.*text/', $key ) ) {
                $stats['role_counts']['button_cta']++;
                break;
            }
        }
    }

    private static function scan_settings( array $settings, array &$stats ): void {
        foreach ( $settings as $key => $value ) {
            $key_l = strtolower( (string) $key );
            if ( is_array( $value ) ) {
                self::scan_settings( $value, $stats );
                continue;
            }
            if ( ! is_scalar( $value ) ) continue;
            $value_s = trim( (string) $value );
            if ( '' === $value_s ) continue;

            if ( false !== strpos( $key_l, 'align' ) ) {
                $align = strtolower( $value_s );
                if ( isset( $stats['alignment_counts'][ $align ] ) ) $stats['alignment_counts'][ $align ]++;
            }
            if ( false !== strpos( $key_l, 'color' ) || false !== strpos( $key_l, 'background' ) ) {
                if ( preg_match( '/^#[0-9a-f]{3,8}$/i', $value_s ) || preg_match( '/^(rgb|rgba|hsl|hsla)\(/i', $value_s ) ) $stats['colors'][ strtolower( $value_s ) ] = true;
            }
            if ( false !== strpos( $key_l, 'shortcode' ) ) {
                $stats['role_counts']['shortcode']++;
                $stats['dependency_widgets'][] = 'setting:' . $key_l;
            }
        }
    }

    private static function dominant_alignment( array $counts ): string {
        $max = max( $counts );
        if ( $max < 1 ) return 'unknown';
        $leaders = [];
        foreach ( $counts as $key => $count ) if ( $count === $max ) $leaders[] = $key;
        return 1 === count( $leaders ) ? $leaders[0] : 'mixed';
    }

    private static function compact_counts( array $counts, int $limit ): string {
        arsort( $counts );
        $parts = [];
        foreach ( array_slice( $counts, 0, $limit, true ) as $key => $count ) $parts[] = $key . ':' . $count;
        return implode( ',', $parts );
    }

    private static function load_source() {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return new WP_Error( 'elementize_elementor_missing', 'Elementor is not loaded.', [ 'status' => 503 ] );
        $loader = trailingslashit( get_template_directory() ) . 'inc/demo-content/elementor/loader.php';
        if ( ! function_exists( 'pixfort_elementor_library_data' ) && file_exists( $loader ) ) require_once $loader;
        if ( ! function_exists( 'pixfort_elementor_library_data' ) ) return new WP_Error( 'elementize_pixfort_library_unavailable', 'The active theme does not expose the Pixfort Elementor template library.', [ 'status' => 503 ] );
        $source_path = defined( 'PIXFORT_PLUGIN_DIR' ) ? trailingslashit( PIXFORT_PLUGIN_DIR ) . 'includes/import/elementor/source.php' : '';
        if ( '' === $source_path || ! file_exists( $source_path ) ) return new WP_Error( 'elementize_pixfort_source_unavailable', 'Pixfort Core template import source was not found.', [ 'status' => 503 ] );
        if ( ! class_exists( '\\Elementor\\TemplateLibrary\\Source_Base' ) ) return new WP_Error( 'elementize_elementor_template_library_unavailable', 'Elementor template-library classes are not available.', [ 'status' => 503 ] );
        if ( ! class_exists( '\\Elementor\\TemplateLibrary\\Source_Pixfort', false ) ) require_once $source_path;
        return class_exists( '\\Elementor\\TemplateLibrary\\Source_Pixfort', false ) ? true : new WP_Error( 'elementize_pixfort_source_unavailable', 'Pixfort Core template import source could not be loaded.', [ 'status' => 503 ] );
    }

    public static function remote_headers( $args ) {
        if ( ! is_array( $args ) ) $args = [];
        $key = (string) get_option( 'envato_purchase_code_27889640', '' );
        if ( '' === trim( $key ) ) return $args;
        $headers = is_array( $args['headers'] ?? null ) ? $args['headers'] : [];
        $headers['pix_domain'] = site_url();
        $headers['purchase_key'] = $key;
        $args['headers'] = $headers;
        return $args;
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_template_structure_inspection'] = true;
        $data['design_template_structure_max_candidates'] = self::MAX_TEMPLATES;
        $data['design_template_structure_read_only'] = true;
        $response->set_data( $data );
        return $response;
    }
}

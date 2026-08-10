<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Page_Quality', false ) ) return;

final class Elementize_Page_Quality_Hardening {
    private const VERSION = '0.5.16';
    private const NS = 'elementize/v1';
    private const MAX_FINDINGS = 100;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 100, 3 );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::annotate_status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) ) return $response;

        if ( preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/\d+/links$#', $route ) ) {
            return self::harden_links_response( $response );
        }
        if ( preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/\d+/quality-audit$#', $route ) ) {
            return self::harden_quality_response( $response, $request );
        }
        return $response;
    }

    private static function annotate_status( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['page_quality_hardening'] = true;
        $data['page_quality_top_level_star_aggregation'] = true;
        $data['page_quality_rendered_star_check'] = true;
        $data['page_quality_all_placeholder_links_block'] = true;
        $data['page_quality_pixfort_demo_link_check'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function harden_links_response( $response ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) || ! isset( $data['links'] ) || ! is_array( $data['links'] ) ) return $response;

        $placeholder = 0;
        $non_cta_placeholder = 0;
        $demo = 0;
        foreach ( $data['links'] as &$link ) {
            if ( ! is_array( $link ) ) continue;
            $is_placeholder = ! empty( $link['placeholder_destination'] );
            $is_cta = ! empty( $link['cta_like'] );
            $is_demo = self::demo_vendor_destination( (string) ( $link['value'] ?? '' ) );
            $link['demo_vendor_destination'] = $is_demo;
            if ( $is_placeholder ) {
                $placeholder++;
                if ( ! $is_cta ) $non_cta_placeholder++;
            }
            if ( $is_demo ) $demo++;
        }
        unset( $link );

        $data['placeholder_link_count'] = $placeholder;
        $data['non_cta_placeholder_count'] = $non_cta_placeholder;
        $data['demo_vendor_link_count'] = $demo;
        $data['quality_hardening_version'] = self::VERSION;
        $response->set_data( $data );
        return $response;
    }

    private static function harden_quality_response( $response, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $id = absint( $request['id'] );
        if ( $id < 1 ) return $response;

        $findings = isset( $data['findings'] ) && is_array( $data['findings'] ) ? array_values( $data['findings'] ) : [];

        $link_request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/links' );
        $link_request->set_param( 'id', $id );
        $permission = Elementize_Page_Quality::can_read( $link_request );
        if ( ! is_wp_error( $permission ) ) {
            $links_response = Elementize_Page_Quality::links( $link_request );
            if ( $links_response instanceof WP_REST_Response && $links_response->get_status() < 400 ) {
                $link_data = $links_response->get_data();
                $links = is_array( $link_data ) && isset( $link_data['links'] ) && is_array( $link_data['links'] ) ? $link_data['links'] : [];
                foreach ( $links as $link ) {
                    if ( ! is_array( $link ) || count( $findings ) >= self::MAX_FINDINGS ) break;
                    $is_cta = ! empty( $link['cta_like'] );
                    if ( ! $is_cta && ! empty( $link['placeholder_destination'] ) ) {
                        self::append_unique( $findings, [
                            'category' => 'placeholder_link',
                            'severity' => 'blocker',
                            'completion_blocker_by_default' => true,
                            'element_id' => (string) ( $link['element_id'] ?? '' ),
                            'top_level_element_id' => (string) ( $link['top_level_element_id'] ?? '' ),
                            'widget_type' => (string) ( $link['widget_type'] ?? '' ),
                            'setting_path' => isset( $link['setting_path'] ) && is_array( $link['setting_path'] ) ? array_values( $link['setting_path'] ) : [],
                            'evidence' => 'Recognized page link has an empty or placeholder destination.',
                            'recommended_action' => 'Set a verified destination with the guarded links endpoint, or remove the interactive link.',
                        ] );
                    }
                    if ( self::demo_vendor_destination( (string) ( $link['value'] ?? '' ) ) ) {
                        self::append_unique( $findings, [
                            'category' => 'demo_vendor_link',
                            'severity' => 'blocker',
                            'completion_blocker_by_default' => true,
                            'element_id' => (string) ( $link['element_id'] ?? '' ),
                            'top_level_element_id' => (string) ( $link['top_level_element_id'] ?? '' ),
                            'widget_type' => (string) ( $link['widget_type'] ?? '' ),
                            'setting_path' => isset( $link['setting_path'] ) && is_array( $link['setting_path'] ) ? array_values( $link['setting_path'] ) : [],
                            'evidence' => 'Recognized page link still points to the Pixfort/Essentials demo redirect.',
                            'recommended_action' => 'Replace the demo destination with a verified site-specific destination or remove the link.',
                        ] );
                    }
                }
            }
        }

        $elements = self::elements( $id );
        if ( is_array( $elements ) ) {
            foreach ( $elements as $top ) {
                if ( ! is_array( $top ) || count( $findings ) >= self::MAX_FINDINGS ) break;
                $top_id = (string) ( $top['id'] ?? '' );
                $star_count = self::count_star_tokens( $top );
                if ( $star_count >= 3 && ! self::has_star_finding( $findings, $top_id ) ) {
                    self::append_unique( $findings, [
                        'category' => 'proof_like',
                        'severity' => 'blocker',
                        'completion_blocker_by_default' => true,
                        'element_id' => $top_id,
                        'top_level_element_id' => $top_id,
                        'widget_type' => '',
                        'evidence' => 'Top-level section contains ' . $star_count . ' star-like tokens across its descendants, consistent with a rating/proof pattern.',
                        'recommended_action' => 'Remove the star/rating pattern unless the user supplied or verified the proof.',
                    ] );
                }
            }
        }

        $rendered_star_count = self::rendered_star_count( $id );
        if ( $rendered_star_count >= 3 && ! self::has_star_finding( $findings, '' ) ) {
            self::append_unique( $findings, [
                'category' => 'proof_like',
                'severity' => 'blocker',
                'completion_blocker_by_default' => true,
                'element_id' => '',
                'top_level_element_id' => '',
                'widget_type' => '',
                'evidence' => 'Rendered page contains ' . $rendered_star_count . ' star-like icon/glyph markers, consistent with a rating/proof pattern.',
                'recommended_action' => 'Remove the star/rating pattern unless the user supplied or verified the proof.',
            ] );
        }

        $blockers = 0;
        $proof_like = 0;
        $dependencies = 0;
        $placeholder_links = 0;
        $demo_links = 0;
        foreach ( $findings as $finding ) {
            if ( ! is_array( $finding ) ) continue;
            if ( ! empty( $finding['completion_blocker_by_default'] ) ) $blockers++;
            $category = (string) ( $finding['category'] ?? '' );
            if ( 'proof_like' === $category ) $proof_like++;
            if ( 'external_dependency' === $category ) $dependencies++;
            if ( in_array( $category, [ 'cta_destination', 'placeholder_link' ], true ) ) $placeholder_links++;
            if ( 'demo_vendor_link' === $category ) $demo_links++;
        }

        $data['findings'] = array_values( $findings );
        $data['finding_count'] = count( $findings );
        $data['completion_blocker_count'] = $blockers;
        $data['proof_like_finding_count'] = $proof_like;
        $data['external_dependency_finding_count'] = $dependencies;
        $data['placeholder_link_finding_count'] = $placeholder_links;
        $data['demo_vendor_link_finding_count'] = $demo_links;
        $data['rendered_star_marker_count'] = $rendered_star_count;
        $data['default_completion_pass'] = 0 === $blockers;
        $data['quality_hardening_version'] = self::VERSION;
        $data['policy_note'] = 'Proof-like patterns, any placeholder interactive link, Pixfort/Essentials demo redirects, unsafe links, and render/dependency errors block clean completion unless explicitly verified by the user where applicable.';
        $response->set_data( $data );
        return $response;
    }

    private static function append_unique( array &$findings, array $candidate ): void {
        if ( count( $findings ) >= self::MAX_FINDINGS ) return;
        $key = self::finding_key( $candidate );
        foreach ( $findings as $finding ) {
            if ( is_array( $finding ) && self::finding_key( $finding ) === $key ) return;
        }
        $findings[] = $candidate;
    }

    private static function finding_key( array $finding ): string {
        $path = isset( $finding['setting_path'] ) && is_array( $finding['setting_path'] ) ? wp_json_encode( array_values( $finding['setting_path'] ) ) : '';
        return implode( '|', [
            (string) ( $finding['category'] ?? '' ),
            (string) ( $finding['element_id'] ?? '' ),
            (string) ( $finding['top_level_element_id'] ?? '' ),
            false === $path ? '' : (string) $path,
        ] );
    }

    private static function has_star_finding( array $findings, string $top_id ): bool {
        foreach ( $findings as $finding ) {
            if ( ! is_array( $finding ) || 'proof_like' !== (string) ( $finding['category'] ?? '' ) ) continue;
            $evidence = strtolower( (string) ( $finding['evidence'] ?? '' ) );
            if ( false === strpos( $evidence, 'star' ) && false === strpos( $evidence, 'rating' ) ) continue;
            if ( '' === $top_id || $top_id === (string) ( $finding['top_level_element_id'] ?? '' ) ) return true;
        }
        return false;
    }

    private static function elements( int $id ) {
        if ( ! class_exists( '\Elementor\Plugin' ) ) return null;
        try { $doc = \Elementor\Plugin::$instance->documents->get( $id ); }
        catch ( Throwable $e ) { return null; }
        if ( ! $doc || ! $doc->is_built_with_elementor() ) return null;
        $elements = $doc->get_elements_data();
        return is_array( $elements ) ? $elements : null;
    }

    private static function count_star_tokens( $value ): int {
        $count = 0;
        if ( is_array( $value ) ) {
            foreach ( $value as $child ) $count += self::count_star_tokens( $child );
            return $count;
        }
        if ( ! is_scalar( $value ) ) return 0;
        $text = (string) $value;
        if ( '' === trim( $text ) ) return 0;
        $count += substr_count( $text, '★' );
        $count += substr_count( $text, '⭐' );
        $count += substr_count( strtolower( $text ), '&#9733;' );
        $count += substr_count( strtolower( $text ), '&#x2605;' );
        if ( preg_match_all( '/(?:fa[srlb]?[-_ ]+fa[-_ ]*star(?:[-_][a-z0-9]+)?|pixicon[-_a-z0-9]*star[-_a-z0-9]*|icon[-_a-z0-9]*star[-_a-z0-9]*|(?:^|[^a-z0-9])star(?:[^a-z0-9]|$))/i', $text, $matches ) ) {
            $count += count( $matches[0] );
        }
        return $count;
    }

    private static function rendered_star_count( int $id ): int {
        if ( ! class_exists( '\Elementor\Plugin' ) || ! isset( \Elementor\Plugin::$instance->frontend ) ) return 0;
        $frontend = \Elementor\Plugin::$instance->frontend;
        if ( ! method_exists( $frontend, 'get_builder_content_for_display' ) ) return 0;
        $post = get_post( $id );
        if ( ! $post ) return 0;
        $previous = $GLOBALS['post'] ?? null;
        try {
            $GLOBALS['post'] = $post;
            setup_postdata( $post );
            $html = (string) $frontend->get_builder_content_for_display( $id, false );
        } catch ( Throwable $e ) {
            return 0;
        } finally {
            wp_reset_postdata();
            $GLOBALS['post'] = $previous;
        }
        if ( '' === $html ) return 0;
        $count = substr_count( $html, '★' ) + substr_count( $html, '⭐' );
        if ( preg_match_all( '/<(?:i|span|svg)[^>]{0,1000}(?:fa-star|pixicon[^"\'<> ]*star|icon[^"\'<> ]*star|data-icon=["\'][^"\']*star|aria-label=["\'][^"\']*star)[^>]*>/i', $html, $matches ) ) {
            $count += count( $matches[0] );
        }
        return $count;
    }

    private static function demo_vendor_destination( string $value ): bool {
        $value = trim( $value );
        if ( '' === $value ) return false;
        $parts = wp_parse_url( $value );
        if ( ! is_array( $parts ) ) return false;
        $host = strtolower( (string) ( $parts['host'] ?? '' ) );
        $path = rtrim( strtolower( (string) ( $parts['path'] ?? '' ) ), '/' );
        $query = strtolower( (string) ( $parts['query'] ?? '' ) );
        if ( in_array( $host, [ 'pixfort.website', 'www.pixfort.website' ], true ) && '/redirect' === $path ) {
            parse_str( $query, $args );
            return isset( $args['to'] ) && 'essentials' === strtolower( (string) $args['to'] );
        }
        return false;
    }
}
Elementize_Page_Quality_Hardening::init();

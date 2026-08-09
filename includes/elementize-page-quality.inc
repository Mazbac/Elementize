<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Page_Quality {
    private const VERSION = '0.5.15';
    private const NS = 'elementize/v1';
    private const MAX_LINK_UPDATES = 20;
    private const MAX_FINDINGS = 100;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 73 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'annotate_status' ], 99, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/links', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'links' ],
                'permission_callback' => [ self::class, 'can_read' ],
                'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update_links' ],
                'permission_callback' => [ self::class, 'can_write' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'expected_status' => [ 'type' => 'string', 'required' => true ],
                    'expected_title' => [ 'type' => 'string', 'required' => true ],
                    'content_hash' => [ 'type' => 'string', 'required' => true ],
                    'updates' => [ 'type' => 'array', 'required' => true ],
                    'confirm_link_write' => [ 'type' => 'boolean', 'required' => true ],
                ],
            ],
        ] );

        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/quality-audit', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'quality_audit' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'manage_options' ) || ! current_user_can( 'edit_post', $id ) ) {
            return new WP_Error( 'elementize_quality_forbidden', 'Page quality and link access require an administrator who can edit the page.', [ 'status' => 403 ] );
        }
        return true;
    }

    public static function can_write( WP_REST_Request $request ) {
        return self::can_read( $request );
    }

    public static function links( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $doc = self::need_doc( $id );
        if ( is_wp_error( $doc ) ) return $doc;
        $elements = $doc->get_elements_data();
        if ( ! is_array( $elements ) ) return self::bad_data();

        $items = [];
        self::collect_links( $elements, $items );
        $unsafe = 0;
        $cta_placeholders = 0;
        foreach ( $items as $item ) {
            if ( empty( $item['current_value_safe'] ) ) $unsafe++;
            if ( ! empty( $item['cta_like'] ) && ! empty( $item['placeholder_destination'] ) ) $cta_placeholders++;
        }

        $post = get_post( $id );
        return new WP_REST_Response( [
            'page_id' => $id,
            'page_title' => (string) get_the_title( $id ),
            'page_status' => $post ? (string) $post->post_status : '',
            'content_hash' => self::hash_elements( $elements ),
            'link_item_count' => count( $items ),
            'unsafe_current_link_count' => $unsafe,
            'cta_placeholder_count' => $cta_placeholders,
            'main_page_only' => true,
            'links' => $items,
        ], 200 );
    }

    public static function update_links( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $guard = self::guard_write( $request );
        if ( is_wp_error( $guard ) ) return $guard;
        $doc = $guard['document'];
        $elements = $guard['elements'];
        $before = $elements;

        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || ! $updates ) return new WP_Error( 'elementize_link_updates_required', 'At least one link update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_LINK_UPDATES ) return new WP_Error( 'elementize_too_many_link_updates', 'Send at most ' . self::MAX_LINK_UPDATES . ' link updates per request.', [ 'status' => 400 ] );

        $changes = [];
        foreach ( $updates as $index => $update ) {
            $valid = self::validate_link_update( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $new = self::sanitize_destination( (string) $update['value'] );
            if ( is_wp_error( $new ) ) return $new;
            $old = null;
            $applied = self::apply_link( $elements, (string) $update['element_id'], $update['setting_path'], (string) $update['expected_value'], $new, $old );
            if ( is_wp_error( $applied ) ) return $applied;
            if ( false === $applied ) return new WP_Error( 'elementize_link_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409 ] );
            $changes[] = [
                'element_id' => (string) $update['element_id'],
                'setting_path' => array_values( $update['setting_path'] ),
                'old_value' => $old,
                'new_value' => $new,
            ];
        }

        $revision = self::save_revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;

        try { $saved = $doc->save( [ 'elements' => $elements ] ); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_link_save_failed', 'Elementor could not save the link changes: ' . $e->getMessage(), [ 'status' => 500 ] ); }
        if ( ! $saved ) return new WP_Error( 'elementize_link_save_failed', 'Elementor refused to save the link changes.', [ 'status' => 500 ] );

        $fresh_doc = self::doc( $id );
        $fresh = $fresh_doc ? $fresh_doc->get_elements_data() : null;
        if ( ! is_array( $fresh ) || ! self::verify_link_changes( $fresh, $changes ) ) {
            try { $doc->save( [ 'elements' => $before ] ); } catch ( Throwable $ignored ) {}
            return new WP_Error( 'elementize_link_verification_failed', 'The saved link changes could not be verified, so Elementize attempted to restore the pre-change page.', [ 'status' => 500, 'revision_id' => $revision ] );
        }

        self::regenerate_css( $id );
        return new WP_REST_Response( [
            'saved' => true,
            'page_id' => $id,
            'updated_count' => count( $changes ),
            'revision_id' => $revision,
            'content_hash' => self::hash_elements( $fresh ),
            'changes' => $changes,
            'edit_url' => $doc->get_edit_url(),
            'preview_url' => get_preview_post_link( $id ),
        ], 200 );
    }

    public static function quality_audit( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $doc = self::need_doc( $id );
        if ( is_wp_error( $doc ) ) return $doc;
        $elements = $doc->get_elements_data();
        if ( ! is_array( $elements ) ) return self::bad_data();

        $findings = [];
        self::audit_elements( $elements, $findings );

        $links = [];
        self::collect_links( $elements, $links );
        foreach ( $links as $link ) {
            if ( count( $findings ) >= self::MAX_FINDINGS ) break;
            if ( ! empty( $link['cta_like'] ) && ! empty( $link['placeholder_destination'] ) ) {
                $findings[] = [
                    'category' => 'cta_destination',
                    'severity' => 'blocker',
                    'completion_blocker_by_default' => true,
                    'element_id' => $link['element_id'],
                    'top_level_element_id' => $link['top_level_element_id'],
                    'widget_type' => $link['widget_type'],
                    'setting_path' => $link['setting_path'],
                    'evidence' => 'CTA-like link has an empty or placeholder destination.',
                    'recommended_action' => 'Set a verified destination with the guarded links endpoint, or replace/remove the CTA.',
                ];
            }
            if ( empty( $link['current_value_safe'] ) ) {
                $findings[] = [
                    'category' => 'unsafe_link',
                    'severity' => 'blocker',
                    'completion_blocker_by_default' => true,
                    'element_id' => $link['element_id'],
                    'top_level_element_id' => $link['top_level_element_id'],
                    'widget_type' => $link['widget_type'],
                    'setting_path' => $link['setting_path'],
                    'evidence' => 'Recognized link uses an unsupported or unsafe destination scheme.',
                    'recommended_action' => 'Replace the destination with an allowed verified URL, path, fragment, mailto, or tel target.',
                ];
            }
        }

        $render = self::render_findings( $id );
        foreach ( $render['findings'] as $finding ) {
            if ( count( $findings ) >= self::MAX_FINDINGS ) break;
            $findings[] = $finding;
        }

        $blockers = 0;
        $proof_like = 0;
        $dependencies = 0;
        foreach ( $findings as $finding ) {
            if ( ! empty( $finding['completion_blocker_by_default'] ) ) $blockers++;
            if ( 'proof_like' === ( $finding['category'] ?? '' ) ) $proof_like++;
            if ( 'external_dependency' === ( $finding['category'] ?? '' ) ) $dependencies++;
        }

        $post = get_post( $id );
        return new WP_REST_Response( [
            'page_id' => $id,
            'page_title' => (string) get_the_title( $id ),
            'page_status' => $post ? (string) $post->post_status : '',
            'content_hash' => self::hash_elements( $elements ),
            'finding_count' => count( $findings ),
            'completion_blocker_count' => $blockers,
            'proof_like_finding_count' => $proof_like,
            'external_dependency_finding_count' => $dependencies,
            'default_completion_pass' => 0 === $blockers,
            'render_checked' => $render['checked'],
            'render_error' => $render['error'],
            'findings' => $findings,
            'policy_note' => 'Proof-like findings are blockers by default unless the user explicitly supplied or verified that proof. External dependencies require verification before completion.',
            'read_only' => true,
        ], 200 );
    }

    private static function audit_elements( array $elements, array &$findings, string $top_level_id = '' ): void {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) || count( $findings ) >= self::MAX_FINDINGS ) continue;
            $id = (string) ( $element['id'] ?? '' );
            $top = '' !== $top_level_id ? $top_level_id : $id;
            $widget = strtolower( (string) ( $element['widgetType'] ?? '' ) );
            $settings = isset( $element['settings'] ) && is_array( $element['settings'] ) ? $element['settings'] : [];

            if ( '' !== $widget && preg_match( '/(^|[-_])(rating|review|testimonial)([-_]|$)/i', $widget ) ) {
                self::add_finding( $findings, 'proof_like', 'blocker', true, $id, $top, $widget, 'Widget type is rating/review/testimonial-like.', 'Remove or replace unless the user supplied or verified the proof.' );
            } elseif ( '' !== $widget && preg_match( '/(^|[-_])(counter|stats?|client-logos?|brands?)([-_]|$)/i', $widget ) ) {
                self::add_finding( $findings, 'proof_like', 'review', true, $id, $top, $widget, 'Widget type can imply quantitative or customer proof.', 'Verify the claim/customer relationship or replace the section.' );
            }

            if ( '' !== $widget && preg_match( '/(^|[-_])(form|shortcode|feed)([-_]|$)/i', $widget ) ) {
                self::add_finding( $findings, 'external_dependency', 'review', false, $id, $top, $widget, 'Widget depends on another object or plugin integration.', 'Verify the dependency renders correctly before completion.' );
            }

            $star_count = self::count_star_tokens( $settings );
            if ( $star_count >= 3 ) {
                self::add_finding( $findings, 'proof_like', 'blocker', true, $id, $top, $widget, 'Element contains ' . $star_count . ' star-like icon/value tokens, consistent with a rating pattern.', 'Remove the rating pattern unless the user supplied or verified it.' );
            }

            $proof_text = self::find_proof_text( $settings );
            if ( '' !== $proof_text ) {
                self::add_finding( $findings, 'proof_like', 'review', true, $id, $top, $widget, 'Proof-like text detected: ' . $proof_text, 'Verify the claim or rewrite/remove it.' );
            }

            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                self::audit_elements( $element['elements'], $findings, $top );
            }
        }
    }

    private static function add_finding( array &$findings, string $category, string $severity, bool $blocker, string $id, string $top, string $widget, string $evidence, string $action ): void {
        if ( count( $findings ) >= self::MAX_FINDINGS ) return;
        $findings[] = [
            'category' => $category,
            'severity' => $severity,
            'completion_blocker_by_default' => $blocker,
            'element_id' => $id,
            'top_level_element_id' => $top,
            'widget_type' => $widget,
            'evidence' => $evidence,
            'recommended_action' => $action,
        ];
    }

    private static function count_star_tokens( $value ): int {
        $count = 0;
        if ( is_array( $value ) ) {
            foreach ( $value as $child ) $count += self::count_star_tokens( $child );
            return $count;
        }
        if ( ! is_scalar( $value ) ) return 0;
        $text = strtolower( trim( (string) $value ) );
        if ( '' === $text ) return 0;
        if ( preg_match( '/(^|[^a-z])(fa[srlb]?[-_ ]?fa[-_ ]?star|pixicon[^ ]*star|icon[^ ]*star|star)([^a-z]|$)/i', $text ) ) $count++;
        $count += substr_count( $text, '★' );
        $count += substr_count( $text, '⭐' );
        return $count;
    }

    private static function find_proof_text( $value ): string {
        if ( is_array( $value ) ) {
            foreach ( $value as $child ) {
                $found = self::find_proof_text( $child );
                if ( '' !== $found ) return $found;
            }
            return '';
        }
        if ( ! is_string( $value ) ) return '';
        $plain = trim( preg_replace( '/\\s+/', ' ', wp_strip_all_tags( $value ) ) );
        if ( '' === $plain || strlen( $plain ) > 500 ) return '';
        if ( preg_match( '/\\b(4\\.[0-9]\\s*\\/\\s*5|5\\s*\\/\\s*5|trusted by|rated by|reviews?|testimonials?|customers? served|clients? served)\\b/i', $plain ) ) {
            return mb_strlen( $plain ) > 180 ? mb_substr( $plain, 0, 180 ) . '…' : $plain;
        }
        return '';
    }

    private static function render_findings( int $id ): array {
        $out = [ 'checked' => false, 'error' => null, 'findings' => [] ];
        if ( ! class_exists( '\\Elementor\\Plugin' ) || ! isset( \Elementor\Plugin::$instance->frontend ) ) return $out;
        $frontend = \Elementor\Plugin::$instance->frontend;
        if ( ! method_exists( $frontend, 'get_builder_content_for_display' ) ) return $out;
        $post = get_post( $id );
        $previous = $GLOBALS['post'] ?? null;
        try {
            $GLOBALS['post'] = $post;
            setup_postdata( $post );
            $html = (string) $frontend->get_builder_content_for_display( $id, false );
            $out['checked'] = true;
        } catch ( Throwable $e ) {
            $out['checked'] = true;
            $out['error'] = [ 'class' => get_class( $e ), 'message' => $e->getMessage() ];
            $out['findings'][] = [
                'category' => 'render_error', 'severity' => 'blocker', 'completion_blocker_by_default' => true,
                'element_id' => '', 'top_level_element_id' => '', 'widget_type' => '',
                'evidence' => 'Elementor frontend rendering threw an exception.',
                'recommended_action' => 'Do not claim completion until the render error is resolved.',
            ];
            return $out;
        } finally {
            wp_reset_postdata();
            $GLOBALS['post'] = $previous;
        }

        $text = trim( preg_replace( '/\\s+/', ' ', wp_strip_all_tags( $html ) ) );
        $needles = [ 'contact form not found', 'form not found', 'shortcode not found', 'error: contact form' ];
        foreach ( $needles as $needle ) {
            if ( false !== stripos( $text, $needle ) ) {
                $out['findings'][] = [
                    'category' => 'render_error', 'severity' => 'blocker', 'completion_blocker_by_default' => true,
                    'element_id' => '', 'top_level_element_id' => '', 'widget_type' => '',
                    'evidence' => 'Rendered page contains error text: ' . $needle,
                    'recommended_action' => 'Replace or repair the broken dependency before completion.',
                ];
                break;
            }
        }
        if ( preg_match( '/\\[[a-zA-Z][a-zA-Z0-9_-]+(?:\\s[^\\]]*)?\\]/', $text, $match ) ) {
            $out['findings'][] = [
                'category' => 'unresolved_shortcode', 'severity' => 'blocker', 'completion_blocker_by_default' => true,
                'element_id' => '', 'top_level_element_id' => '', 'widget_type' => '',
                'evidence' => 'Rendered page still contains a shortcode-like token: ' . substr( $match[0], 0, 120 ),
                'recommended_action' => 'Verify or replace the unresolved shortcode before completion.',
            ];
        }
        return $out;
    }

    private static function collect_links( array $elements, array &$items, string $top_level_id = '' ): void {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            $top = '' !== $top_level_id ? $top_level_id : $id;
            $widget = isset( $element['widgetType'] ) ? (string) $element['widgetType'] : '';
            $type = isset( $element['elType'] ) ? (string) $element['elType'] : '';
            if ( '' !== $id && ! empty( $element['settings'] ) && is_array( $element['settings'] ) ) {
                self::collect_link_settings( $element['settings'], [], $id, $top, $type, $widget, $items );
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) self::collect_links( $element['elements'], $items, $top );
        }
    }

    private static function collect_link_settings( $value, array $path, string $id, string $top, string $type, string $widget, array &$items ): void {
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $child ) {
                self::collect_link_settings( $child, array_merge( $path, [ is_int( $key ) ? $key : (string) $key ] ), $id, $top, $type, $widget, $items );
            }
            return;
        }
        if ( ! is_string( $value ) || ! self::link_path( $path, $widget ) ) return;
        $dynamic = str_starts_with( ltrim( $value ), '[elementor-tag' );
        $safe = ! is_wp_error( self::sanitize_destination( $value ) );
        $items[] = [
            'element_id' => $id,
            'top_level_element_id' => $top,
            'element_type' => $type,
            'widget_type' => $widget,
            'setting_path' => array_values( $path ),
            'setting_key' => self::last_key( $path ),
            'value' => $value,
            'writable' => ! $dynamic,
            'dynamic' => $dynamic,
            'current_value_safe' => $safe,
            'cta_like' => self::cta_like( $path, $widget ),
            'placeholder_destination' => self::placeholder_destination( $value ),
        ];
    }

    private static function link_path( array $path, string $widget ): bool {
        $strings = [];
        foreach ( $path as $segment ) if ( is_string( $segment ) ) $strings[] = strtolower( $segment );
        if ( ! $strings ) return false;
        $joined = implode( '_', $strings );
        if ( preg_match( '/(^|_)(image|media|logo|photo|thumbnail|poster|background|video|icon)(_|$)/', $joined ) ) return false;
        if ( preg_match( '/(^|_)(color|hover|typography|font|size|width|height|margin|padding|border|radius|style)(_|$)/', $joined ) ) return false;
        $last = end( $strings );
        if ( in_array( $last, [ 'href', 'link', 'button_link', 'btn_link', 'cta_link' ], true ) ) return true;
        if ( str_ends_with( $last, '_link' ) || str_ends_with( $last, '_url' ) ) return true;
        if ( 'url' === $last ) {
            if ( preg_match( '/(link|button|btn|cta|href)/', $joined ) ) return true;
            if ( preg_match( '/button/i', $widget ) ) return true;
        }
        return false;
    }

    private static function cta_like( array $path, string $widget ): bool {
        $joined = strtolower( implode( '_', array_map( static fn( $x ) => is_scalar( $x ) ? (string) $x : '', $path ) ) . '_' . $widget );
        return (bool) preg_match( '/(^|_)(button|btn|cta)(_|$)/', $joined );
    }

    private static function placeholder_destination( string $value ): bool {
        $v = trim( $value );
        return '' === $v || '#' === $v || in_array( strtolower( $v ), [ 'javascript:void(0)', 'javascript:void(0);' ], true );
    }

    private static function sanitize_destination( string $raw ) {
        $value = trim( $raw );
        if ( '' === $value ) return '';
        if ( str_starts_with( ltrim( $value ), '[elementor-tag' ) ) return new WP_Error( 'elementize_dynamic_link_read_only', 'Dynamic link values are read-only.', [ 'status' => 400 ] );
        if ( '#' === $value ) return '#';
        if ( str_starts_with( $value, '#' ) ) {
            return preg_match( '/^#[A-Za-z][A-Za-z0-9_:\\-.]*$/', $value ) ? $value : new WP_Error( 'elementize_invalid_link', 'Fragment links must use a simple valid anchor.', [ 'status' => 400 ] );
        }
        if ( str_starts_with( $value, '/' ) && ! str_starts_with( $value, '//' ) ) {
            return preg_match( '/^\\/[A-Za-z0-9_~!$&\'()*+,;=:@%\\/?.#-]*$/', $value ) ? $value : new WP_Error( 'elementize_invalid_link', 'Relative links contain unsupported characters.', [ 'status' => 400 ] );
        }
        if ( preg_match( '#^https?://#i', $value ) ) {
            $url = esc_url_raw( $value, [ 'http', 'https' ] );
            return '' !== $url ? $url : new WP_Error( 'elementize_invalid_link', 'HTTP(S) link is invalid.', [ 'status' => 400 ] );
        }
        if ( str_starts_with( strtolower( $value ), 'mailto:' ) ) {
            $email = sanitize_email( substr( $value, 7 ) );
            return '' !== $email ? 'mailto:' . $email : new WP_Error( 'elementize_invalid_link', 'mailto link is invalid.', [ 'status' => 400 ] );
        }
        if ( str_starts_with( strtolower( $value ), 'tel:' ) ) {
            $phone = trim( substr( $value, 4 ) );
            return preg_match( '/^[0-9+(). -]{3,40}$/', $phone ) ? 'tel:' . $phone : new WP_Error( 'elementize_invalid_link', 'tel link is invalid.', [ 'status' => 400 ] );
        }
        return new WP_Error( 'elementize_invalid_link_scheme', 'Only http, https, relative paths, fragments, mailto, and tel destinations are allowed.', [ 'status' => 400 ] );
    }

    private static function validate_link_update( $update, int $index ) {
        if ( ! is_array( $update ) ) return self::invalid_update( $index, 'Each update must be an object.' );
        if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) return self::invalid_update( $index, 'element_id is required.' );
        if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) ) return self::invalid_update( $index, 'setting_path must be a non-empty array.' );
        foreach ( $update['setting_path'] as $segment ) if ( ! is_string( $segment ) && ! is_int( $segment ) ) return self::invalid_update( $index, 'setting_path segments must be strings or integers.' );
        if ( ! array_key_exists( 'expected_value', $update ) || ! is_string( $update['expected_value'] ) ) return self::invalid_update( $index, 'expected_value is required.' );
        if ( ! array_key_exists( 'value', $update ) || ! is_string( $update['value'] ) ) return self::invalid_update( $index, 'value must be a string.' );
        return true;
    }

    private static function invalid_update( int $index, string $message ): WP_Error {
        return new WP_Error( 'elementize_invalid_link_update', 'Invalid link update at index ' . $index . ': ' . $message, [ 'status' => 400 ] );
    }

    private static function apply_link( array &$elements, string $element_id, array $path, string $expected, string $new, ?string &$old ) {
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) return new WP_Error( 'elementize_link_setting_not_found', 'The target Elementor element has no settings.', [ 'status' => 409 ] );
                $cursor =& $element['settings'];
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_link_setting_not_found', 'The requested link setting no longer exists.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }
                if ( ! is_string( $cursor ) || ! self::link_path( $path, (string) ( $element['widgetType'] ?? '' ) ) ) return new WP_Error( 'elementize_not_link_field', 'Elementize only permits writes to recognized link destination fields.', [ 'status' => 400 ] );
                if ( str_starts_with( ltrim( $cursor ), '[elementor-tag' ) ) return new WP_Error( 'elementize_dynamic_link_read_only', 'Dynamic link values are read-only.', [ 'status' => 400 ] );
                if ( $cursor !== $expected ) return new WP_Error( 'elementize_link_value_changed', 'The target link changed after it was read.', [ 'status' => 409 ] );
                $old = $cursor;
                $cursor = $new;
                return true;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $result = self::apply_link( $element['elements'], $element_id, $path, $expected, $new, $old );
                if ( true === $result || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function verify_link_changes( array $elements, array $changes ): bool {
        foreach ( $changes as $change ) {
            $value = self::read_value( $elements, (string) $change['element_id'], $change['setting_path'] );
            if ( ! is_string( $value ) || $value !== $change['new_value'] ) return false;
        }
        return true;
    }

    private static function read_value( array $elements, string $element_id, array $path ) {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                $cursor = $element['settings'] ?? null;
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return null;
                    $cursor = $cursor[ $segment ];
                }
                return $cursor;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $found = self::read_value( $element['elements'], $element_id, $path );
                if ( null !== $found ) return $found;
            }
        }
        return null;
    }

    private static function guard_write( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_link_write' ) ) return new WP_Error( 'elementize_link_confirmation_required', 'Explicit confirm_link_write=true is required.', [ 'status' => 400 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( (string) $request->get_param( 'expected_status' ) !== (string) $post->post_status ) return new WP_Error( 'elementize_status_changed', 'The page status changed after the fresh read.', [ 'status' => 409, 'status_now' => $post->post_status ] );
        if ( (string) $request->get_param( 'expected_title' ) !== (string) $post->post_title ) return new WP_Error( 'elementize_title_changed', 'The page title changed after the fresh read.', [ 'status' => 409, 'title_now' => $post->post_title ] );
        $doc = self::need_doc( $id );
        if ( is_wp_error( $doc ) ) return $doc;
        $elements = $doc->get_elements_data();
        if ( ! is_array( $elements ) ) return self::bad_data();
        $hash = self::hash_elements( $elements );
        $expected_hash = (string) $request->get_param( 'content_hash' );
        if ( '' === $expected_hash || ! hash_equals( $hash, $expected_hash ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after the link read.', [ 'status' => 409, 'content_hash' => $hash ] );
        return [ 'document' => $doc, 'elements' => $elements ];
    }

    private static function need_doc( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return new WP_Error( 'elementize_elementor_required', 'Elementor must be installed and active.', [ 'status' => 503 ] );
        $doc = self::doc( $id );
        if ( ! $doc ) return new WP_Error( 'elementize_elementor_document_not_found', 'Elementor could not load this page as a document.', [ 'status' => 404 ] );
        if ( ! $doc->is_built_with_elementor() ) return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        return $doc;
    }

    private static function doc( int $id ) {
        try { return \Elementor\Plugin::$instance->documents->get( $id ); }
        catch ( Throwable $e ) { return null; }
    }

    private static function hash_elements( array $elements ): string {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return hash( 'sha256', false === $json ? '' : $json );
    }

    private static function save_revision( int $id ) {
        $post = get_post( $id );
        if ( ! $post || ! post_type_supports( $post->post_type, 'revisions' ) || ! wp_revisions_enabled( $post ) ) return null;
        add_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        try { $revision = wp_save_post_revision( $id ); }
        finally { remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX ); }
        return is_int( $revision ) && $revision > 0 ? $revision : new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the page was not modified.', [ 'status' => 500 ] );
    }

    private static function regenerate_css( int $id ): void {
        try {
            if ( class_exists( '\\Elementor\\Core\\Files\\CSS\\Post' ) ) {
                $css = new \Elementor\Core\Files\CSS\Post( $id );
                $css->update();
            }
        } catch ( Throwable $ignored ) {}
    }

    private static function last_key( array $path ): string {
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) if ( is_string( $path[ $i ] ) ) return strtolower( $path[ $i ] );
        return '';
    }

    private static function bad_data(): WP_Error {
        return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
    }

    public static function annotate_status( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['page_quality_audit'] = true;
        $data['page_quality_audit_render_check'] = true;
        $data['page_quality_audit_proof_patterns'] = true;
        $data['page_quality_audit_cta_destination_check'] = true;
        $data['page_links_read'] = true;
        $data['page_links_write'] = true;
        $data['page_links_allowed_schemes'] = [ 'http', 'https', 'relative', 'fragment', 'mailto', 'tel' ];
        $response->set_data( $data );
        return $response;
    }
}

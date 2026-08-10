<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Writes {
    private const VERSION = '0.11.0';
    private const NS = 'elementize/v1';
    private const MAX_UPDATES = 20;
    private const META_MANAGED = '_elementize_managed';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 29 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 255, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\d+)/design-settings', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'update' ],
            'permission_callback' => [ self::class, 'can_write' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'expected_status' => [ 'type' => 'string', 'required' => true ],
                'expected_title' => [ 'type' => 'string', 'required' => true ],
                'content_hash' => [ 'type' => 'string', 'required' => true ],
                'updates' => [ 'type' => 'array', 'required' => true ],
                'confirm_design_write' => [ 'type' => 'boolean', 'required' => true ],
            ],
        ] );
    }

    public static function can_write( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_design_write_forbidden', 'Design-setting changes require an administrator.', [ 'status' => 403 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function update( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( '1' !== (string) get_post_meta( $id, self::META_MANAGED, true ) ) return new WP_Error( 'elementize_unmanaged_page', 'Design writes are allowed only on Elementize-managed pages.', [ 'status' => 409 ] );
        if ( 'draft' !== $post->post_status || 'draft' !== (string) $request->get_param( 'expected_status' ) ) return new WP_Error( 'elementize_design_write_draft_only', 'Design writes are allowed only on managed drafts.', [ 'status' => 409, 'status_now' => $post->post_status ] );
        if ( (string) $request->get_param( 'expected_title' ) !== (string) $post->post_title ) return new WP_Error( 'elementize_title_changed', 'The page title changed after design settings were read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        if ( true !== $request->get_param( 'confirm_design_write' ) ) return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_design_write=true is required.', [ 'status' => 400 ] );

        $document = self::need_doc( $id );
        if ( is_wp_error( $document ) ) return $document;
        $elements = $document->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
        $current_hash = self::hash( $elements );
        if ( ! hash_equals( $current_hash, (string) $request->get_param( 'content_hash' ) ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after design settings were read. Read them again.', [ 'status' => 409, 'content_hash' => $current_hash ] );

        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || ! $updates ) return new WP_Error( 'elementize_design_updates_required', 'At least one design-setting update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_UPDATES ) return new WP_Error( 'elementize_too_many_design_updates', 'Send at most ' . self::MAX_UPDATES . ' design-setting updates per request.', [ 'status' => 400 ] );

        $before = $elements;
        $changes = [];
        foreach ( $updates as $index => $update ) {
            $valid = self::validate_update_shape( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $old = null;
            $new = null;
            $applied = self::apply_update( $elements, $update, $old, $new );
            if ( is_wp_error( $applied ) ) return $applied;
            if ( false === $applied ) return new WP_Error( 'elementize_design_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409 ] );
            $changes[] = [
                'category' => (string) $update['category'],
                'element_id' => (string) $update['element_id'],
                'setting_path' => array_values( $update['setting_path'] ),
                'old_value' => $old,
                'new_value' => $new,
            ];
        }

        $revision = self::revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        try { $saved = $document->save( [ 'elements' => $elements ] ); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_design_save_failed', 'Elementor could not save the design changes: ' . $e->getMessage(), [ 'status' => 500 ] ); }
        if ( ! $saved ) return new WP_Error( 'elementize_design_save_failed', 'Elementor refused to save the design changes.', [ 'status' => 500 ] );

        $fresh_doc = self::doc( $id );
        $fresh = $fresh_doc ? $fresh_doc->get_elements_data() : null;
        if ( ! is_array( $fresh ) || ! self::verify_changes( $fresh, $changes ) ) {
            try { $document->save( [ 'elements' => $before ] ); self::regenerate_css( $id ); } catch ( Throwable $ignored ) {}
            return new WP_Error( 'elementize_design_verification_failed', 'The saved design changes could not be verified, so Elementize attempted to restore the pre-change page.', [ 'status' => 500, 'revision_id' => $revision ] );
        }

        self::regenerate_css( $id );
        return new WP_REST_Response( [
            'saved' => true,
            'design_writer_version' => self::VERSION,
            'page_id' => $id,
            'updated_count' => count( $changes ),
            'revision_id' => $revision,
            'content_hash' => self::hash( $fresh ),
            'supported_categories' => [ 'spacing', 'alignment', 'typography', 'border_radius' ],
            'changes' => $changes,
            'edit_url' => $document->get_edit_url(),
            'preview_url' => get_preview_post_link( $id ),
        ], 200 );
    }

    private static function validate_update_shape( $update, int $index ) {
        if ( ! is_array( $update ) ) return self::bad_update( $index, 'Each update must be an object.' );
        $category = isset( $update['category'] ) && is_string( $update['category'] ) ? sanitize_key( $update['category'] ) : '';
        if ( ! in_array( $category, [ 'spacing', 'alignment', 'typography', 'border_radius' ], true ) ) return self::bad_update( $index, 'category must be spacing, alignment, typography, or border_radius.' );
        if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) return self::bad_update( $index, 'element_id is required.' );
        if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) || count( $update['setting_path'] ) > 20 ) return self::bad_update( $index, 'setting_path must be a non-empty array with at most 20 segments.' );
        foreach ( $update['setting_path'] as $segment ) if ( ! is_string( $segment ) && ! is_int( $segment ) ) return self::bad_update( $index, 'Each setting_path segment must be a string or integer.' );
        if ( self::path_has_key( $update['setting_path'], '__globals__' ) || self::path_has_key( $update['setting_path'], '__dynamic__' ) ) return self::bad_update( $index, 'Global and dynamic controls are read-only.' );
        if ( ! array_key_exists( 'expected_value', $update ) || ! array_key_exists( 'value', $update ) ) return self::bad_update( $index, 'expected_value and value are required.' );
        if ( empty( $update['control_fingerprint'] ) || ! is_string( $update['control_fingerprint'] ) ) return self::bad_update( $index, 'control_fingerprint from a fresh design-settings read is required.' );
        return true;
    }

    private static function apply_update( array &$elements, array $update, &$old, &$new ) {
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === (string) $update['element_id'] ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) return new WP_Error( 'elementize_design_target_missing', 'The target element has no settings.', [ 'status' => 409 ] );
                if ( self::source_is_global_or_dynamic( $element['settings'], $update['setting_path'] ) ) return new WP_Error( 'elementize_design_target_read_only', 'Global and dynamic design controls are read-only.', [ 'status' => 400 ] );
                $cursor =& $element['settings'];
                foreach ( $update['setting_path'] as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_design_value_changed', 'The design control path changed after it was read.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }
                $fingerprint = self::fingerprint( (string) $update['element_id'], $update['setting_path'], $cursor );
                if ( ! hash_equals( $fingerprint, (string) $update['control_fingerprint'] ) ) return new WP_Error( 'elementize_design_fingerprint_changed', 'The design control changed after it was read.', [ 'status' => 409, 'control_fingerprint' => $fingerprint ] );
                if ( ! self::same_value( $cursor, $update['expected_value'] ) ) return new WP_Error( 'elementize_design_value_changed', 'The design control value changed after it was read.', [ 'status' => 409 ] );
                $candidate = self::sanitize_candidate( (string) $update['category'], $update['setting_path'], $cursor, $update['value'] );
                if ( is_wp_error( $candidate ) ) return $candidate;
                $old = $cursor;
                $cursor = $candidate;
                $new = $candidate;
                return true;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $result = self::apply_update( $element['elements'], $update, $old, $new );
                if ( true === $result || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function sanitize_candidate( string $category, array $path, $current, $candidate ) {
        $key = self::last_path_key( $path );
        $path_text = strtolower( implode( '.', array_map( 'strval', $path ) ) );
        if ( preg_match( '/moving_divider|gradient|overlay|slideshow|motion|animation|transform|custom_css|css_class|css_id/', $path_text ) ) return new WP_Error( 'elementize_design_target_not_writable', 'This complex or behavioral design control is discovery-only.', [ 'status' => 400 ] );

        if ( 'spacing' === $category ) {
            if ( ! preg_match( '/margin|padding|(?:^|[._-])gap(?:$|[._-])|column[_-]?gap|row[_-]?gap|spacing|space[_-]?(?:between|before|after)/', $path_text ) ) return self::not_writable();
            if ( is_array( $current ) && self::is_box( $current ) && is_array( $candidate ) ) return self::sanitize_box( $candidate, 'padding' === $key ? 0.0 : -500.0, 500.0, [ 'px', 'em', 'rem', '%', 'vw', 'vh' ] );
            if ( is_array( $current ) && self::is_dimension( $current ) && is_array( $candidate ) ) return self::sanitize_dimension( $candidate, -500.0, 500.0, [ 'px', 'em', 'rem', '%', 'vw', 'vh' ] );
            return self::not_writable();
        }

        if ( 'border_radius' === $category ) {
            if ( ! preg_match( '/border[_-]?radius|radius/', $path_text ) ) return self::not_writable();
            if ( is_array( $current ) && self::is_box( $current ) && is_array( $candidate ) ) return self::sanitize_box( $candidate, 0.0, 300.0, [ 'px', '%' ] );
            if ( is_array( $current ) && self::is_dimension( $current ) && is_array( $candidate ) ) return self::sanitize_dimension( $candidate, 0.0, 300.0, [ 'px', '%' ] );
            return self::not_writable();
        }

        if ( 'alignment' === $category ) {
            if ( ! is_string( $current ) || ! is_string( $candidate ) || ! preg_match( '/align|justify/', $path_text ) ) return self::not_writable();
            $value = strtolower( trim( $candidate ) );
            $allowed = [ 'left', 'center', 'right', 'justify', 'start', 'end', 'flex-start', 'flex-end', 'space-between', 'space-around', 'space-evenly', 'stretch' ];
            if ( ! in_array( $value, $allowed, true ) ) return new WP_Error( 'elementize_invalid_design_value', 'Unsupported alignment value.', [ 'status' => 400 ] );
            return $value;
        }

        if ( 'typography' === $category ) {
            if ( preg_match( '/font[_-]?family/', $path_text ) ) return new WP_Error( 'elementize_design_target_not_writable', 'Font-family changes remain discovery-only in the first writer release.', [ 'status' => 400 ] );
            if ( is_array( $current ) && self::is_dimension( $current ) && is_array( $candidate ) ) {
                if ( preg_match( '/font[_-]?size/', $path_text ) ) return self::sanitize_dimension( $candidate, 8.0, 160.0, [ 'px', 'em', 'rem', 'vw' ] );
                if ( preg_match( '/line[_-]?height/', $path_text ) ) return self::sanitize_dimension( $candidate, 0.5, 4.0, [ 'em', 'rem' ], true );
                if ( preg_match( '/letter[_-]?spacing/', $path_text ) ) return self::sanitize_dimension( $candidate, -10.0, 30.0, [ 'px', 'em', 'rem' ] );
                return self::not_writable();
            }
            if ( preg_match( '/font[_-]?weight/', $path_text ) && ( is_string( $candidate ) || is_numeric( $candidate ) ) ) {
                $v = strtolower( trim( (string) $candidate ) );
                if ( in_array( $v, [ 'normal', 'bold', 'bolder', 'lighter' ], true ) ) return $v;
                if ( ctype_digit( $v ) && (int) $v >= 100 && (int) $v <= 900 && 0 === (int) $v % 100 ) return (int) $v;
                return new WP_Error( 'elementize_invalid_design_value', 'font weight must be normal/bold/bolder/lighter or 100-900 in 100 steps.', [ 'status' => 400 ] );
            }
            if ( preg_match( '/text[_-]?transform/', $path_text ) && is_string( $candidate ) ) return self::enum_value( $candidate, [ 'none', 'uppercase', 'lowercase', 'capitalize' ] );
            if ( preg_match( '/font[_-]?style/', $path_text ) && is_string( $candidate ) ) return self::enum_value( $candidate, [ 'normal', 'italic', 'oblique' ] );
            if ( preg_match( '/text[_-]?decoration/', $path_text ) && is_string( $candidate ) ) return self::enum_value( $candidate, [ 'none', 'underline', 'overline', 'line-through' ] );
            return self::not_writable();
        }

        return self::not_writable();
    }

    private static function sanitize_dimension( array $value, float $min, float $max, array $units, bool $unitless_ok = false ) {
        if ( ! array_key_exists( 'size', $value ) ) return new WP_Error( 'elementize_invalid_design_value', 'Dimension value must include size.', [ 'status' => 400 ] );
        $unit = isset( $value['unit'] ) ? strtolower( trim( (string) $value['unit'] ) ) : '';
        if ( '' === $unit && ! $unitless_ok ) return new WP_Error( 'elementize_invalid_design_value', 'Dimension value must include an allowed unit.', [ 'status' => 400 ] );
        if ( '' !== $unit && ! in_array( $unit, $units, true ) ) return new WP_Error( 'elementize_invalid_design_value', 'Unsupported dimension unit.', [ 'status' => 400 ] );
        if ( ! is_numeric( $value['size'] ) ) return new WP_Error( 'elementize_invalid_design_value', 'Dimension size must be numeric.', [ 'status' => 400 ] );
        $size = (float) $value['size'];
        if ( $size < $min || $size > $max ) return new WP_Error( 'elementize_invalid_design_value', 'Dimension size is outside the guarded range.', [ 'status' => 400 ] );
        $next = $value;
        $next['size'] = $size;
        if ( '' !== $unit ) $next['unit'] = $unit;
        return $next;
    }

    private static function sanitize_box( array $value, float $min, float $max, array $units ) {
        $unit = isset( $value['unit'] ) ? strtolower( trim( (string) $value['unit'] ) ) : '';
        if ( ! in_array( $unit, $units, true ) ) return new WP_Error( 'elementize_invalid_design_value', 'Unsupported box unit.', [ 'status' => 400 ] );
        $next = $value;
        foreach ( [ 'top', 'right', 'bottom', 'left' ] as $side ) {
            if ( ! array_key_exists( $side, $value ) || ! is_numeric( $value[ $side ] ) ) return new WP_Error( 'elementize_invalid_design_value', 'Box values must contain numeric top/right/bottom/left.', [ 'status' => 400 ] );
            $n = (float) $value[ $side ];
            if ( $n < $min || $n > $max ) return new WP_Error( 'elementize_invalid_design_value', 'A box side is outside the guarded range.', [ 'status' => 400 ] );
            $next[ $side ] = (string) ( 0.0 === $n ? '0' : rtrim( rtrim( sprintf( '%.4F', $n ), '0' ), '.' ) );
        }
        $next['unit'] = $unit;
        if ( array_key_exists( 'isLinked', $value ) ) $next['isLinked'] = (bool) $value['isLinked'];
        return $next;
    }

    private static function enum_value( string $value, array $allowed ) {
        $value = strtolower( trim( $value ) );
        return in_array( $value, $allowed, true ) ? $value : new WP_Error( 'elementize_invalid_design_value', 'Unsupported typography value.', [ 'status' => 400 ] );
    }

    private static function source_is_global_or_dynamic( array $settings, array $path ): bool {
        $globals = is_array( $settings['__globals__'] ?? null ) ? $settings['__globals__'] : [];
        $dynamic = is_array( $settings['__dynamic__'] ?? null ) ? $settings['__dynamic__'] : [];
        $keys = [];
        if ( $path ) {
            $keys[] = (string) $path[0];
            $keys[] = (string) end( $path );
            $keys[] = implode( '.', array_map( 'strval', $path ) );
        }
        foreach ( array_unique( $keys ) as $key ) if ( array_key_exists( $key, $globals ) || array_key_exists( $key, $dynamic ) ) return true;
        return false;
    }

    private static function verify_changes( array $elements, array $changes ): bool {
        foreach ( $changes as $change ) {
            $found = false;
            $value = self::get_value( $elements, (string) $change['element_id'], $change['setting_path'], $found );
            if ( ! $found || ! self::same_value( $value, $change['new_value'] ) ) return false;
        }
        return true;
    }

    private static function get_value( array $elements, string $element_id, array $path, bool &$found ) {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                $cursor = is_array( $element['settings'] ?? null ) ? $element['settings'] : [];
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) { $found = false; return null; }
                    $cursor = $cursor[ $segment ];
                }
                $found = true;
                return $cursor;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $value = self::get_value( $element['elements'], $element_id, $path, $found );
                if ( $found ) return $value;
            }
        }
        $found = false;
        return null;
    }

    private static function same_value( $a, $b ): bool { return wp_json_encode( $a ) === wp_json_encode( $b ); }
    private static function fingerprint( string $element_id, array $path, $value ): string { return substr( hash( 'sha256', $element_id . '|' . wp_json_encode( $path ) . '|' . wp_json_encode( $value ) ), 0, 20 ); }
    private static function is_dimension( array $value ): bool { return array_key_exists( 'size', $value ) && ( isset( $value['unit'] ) || is_numeric( $value['size'] ) ); }
    private static function is_box( array $value ): bool { $hits = 0; foreach ( [ 'top', 'right', 'bottom', 'left' ] as $key ) if ( array_key_exists( $key, $value ) ) $hits++; return $hits >= 2; }
    private static function last_path_key( array $path ): string { for ( $i = count( $path ) - 1; $i >= 0; $i-- ) if ( is_string( $path[ $i ] ) ) return strtolower( $path[ $i ] ); return ''; }
    private static function path_has_key( array $path, string $needle ): bool { $needle = strtolower( $needle ); foreach ( $path as $part ) if ( is_string( $part ) && strtolower( $part ) === $needle ) return true; return false; }
    private static function not_writable() { return new WP_Error( 'elementize_design_target_not_writable', 'This design control is not in the guarded writable set.', [ 'status' => 400 ] ); }
    private static function bad_update( int $index, string $message ) { return new WP_Error( 'elementize_invalid_design_update', 'Update ' . $index . ': ' . $message, [ 'status' => 400 ] ); }

    private static function revision( int $id ) {
        $post = get_post( $id );
        if ( ! $post || ! post_type_supports( $post->post_type, 'revisions' ) || ! wp_revisions_enabled( $post ) ) return null;
        add_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
        try { $revision = wp_save_post_revision( $id ); }
        finally { remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX ); }
        return is_int( $revision ) && $revision > 0 ? $revision : new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the page was not modified.', [ 'status' => 500 ] );
    }

    private static function need_doc( int $id ) {
        if ( ! class_exists( '\Elementor\Plugin' ) ) return new WP_Error( 'elementize_elementor_required', 'Elementor must be installed and active.', [ 'status' => 503 ] );
        $doc = self::doc( $id );
        if ( ! $doc ) return new WP_Error( 'elementize_elementor_document_not_found', 'Elementor could not load this page as a document.', [ 'status' => 404 ] );
        if ( ! $doc->is_built_with_elementor() ) return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        return $doc;
    }

    private static function doc( int $id ) { try { return \Elementor\Plugin::$instance->documents->get( $id ); } catch ( Throwable $e ) { return null; } }
    private static function hash( array $elements ): string { $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ); return hash( 'sha256', false === $json ? '' : $json ); }

    private static function regenerate_css( int $id ): void {
        if ( class_exists( '\Elementor\Core\Files\CSS\Post' ) ) { try { $css = new \Elementor\Core\Files\CSS\Post( $id ); $css->update(); } catch ( Throwable $ignored ) {} }
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_settings_write'] = true;
        $data['design_settings_write_managed_drafts_only'] = true;
        $data['design_settings_write_categories'] = [ 'spacing', 'alignment', 'typography', 'border_radius' ];
        $data['design_settings_write_fingerprint_guard'] = true;
        $data['design_settings_write_revision'] = true;
        $data['design_settings_write_verify_rollback'] = true;
        $data['design_settings_write_css_regeneration'] = true;
        $data['design_settings_write_global_dynamic'] = false;
        $data['design_settings_write_complex_composited'] = false;
        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Content {
    private const MAX_UPDATES = 50;
    private const MAX_TEXT = 20000;
    private const MAX_ITEMS = 5000;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 20 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/content', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'read' ],
                'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'kind' => [ 'type' => 'string', 'required' => false, 'default' => 'all', 'sanitize_callback' => 'sanitize_key' ],
                    'element_id' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                    'top_level_id' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                    'q' => [ 'type' => 'string', 'required' => false, 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                    'offset' => [ 'type' => 'integer', 'required' => false, 'default' => 0, 'minimum' => 0, 'sanitize_callback' => 'absint' ],
                    'limit' => [ 'type' => 'integer', 'required' => false, 'default' => 100, 'minimum' => 1, 'maximum' => 500, 'sanitize_callback' => 'absint' ],
                ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update' ],
                'permission_callback' => [ Elementize_Core::class, 'can_write_page' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'expected_status' => [ 'type' => 'string', 'required' => true ],
                    'expected_title' => [ 'type' => 'string', 'required' => true ],
                    'content_hash' => [ 'type' => 'string', 'required' => true ],
                    'updates' => [ 'type' => 'array', 'required' => true ],
                    'confirm_content_write' => [ 'type' => 'boolean', 'required' => true ],
                    'confirm_published_page' => [ 'type' => 'boolean', 'required' => false, 'default' => false ],
                ],
            ],
        ] );
    }

    public static function read( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;

        $items = [];
        self::collect_elements( $loaded['elements'], $items );
        if ( count( $items ) > self::MAX_ITEMS ) $items = array_slice( $items, 0, self::MAX_ITEMS );

        $kind = strtolower( trim( (string) $request->get_param( 'kind' ) ) );
        if ( '' === $kind ) $kind = 'all';
        $allowed = [ 'all', 'text', 'link', 'media', 'pixfort_icon' ];
        if ( ! in_array( $kind, $allowed, true ) ) return new WP_Error( 'elementize_invalid_content_kind', 'kind must be all, text, link, media, or pixfort_icon.', [ 'status' => 400 ] );

        $element_id = trim( (string) $request->get_param( 'element_id' ) );
        $top_level_id = trim( (string) $request->get_param( 'top_level_id' ) );
        $query = strtolower( trim( (string) $request->get_param( 'q' ) ) );
        $filtered = [];
        foreach ( $items as $item ) {
            if ( 'all' !== $kind && $kind !== (string) $item['kind'] ) continue;
            if ( '' !== $element_id && $element_id !== (string) $item['element_id'] ) continue;
            if ( '' !== $top_level_id && $top_level_id !== (string) $item['top_level_id'] ) continue;
            if ( '' !== $query && ! self::item_matches_query( $item, $query ) ) continue;
            $filtered[] = $item;
        }

        $offset = max( 0, absint( $request->get_param( 'offset' ) ) );
        $limit = min( 500, max( 1, absint( $request->get_param( 'limit' ) ) ) );
        $matched = count( $filtered );
        $returned = array_slice( $filtered, $offset, $limit );
        $next_offset = ( $offset + count( $returned ) ) < $matched ? $offset + count( $returned ) : null;

        $counts = [ 'text' => 0, 'link' => 0, 'media' => 0, 'pixfort_icon' => 0 ];
        foreach ( $items as $item ) {
            $item_kind = (string) ( $item['kind'] ?? '' );
            if ( array_key_exists( $item_kind, $counts ) ) $counts[ $item_kind ]++;
        }

        return new WP_REST_Response( [
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'scope' => 'main_elementor_document_only',
            'shared_or_embedded_template_writes' => false,
            'dynamic_values_writable' => false,
            'global_style_values_writable' => false,
            'supported_kinds' => [ 'text', 'link', 'media', 'pixfort_icon' ],
            'counts' => $counts,
            'total_item_count' => count( $items ),
            'matched_item_count' => $matched,
            'returned_item_count' => count( $returned ),
            'offset' => $offset,
            'limit' => $limit,
            'next_offset' => $next_offset,
            'items' => array_values( $returned ),
        ], 200 );
    }

    public static function update( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_content_write' ) ) return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_content_write=true is required.', [ 'status' => 400 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );

        $expected_status = (string) $request->get_param( 'expected_status' );
        $expected_title = (string) $request->get_param( 'expected_title' );
        if ( $expected_status !== (string) $post->post_status ) return new WP_Error( 'elementize_status_changed', 'The page status changed after the fresh content read.', [ 'status' => 409, 'status_now' => (string) $post->post_status ] );
        if ( $expected_title !== (string) $post->post_title ) return new WP_Error( 'elementize_title_changed', 'The page title changed after the fresh content read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        if ( 'publish' === $post->post_status && true !== $request->get_param( 'confirm_published_page' ) ) return new WP_Error( 'elementize_published_confirmation_required', 'This is a published page. Explicit confirm_published_page=true is required before changing live content.', [ 'status' => 400 ] );

        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $document = $loaded['document'];
        $elements = $loaded['elements'];
        $before = $elements;
        $current_hash = Elementize_Core::hash_elements( $elements );
        $expected_hash = trim( (string) $request->get_param( 'content_hash' ) );
        if ( '' === $expected_hash || ! hash_equals( $current_hash, $expected_hash ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after the fresh content read. Read the content again before applying edits.', [ 'status' => 409, 'content_hash' => $current_hash ] );

        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || [] === $updates ) return new WP_Error( 'elementize_updates_required', 'At least one content update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_UPDATES ) return new WP_Error( 'elementize_too_many_updates', 'Send at most ' . self::MAX_UPDATES . ' content updates per request.', [ 'status' => 400 ] );

        $changes = [];
        foreach ( array_values( $updates ) as $index => $update ) {
            $valid = self::validate_update( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $old = null;
            $new = null;
            $applied = self::apply_update( $elements, $update, $old, $new );
            if ( is_wp_error( $applied ) ) return $applied;
            if ( false === $applied ) return new WP_Error( 'elementize_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409, 'update_index' => $index ] );
            $changes[] = [ 'kind' => (string) $update['kind'], 'element_id' => (string) $update['element_id'], 'setting_path' => array_values( $update['setting_path'] ), 'old_value' => $old, 'new_value' => $new ];
        }

        $revision = Elementize_Core::save_revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        try { $saved = $document->save( [ 'elements' => $elements ] ); }
        catch ( Throwable $e ) {
            self::attempt_restore( $document, $before );
            return new WP_Error( 'elementize_save_failed', 'Elementor could not save the content changes: ' . $e->getMessage(), [ 'status' => 500, 'revision_id' => $revision ] );
        }
        if ( ! $saved ) {
            self::attempt_restore( $document, $before );
            return new WP_Error( 'elementize_save_failed', 'Elementor refused to save the content changes.', [ 'status' => 500, 'revision_id' => $revision ] );
        }

        $fresh_document = Elementize_Core::document( $id );
        $fresh = $fresh_document ? $fresh_document->get_elements_data() : null;
        if ( ! is_array( $fresh ) || ! self::verify_changes( $fresh, $changes ) ) {
            self::attempt_restore( $document, $before );
            Elementize_Core::regenerate_css( $id );
            return new WP_Error( 'elementize_verification_failed', 'The saved content changes could not be verified, so Elementize attempted to restore the exact pre-change Elementor content.', [ 'status' => 500, 'revision_id' => $revision ] );
        }

        Elementize_Core::regenerate_css( $id );
        clean_post_cache( $id );
        $fresh_post = get_post( $id );
        return new WP_REST_Response( [
            'saved' => true,
            'page' => Elementize_Core::page_identity( $id, $fresh_document ?: $document ),
            'updated_count' => count( $changes ),
            'revision_id' => $revision,
            'content_hash' => Elementize_Core::hash_elements( $fresh ),
            'page_status_after' => $fresh_post ? (string) $fresh_post->post_status : (string) $post->post_status,
            'changes' => $changes,
        ], 200 );
    }

    private static function collect_elements( array $elements, array &$items, string $top_level_id = '' ): void {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            $top = '' !== $top_level_id ? $top_level_id : $id;
            $element_type = isset( $element['elType'] ) && is_string( $element['elType'] ) ? $element['elType'] : '';
            $widget_type = isset( $element['widgetType'] ) && is_string( $element['widgetType'] ) ? $element['widgetType'] : '';
            if ( '' !== $id && isset( $element['settings'] ) && is_array( $element['settings'] ) ) self::collect_settings( $element['settings'], [], $id, $top, $element_type, $widget_type, $items );
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) self::collect_elements( $element['elements'], $items, $top );
        }
    }

    private static function collect_settings( $value, array $path, string $id, string $top, string $element_type, string $widget_type, array &$items ): void {
        if ( count( $items ) >= self::MAX_ITEMS ) return;
        if ( is_array( $value ) ) {
            if ( self::safe_media_target( $path, $value ) ) {
                $items[] = self::item( 'media', $id, $top, $element_type, $widget_type, $path, self::media_snapshot( $value ) );
                return;
            }
            foreach ( $value as $key => $child ) {
                self::collect_settings( $child, array_merge( $path, [ is_int( $key ) ? $key : (string) $key ] ), $id, $top, $element_type, $widget_type, $items );
                if ( count( $items ) >= self::MAX_ITEMS ) return;
            }
            return;
        }
        if ( ! is_string( $value ) ) return;
        if ( self::is_dynamic_value( $value ) || self::path_has_key( $path, '__globals__' ) ) return;
        if ( '' !== trim( $value ) && self::safe_pixfort_icon_target( $path, $value ) ) {
            $items[] = self::item( 'pixfort_icon', $id, $top, $element_type, $widget_type, $path, $value );
            return;
        }
        if ( self::link_path( $path, $widget_type ) ) {
            $items[] = self::item( 'link', $id, $top, $element_type, $widget_type, $path, $value );
            return;
        }
        if ( self::copy_field( $path, $value ) ) $items[] = self::item( 'text', $id, $top, $element_type, $widget_type, $path, $value );
    }

    private static function item( string $kind, string $id, string $top, string $element_type, string $widget_type, array $path, $value ): array {
        $item = [ 'kind' => $kind, 'element_id' => $id, 'top_level_id' => $top, 'element_type' => $element_type, 'widget_type' => $widget_type, 'setting_path' => array_values( $path ), 'setting_key' => self::last_key( $path ), 'value' => $value, 'writable' => true ];
        if ( 'text' === $kind && is_string( $value ) ) $item['format'] = wp_strip_all_tags( $value ) === $value ? 'text' : 'html';
        if ( 'media' === $kind && is_array( $value ) ) $item['attachment_id'] = absint( $value['id'] ?? 0 );
        return $item;
    }

    private static function item_matches_query( array $item, string $query ): bool {
        $parts = [ (string) ( $item['kind'] ?? '' ), (string) ( $item['element_id'] ?? '' ), (string) ( $item['top_level_id'] ?? '' ), (string) ( $item['widget_type'] ?? '' ), (string) ( $item['setting_key'] ?? '' ) ];
        $value = $item['value'] ?? '';
        if ( is_scalar( $value ) ) $parts[] = wp_strip_all_tags( (string) $value );
        if ( is_array( $value ) ) $parts[] = (string) wp_json_encode( $value );
        return false !== strpos( strtolower( implode( ' ', $parts ) ), $query );
    }

    private static function validate_update( $update, int $index ) {
        if ( ! is_array( $update ) ) return self::invalid_update( $index, 'Each update must be an object.' );
        $kind = isset( $update['kind'] ) && is_string( $update['kind'] ) ? strtolower( trim( $update['kind'] ) ) : '';
        if ( ! in_array( $kind, [ 'text', 'link', 'media', 'pixfort_icon' ], true ) ) return self::invalid_update( $index, 'kind must be text, link, media, or pixfort_icon.' );
        if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) return self::invalid_update( $index, 'element_id is required.' );
        if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) ) return self::invalid_update( $index, 'setting_path must be a non-empty array.' );
        if ( count( $update['setting_path'] ) > 30 ) return self::invalid_update( $index, 'setting_path is too deep.' );
        foreach ( $update['setting_path'] as $segment ) if ( ! is_string( $segment ) && ! is_int( $segment ) ) return self::invalid_update( $index, 'setting_path segments must be strings or integers.' );
        if ( self::path_has_key( $update['setting_path'], '__globals__' ) ) return self::invalid_update( $index, 'Global style values are not writable.' );
        if ( 'media' === $kind ) {
            if ( ! array_key_exists( 'expected_attachment_id', $update ) || ! is_numeric( $update['expected_attachment_id'] ) ) return self::invalid_update( $index, 'expected_attachment_id is required for media.' );
            if ( ! array_key_exists( 'attachment_id', $update ) || ! is_numeric( $update['attachment_id'] ) ) return self::invalid_update( $index, 'attachment_id is required for media.' );
            return true;
        }
        if ( ! array_key_exists( 'expected_value', $update ) || ! is_string( $update['expected_value'] ) ) return self::invalid_update( $index, 'expected_value is required.' );
        if ( ! array_key_exists( 'value', $update ) || ! is_string( $update['value'] ) ) return self::invalid_update( $index, 'value must be a string.' );
        if ( 'text' === $kind && strlen( $update['value'] ) > self::MAX_TEXT ) return self::invalid_update( $index, 'text value is too long.' );
        return true;
    }

    private static function invalid_update( int $index, string $message ): WP_Error {
        return new WP_Error( 'elementize_invalid_update', 'Invalid content update at index ' . $index . ': ' . $message, [ 'status' => 400 ] );
    }

    private static function apply_update( array &$elements, array $update, &$old, &$new ) {
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === (string) $update['element_id'] ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) return new WP_Error( 'elementize_setting_not_found', 'The target Elementor element has no editable settings.', [ 'status' => 409 ] );
                $cursor =& $element['settings'];
                foreach ( $update['setting_path'] as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_setting_not_found', 'The requested content setting no longer exists.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }
                $kind = strtolower( (string) $update['kind'] );
                if ( 'text' === $kind ) return self::apply_text( $cursor, $update, $old, $new );
                if ( 'link' === $kind ) return self::apply_link( $cursor, $update, (string) ( $element['widgetType'] ?? '' ), $old, $new );
                if ( 'pixfort_icon' === $kind ) return self::apply_icon( $cursor, $update, $old, $new );
                return self::apply_media( $cursor, $update, $old, $new );
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $result = self::apply_update( $element['elements'], $update, $old, $new );
                if ( true === $result || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function apply_text( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::copy_field( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_not_text_field', 'Elementize only permits text writes to recognized copy fields.', [ 'status' => 400 ] );
        if ( $cursor !== (string) $update['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target text changed after the fresh content read.', [ 'status' => 409 ] );
        $candidate = wp_kses_post( (string) $update['value'] );
        $old = $cursor; $cursor = $candidate; $new = $candidate; return true;
    }

    private static function apply_link( &$cursor, array $update, string $widget_type, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::link_path( $update['setting_path'], $widget_type ) || self::is_dynamic_value( $cursor ) ) return new WP_Error( 'elementize_not_link_field', 'Elementize only permits link writes to recognized non-dynamic destination fields.', [ 'status' => 400 ] );
        if ( $cursor !== (string) $update['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target link changed after the fresh content read.', [ 'status' => 409 ] );
        $candidate = self::sanitize_destination( (string) $update['value'] );
        if ( is_wp_error( $candidate ) ) return $candidate;
        $old = $cursor; $cursor = $candidate; $new = $candidate; return true;
    }

    private static function apply_icon( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::safe_pixfort_icon_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_not_pixfort_icon_field', 'Elementize only permits icon writes to recognized Pixfort icon fields.', [ 'status' => 400 ] );
        if ( $cursor !== (string) $update['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target icon changed after the fresh content read.', [ 'status' => 409 ] );
        $candidate = trim( (string) $update['value'] );
        if ( ! self::safe_pixfort_icon( $candidate ) ) return new WP_Error( 'elementize_invalid_pixfort_icon', 'The new icon must be a Pixfort Solid/... or Line/... icon value.', [ 'status' => 400 ] );
        $old = $cursor; $cursor = $candidate; $new = $candidate; return true;
    }

    private static function apply_media( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_array( $cursor ) || ! self::safe_media_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_not_media_field', 'Elementize only permits image writes to recognized Elementor/Pixfort media fields.', [ 'status' => 400 ] );
        $current_id = absint( $cursor['id'] ?? 0 );
        if ( $current_id !== absint( $update['expected_attachment_id'] ) ) return new WP_Error( 'elementize_value_changed', 'The target image attachment changed after the fresh content read.', [ 'status' => 409 ] );
        $attachment_id = absint( $update['attachment_id'] );
        if ( $attachment_id < 1 || 'attachment' !== get_post_type( $attachment_id ) || ! wp_attachment_is_image( $attachment_id ) ) return new WP_Error( 'elementize_invalid_media', 'attachment_id must reference an existing WordPress image attachment.', [ 'status' => 400 ] );
        if ( ! current_user_can( 'edit_post', $attachment_id ) ) return new WP_Error( 'elementize_media_forbidden', 'The authenticated WordPress user cannot use this Media Library image.', [ 'status' => 403 ] );
        $url = wp_get_attachment_url( $attachment_id );
        if ( ! is_string( $url ) || '' === $url ) return new WP_Error( 'elementize_invalid_media', 'WordPress could not resolve the selected image URL.', [ 'status' => 400 ] );
        $old = $cursor;
        $next = $cursor; $next['id'] = $attachment_id; $next['url'] = esc_url_raw( $url ); unset( $next['source'] );
        $cursor = $next; $new = $next; return true;
    }

    private static function verify_changes( array $elements, array $changes ): bool {
        foreach ( $changes as $change ) {
            $found = false;
            $value = self::read_setting( $elements, (string) $change['element_id'], $change['setting_path'], $found );
            if ( ! $found ) return false;
            if ( 'media' === (string) $change['kind'] ) {
                if ( ! is_array( $value ) || ! is_array( $change['new_value'] ) ) return false;
                if ( absint( $value['id'] ?? 0 ) !== absint( $change['new_value']['id'] ?? 0 ) ) return false;
                if ( (string) ( $value['url'] ?? '' ) !== (string) ( $change['new_value']['url'] ?? '' ) ) return false;
                continue;
            }
            if ( $value !== $change['new_value'] ) return false;
        }
        return true;
    }

    private static function read_setting( array $elements, string $element_id, array $path, bool &$found ) {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                $cursor = $element['settings'] ?? null;
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) { $found = false; return null; }
                    $cursor = $cursor[ $segment ];
                }
                $found = true; return $cursor;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $value = self::read_setting( $element['elements'], $element_id, $path, $found );
                if ( $found ) return $value;
            }
        }
        $found = false; return null;
    }

    private static function attempt_restore( $document, array $before ): void {
        try { $document->save( [ 'elements' => $before ] ); } catch ( Throwable $ignored ) {}
    }

    private static function media_snapshot( array $value ): array {
        $out = [ 'id' => absint( $value['id'] ?? 0 ), 'url' => isset( $value['url'] ) && is_string( $value['url'] ) ? esc_url_raw( $value['url'] ) : '' ];
        foreach ( [ 'alt', 'title' ] as $field ) if ( isset( $value[ $field ] ) && is_scalar( $value[ $field ] ) ) $out[ $field ] = (string) $value[ $field ];
        return $out;
    }

    private static function safe_media_target( array $path, array $value ): bool {
        if ( [] === $path || self::path_has_key( $path, '__globals__' ) ) return false;
        $key = self::last_key( $path );
        if ( ! in_array( $key, [ 'image', 'text_image', 'media', 'logo', 'thumbnail', 'avatar', 'photo', 'poster', 'background_image' ], true ) ) return false;
        if ( ! array_key_exists( 'id', $value ) && ! array_key_exists( 'url', $value ) ) return false;
        $url = isset( $value['url'] ) && is_string( $value['url'] ) ? trim( $value['url'] ) : '';
        $id = absint( $value['id'] ?? 0 );
        return $id > 0 || '' !== $url;
    }

    private static function safe_pixfort_icon_target( array $path, string $value ): bool {
        if ( self::path_has_key( $path, '__globals__' ) ) return false;
        $key = self::last_key( $path );
        if ( ! in_array( $key, [ 'icon', 'btn_icon' ], true ) ) return false;
        return self::safe_pixfort_icon( $value );
    }

    private static function safe_pixfort_icon( string $value ): bool {
        return (bool) preg_match( '#^(Solid|Line)/pixfort-icon-[A-Za-z0-9_-]+$#', trim( $value ) );
    }

    private static function copy_field( array $path, string $value ): bool {
        if ( '' === trim( $value ) || strlen( $value ) > self::MAX_TEXT || self::is_dynamic_value( $value ) ) return false;
        $key = self::last_key( $path );
        if ( '' === $key ) return false;
        if ( preg_match( '/(^|_)(url|link|href|src|id|class|selector|shortcode|icon|image|media|color|background|gradient|font|typography|size|width|height|margin|padding|border|radius|opacity|align|justify|position|offset|z_index|animation|transition|transform|query|taxonomy|post_id|flex|grid|layout|display|direction|gap|wrap|grow|shrink|basis|order|overflow)(_|$)/i', $key ) ) return false;
        return (bool) preg_match( '/(^|_)(title|text|content|editor|description|heading|headline|subtitle|sub_title|label|caption|button_text|btn_text|placeholder|prefix|suffix|message)(_|$)/i', $key );
    }

    private static function link_path( array $path, string $widget_type ): bool {
        $strings = [];
        foreach ( $path as $segment ) if ( is_string( $segment ) ) $strings[] = strtolower( $segment );
        if ( [] === $strings ) return false;
        $joined = implode( '_', $strings );
        if ( preg_match( '/(^|_)(image|media|logo|photo|thumbnail|poster|background|video|icon)(_|$)/', $joined ) ) return false;
        if ( preg_match( '/(^|_)(color|hover|typography|font|size|width|height|margin|padding|border|radius|style)(_|$)/', $joined ) ) return false;
        $last = (string) end( $strings );
        if ( in_array( $last, [ 'href', 'link', 'button_link', 'btn_link', 'cta_link' ], true ) ) return true;
        if ( str_ends_with( $last, '_link' ) || str_ends_with( $last, '_url' ) ) return true;
        if ( 'url' === $last ) {
            if ( preg_match( '/(link|button|btn|cta|href)/', $joined ) ) return true;
            if ( preg_match( '/button/i', $widget_type ) ) return true;
        }
        return false;
    }

    private static function sanitize_destination( string $raw ) {
        $value = trim( $raw );
        if ( '' === $value ) return '';
        if ( self::is_dynamic_value( $value ) ) return new WP_Error( 'elementize_dynamic_link_read_only', 'Dynamic link values are read-only.', [ 'status' => 400 ] );
        if ( '#' === $value ) return '#';
        if ( str_starts_with( $value, '#' ) ) return preg_match( '/^#[A-Za-z][A-Za-z0-9_:\\-.]*$/', $value ) ? $value : new WP_Error( 'elementize_invalid_link', 'Fragment links must use a simple valid anchor.', [ 'status' => 400 ] );
        if ( str_starts_with( $value, '/' ) && ! str_starts_with( $value, '//' ) ) return preg_match( '/^\\/[A-Za-z0-9_~!$&\'()*+,;=:@%\\/?.#-]*$/', $value ) ? $value : new WP_Error( 'elementize_invalid_link', 'Relative links contain unsupported characters.', [ 'status' => 400 ] );
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

    private static function is_dynamic_value( string $value ): bool {
        return str_starts_with( ltrim( $value ), '[elementor-tag' );
    }

    private static function last_key( array $path ): string {
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) if ( is_string( $path[ $i ] ) ) return strtolower( $path[ $i ] );
        return '';
    }

    private static function path_has_key( array $path, string $needle ): bool {
        $needle = strtolower( $needle );
        foreach ( $path as $segment ) if ( is_string( $segment ) && strtolower( $segment ) === $needle ) return true;
        return false;
    }
}

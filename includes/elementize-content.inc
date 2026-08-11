<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Content {
    private const MAX_UPDATES = 50;
    private const MAX_TEXT = 20000;
    private const MAX_ITEMS = 5000;
    private const MAX_ELEMENT_ID = 100;
    private const MAX_PATH_DEPTH = 30;
    private const MAX_PATH_SEGMENT = 200;
    private const MAX_LINK = 2048;
    private const MAX_ICON = 255;
    private const MAX_ELEMENT_DEPTH = 80;
    private const MAX_SETTINGS_VISITS = 20000;
    private const KINDS = [ 'text', 'link', 'media', 'pixfort_icon' ];

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
                    'kind' => [ 'type' => 'string', 'default' => 'all', 'sanitize_callback' => 'sanitize_key' ],
                    'element_id' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                    'top_level_id' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                    'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                    'offset' => [ 'type' => 'integer', 'default' => 0, 'minimum' => 0, 'sanitize_callback' => 'absint' ],
                    'limit' => [ 'type' => 'integer', 'default' => 100, 'minimum' => 1, 'maximum' => 500, 'sanitize_callback' => 'absint' ],
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
                    'confirm_published_page' => [ 'type' => 'boolean', 'default' => false ],
                ],
            ],
        ] );
    }

    public static function discover_items( array $elements ): array {
        $items = [];
        $state = [ 'settings_visits' => 0 ];
        self::collect_elements( $elements, $items, '', 0, $state );
        return $items;
    }

    public static function is_writable_text( array $path, string $value ): bool {
        return self::copy_field( $path, $value );
    }

    public static function read( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $kind = strtolower( trim( (string) $request->get_param( 'kind' ) ) ) ?: 'all';
        if ( 'all' !== $kind && ! in_array( $kind, self::KINDS, true ) ) return new WP_Error( 'elementize_invalid_content_kind', 'kind must be all, text, link, media, or pixfort_icon.', [ 'status' => 400 ] );
        $items = self::discover_items( $loaded['elements'] );
        $filters = [
            'kind' => $kind,
            'element_id' => trim( (string) $request->get_param( 'element_id' ) ),
            'top_level_id' => trim( (string) $request->get_param( 'top_level_id' ) ),
            'q' => self::lower( trim( (string) $request->get_param( 'q' ) ) ),
        ];
        $filtered = array_values( array_filter( $items, static fn( array $item ): bool => self::matches( $item, $filters ) ) );
        $offset = max( 0, absint( $request->get_param( 'offset' ) ) );
        $limit = min( 500, max( 1, absint( $request->get_param( 'limit' ) ) ) );
        $returned = array_slice( $filtered, $offset, $limit );
        $counts = array_fill_keys( self::KINDS, 0 );
        foreach ( $items as $item ) $counts[ $item['kind'] ]++;
        return new WP_REST_Response( [
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'scope' => 'main_elementor_document_only',
            'shared_or_embedded_template_writes' => false,
            'dynamic_values_writable' => false,
            'global_style_values_writable' => false,
            'generic_safe_text_detection' => true,
            'supported_kinds' => self::KINDS,
            'counts' => $counts,
            'total_item_count' => count( $items ),
            'matched_item_count' => count( $filtered ),
            'returned_item_count' => count( $returned ),
            'offset' => $offset,
            'limit' => $limit,
            'next_offset' => ( $offset + count( $returned ) ) < count( $filtered ) ? $offset + count( $returned ) : null,
            'items' => $returned,
        ], 200 );
    }

    public static function update( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_content_write' ) ) return new WP_Error( 'elementize_confirmation_required', 'Explicit confirm_content_write=true is required.', [ 'status' => 400 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( (string) $request->get_param( 'expected_status' ) !== (string) $post->post_status ) return new WP_Error( 'elementize_status_changed', 'The page status changed after the fresh content read.', [ 'status' => 409, 'status_now' => (string) $post->post_status ] );
        if ( (string) $request->get_param( 'expected_title' ) !== (string) $post->post_title ) return new WP_Error( 'elementize_title_changed', 'The page title changed after the fresh content read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        if ( 'publish' === $post->post_status && true !== $request->get_param( 'confirm_published_page' ) ) return new WP_Error( 'elementize_published_confirmation_required', 'This is a published page. Explicit confirm_published_page=true is required before changing live content.', [ 'status' => 400 ] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $document = $loaded['document'];
        $before = $loaded['elements'];
        $before_hash = Elementize_Core::hash_elements( $before );
        $expected_hash = trim( (string) $request->get_param( 'content_hash' ) );
        if ( '' === $expected_hash || ! hash_equals( $before_hash, $expected_hash ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after the fresh content read. Read the content again before applying edits.', [ 'status' => 409, 'content_hash' => $before_hash ] );
        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || ! $updates ) return new WP_Error( 'elementize_updates_required', 'At least one content update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_UPDATES ) return new WP_Error( 'elementize_too_many_updates', 'Send at most ' . self::MAX_UPDATES . ' content updates per request.', [ 'status' => 400 ] );

        $elements = $before;
        $changes = [];
        $seen = [];
        foreach ( array_values( $updates ) as $index => $update ) {
            $valid = self::validate_update( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $target_key = hash( 'sha256', (string) $update['element_id'] . '|' . wp_json_encode( array_values( $update['setting_path'] ) ) );
            if ( isset( $seen[ $target_key ] ) ) return self::invalid_update( $index, 'The same Elementor setting may only be updated once per request.' );
            $seen[ $target_key ] = true;
            $old = null;
            $new = null;
            $applied = self::apply_update( $elements, $update, $old, $new );
            if ( is_wp_error( $applied ) ) return $applied;
            if ( false === $applied ) return new WP_Error( 'elementize_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409, 'update_index' => $index ] );
            if ( $old === $new ) return self::invalid_update( $index, 'The requested update does not change the current value.' );
            $changes[] = [ 'kind' => (string) $update['kind'], 'element_id' => (string) $update['element_id'], 'setting_path' => array_values( $update['setting_path'] ), 'old_value' => $old, 'new_value' => $new ];
        }

        $revision = Elementize_Core::save_revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        try { $saved = $document->save( [ 'elements' => $elements ] ); }
        catch ( Throwable $e ) {
            $rollback = self::rollback_cleanup( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_save_failed', 'Elementor could not save the content changes: ' . $e->getMessage(), [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }
        if ( ! $saved ) {
            $rollback = self::rollback_cleanup( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_save_failed', 'Elementor refused to save the content changes.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }
        $persisted = Elementize_Core::persisted_elements( $id );
        if ( is_wp_error( $persisted ) || ! self::verify_changes( $persisted, $changes ) ) {
            $rollback = self::rollback_cleanup( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_verification_failed', 'Persisted Elementor data did not verify every requested change, so Elementize attempted to restore the exact pre-change content.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }
        Elementize_Core::regenerate_css( $id );
        clean_post_cache( $id );
        return new WP_REST_Response( [ 'saved' => true, 'page' => Elementize_Core::page_identity( $id ), 'updated_count' => count( $changes ), 'revision_id' => $revision, 'content_hash' => Elementize_Core::hash_elements( $persisted ), 'persisted_verification' => true, 'changes' => $changes ], 200 );
    }

    private static function collect_elements( array $elements, array &$items, string $top, int $depth, array &$state ): void {
        if ( $depth > self::MAX_ELEMENT_DEPTH || count( $items ) >= self::MAX_ITEMS ) return;
        foreach ( $elements as $element ) {
            if ( count( $items ) >= self::MAX_ITEMS ) return;
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            $current_top = '' !== $top ? $top : $id;
            $type = is_string( $element['elType'] ?? null ) ? $element['elType'] : '';
            $widget = is_string( $element['widgetType'] ?? null ) ? $element['widgetType'] : '';
            if ( '' !== $id && is_array( $element['settings'] ?? null ) ) self::collect_settings( $element['settings'], [], $id, $current_top, $type, $widget, $items, $state );
            if ( is_array( $element['elements'] ?? null ) ) self::collect_elements( $element['elements'], $items, $current_top, $depth + 1, $state );
        }
    }

    private static function collect_settings( $value, array $path, string $id, string $top, string $type, string $widget, array &$items, array &$state ): void {
        if ( count( $items ) >= self::MAX_ITEMS || count( $path ) > self::MAX_PATH_DEPTH || (int) $state['settings_visits'] >= self::MAX_SETTINGS_VISITS ) return;
        $state['settings_visits'] = (int) $state['settings_visits'] + 1;
        if ( is_array( $value ) ) {
            if ( self::media_target( $path, $value ) ) { $items[] = self::item( 'media', $id, $top, $type, $widget, $path, self::media_snapshot( $value ) ); return; }
            foreach ( $value as $key => $child ) {
                self::collect_settings( $child, array_merge( $path, [ is_int( $key ) ? $key : (string) $key ] ), $id, $top, $type, $widget, $items, $state );
                if ( count( $items ) >= self::MAX_ITEMS || (int) $state['settings_visits'] >= self::MAX_SETTINGS_VISITS ) return;
            }
            return;
        }
        if ( ! is_string( $value ) || self::dynamic( $value ) || self::path_has( $path, '__globals__' ) ) return;
        if ( '' !== trim( $value ) && self::icon_target( $path, $value ) ) $kind = 'pixfort_icon';
        elseif ( self::link_path( $path, $widget ) ) $kind = 'link';
        elseif ( self::copy_field( $path, $value ) ) $kind = 'text';
        else return;
        $items[] = self::item( $kind, $id, $top, $type, $widget, $path, $value );
    }

    private static function item( string $kind, string $id, string $top, string $type, string $widget, array $path, $value ): array {
        $item = [ 'kind' => $kind, 'element_id' => $id, 'top_level_id' => $top, 'element_type' => $type, 'widget_type' => $widget, 'setting_path' => array_values( $path ), 'setting_key' => self::last_key( $path ), 'value' => $value, 'writable' => true ];
        if ( 'text' === $kind ) {
            $item['format'] = wp_strip_all_tags( (string) $value ) === $value ? 'text' : 'html';
            $item['text_detection'] = self::known_copy_key( self::last_key( $path ) ) ? 'known_copy_field' : 'generic_safe_string';
        }
        if ( 'media' === $kind ) $item['attachment_id'] = absint( $value['id'] ?? 0 );
        return $item;
    }

    private static function matches( array $item, array $filters ): bool {
        if ( 'all' !== $filters['kind'] && $filters['kind'] !== $item['kind'] ) return false;
        if ( '' !== $filters['element_id'] && $filters['element_id'] !== $item['element_id'] ) return false;
        if ( '' !== $filters['top_level_id'] && $filters['top_level_id'] !== $item['top_level_id'] ) return false;
        if ( '' === $filters['q'] ) return true;
        $value = is_scalar( $item['value'] ) ? (string) $item['value'] : (string) wp_json_encode( $item['value'] );
        $haystack = self::lower( implode( ' ', [ $item['kind'], $item['element_id'], $item['top_level_id'], $item['widget_type'], $item['setting_key'], wp_strip_all_tags( $value ) ] ) );
        return false !== strpos( $haystack, $filters['q'] );
    }

    private static function validate_update( $update, int $index ) {
        if ( ! is_array( $update ) ) return self::invalid_update( $index, 'Each update must be an object.' );
        $kind = is_string( $update['kind'] ?? null ) ? strtolower( trim( $update['kind'] ) ) : '';
        if ( ! in_array( $kind, self::KINDS, true ) ) return self::invalid_update( $index, 'kind must be text, link, media, or pixfort_icon.' );
        if ( ! is_string( $update['element_id'] ?? null ) || '' === $update['element_id'] || strlen( $update['element_id'] ) > self::MAX_ELEMENT_ID ) return self::invalid_update( $index, 'element_id is missing or too long.' );
        if ( ! is_array( $update['setting_path'] ?? null ) || ! $update['setting_path'] || count( $update['setting_path'] ) > self::MAX_PATH_DEPTH ) return self::invalid_update( $index, 'setting_path must contain 1-' . self::MAX_PATH_DEPTH . ' segments.' );
        foreach ( $update['setting_path'] as $segment ) {
            if ( ! is_string( $segment ) && ! is_int( $segment ) ) return self::invalid_update( $index, 'setting_path segments must be strings or integers.' );
            if ( is_string( $segment ) && strlen( $segment ) > self::MAX_PATH_SEGMENT ) return self::invalid_update( $index, 'A setting_path segment is too long.' );
        }
        if ( self::path_has( $update['setting_path'], '__globals__' ) ) return self::invalid_update( $index, 'Global style values are not writable.' );
        if ( 'media' === $kind ) {
            if ( ! self::integerish( $update['expected_attachment_id'] ?? null ) || ! self::integerish( $update['attachment_id'] ?? null ) ) return self::invalid_update( $index, 'Media writes require integer expected_attachment_id and attachment_id values.' );
            return true;
        }
        if ( ! is_string( $update['expected_value'] ?? null ) || ! is_string( $update['value'] ?? null ) ) return self::invalid_update( $index, 'expected_value and value are required strings.' );
        $length = strlen( $update['value'] );
        if ( 'text' === $kind && $length > self::MAX_TEXT ) return self::invalid_update( $index, 'text value is too long.' );
        if ( 'link' === $kind && $length > self::MAX_LINK ) return self::invalid_update( $index, 'link value is too long.' );
        if ( 'pixfort_icon' === $kind && $length > self::MAX_ICON ) return self::invalid_update( $index, 'icon value is too long.' );
        return true;
    }

    private static function invalid_update( int $index, string $message ): WP_Error {
        return new WP_Error( 'elementize_invalid_update', 'Invalid content update at index ' . $index . ': ' . $message, [ 'status' => 400 ] );
    }

    private static function apply_update( array &$elements, array $update, &$old, &$new, int $depth = 0 ) {
        if ( $depth > self::MAX_ELEMENT_DEPTH ) return false;
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( (string) ( $element['id'] ?? '' ) === (string) $update['element_id'] ) {
                if ( ! is_array( $element['settings'] ?? null ) ) return new WP_Error( 'elementize_setting_not_found', 'The target Elementor element has no editable settings.', [ 'status' => 409 ] );
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
            if ( is_array( $element['elements'] ?? null ) ) {
                $result = self::apply_update( $element['elements'], $update, $old, $new, $depth + 1 );
                if ( true === $result || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function apply_text( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::copy_field( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_not_text_field', 'Elementize only permits text writes to safe human-readable content fields.', [ 'status' => 400 ] );
        if ( $cursor !== $update['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target text changed after the fresh content read.', [ 'status' => 409 ] );
        $old = $cursor;
        $cursor = $new = wp_kses_post( $update['value'] );
        return true;
    }

    private static function apply_link( &$cursor, array $update, string $widget, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::link_path( $update['setting_path'], $widget ) || self::dynamic( $cursor ) ) return new WP_Error( 'elementize_not_link_field', 'Elementize only permits link writes to recognized non-dynamic destination fields.', [ 'status' => 400 ] );
        if ( $cursor !== $update['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target link changed after the fresh content read.', [ 'status' => 409 ] );
        $candidate = self::sanitize_destination( $update['value'] );
        if ( is_wp_error( $candidate ) ) return $candidate;
        $old = $cursor;
        $cursor = $new = $candidate;
        return true;
    }

    private static function apply_icon( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_string( $cursor ) || ! self::icon_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_not_pixfort_icon_field', 'Elementize only permits icon writes to recognized Pixfort icon fields.', [ 'status' => 400 ] );
        if ( $cursor !== $update['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target icon changed after the fresh content read.', [ 'status' => 409 ] );
        $candidate = trim( $update['value'] );
        if ( ! self::valid_icon_format( $candidate ) ) return new WP_Error( 'elementize_invalid_pixfort_icon', 'The new icon must use Pixfort Line/..., Duotone/..., or Solid/... format.', [ 'status' => 400 ] );
        $known = Elementize_Pixfort_Icons::validate_value( $candidate );
        if ( is_wp_error( $known ) ) return $known;
        if ( true !== $known ) return new WP_Error( 'elementize_unknown_pixfort_icon', 'The requested icon was not found in the installed Pixfort icon library. Search the Pixfort icon library again and use an exact returned value.', [ 'status' => 400 ] );
        $old = $cursor;
        $cursor = $new = $candidate;
        return true;
    }

    private static function apply_media( &$cursor, array $update, &$old, &$new ) {
        if ( ! is_array( $cursor ) || ! self::media_target( $update['setting_path'], $cursor ) ) return new WP_Error( 'elementize_not_media_field', 'Elementize only permits image writes to recognized Elementor/Pixfort media fields.', [ 'status' => 400 ] );
        if ( absint( $cursor['id'] ?? 0 ) !== absint( $update['expected_attachment_id'] ) ) return new WP_Error( 'elementize_value_changed', 'The target image attachment changed after the fresh content read.', [ 'status' => 409 ] );
        $attachment_id = absint( $update['attachment_id'] );
        if ( $attachment_id < 1 || 'attachment' !== get_post_type( $attachment_id ) || ! wp_attachment_is_image( $attachment_id ) ) return new WP_Error( 'elementize_invalid_media', 'attachment_id must reference an existing WordPress image attachment.', [ 'status' => 400 ] );
        if ( ! current_user_can( 'edit_post', $attachment_id ) ) return new WP_Error( 'elementize_media_forbidden', 'The authenticated WordPress user cannot use this Media Library image.', [ 'status' => 403 ] );
        $url = wp_get_attachment_url( $attachment_id );
        if ( ! is_string( $url ) || '' === $url ) return new WP_Error( 'elementize_invalid_media', 'WordPress could not resolve the selected image URL.', [ 'status' => 400 ] );
        $old = $cursor;
        $new = $cursor;
        $new['id'] = $attachment_id;
        $new['url'] = esc_url_raw( $url );
        unset( $new['source'] );
        $cursor = $new;
        return true;
    }

    private static function verify_changes( array $elements, array $changes ): bool {
        foreach ( $changes as $change ) {
            $found = false;
            $value = self::read_setting( $elements, $change['element_id'], $change['setting_path'], $found );
            if ( ! $found ) return false;
            if ( 'media' === $change['kind'] ) {
                if ( ! is_array( $value ) || absint( $value['id'] ?? 0 ) !== absint( $change['new_value']['id'] ?? 0 ) || (string) ( $value['url'] ?? '' ) !== (string) ( $change['new_value']['url'] ?? '' ) ) return false;
            } elseif ( $value !== $change['new_value'] ) return false;
        }
        return true;
    }

    private static function read_setting( array $elements, string $id, array $path, bool &$found, int $depth = 0 ) {
        if ( $depth > self::MAX_ELEMENT_DEPTH ) { $found = false; return null; }
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( (string) ( $element['id'] ?? '' ) === $id ) {
                $cursor = $element['settings'] ?? null;
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) { $found = false; return null; }
                    $cursor = $cursor[ $segment ];
                }
                $found = true;
                return $cursor;
            }
            if ( is_array( $element['elements'] ?? null ) ) {
                $value = self::read_setting( $element['elements'], $id, $path, $found, $depth + 1 );
                if ( $found ) return $value;
            }
        }
        $found = false;
        return null;
    }

    private static function restore( $document, int $id, array $before, string $before_hash ): bool {
        try { $saved = $document->save( [ 'elements' => $before ] ); } catch ( Throwable $e ) { return false; }
        if ( ! $saved ) return false;
        $persisted = Elementize_Core::persisted_elements( $id );
        return is_array( $persisted ) && hash_equals( $before_hash, Elementize_Core::hash_elements( $persisted ) );
    }

    private static function rollback_cleanup( $document, int $id, array $before, string $before_hash ): bool {
        $restored = self::restore( $document, $id, $before, $before_hash );
        Elementize_Core::regenerate_css( $id );
        clean_post_cache( $id );
        return $restored;
    }

    private static function media_snapshot( array $value ): array {
        $out = [ 'id' => absint( $value['id'] ?? 0 ), 'url' => is_string( $value['url'] ?? null ) ? esc_url_raw( $value['url'] ) : '' ];
        foreach ( [ 'alt', 'title' ] as $field ) if ( is_scalar( $value[ $field ] ?? null ) ) $out[ $field ] = (string) $value[ $field ];
        return $out;
    }

    private static function media_target( array $path, array $value ): bool {
        if ( ! $path || self::path_has( $path, '__globals__' ) ) return false;
        if ( ! in_array( self::last_key( $path ), [ 'image', 'text_image', 'media', 'logo', 'thumbnail', 'avatar', 'photo', 'poster', 'background_image' ], true ) ) return false;
        return absint( $value['id'] ?? 0 ) > 0 || ( is_string( $value['url'] ?? null ) && '' !== trim( $value['url'] ) );
    }

    private static function icon_target( array $path, string $value ): bool {
        return ! self::path_has( $path, '__globals__' ) && in_array( self::last_key( $path ), [ 'icon', 'btn_icon' ], true ) && self::valid_icon_format( $value );
    }

    private static function valid_icon_format( string $value ): bool {
        return (bool) preg_match( '#^(Line|Duotone|Solid)/pixfort-icon-[A-Za-z0-9_-]+$#', trim( $value ) );
    }

    private static function copy_field( array $path, string $value ): bool {
        if ( '' === trim( $value ) || strlen( $value ) > self::MAX_TEXT || self::dynamic( $value ) || self::path_has( $path, '__globals__' ) ) return false;
        $key = self::last_key( $path );
        if ( '' === $key ) return false;

        if ( self::known_copy_key( $key ) ) {
            if ( self::known_technical_copy_collision( $path, $key ) || self::technical_value( $value ) ) return false;
            return true;
        }
        if ( self::technical_path( $path ) ) return false;
        return self::looks_like_human_copy( $value, $path, $key );
    }

    private static function known_copy_key( string $key ): bool {
        return (bool) preg_match( '/(^|_)(title|text|content|editor|description|heading|headline|subtitle|sub_title|label|caption|button_text|btn_text|button_label|cta_text|placeholder|prefix|suffix|message|quote|author|client|person|eyebrow|kicker|overline|tagline|question|answer|testimonial|review|feature|service|intro|body|paragraph|copy|notice|note|html)(_|$)/i', $key );
    }

    private static function known_technical_copy_collision( array $path, string $key ): bool {
        if ( preg_match( '/(^|_)(css|custom_css|selector|class|classes|shortcode|template|template_id|html_tag|aria|attribute|attributes|url|link|href|src|id|element_id|html_id|icon|image|media|logo|photo|thumbnail|poster|video|color|background|gradient|font|typography|size|width|height|margin|padding|border|radius|opacity|align|justify|position|offset|z_index|animation|transition|transform|query|taxonomy|post_id|flex|grid|layout|display|direction|gap|wrap|grow|shrink|basis|order|overflow|breakpoint|responsive|visibility|motion|mask|filter|blend|shadow|menu_id|form_id|role|rel|target)(_|$)/i', $key ) ) return true;
        $parents = array_slice( $path, 0, -1 );
        if ( self::technical_path( $parents ) ) return true;
        return false;
    }

    private static function technical_path( array $path ): bool {
        foreach ( $path as $segment ) {
            if ( ! is_string( $segment ) ) continue;
            $segment = strtolower( $segment );
            if ( preg_match( '/(^|_)(url|link|href|src|id|element_id|html_id|class|classes|css|selector|shortcode|icon|image|media|logo|photo|thumbnail|poster|video|color|background|gradient|font|typography|size|width|height|margin|padding|border|radius|opacity|align|justify|position|offset|z_index|animation|transition|transform|query|taxonomy|post_id|flex|grid|layout|display|direction|gap|wrap|grow|shrink|basis|order|overflow|breakpoint|responsive|visibility|motion|mask|filter|blend|shadow|template|template_id|menu_id|form_id|role|rel|target|html_tag)(_|$)/i', $segment ) ) return true;
        }
        return false;
    }

    private static function technical_value( string $value ): bool {
        $plain = trim( html_entity_decode( wp_strip_all_tags( $value ), ENT_QUOTES | ENT_HTML5, 'UTF-8' ) );
        if ( '' === $plain || is_serialized( $plain ) ) return true;
        $decoded = json_decode( $plain, true );
        if ( ( is_array( $decoded ) || is_object( $decoded ) ) && JSON_ERROR_NONE === json_last_error() ) return true;
        if ( preg_match( '#^(https?://|mailto:|tel:)#i', $plain ) ) return true;
        if ( preg_match( '#^(Line|Duotone|Solid)/pixfort-icon-#', $plain ) ) return true;
        if ( preg_match( '/^#[0-9a-f]{3,8}$/i', $plain ) || preg_match( '/^(rgb|rgba|hsl|hsla)\\(/i', $plain ) ) return true;
        if ( preg_match( '/^-?\\d+(?:\\.\\d+)?(?:px|em|rem|%|vh|vw|deg|s|ms)?$/i', $plain ) ) return true;
        $enum = strtolower( $plain );
        return in_array( $enum, [ 'default','none','inherit','initial','auto','left','right','center','top','bottom','middle','solid','dashed','dotted','cover','contain','fixed','absolute','relative','sticky','row','column','nowrap','wrap','flex','grid','block','inline','hidden','visible' ], true );
    }

    private static function looks_like_human_copy( string $value, array $path, string $key ): bool {
        $plain = trim( html_entity_decode( wp_strip_all_tags( $value ), ENT_QUOTES | ENT_HTML5, 'UTF-8' ) );
        if ( '' === $plain || self::technical_value( $value ) ) return false;

        $joined = strtolower( implode( '_', array_map( static fn( $part ) => is_string( $part ) ? $part : '', $path ) ) );
        if ( preg_match( '/(^|_)(form|input|field|validation|webhook)(_|$)/', $joined ) && in_array( $key, [ 'name', 'value' ], true ) ) return false;
        if ( in_array( $key, [ 'name', 'author_name', 'client_name', 'person_name' ], true ) ) return true;

        if ( false !== strpos( $plain, ' ' ) ) return true;
        if ( preg_match( '/[.!?,:;]/', $plain ) ) return true;
        if ( preg_match( '/<[^>]+>/', $value ) ) return true;
        return strlen( $plain ) >= 24;
    }

    private static function link_path( array $path, string $widget ): bool {
        $parts = array_values( array_map( 'strtolower', array_filter( $path, 'is_string' ) ) );
        if ( ! $parts ) return false;
        $joined = implode( '_', $parts );
        if ( preg_match( '/(^|_)(image|media|logo|photo|thumbnail|poster|background|video|icon)(_|$)/', $joined ) || preg_match( '/(^|_)(color|hover|typography|font|size|width|height|margin|padding|border|radius|style)(_|$)/', $joined ) ) return false;
        $last = end( $parts );
        if ( in_array( $last, [ 'href', 'link', 'button_link', 'btn_link', 'cta_link' ], true ) || str_ends_with( $last, '_link' ) || str_ends_with( $last, '_url' ) ) return true;
        return 'url' === $last && ( preg_match( '/(link|button|btn|cta|href)/', $joined ) || preg_match( '/button/i', $widget ) );
    }

    private static function sanitize_destination( string $raw ) {
        $value = trim( $raw );
        if ( '' === $value ) return '';
        if ( self::dynamic( $value ) ) return new WP_Error( 'elementize_dynamic_link_read_only', 'Dynamic link values are read-only.', [ 'status' => 400 ] );
        if ( '#' === $value ) return '#';
        if ( str_starts_with( $value, '#' ) ) return preg_match( '/^#[A-Za-z][A-Za-z0-9_:\\-.]*$/', $value ) ? $value : new WP_Error( 'elementize_invalid_link', 'Fragment links must use a simple valid anchor.', [ 'status' => 400 ] );
        if ( str_starts_with( $value, '/' ) && ! str_starts_with( $value, '//' ) ) return preg_match( '/^\\/[A-Za-z0-9_~!$&\'()*+,;=:@%\\/?.#-]*$/', $value ) ? $value : new WP_Error( 'elementize_invalid_link', 'Relative links contain unsupported characters.', [ 'status' => 400 ] );
        if ( preg_match( '#^https?://#i', $value ) ) {
            $url = esc_url_raw( $value, [ 'http', 'https' ] );
            return $url ?: new WP_Error( 'elementize_invalid_link', 'HTTP(S) link is invalid.', [ 'status' => 400 ] );
        }
        if ( str_starts_with( strtolower( $value ), 'mailto:' ) ) {
            $email = sanitize_email( substr( $value, 7 ) );
            return $email ? 'mailto:' . $email : new WP_Error( 'elementize_invalid_link', 'mailto link is invalid.', [ 'status' => 400 ] );
        }
        if ( str_starts_with( strtolower( $value ), 'tel:' ) ) {
            $phone = trim( substr( $value, 4 ) );
            return preg_match( '/^[0-9+(). -]{3,40}$/', $phone ) ? 'tel:' . $phone : new WP_Error( 'elementize_invalid_link', 'tel link is invalid.', [ 'status' => 400 ] );
        }
        return new WP_Error( 'elementize_invalid_link_scheme', 'Only http, https, relative paths, fragments, mailto, and tel destinations are allowed.', [ 'status' => 400 ] );
    }

    private static function dynamic( string $value ): bool {
        return str_starts_with( ltrim( $value ), '[elementor-tag' );
    }

    private static function integerish( $value ): bool {
        if ( is_int( $value ) ) return $value >= 0;
        return is_string( $value ) && (bool) preg_match( '/^\\d+$/', $value );
    }

    private static function lower( string $value ): string {
        return function_exists( 'mb_strtolower' ) ? mb_strtolower( $value, 'UTF-8' ) : strtolower( $value );
    }

    private static function last_key( array $path ): string {
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) if ( is_string( $path[ $i ] ) ) return strtolower( $path[ $i ] );
        return '';
    }

    private static function path_has( array $path, string $needle ): bool {
        foreach ( $path as $segment ) if ( is_string( $segment ) && strtolower( $segment ) === strtolower( $needle ) ) return true;
        return false;
    }
}

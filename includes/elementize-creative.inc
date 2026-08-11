<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Creative {
    private const MAX_OPERATIONS = 50;
    private const MAX_DEPTH = 80;
    private const MAX_ELEMENTS = 10000;
    private const MAX_PATH = 30;
    private const MAX_TEXT = 20000;
    private const MAX_LINK = 2048;
    private const MANAGED_META = '_elementize_managed_roots';
    private const MANAGED_SNAPSHOT_META = '_elementize_managed_roots_snapshot';
    private const ACTIVITY_LOG = 'elementize_activity_log';
    private const ACTIVITY_SNAPSHOT_META = '_elementize_activity_snapshot';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 28 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'restore_managed_after_undo' ], 20, 3 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/creative', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'apply' ],
            'permission_callback' => [ Elementize_Core::class, 'can_write_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'expected_status' => [ 'type' => 'string', 'required' => true ],
                'expected_title' => [ 'type' => 'string', 'required' => true ],
                'content_hash' => [ 'type' => 'string', 'required' => true ],
                'expected_capability_revision' => [ 'type' => 'integer', 'required' => true, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'plan_label' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'operations' => [ 'type' => 'array', 'required' => true ],
                'confirm_creative_write' => [ 'type' => 'boolean', 'required' => true ],
                'confirm_published_page' => [ 'type' => 'boolean', 'default' => false ],
            ],
        ] );
    }

    public static function apply( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_creative_write' ) ) return new WP_Error( 'elementize_creative_confirmation_required', 'Explicit confirm_creative_write=true is required.', [ 'status' => 400 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( (string) $request->get_param( 'expected_status' ) !== (string) $post->post_status ) return new WP_Error( 'elementize_status_changed', 'The page status changed after the creative read.', [ 'status' => 409, 'status_now' => (string) $post->post_status ] );
        if ( (string) $request->get_param( 'expected_title' ) !== (string) $post->post_title ) return new WP_Error( 'elementize_title_changed', 'The page title changed after the creative read.', [ 'status' => 409, 'title_now' => (string) $post->post_title ] );
        if ( 'publish' === $post->post_status && true !== $request->get_param( 'confirm_published_page' ) ) return new WP_Error( 'elementize_published_confirmation_required', 'This is a published page. Explicit confirm_published_page=true is required before a creative change.', [ 'status' => 400 ] );

        $capability_revision = absint( $request->get_param( 'expected_capability_revision' ) );
        $allowed = Elementize_Capabilities::require_creative( $id, $capability_revision );
        if ( is_wp_error( $allowed ) ) return $allowed;

        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $document = $loaded['document'];
        $before = $loaded['elements'];
        $before_hash = Elementize_Core::hash_elements( $before );
        $expected_hash = trim( (string) $request->get_param( 'content_hash' ) );
        if ( '' === $expected_hash || ! hash_equals( $before_hash, $expected_hash ) ) return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after the creative read. Refresh page structure/design before applying the plan.', [ 'status' => 409, 'content_hash' => $before_hash ] );

        $operations = $request->get_param( 'operations' );
        if ( ! is_array( $operations ) || ! $operations ) return new WP_Error( 'elementize_creative_operations_required', 'At least one creative operation is required.', [ 'status' => 400 ] );
        if ( count( $operations ) > self::MAX_OPERATIONS ) return new WP_Error( 'elementize_creative_operations_limit', 'Send at most ' . self::MAX_OPERATIONS . ' creative operations in one transaction.', [ 'status' => 400 ] );

        $working = $before;
        $managed_before = self::managed_roots( $id );
        $managed = $managed_before;
        $aliases = [];
        $changes = [];
        $used_ids = self::all_ids( $working );

        foreach ( array_values( $operations ) as $index => $operation ) {
            if ( ! is_array( $operation ) ) return self::operation_error( $index, 'Each creative operation must be an object.' );
            $type = sanitize_key( (string) ( $operation['type'] ?? '' ) );
            if ( ! in_array( $type, [ 'insert_template', 'remove_element', 'duplicate_element', 'move_element', 'reorder_children', 'style', 'content' ], true ) ) return self::operation_error( $index, 'Unsupported creative operation type.' );
            $result = self::apply_operation( $working, $operation, $index, $aliases, $used_ids, $managed, $changes );
            if ( is_wp_error( $result ) ) return $result;
        }

        $tree_check = self::validate_tree( $working );
        if ( is_wp_error( $tree_check ) ) return $tree_check;
        $after_hash_expected = Elementize_Core::hash_elements( $working );
        if ( hash_equals( $before_hash, $after_hash_expected ) ) return new WP_Error( 'elementize_creative_no_change', 'The creative plan does not change the current Elementor page.', [ 'status' => 400 ] );

        $revision = Elementize_Core::save_revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        self::store_revision_snapshots( (int) $revision, $before, $managed_before );

        try { $saved = $document->save( [ 'elements' => $working ] ); }
        catch ( Throwable $e ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_save_failed', 'Elementor could not save the creative transaction: ' . $e->getMessage(), [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }
        if ( ! $saved ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_save_failed', 'Elementor refused to save the creative transaction.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }

        $persisted = Elementize_Core::persisted_elements( $id );
        if ( is_wp_error( $persisted ) || ! hash_equals( $after_hash_expected, Elementize_Core::hash_elements( is_array( $persisted ) ? $persisted : [] ) ) ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_verification_failed', 'Persisted Elementor data did not exactly match the validated creative transaction, so Elementize attempted to restore the pre-change page.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }

        self::save_managed_roots( $id, $managed );
        Elementize_Core::regenerate_css( $id );
        clean_post_cache( $id );
        $after_hash = Elementize_Core::hash_elements( $persisted );
        $activity_id = self::record_activity( $id, (int) $revision, $before_hash, $after_hash, $changes, sanitize_text_field( (string) $request->get_param( 'plan_label' ) ) );
        $quality = self::quality_summary( $persisted, $managed );

        return new WP_REST_Response( [
            'saved' => true,
            'creative_transaction' => true,
            'page' => Elementize_Core::page_identity( $id ),
            'revision_id' => (int) $revision,
            'activity_id' => $activity_id,
            'content_hash' => $after_hash,
            'persisted_verification' => true,
            'capability_revision' => $capability_revision,
            'operation_count' => count( $operations ),
            'changes' => $changes,
            'aliases' => self::public_aliases( $aliases ),
            'managed_root_ids' => array_values( array_keys( $managed ) ),
            'quality' => $quality,
        ], 200 );
    }

    public static function restore_managed_after_undo( $response, $handler, WP_REST_Request $request ) {
        if ( 'POST' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/activity/[A-Fa-f0-9-]{36}/undo$#', $request->get_route() ) ) return $response;
        if ( is_wp_error( $response ) ) return $response;
        $rest = rest_ensure_response( $response );
        $data = $rest->get_data();
        if ( ! is_array( $data ) || empty( $data['undone'] ) ) return $response;
        $activity_id = sanitize_text_field( (string) ( $data['activity_id'] ?? '' ) );
        $page_id = absint( $data['page']['id'] ?? 0 );
        if ( ! $page_id || '' === $activity_id ) return $response;
        $record = self::activity_record( $activity_id );
        if ( ! is_array( $record ) || empty( $record['creative'] ) ) return $response;
        $revision_id = absint( $record['revision_id'] ?? 0 );
        $payload = $revision_id ? (string) get_metadata( 'post', $revision_id, self::MANAGED_SNAPSHOT_META, true ) : '';
        if ( '' === $payload ) return $response;
        $json = base64_decode( $payload, true );
        $managed = is_string( $json ) ? json_decode( $json, true ) : null;
        if ( is_array( $managed ) ) self::save_managed_roots( $page_id, $managed );
        return $response;
    }

    private static function apply_operation( array &$elements, array $operation, int $index, array &$aliases, array &$used_ids, array &$managed, array &$changes ) {
        $type = sanitize_key( (string) $operation['type'] );
        if ( 'insert_template' === $type ) return self::op_insert_template( $elements, $operation, $index, $aliases, $used_ids, $managed, $changes );
        if ( 'remove_element' === $type ) return self::op_remove( $elements, $operation, $index, $aliases, $managed, $changes );
        if ( 'duplicate_element' === $type ) return self::op_duplicate( $elements, $operation, $index, $aliases, $used_ids, $managed, $changes );
        if ( 'move_element' === $type ) return self::op_move( $elements, $operation, $index, $aliases, $changes );
        if ( 'reorder_children' === $type ) return self::op_reorder( $elements, $operation, $index, $aliases, $changes );
        if ( 'style' === $type ) return self::op_style( $elements, $operation, $index, $aliases, $changes );
        return self::op_content( $elements, $operation, $index, $aliases, $changes );
    }

    private static function op_insert_template( array &$elements, array $op, int $index, array &$aliases, array &$used_ids, array &$managed, array &$changes ) {
        $alias = sanitize_key( (string) ( $op['alias'] ?? '' ) );
        if ( '' === $alias || strlen( $alias ) > 60 || isset( $aliases[ $alias ] ) ) return self::operation_error( $index, 'insert_template requires a unique alias of at most 60 characters.' );
        $source_id = sanitize_key( (string) ( $op['source_id'] ?? '' ) );
        $template_id = sanitize_text_field( (string) ( $op['template_id'] ?? '' ) );
        if ( '' === $source_id || '' === $template_id ) return self::operation_error( $index, 'insert_template requires source_id and template_id from template discovery.' );
        $template = Elementize_Templates::load_template( $source_id, $template_id );
        if ( is_wp_error( $template ) ) return $template;

        $map = [];
        $nodes = self::clone_with_fresh_ids( $template['elements'], $used_ids, $map );
        if ( ! $nodes ) return self::operation_error( $index, 'The selected template produced no insertable roots.' );
        $position = sanitize_key( (string) ( $op['position'] ?? 'after' ) );
        $anchor_ref = (string) ( $op['anchor_ref'] ?? '' );
        $anchor_id = '' !== $anchor_ref ? self::resolve_ref( $anchor_ref, $aliases ) : '';
        if ( is_wp_error( $anchor_id ) ) return $anchor_id;
        $inserted = self::insert_nodes( $elements, $anchor_id, $nodes, $position );
        if ( is_wp_error( $inserted ) ) return self::operation_error( $index, $inserted->get_error_message(), $inserted->get_error_data() );

        $roots = array_values( array_filter( array_map( static fn( $node ) => is_array( $node ) ? (string) ( $node['id'] ?? '' ) : '', $nodes ) ) );
        $aliases[ $alias ] = [ 'map' => $map, 'roots' => $roots ];
        $contract = sanitize_key( (string) ( $op['contract'] ?? 'generic' ) );
        if ( ! in_array( $contract, [ 'feature_grid', 'content_split', 'testimonial_grid', 'cta', 'generic' ], true ) ) $contract = 'generic';
        foreach ( $roots as $root_id ) {
            $managed[ $root_id ] = [
                'source_type' => 'template',
                'source_id' => $source_id,
                'template_id' => $template_id,
                'template_title' => (string) $template['title'],
                'provider' => (string) $template['provider'],
                'alias' => $alias,
                'contract' => $contract,
                'created_gmt' => gmdate( 'Y-m-d H:i:s' ),
            ];
        }
        $changes[] = [ 'kind' => 'insert_template', 'element_id' => (string) ( $roots[0] ?? '' ), 'setting_path' => [], 'root_ids' => $roots, 'source_id' => $source_id, 'template_id' => $template_id, 'alias' => $alias ];
        return true;
    }

    private static function op_remove( array &$elements, array $op, int $index, array $aliases, array &$managed, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $removed = null; $parent = ''; $position = -1;
        if ( ! self::remove_node( $elements, $id, $removed, $parent, $position ) || ! is_array( $removed ) ) return self::operation_error( $index, 'The element to remove no longer exists.' );
        $removed_ids = self::all_ids( [ $removed ] );
        foreach ( array_keys( $managed ) as $root_id ) if ( isset( $removed_ids[ $root_id ] ) ) unset( $managed[ $root_id ] );
        $changes[] = [ 'kind' => 'remove_element', 'element_id' => $id, 'setting_path' => [], 'parent_id' => $parent, 'index' => $position ];
        return true;
    }

    private static function op_duplicate( array &$elements, array $op, int $index, array $aliases, array &$used_ids, array &$managed, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $node = self::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The element to duplicate no longer exists.' );
        $map = [];
        $clones = self::clone_with_fresh_ids( [ $node ], $used_ids, $map );
        $clone = $clones[0] ?? null;
        if ( ! is_array( $clone ) ) return self::operation_error( $index, 'Element duplication failed.' );
        $position = sanitize_key( (string) ( $op['position'] ?? 'after' ) );
        if ( ! in_array( $position, [ 'before', 'after' ], true ) ) return self::operation_error( $index, 'duplicate_element position must be before or after.' );
        $inserted = self::insert_nodes( $elements, $id, [ $clone ], $position );
        if ( is_wp_error( $inserted ) ) return self::operation_error( $index, $inserted->get_error_message() );
        $new_id = (string) ( $clone['id'] ?? '' );
        if ( isset( $managed[ $id ] ) && '' !== $new_id ) {
            $managed[ $new_id ] = array_merge( $managed[ $id ], [ 'source_type' => 'duplicate', 'created_gmt' => gmdate( 'Y-m-d H:i:s' ) ] );
        }
        $alias = sanitize_key( (string) ( $op['alias'] ?? '' ) );
        if ( '' !== $alias ) {
            if ( isset( $GLOBALS['elementize_creative_duplicate_aliases'][ $alias ] ) ) return self::operation_error( $index, 'Duplicate alias is already in use.' );
            $GLOBALS['elementize_creative_duplicate_aliases'][ $alias ] = $new_id;
        }
        $changes[] = [ 'kind' => 'duplicate_element', 'element_id' => $id, 'setting_path' => [], 'new_element_id' => $new_id ];
        return true;
    }

    private static function op_move( array &$elements, array $op, int $index, array $aliases, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $position = sanitize_key( (string) ( $op['position'] ?? 'after' ) );
        $anchor_ref = (string) ( $op['anchor_ref'] ?? '' );
        $anchor_id = '' !== $anchor_ref ? self::resolve_ref( $anchor_ref, $aliases ) : '';
        if ( is_wp_error( $anchor_id ) ) return $anchor_id;
        if ( $id === $anchor_id ) return self::operation_error( $index, 'An element cannot be moved relative to itself.' );
        $node = self::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The element to move no longer exists.' );
        $descendants = self::all_ids( [ $node ] );
        if ( '' !== $anchor_id && isset( $descendants[ $anchor_id ] ) ) return self::operation_error( $index, 'An element cannot be moved inside or relative to its own descendant.' );
        $removed = null; $old_parent = ''; $old_index = -1;
        if ( ! self::remove_node( $elements, $id, $removed, $old_parent, $old_index ) || ! is_array( $removed ) ) return self::operation_error( $index, 'The element could not be detached for moving.' );
        $inserted = self::insert_nodes( $elements, $anchor_id, [ $removed ], $position );
        if ( is_wp_error( $inserted ) ) return self::operation_error( $index, $inserted->get_error_message() );
        $changes[] = [ 'kind' => 'move_element', 'element_id' => $id, 'setting_path' => [], 'old_parent_id' => $old_parent, 'old_index' => $old_index, 'anchor_id' => $anchor_id, 'position' => $position ];
        return true;
    }

    private static function op_reorder( array &$elements, array $op, int $index, array $aliases, array &$changes ) {
        $parent_ref = (string) ( $op['parent_ref'] ?? '' );
        $parent_id = '' !== $parent_ref ? self::resolve_ref( $parent_ref, $aliases ) : '';
        if ( is_wp_error( $parent_id ) ) return $parent_id;
        $refs = $op['ordered_refs'] ?? null;
        if ( ! is_array( $refs ) || count( $refs ) < 2 || count( $refs ) > 100 ) return self::operation_error( $index, 'reorder_children requires 2-100 ordered_refs.' );
        $ids = [];
        foreach ( $refs as $ref ) {
            if ( ! is_string( $ref ) ) return self::operation_error( $index, 'Each ordered_refs entry must be an element reference string.' );
            $resolved = self::resolve_ref( $ref, $aliases );
            if ( is_wp_error( $resolved ) ) return $resolved;
            $ids[] = $resolved;
        }
        if ( count( array_unique( $ids ) ) !== count( $ids ) ) return self::operation_error( $index, 'ordered_refs may not contain duplicate elements.' );
        $result = self::reorder_children( $elements, $parent_id, $ids );
        if ( is_wp_error( $result ) ) return self::operation_error( $index, $result->get_error_message() );
        $changes[] = [ 'kind' => 'reorder_children', 'element_id' => $parent_id, 'setting_path' => [], 'ordered_ids' => $ids ];
        return true;
    }

    private static function op_style( array &$elements, array $op, int $index, array $aliases, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $path = self::valid_path( $op['setting_path'] ?? null );
        if ( is_wp_error( $path ) ) return self::operation_error( $index, $path->get_error_message() );
        if ( ! is_string( $op['expected_json'] ?? null ) || ! is_string( $op['value_json'] ?? null ) ) return self::operation_error( $index, 'style requires expected_json and value_json from the design profile.' );
        $ok = false; $expected = Elementize_Design::decode_json_value( $op['expected_json'], $ok );
        if ( ! $ok ) return self::operation_error( $index, 'style expected_json is invalid JSON.' );
        $new_ok = false; $new = Elementize_Design::decode_json_value( $op['value_json'], $new_ok );
        if ( ! $new_ok ) return self::operation_error( $index, 'style value_json is invalid JSON.' );
        $old = null;
        $result = self::mutate_setting( $elements, $id, $path, static function( &$cursor ) use ( $expected, $new, &$old ) {
            if ( Elementize_Design::canonical_json( $cursor ) !== Elementize_Design::canonical_json( $expected ) ) return new WP_Error( 'elementize_design_value_changed', 'The design setting changed after the creative read.', [ 'status' => 409 ] );
            $category = Elementize_Design::classify_control( $GLOBALS['elementize_creative_path'], $cursor );
            if ( false === $category ) return new WP_Error( 'elementize_design_control_invalid', 'This setting is not an approved creative design control.', [ 'status' => 400 ] );
            $validated = Elementize_Design::validate_control_value( $category, $new, $cursor );
            if ( is_wp_error( $validated ) ) return $validated;
            $old = $cursor;
            $cursor = $validated;
            return [ 'category' => $category, 'new' => $validated ];
        } );
        unset( $GLOBALS['elementize_creative_path'] );
        if ( is_wp_error( $result ) ) return $result;
        if ( ! is_array( $result ) ) return self::operation_error( $index, 'The design target no longer exists.' );
        $changes[] = [ 'kind' => 'design_' . $result['category'], 'element_id' => $id, 'setting_path' => $path, 'old_value' => $old, 'new_value' => $result['new'] ];
        return true;
    }

    private static function op_content( array &$elements, array $op, int $index, array $aliases, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $kind = sanitize_key( (string) ( $op['kind'] ?? '' ) );
        if ( ! in_array( $kind, [ 'text', 'link', 'media', 'pixfort_icon' ], true ) ) return self::operation_error( $index, 'content kind must be text, link, media, or pixfort_icon.' );
        $path = self::valid_path( $op['setting_path'] ?? null );
        if ( is_wp_error( $path ) ) return self::operation_error( $index, $path->get_error_message() );
        $node = self::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The content element no longer exists.' );
        $eligible = false;
        foreach ( Elementize_Content::discover_items( [ $node ] ) as $item ) {
            if ( (string) $item['kind'] === $kind && (string) $item['element_id'] === $id && self::same_path( (array) $item['setting_path'], $path ) ) { $eligible = true; break; }
        }
        if ( ! $eligible ) return self::operation_error( $index, 'The requested content setting is not a currently writable Elementize content field.' );

        $old = null; $new = null;
        $result = self::mutate_setting( $elements, $id, $path, static function( &$cursor ) use ( $op, $kind, &$old, &$new ) {
            if ( 'media' === $kind ) {
                $expected_id = absint( $op['expected_attachment_id'] ?? -1 );
                $attachment_id = absint( $op['attachment_id'] ?? 0 );
                if ( ! is_array( $cursor ) || absint( $cursor['id'] ?? 0 ) !== $expected_id ) return new WP_Error( 'elementize_value_changed', 'The target image changed after the creative read.', [ 'status' => 409 ] );
                if ( $attachment_id < 1 || 'attachment' !== get_post_type( $attachment_id ) || ! wp_attachment_is_image( $attachment_id ) || ! current_user_can( 'edit_post', $attachment_id ) ) return new WP_Error( 'elementize_invalid_media', 'attachment_id must reference an image the authenticated WordPress user can use.', [ 'status' => 400 ] );
                $url = wp_get_attachment_url( $attachment_id );
                if ( ! is_string( $url ) || '' === $url ) return new WP_Error( 'elementize_invalid_media', 'WordPress could not resolve the selected image URL.', [ 'status' => 400 ] );
                $old = $cursor; $new = $cursor; $new['id'] = $attachment_id; $new['url'] = esc_url_raw( $url ); unset( $new['source'] ); $cursor = $new; return true;
            }
            if ( ! is_string( $cursor ) || ! is_string( $op['expected_value'] ?? null ) || ! is_string( $op['value'] ?? null ) ) return new WP_Error( 'elementize_content_value_invalid', 'String content writes require exact expected_value and value strings.', [ 'status' => 400 ] );
            if ( $cursor !== $op['expected_value'] ) return new WP_Error( 'elementize_value_changed', 'The target content changed after the creative read.', [ 'status' => 409 ] );
            $candidate = (string) $op['value'];
            if ( 'text' === $kind ) {
                if ( strlen( $candidate ) > self::MAX_TEXT ) return new WP_Error( 'elementize_text_too_long', 'Text value is too long.', [ 'status' => 400 ] );
                $candidate = wp_kses_post( $candidate );
            } elseif ( 'link' === $kind ) {
                if ( strlen( $candidate ) > self::MAX_LINK ) return new WP_Error( 'elementize_link_too_long', 'Link value is too long.', [ 'status' => 400 ] );
                $candidate = self::sanitize_destination( $candidate );
                if ( is_wp_error( $candidate ) ) return $candidate;
            } else {
                $candidate = trim( $candidate );
                $known = Elementize_Pixfort_Icons::validate_value( $candidate );
                if ( is_wp_error( $known ) ) return $known;
                if ( true !== $known ) return new WP_Error( 'elementize_unknown_pixfort_icon', 'Use an exact icon value returned by Pixfort icon search.', [ 'status' => 400 ] );
            }
            $old = $cursor; $cursor = $new = $candidate; return true;
        } );
        if ( is_wp_error( $result ) ) return $result;
        if ( true !== $result ) return self::operation_error( $index, 'The content target no longer exists.' );
        $changes[] = [ 'kind' => $kind, 'element_id' => $id, 'setting_path' => $path, 'old_value' => $old, 'new_value' => $new ];
        return true;
    }

    private static function mutate_setting( array &$elements, string $id, array $path, callable $callback, int $depth = 0 ) {
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
                $GLOBALS['elementize_creative_path'] = $path;
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

    private static function insert_nodes( array &$elements, string $anchor_id, array $nodes, string $position, int $depth = 0 ) {
        if ( 'page_start' === $position ) { array_splice( $elements, 0, 0, $nodes ); return true; }
        if ( 'page_end' === $position ) { array_splice( $elements, count( $elements ), 0, $nodes ); return true; }
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

    private static function remove_node( array &$elements, string $id, &$removed, string &$parent_id, int &$index, string $parent = '', int $depth = 0 ): bool {
        if ( $depth > self::MAX_DEPTH ) return false;
        for ( $i = 0; $i < count( $elements ); $i++ ) {
            if ( ! is_array( $elements[ $i ] ) ) continue;
            if ( (string) ( $elements[ $i ]['id'] ?? '' ) === $id ) {
                $removed = $elements[ $i ]; $parent_id = $parent; $index = $i; array_splice( $elements, $i, 1 ); return true;
            }
            if ( is_array( $elements[ $i ]['elements'] ?? null ) ) {
                $current = (string) ( $elements[ $i ]['id'] ?? '' );
                if ( self::remove_node( $elements[ $i ]['elements'], $id, $removed, $parent_id, $index, $current, $depth + 1 ) ) return true;
            }
        }
        return false;
    }

    private static function reorder_children( array &$elements, string $parent_id, array $ordered_ids, int $depth = 0 ) {
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

    private static function reorder_array( array &$children, array $ordered_ids ) {
        $current_ids = [];
        foreach ( $children as $child ) if ( is_array( $child ) ) $current_ids[] = (string) ( $child['id'] ?? '' );
        if ( count( $current_ids ) !== count( $ordered_ids ) || array_diff( $current_ids, $ordered_ids ) || array_diff( $ordered_ids, $current_ids ) ) return new WP_Error( 'elementize_reorder_set_mismatch', 'ordered_refs must contain every current child exactly once. Refresh structure before reordering.', [ 'status' => 409, 'current_child_ids' => $current_ids ] );
        $by_id = [];
        foreach ( $children as $child ) if ( is_array( $child ) ) $by_id[ (string) $child['id'] ] = $child;
        $children = array_map( static fn( string $id ) => $by_id[ $id ], $ordered_ids );
        return true;
    }

    private static function clone_with_fresh_ids( array $elements, array &$used_ids, array &$map ): array {
        self::collect_mappable_ids( $elements, $map, $used_ids );
        return self::replace_mapped_ids( $elements, $map );
    }

    private static function collect_mappable_ids( $value, array &$map, array &$used_ids ): void {
        if ( ! is_array( $value ) ) return;
        foreach ( $value as $key => $child ) {
            if ( in_array( (string) $key, [ 'id', '_id' ], true ) && is_string( $child ) && preg_match( '/^[A-Za-z0-9_-]{4,40}$/', $child ) ) {
                if ( ! isset( $map[ $child ] ) ) $map[ $child ] = self::fresh_id( $used_ids );
            }
            if ( is_array( $child ) ) self::collect_mappable_ids( $child, $map, $used_ids );
        }
    }

    private static function replace_mapped_ids( $value, array $map ) {
        if ( is_array( $value ) ) {
            $out = [];
            foreach ( $value as $key => $child ) $out[ $key ] = self::replace_mapped_ids( $child, $map );
            return $out;
        }
        if ( ! is_string( $value ) ) return $value;
        if ( isset( $map[ $value ] ) ) return $map[ $value ];
        foreach ( $map as $old => $new ) {
            if ( false !== strpos( $value, 'elementor-element-' . $old ) ) $value = str_replace( 'elementor-element-' . $old, 'elementor-element-' . $new, $value );
            if ( false !== strpos( $value, '#' . $old ) ) $value = str_replace( '#' . $old, '#' . $new, $value );
        }
        return $value;
    }

    private static function fresh_id( array &$used ): string {
        do { $id = substr( md5( wp_generate_uuid4() . microtime( true ) . wp_rand() ), 0, 8 ); } while ( isset( $used[ $id ] ) );
        $used[ $id ] = true;
        return $id;
    }

    private static function all_ids( array $elements ): array {
        $ids = [];
        self::collect_ids( $elements, $ids, 0 );
        return $ids;
    }

    private static function collect_ids( array $elements, array &$ids, int $depth ): void {
        if ( $depth > self::MAX_DEPTH ) return;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            if ( '' !== $id ) $ids[ $id ] = true;
            if ( is_array( $element['elements'] ?? null ) ) self::collect_ids( $element['elements'], $ids, $depth + 1 );
        }
    }

    private static function find_node( array $elements, string $id, int $depth = 0 ): ?array {
        if ( $depth > self::MAX_DEPTH ) return null;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( (string) ( $element['id'] ?? '' ) === $id ) return $element;
            if ( is_array( $element['elements'] ?? null ) ) { $found = self::find_node( $element['elements'], $id, $depth + 1 ); if ( is_array( $found ) ) return $found; }
        }
        return null;
    }

    private static function validate_tree( array $elements ) {
        if ( ! $elements ) return new WP_Error( 'elementize_creative_empty_page', 'Creative Control will not save an empty Elementor page.', [ 'status' => 400 ] );
        $seen = []; $count = 0; $max_depth = 0;
        $walk = function( array $nodes, int $depth ) use ( &$walk, &$seen, &$count, &$max_depth ) {
            if ( $depth > self::MAX_DEPTH ) return new WP_Error( 'elementize_creative_depth_limit', 'The resulting Elementor tree is too deeply nested.', [ 'status' => 400 ] );
            $max_depth = max( $max_depth, $depth );
            foreach ( $nodes as $node ) {
                if ( ! is_array( $node ) ) return new WP_Error( 'elementize_creative_tree_invalid', 'The resulting Elementor tree contains an invalid element.', [ 'status' => 400 ] );
                $count++;
                if ( $count > self::MAX_ELEMENTS ) return new WP_Error( 'elementize_creative_element_limit', 'The resulting Elementor page exceeds the Elementize element limit.', [ 'status' => 400 ] );
                $id = trim( (string) ( $node['id'] ?? '' ) );
                if ( '' === $id ) return new WP_Error( 'elementize_creative_element_id_missing', 'Every Elementor element must keep a non-empty ID.', [ 'status' => 400 ] );
                if ( isset( $seen[ $id ] ) ) return new WP_Error( 'elementize_creative_duplicate_id', 'The resulting Elementor tree contains a duplicate element ID.', [ 'status' => 400, 'element_id' => $id ] );
                $seen[ $id ] = true;
                if ( isset( $node['elements'] ) && ! is_array( $node['elements'] ) ) return new WP_Error( 'elementize_creative_children_invalid', 'An Elementor element has an invalid child list.', [ 'status' => 400, 'element_id' => $id ] );
                if ( is_array( $node['elements'] ?? null ) ) { $result = $walk( $node['elements'], $depth + 1 ); if ( is_wp_error( $result ) ) return $result; }
            }
            return true;
        };
        return $walk( $elements, 0 );
    }

    private static function managed_roots( int $page_id ): array {
        $value = get_post_meta( $page_id, self::MANAGED_META, true );
        return is_array( $value ) ? $value : [];
    }

    private static function save_managed_roots( int $page_id, array $managed ): void {
        if ( $managed ) update_post_meta( $page_id, self::MANAGED_META, $managed );
        else delete_post_meta( $page_id, self::MANAGED_META );
    }

    private static function store_revision_snapshots( int $revision_id, array $elements, array $managed ): void {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( is_string( $json ) && '' !== $json ) update_metadata( 'post', $revision_id, self::ACTIVITY_SNAPSHOT_META, base64_encode( $json ) );
        $managed_json = wp_json_encode( $managed, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( is_string( $managed_json ) ) update_metadata( 'post', $revision_id, self::MANAGED_SNAPSHOT_META, base64_encode( $managed_json ) );
    }

    private static function restore_page( $document, int $id, array $before, string $before_hash ): bool {
        try { $saved = $document->save( [ 'elements' => $before ] ); } catch ( Throwable $e ) { return false; }
        if ( ! $saved ) return false;
        $persisted = Elementize_Core::persisted_elements( $id );
        $ok = is_array( $persisted ) && hash_equals( $before_hash, Elementize_Core::hash_elements( $persisted ) );
        if ( $ok ) { Elementize_Core::regenerate_css( $id ); clean_post_cache( $id ); }
        return $ok;
    }

    private static function record_activity( int $page_id, int $revision_id, string $before_hash, string $after_hash, array $changes, string $label ): string {
        $records = get_option( self::ACTIVITY_LOG, [] );
        if ( ! is_array( $records ) ) $records = [];
        $post = get_post( $page_id );
        $user = wp_get_current_user();
        $kinds = [];
        foreach ( $changes as $change ) { $kind = sanitize_key( (string) ( $change['kind'] ?? '' ) ); if ( '' !== $kind ) $kinds[ $kind ] = ( $kinds[ $kind ] ?? 0 ) + 1; }
        $activity_id = wp_generate_uuid4();
        array_unshift( $records, [
            'id' => $activity_id,
            'created_gmt' => gmdate( 'Y-m-d H:i:s' ),
            'page_id' => $page_id,
            'page_title' => $post ? (string) $post->post_title : '',
            'page_status' => $post ? (string) $post->post_status : '',
            'user_id' => $user && $user->exists() ? (int) $user->ID : 0,
            'user_name' => $user && $user->exists() ? (string) $user->display_name : '',
            'revision_id' => $revision_id,
            'before_hash' => $before_hash,
            'after_hash' => $after_hash,
            'updated_count' => count( $changes ),
            'kinds' => $kinds,
            'changes' => array_slice( $changes, 0, 50 ),
            'creative' => true,
            'plan_label' => $label,
            'undone_at_gmt' => '',
            'undo_revision_id' => 0,
        ] );
        update_option( self::ACTIVITY_LOG, array_slice( $records, 0, 100 ), false );
        return $activity_id;
    }

    private static function activity_record( string $activity_id ): ?array {
        $records = get_option( self::ACTIVITY_LOG, [] );
        if ( ! is_array( $records ) ) return null;
        foreach ( $records as $record ) if ( is_array( $record ) && (string) ( $record['id'] ?? '' ) === $activity_id ) return $record;
        return null;
    }

    private static function quality_summary( array $elements, array $managed ): array {
        $tree = Elementize_Templates::structure_summary( $elements );
        $profile = Elementize_Design::profile_for_elements( $elements, '', 500 );
        $warnings = [];
        if ( count( (array) ( $profile['palette'] ?? [] ) ) > 12 ) $warnings[] = 'The page uses a broad color palette; prefer the most frequently observed colors in future creative passes.';
        if ( (int) ( $tree['max_depth'] ?? 0 ) > 60 ) $warnings[] = 'The page has deep nesting; avoid adding more nested wrapper containers.';
        if ( (int) ( $tree['element_count'] ?? 0 ) > 1500 ) $warnings[] = 'The page is structurally large; keep future creative transactions focused.';
        return [
            'structural_validity' => true,
            'element_count' => (int) ( $tree['element_count'] ?? 0 ),
            'max_depth' => (int) ( $tree['max_depth'] ?? 0 ),
            'managed_root_count' => count( $managed ),
            'observed_palette_tokens' => array_slice( (array) ( $profile['palette'] ?? [] ), 0, 8 ),
            'observed_spacing_tokens' => array_slice( (array) ( $profile['spacing_scale'] ?? [] ), 0, 8 ),
            'warnings' => $warnings,
            'visual_render_verified' => false,
        ];
    }

    private static function sanitize_destination( string $value ) {
        $value = trim( $value );
        if ( '' === $value || '#' === $value ) return $value;
        if ( str_starts_with( $value, '#' ) || str_starts_with( $value, '/' ) || str_starts_with( $value, './' ) || str_starts_with( $value, '../' ) || str_starts_with( strtolower( $value ), 'mailto:' ) || str_starts_with( strtolower( $value ), 'tel:' ) ) {
            if ( preg_match( '/[\r\n<>]/', $value ) ) return new WP_Error( 'elementize_invalid_link', 'The link destination contains unsafe characters.', [ 'status' => 400 ] );
            return $value;
        }
        $url = esc_url_raw( $value, [ 'http', 'https' ] );
        if ( '' === $url || ! wp_http_validate_url( $url ) ) return new WP_Error( 'elementize_invalid_link', 'The link destination must be a valid relative path, fragment, mailto, tel, HTTP, or HTTPS destination.', [ 'status' => 400 ] );
        return $url;
    }

    private static function resolve_ref( string $ref, array $aliases ) {
        $ref = trim( $ref );
        if ( '' === $ref || strlen( $ref ) > 160 ) return new WP_Error( 'elementize_creative_ref_invalid', 'Element reference is missing or too long.', [ 'status' => 400 ] );
        if ( false === strpos( $ref, ':' ) ) return $ref;
        [ $alias, $old_id ] = array_pad( explode( ':', $ref, 2 ), 2, '' );
        $alias = sanitize_key( $alias );
        if ( '' === $alias || '' === $old_id || ! isset( $aliases[ $alias ]['map'][ $old_id ] ) ) return new WP_Error( 'elementize_creative_ref_unknown', 'The creative plan references an inserted template element that is not available yet.', [ 'status' => 400, 'element_ref' => $ref ] );
        return (string) $aliases[ $alias ]['map'][ $old_id ];
    }

    private static function required_ref( array $op, string $key, array $aliases, int $index ) {
        if ( ! is_string( $op[ $key ] ?? null ) || '' === trim( $op[ $key ] ) ) return self::operation_error( $index, $key . ' is required.' );
        return self::resolve_ref( (string) $op[ $key ], $aliases );
    }

    private static function valid_path( $path ) {
        if ( ! is_array( $path ) || ! $path || count( $path ) > self::MAX_PATH ) return new WP_Error( 'elementize_creative_path_invalid', 'setting_path must contain 1-' . self::MAX_PATH . ' segments.', [ 'status' => 400 ] );
        $out = [];
        foreach ( $path as $part ) {
            if ( ! is_string( $part ) && ! is_int( $part ) ) return new WP_Error( 'elementize_creative_path_invalid', 'setting_path segments must be strings or integers.', [ 'status' => 400 ] );
            if ( is_string( $part ) && strlen( $part ) > 200 ) return new WP_Error( 'elementize_creative_path_invalid', 'A setting_path segment is too long.', [ 'status' => 400 ] );
            if ( '__globals__' === (string) $part ) return new WP_Error( 'elementize_creative_global_style_read_only', 'Elementor global style references remain read-only.', [ 'status' => 400 ] );
            $out[] = $part;
        }
        return $out;
    }

    private static function same_path( array $a, array $b ): bool {
        return Elementize_Design::canonical_json( array_values( $a ) ) === Elementize_Design::canonical_json( array_values( $b ) );
    }

    private static function operation_error( int $index, string $message, $extra = null ): WP_Error {
        $data = [ 'status' => 400, 'operation_index' => $index ];
        if ( is_array( $extra ) ) $data = array_merge( $data, $extra );
        return new WP_Error( 'elementize_creative_operation_invalid', 'Invalid creative operation at index ' . $index . ': ' . $message, $data );
    }

    private static function public_aliases( array $aliases ): array {
        $out = [];
        foreach ( $aliases as $alias => $data ) $out[ $alias ] = [ 'root_ids' => array_values( (array) ( $data['roots'] ?? [] ) ), 'mapped_element_count' => count( (array) ( $data['map'] ?? [] ) ) ];
        return $out;
    }
}

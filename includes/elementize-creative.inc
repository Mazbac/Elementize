<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Creative {
    private const MAX_OPERATIONS = 50;
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
        $used_ids = Elementize_Tree::reserved_ids( $working );

        foreach ( array_values( $operations ) as $index => $operation ) {
            if ( ! is_array( $operation ) ) return self::operation_error( $index, 'Each creative operation must be an object.' );
            $type = sanitize_key( (string) ( $operation['type'] ?? '' ) );
            if ( ! in_array( $type, [ 'insert_template', 'remove_element', 'duplicate_element', 'move_element', 'reorder_children', 'style', 'content' ], true ) ) return self::operation_error( $index, 'Unsupported creative operation type.' );
            $result = self::apply_operation( $working, $operation, $index, $aliases, $used_ids, $managed, $changes );
            if ( is_wp_error( $result ) ) return $result;
            if ( true !== $result ) return self::operation_error( $index, 'The creative operation could not be applied to the current page state.' );
        }

        self::prune_managed_roots( $working, $managed );
        $tree_check = Elementize_Tree::validate( $working );
        if ( is_wp_error( $tree_check ) ) return $tree_check;
        $after_hash_expected = Elementize_Core::hash_elements( $working );
        if ( hash_equals( $before_hash, $after_hash_expected ) ) return new WP_Error( 'elementize_creative_no_change', 'The creative plan does not change the current Elementor page.', [ 'status' => 400 ] );

        $revision = Elementize_Core::save_revision( $id );
        if ( is_wp_error( $revision ) ) return $revision;
        if ( ! self::store_revision_snapshots( (int) $revision, $before, $managed_before ) ) return new WP_Error( 'elementize_creative_snapshot_failed', 'Elementize could not persist the pre-change creative snapshot, so the page was not modified.', [ 'status' => 500, 'revision_id' => $revision ] );

        // Elementor does not persist the exact array passed to Document::save(). During save it runs the
        // document/save/data filter and normalizes every element through get_data_for_save() before writing
        // _elementor_data. Capture both stages so verification can remain exact without treating legitimate
        // Elementor normalization of newly imported templates as corruption.
        $save_input = null;
        $saved_editor_data = null;
        $capture_save_input = static function( $data, $saving_document ) use ( $id, &$save_input ) {
            if ( ! is_array( $data ) || ! is_object( $saving_document ) || ! method_exists( $saving_document, 'get_main_id' ) ) return $data;
            try { $saving_id = absint( $saving_document->get_main_id() ); } catch ( Throwable $ignored ) { $saving_id = 0; }
            if ( $saving_id === $id && isset( $data['elements'] ) && is_array( $data['elements'] ) ) $save_input = $data['elements'];
            return $data;
        };
        $capture_saved_editor_data = static function( $post_id, $editor_data ) use ( $id, &$saved_editor_data ): void {
            if ( absint( $post_id ) === $id && is_array( $editor_data ) ) $saved_editor_data = $editor_data;
        };
        add_filter( 'elementor/document/save/data', $capture_save_input, PHP_INT_MAX, 2 );
        add_action( 'elementor/editor/after_save', $capture_saved_editor_data, PHP_INT_MAX, 2 );
        try { $saved = $document->save( [ 'elements' => $working ] ); }
        catch ( Throwable $e ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_save_failed', 'Elementor could not save the creative transaction: ' . $e->getMessage(), [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        } finally {
            remove_filter( 'elementor/document/save/data', $capture_save_input, PHP_INT_MAX );
            remove_action( 'elementor/editor/after_save', $capture_saved_editor_data, PHP_INT_MAX );
        }
        if ( ! $saved ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_save_failed', 'Elementor refused to save the creative transaction.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }

        $save_input_hash = is_array( $save_input ) ? Elementize_Core::hash_elements( $save_input ) : '';
        $validated_save_input_hash = Elementize_Core::hash_elements( self::canonical_save_input_elements( $working ) );
        $canonical_save_input_hash = is_array( $save_input ) ? Elementize_Core::hash_elements( self::canonical_save_input_elements( $save_input ) ) : '';
        if ( ! is_array( $save_input ) || ! hash_equals( $validated_save_input_hash, $canonical_save_input_hash ) ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_save_input_changed', 'Another Elementor save filter changed the validated creative transaction before persistence, so Elementize restored the pre-change page.', [
                'status' => 500,
                'revision_id' => $revision,
                'rollback_verified' => $rollback,
                'validated_hash' => $after_hash_expected,
                'save_input_hash' => $save_input_hash,
                'validated_canonical_hash' => $validated_save_input_hash,
                'save_input_canonical_hash' => $canonical_save_input_hash,
            ] );
        }
        if ( ! is_array( $saved_editor_data ) ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_normalization_capture_failed', 'Elementor saved the creative transaction but did not expose the normalized editor data required for exact verification, so Elementize restored the pre-change page.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }

        $persisted = Elementize_Core::persisted_elements( $id );
        $normalized_expected_hash = Elementize_Core::hash_elements( $saved_editor_data );
        $persisted_hash = is_array( $persisted ) ? Elementize_Core::hash_elements( $persisted ) : '';
        $normalized_hash_verified = is_array( $persisted ) && hash_equals( $normalized_expected_hash, $persisted_hash );
        $structure_verified = is_array( $persisted ) && self::same_structure( $working, $persisted );
        $changes_verified = is_array( $persisted ) && self::verify_persisted_changes( $persisted, $changes );
        if ( is_wp_error( $persisted ) || ! $normalized_hash_verified || ! $structure_verified || ! $changes_verified ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            return new WP_Error( 'elementize_creative_verification_failed', 'Persisted Elementor data did not verify against Elementor\'s normalized save result and the validated creative structure, so Elementize restored the pre-change page.', [
                'status' => 500,
                'revision_id' => $revision,
                'rollback_verified' => $rollback,
                'save_input_verified' => true,
                'normalization_capture_verified' => true,
                'normalized_hash_verified' => $normalized_hash_verified,
                'structural_identity_verified' => $structure_verified,
                'change_targets_verified' => $changes_verified,
                'normalized_expected_hash' => $normalized_expected_hash,
                'persisted_hash' => $persisted_hash,
            ] );
        }

        if ( ! self::save_managed_roots( $id, $managed ) ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            self::save_managed_roots( $id, $managed_before );
            return new WP_Error( 'elementize_creative_metadata_failed', 'Elementize could not verify managed-section metadata, so it attempted to restore the pre-change page.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }

        Elementize_Core::regenerate_css( $id );
        clean_post_cache( $id );
        $after_hash = $persisted_hash;
        $activity_id = self::record_activity( $id, (int) $revision, $before_hash, $after_hash, $changes, sanitize_text_field( (string) $request->get_param( 'plan_label' ) ) );
        if ( is_wp_error( $activity_id ) ) {
            $rollback = self::restore_page( $document, $id, $before, $before_hash );
            self::save_managed_roots( $id, $managed_before );
            return new WP_Error( 'elementize_creative_activity_failed', 'Elementize could not persist the creative Activity record, so it attempted to restore the pre-change page.', [ 'status' => 500, 'revision_id' => $revision, 'rollback_verified' => $rollback ] );
        }

        return new WP_REST_Response( [
            'saved' => true,
            'creative_transaction' => true,
            'page' => Elementize_Core::page_identity( $id ),
            'revision_id' => (int) $revision,
            'activity_id' => $activity_id,
            'previous_content_hash' => $before_hash,
            'content_hash' => $after_hash,
            'persisted_verification' => true,
            'save_input_verification' => true,
            'normalization_verified' => true,
            'structural_identity_verified' => true,
            'change_target_verification' => true,
            'managed_root_verification' => true,
            'capability_revision' => $capability_revision,
            'operation_count' => count( $operations ),
            'changes' => $changes,
            'aliases' => self::public_aliases( $aliases ),
            'managed_root_ids' => array_values( array_keys( $managed ) ),
            'quality' => self::quality_summary( $persisted, $managed ),
            'visual_render_verified' => false,
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
        $nodes = Elementize_Tree::clone_with_fresh_ids( $template['elements'], $used_ids, $map );
        if ( ! $nodes ) return self::operation_error( $index, 'The selected template produced no insertable roots.' );
        $position = sanitize_key( (string) ( $op['position'] ?? 'after' ) );
        $anchor_ref = (string) ( $op['anchor_ref'] ?? '' );
        $anchor_id = '' !== $anchor_ref ? self::resolve_ref( $anchor_ref, $aliases ) : '';
        if ( is_wp_error( $anchor_id ) ) return $anchor_id;
        $inserted = Elementize_Tree::insert_nodes( $elements, $anchor_id, $nodes, $position );
        if ( is_wp_error( $inserted ) ) return self::operation_error( $index, $inserted->get_error_message(), $inserted->get_error_data() );
        if ( true !== $inserted ) return self::operation_error( $index, 'The insertion anchor no longer exists. Refresh structure and rebuild the plan.' );

        $roots = array_values( array_filter( array_map( static fn( $node ) => is_array( $node ) ? (string) ( $node['id'] ?? '' ) : '', $nodes ) ) );
        $aliases[ $alias ] = [ 'map' => $map, 'roots' => $roots, 'direct' => 1 === count( $roots ) ? $roots[0] : '' ];
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
        if ( ! Elementize_Tree::remove_node( $elements, $id, $removed, $parent, $position ) || ! is_array( $removed ) ) return self::operation_error( $index, 'The element to remove no longer exists.' );
        $removed_ids = Elementize_Tree::element_ids( [ $removed ] );
        foreach ( array_keys( $managed ) as $root_id ) if ( isset( $removed_ids[ $root_id ] ) ) unset( $managed[ $root_id ] );
        $changes[] = [ 'kind' => 'remove_element', 'element_id' => $id, 'setting_path' => [], 'parent_id' => $parent, 'index' => $position ];
        return true;
    }

    private static function op_duplicate( array &$elements, array $op, int $index, array &$aliases, array &$used_ids, array &$managed, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $node = Elementize_Tree::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The element to duplicate no longer exists.' );
        $map = [];
        $clones = Elementize_Tree::clone_with_fresh_ids( [ $node ], $used_ids, $map );
        $clone = $clones[0] ?? null;
        if ( ! is_array( $clone ) ) return self::operation_error( $index, 'Element duplication failed.' );
        $position = sanitize_key( (string) ( $op['position'] ?? 'after' ) );
        if ( ! in_array( $position, [ 'before', 'after' ], true ) ) return self::operation_error( $index, 'duplicate_element position must be before or after.' );
        $inserted = Elementize_Tree::insert_nodes( $elements, $id, [ $clone ], $position );
        if ( is_wp_error( $inserted ) ) return self::operation_error( $index, $inserted->get_error_message() );
        if ( true !== $inserted ) return self::operation_error( $index, 'The element to duplicate no longer exists at its expected location.' );
        $new_id = (string) ( $clone['id'] ?? '' );
        if ( isset( $managed[ $id ] ) && '' !== $new_id ) $managed[ $new_id ] = array_merge( $managed[ $id ], [ 'source_type' => 'duplicate', 'created_gmt' => gmdate( 'Y-m-d H:i:s' ) ] );
        $alias = sanitize_key( (string) ( $op['alias'] ?? '' ) );
        if ( '' !== $alias ) {
            if ( strlen( $alias ) > 60 || isset( $aliases[ $alias ] ) ) return self::operation_error( $index, 'duplicate_element alias must be unique and at most 60 characters.' );
            $aliases[ $alias ] = [ 'map' => $map, 'roots' => [ $new_id ], 'direct' => $new_id ];
        }
        $changes[] = [ 'kind' => 'duplicate_element', 'element_id' => $id, 'setting_path' => [], 'new_element_id' => $new_id, 'alias' => $alias ];
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
        $node = Elementize_Tree::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The element to move no longer exists.' );
        $descendants = Elementize_Tree::element_ids( [ $node ] );
        if ( '' !== $anchor_id && isset( $descendants[ $anchor_id ] ) ) return self::operation_error( $index, 'An element cannot be moved inside or relative to its own descendant.' );
        $removed = null; $old_parent = ''; $old_index = -1;
        if ( ! Elementize_Tree::remove_node( $elements, $id, $removed, $old_parent, $old_index ) || ! is_array( $removed ) ) return self::operation_error( $index, 'The element could not be detached for moving.' );
        $inserted = Elementize_Tree::insert_nodes( $elements, $anchor_id, [ $removed ], $position );
        if ( is_wp_error( $inserted ) ) return self::operation_error( $index, $inserted->get_error_message() );
        if ( true !== $inserted ) return self::operation_error( $index, 'The move anchor no longer exists. Refresh structure and rebuild the plan.' );
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
        $result = Elementize_Tree::reorder_children( $elements, $parent_id, $ids );
        if ( is_wp_error( $result ) ) return self::operation_error( $index, $result->get_error_message() );
        if ( true !== $result ) return self::operation_error( $index, 'The reorder parent no longer exists.' );
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

        $node = Elementize_Tree::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The design target no longer exists.' );
        $widget = sanitize_key( (string) ( $node['widgetType'] ?? '' ) );
        if ( class_exists( 'Elementize_Pixfort_Repeater_Colors' ) ) {
            $special = Elementize_Pixfort_Repeater_Colors::apply_style( $elements, $id, $path, $expected, $new, $changes );
            if ( is_wp_error( $special ) ) return $special;
            if ( is_array( $special ) ) return true;
        }
        $old = null;
        $result = Elementize_Tree::mutate_setting( $elements, $id, $path, static function( &$cursor ) use ( $expected, $new, $path, $widget, &$old ) {
            if ( Elementize_Design::canonical_json( $cursor ) !== Elementize_Design::canonical_json( $expected ) ) return new WP_Error( 'elementize_design_value_changed', 'The design setting changed after the creative read.', [ 'status' => 409 ] );
            $category = Elementize_Design::classify_control( $path, $cursor, $widget );
            if ( false === $category ) return new WP_Error( 'elementize_design_control_invalid', 'This setting is not an approved creative design control for the exact target widget.', [ 'status' => 400 ] );

            if ( str_starts_with( $category, 'pixfort_theme_color:' ) ) {
                $setting_key = sanitize_key( substr( $category, strlen( 'pixfort_theme_color:' ) ) );
                if ( '' === $widget || '' === $setting_key || ! class_exists( 'Elementize_Pixfort_Theme_Tokens' ) ) {
                    return new WP_Error( 'elementize_pixfort_theme_token_unavailable', 'Pixfort semantic theme-color validation is unavailable for the exact target widget.', [ 'status' => 400 ] );
                }
                $validated = Elementize_Pixfort_Theme_Tokens::validate_transition( $widget, $setting_key, $cursor, $new );
            } else {
                $validated = Elementize_Design::validate_control_value( $category, $new, $cursor );
            }
            if ( is_wp_error( $validated ) ) return $validated;
            $old = $cursor;
            $cursor = $validated;
            return [ 'category' => $category, 'new' => $validated ];
        } );
        if ( is_wp_error( $result ) ) return $result;
        if ( ! is_array( $result ) ) return self::operation_error( $index, 'The design target no longer exists.' );
        $kind_category = sanitize_key( str_replace( ':', '_', (string) $result['category'] ) );
        $changes[] = [ 'kind' => 'design_' . $kind_category, 'element_id' => $id, 'setting_path' => $path, 'old_value' => $old, 'new_value' => $result['new'] ];
        return true;
    }

    private static function op_content( array &$elements, array $op, int $index, array $aliases, array &$changes ) {
        $id = self::required_ref( $op, 'element_ref', $aliases, $index );
        if ( is_wp_error( $id ) ) return $id;
        $kind = sanitize_key( (string) ( $op['kind'] ?? '' ) );
        if ( ! in_array( $kind, [ 'text', 'link', 'media', 'pixfort_icon' ], true ) ) return self::operation_error( $index, 'content kind must be text, link, media, or pixfort_icon.' );
        $path = self::valid_path( $op['setting_path'] ?? null );
        if ( is_wp_error( $path ) ) return self::operation_error( $index, $path->get_error_message() );
        $node = Elementize_Tree::find_node( $elements, $id );
        if ( ! is_array( $node ) ) return self::operation_error( $index, 'The content element no longer exists.' );
        $eligible = false;
        foreach ( Elementize_Content::discover_items( [ $node ] ) as $item ) {
            if ( (string) $item['kind'] === $kind && (string) $item['element_id'] === $id && self::same_path( (array) $item['setting_path'], $path ) ) { $eligible = true; break; }
        }
        if ( ! $eligible ) return self::operation_error( $index, 'The requested content setting is not a currently writable Elementize content field.' );

        $old = null; $new = null;
        $result = Elementize_Tree::mutate_setting( $elements, $id, $path, static function( &$cursor ) use ( $op, $kind, &$old, &$new ) {
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

    private static function same_structure( array $expected, array $persisted ): bool {
        return Elementize_Design::canonical_json( self::structure_identity( $expected ) ) === Elementize_Design::canonical_json( self::structure_identity( $persisted ) );
    }

    private static function structure_identity( array $elements ): array {
        $out = [];
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $out[] = [
                'id' => (string) ( $element['id'] ?? '' ),
                'elType' => (string) ( $element['elType'] ?? '' ),
                'widgetType' => (string) ( $element['widgetType'] ?? '' ),
                'elements' => is_array( $element['elements'] ?? null ) ? self::structure_identity( $element['elements'] ) : [],
            ];
        }
        return $out;
    }

    private static function canonical_save_input_elements( array $elements ): array {
        $out = [];
        foreach ( $elements as $node ) {
            if ( ! is_array( $node ) ) { $out[] = $node; continue; }
            $copy = $node;
            if ( 'global' === (string) ( $copy['widgetType'] ?? '' ) && 'global' === (string) ( $copy['originalWidgetType'] ?? '' ) ) {
                unset( $copy['originalWidgetType'] );
            }
            if ( is_array( $copy['elements'] ?? null ) ) $copy['elements'] = self::canonical_save_input_elements( $copy['elements'] );
            $out[] = $copy;
        }
        return $out;
    }
    private static function verify_persisted_changes( array $persisted, array $changes ): bool {
        foreach ( $changes as $change ) {
            if ( ! is_array( $change ) ) return false;
            $kind = sanitize_key( (string) ( $change['kind'] ?? '' ) );
            if ( in_array( $kind, [ 'insert_template', 'remove_element', 'duplicate_element', 'move_element', 'reorder_children' ], true ) ) continue;
            if ( ! str_starts_with( $kind, 'design_' ) && ! in_array( $kind, [ 'text', 'link', 'media', 'pixfort_icon' ], true ) ) continue;
            $id = (string) ( $change['element_id'] ?? '' );
            $node = '' !== $id ? Elementize_Tree::find_node( $persisted, $id ) : null;
            if ( ! is_array( $node ) || ! is_array( $node['settings'] ?? null ) ) return false;
            $found = false;
            $value = self::setting_value( $node['settings'], (array) ( $change['setting_path'] ?? [] ), $found );
            if ( ! $found || Elementize_Design::canonical_json( $value ) !== Elementize_Design::canonical_json( $change['new_value'] ?? null ) ) return false;
        }
        return true;
    }

    private static function setting_value( array $settings, array $path, bool &$found ) {
        $found = false;
        if ( ! $path ) return null;
        $cursor = $settings;
        foreach ( $path as $part ) {
            if ( ! is_array( $cursor ) || ! array_key_exists( $part, $cursor ) ) return null;
            $cursor = $cursor[ $part ];
        }
        $found = true;
        return $cursor;
    }

    private static function managed_roots( int $page_id ): array {
        $value = get_post_meta( $page_id, self::MANAGED_META, true );
        return is_array( $value ) ? $value : [];
    }

    private static function prune_managed_roots( array $elements, array &$managed ): void {
        $ids = Elementize_Tree::element_ids( $elements );
        foreach ( array_keys( $managed ) as $root_id ) if ( ! isset( $ids[ $root_id ] ) ) unset( $managed[ $root_id ] );
    }

    private static function save_managed_roots( int $page_id, array $managed ): bool {
        if ( $managed ) update_post_meta( $page_id, self::MANAGED_META, $managed );
        else delete_post_meta( $page_id, self::MANAGED_META );
        $persisted = get_post_meta( $page_id, self::MANAGED_META, true );
        $persisted = is_array( $persisted ) ? $persisted : [];
        return Elementize_Design::canonical_json( $persisted ) === Elementize_Design::canonical_json( $managed );
    }

    private static function store_revision_snapshots( int $revision_id, array $elements, array $managed ): bool {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        $managed_json = wp_json_encode( $managed, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $json ) || '' === $json || ! is_string( $managed_json ) ) return false;
        $snapshot = base64_encode( $json );
        $managed_snapshot = base64_encode( $managed_json );
        update_metadata( 'post', $revision_id, self::ACTIVITY_SNAPSHOT_META, $snapshot );
        update_metadata( 'post', $revision_id, self::MANAGED_SNAPSHOT_META, $managed_snapshot );
        return hash_equals( $snapshot, (string) get_metadata( 'post', $revision_id, self::ACTIVITY_SNAPSHOT_META, true ) )
            && hash_equals( $managed_snapshot, (string) get_metadata( 'post', $revision_id, self::MANAGED_SNAPSHOT_META, true ) );
    }

    private static function restore_page( $document, int $id, array $before, string $before_hash ): bool {
        try { $saved = $document->save( [ 'elements' => $before ] ); } catch ( Throwable $e ) { return false; }
        if ( ! $saved ) return false;
        $persisted = Elementize_Core::persisted_elements( $id );
        $ok = is_array( $persisted ) && hash_equals( $before_hash, Elementize_Core::hash_elements( $persisted ) );
        if ( $ok ) { Elementize_Core::regenerate_css( $id ); clean_post_cache( $id ); }
        return $ok;
    }

    private static function record_activity( int $page_id, int $revision_id, string $before_hash, string $after_hash, array $changes, string $label ) {
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
            'changes' => self::activity_changes( $changes ),
            'creative' => true,
            'plan_label' => $label,
            'undone_at_gmt' => '',
            'undo_revision_id' => 0,
        ] );
        $records = array_slice( $records, 0, 100 );
        update_option( self::ACTIVITY_LOG, $records, false );
        $verify = get_option( self::ACTIVITY_LOG, [] );
        if ( ! is_array( $verify ) || ! isset( $verify[0]['id'] ) || (string) $verify[0]['id'] !== $activity_id ) return new WP_Error( 'elementize_activity_persist_failed', 'Creative Activity history could not be persisted.', [ 'status' => 500 ] );
        return $activity_id;
    }

    private static function activity_changes( array $changes ): array {
        $out = [];
        foreach ( array_slice( $changes, 0, 50 ) as $change ) {
            if ( ! is_array( $change ) ) continue;
            $entry = [
                'kind' => sanitize_key( (string) ( $change['kind'] ?? '' ) ),
                'element_id' => sanitize_text_field( (string) ( $change['element_id'] ?? '' ) ),
                'setting_path' => array_values( array_map( static fn( $part ) => is_int( $part ) ? $part : sanitize_text_field( (string) $part ), (array) ( $change['setting_path'] ?? [] ) ) ),
            ];
            foreach ( [ 'new_element_id', 'alias', 'source_id', 'template_id', 'position', 'anchor_id', 'parent_id' ] as $key ) if ( isset( $change[ $key ] ) && is_scalar( $change[ $key ] ) ) $entry[ $key ] = sanitize_text_field( (string) $change[ $key ] );
            $out[] = $entry;
        }
        return $out;
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
        if ( count( (array) ( $profile['palette'] ?? [] ) ) > 12 ) $warnings[] = 'The page uses a broad color palette; prefer its most frequent colors in future creative passes.';
        if ( (int) ( $tree['max_depth'] ?? 0 ) > 60 ) $warnings[] = 'The page has deep nesting; avoid adding more wrapper containers.';
        if ( (int) ( $tree['element_count'] ?? 0 ) > 1500 ) $warnings[] = 'The page is structurally large; keep future creative transactions focused.';
        $warnings = array_merge( $warnings, self::contract_warnings( $elements, $managed ) );
        return [
            'structural_validity' => true,
            'element_count' => (int) ( $tree['element_count'] ?? 0 ),
            'max_depth' => (int) ( $tree['max_depth'] ?? 0 ),
            'managed_root_count' => count( $managed ),
            'observed_palette_tokens' => array_slice( (array) ( $profile['palette'] ?? [] ), 0, 8 ),
            'observed_spacing_tokens' => array_slice( (array) ( $profile['spacing_scale'] ?? [] ), 0, 8 ),
            'warnings' => array_values( array_unique( $warnings ) ),
            'visual_render_verified' => false,
        ];
    }

    private static function contract_warnings( array $elements, array $managed ): array {
        $warnings = [];
        foreach ( $managed as $root_id => $meta ) {
            $node = Elementize_Tree::find_node( $elements, (string) $root_id );
            if ( ! is_array( $node ) ) continue;
            $contract = sanitize_key( (string) ( $meta['contract'] ?? 'generic' ) );
            $summary = Elementize_Templates::structure_summary( [ $node ] );
            $widgets = (array) ( $summary['widgets'] ?? [] );
            $buttons = 0;
            foreach ( $widgets as $widget => $count ) if ( false !== strpos( strtolower( (string) $widget ), 'button' ) ) $buttons += (int) $count;
            if ( 'feature_grid' === $contract && $buttons > 1 ) $warnings[] = 'A managed feature-grid section contains multiple button widgets; keep calls-to-action simple and consistent.';
            if ( 'cta' === $contract && ( $buttons < 1 || $buttons > 2 ) ) $warnings[] = 'A managed CTA section should normally contain one primary CTA and at most one secondary CTA.';
            if ( in_array( $contract, [ 'feature_grid', 'testimonial_grid' ], true ) && (int) ( $summary['element_count'] ?? 0 ) > 180 ) $warnings[] = 'A managed repeated-content section is unusually complex; consider simplifying it.';
        }
        return $warnings;
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
        if ( false === strpos( $ref, ':' ) ) {
            if ( isset( $aliases[ $ref ]['direct'] ) && '' !== (string) $aliases[ $ref ]['direct'] ) return (string) $aliases[ $ref ]['direct'];
            return $ref;
        }
        [ $alias, $old_id ] = array_pad( explode( ':', $ref, 2 ), 2, '' );
        $alias = sanitize_key( $alias );
        if ( '' === $alias || '' === $old_id || ! isset( $aliases[ $alias ]['map'][ $old_id ] ) ) return new WP_Error( 'elementize_creative_ref_unknown', 'The creative plan references an inserted or duplicated element that is not available yet.', [ 'status' => 400, 'element_ref' => $ref ] );
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
        foreach ( $aliases as $alias => $data ) $out[ $alias ] = [ 'root_ids' => array_values( (array) ( $data['roots'] ?? [] ) ), 'direct_element_id' => (string) ( $data['direct'] ?? '' ), 'mapped_element_count' => count( (array) ( $data['map'] ?? [] ) ) ];
        return $out;
    }
}

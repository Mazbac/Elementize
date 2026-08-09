<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Embedded_Documents {
    private const VERSION = '0.5.12';
    private const NS = 'elementize/v1';
    private const MAX_DEPTH = 6;
    private const MAX_NODES = 100;
    private const MAX_TEXT = 20000;
    private const MAX_UPDATES = 100;
    private const META_MANAGED_PAGE = '_elementize_managed';
    private const META_EMBEDDED_MANAGED = '_elementize_embedded_managed';
    private const META_EMBEDDED_OWNER = '_elementize_embedded_owner_page';
    private const META_EMBEDDED_SOURCE = '_elementize_embedded_source_id';
    private const META_EMBEDDED_CREATED = '_elementize_embedded_created_at_gmt';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 70 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_callbacks' ], 90, 3 );
    }

    public static function routes(): void {
        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/embedded-documents', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'audit' ],
            'permission_callback' => [ self::class, 'can_read' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
            ],
        ] );

        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/embedded-documents/clone-relink', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'clone_relink' ],
            'permission_callback' => [ self::class, 'can_write' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'expected_status' => [ 'type' => 'string', 'required' => true ],
                'expected_title' => [ 'type' => 'string', 'required' => true ],
                'content_hash' => [ 'type' => 'string', 'required' => true ],
                'expected_documents' => [ 'type' => 'array', 'required' => true ],
                'confirm_clone_and_relink' => [ 'type' => 'boolean', 'required' => true ],
            ],
        ] );

        register_rest_route( self::NS, '/pages/(?P<id>\\d+)/embedded-text', [
            [
                'methods' => WP_REST_Server::READABLE,
                'callback' => [ self::class, 'embedded_text' ],
                'permission_callback' => [ self::class, 'can_read' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                ],
            ],
            [
                'methods' => WP_REST_Server::EDITABLE,
                'callback' => [ self::class, 'update_embedded_text' ],
                'permission_callback' => [ self::class, 'can_write' ],
                'args' => [
                    'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                    'expected_status' => [ 'type' => 'string', 'required' => true ],
                    'expected_title' => [ 'type' => 'string', 'required' => true ],
                    'page_content_hash' => [ 'type' => 'string', 'required' => true ],
                    'updates' => [ 'type' => 'array', 'required' => true ],
                    'confirm_embedded_text_write' => [ 'type' => 'boolean', 'required' => true ],
                ],
            ],
        ] );
    }

    public static function can_read( WP_REST_Request $request ) {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_embedded_documents_forbidden', 'Embedded document access requires an administrator.', [ 'status' => 403 ] );
        $id = absint( $request['id'] );
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $id ) ) return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot inspect this page.', [ 'status' => 403 ] );
        return true;
    }

    public static function can_write( WP_REST_Request $request ) {
        $read = self::can_read( $request );
        if ( is_wp_error( $read ) ) return $read;
        $id = absint( $request['id'] );
        if ( '1' !== (string) get_post_meta( $id, self::META_MANAGED_PAGE, true ) ) {
            return new WP_Error( 'elementize_unmanaged_page', 'Embedded document writes are allowed only for Elementize-managed pages.', [ 'status' => 409 ] );
        }
        $post = get_post( $id );
        if ( ! $post || 'draft' !== $post->post_status ) {
            return new WP_Error( 'elementize_embedded_write_draft_only', 'Embedded document writes are allowed only while the managed page is a draft.', [ 'status' => 409, 'status_now' => $post ? $post->post_status : '' ] );
        }
        return true;
    }

    public static function audit( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $query = trim( (string) $request->get_param( 'q' ) );
        $queue = [ [ 'post_id' => $page_id, 'depth' => 0 ] ];
        $seen = [ $page_id => true ];
        $edges = [];
        $documents = [];

        while ( $queue && count( $seen ) < self::MAX_NODES ) {
            $current = array_shift( $queue );
            $source_id = absint( $current['post_id'] );
            $depth = absint( $current['depth'] );
            if ( $depth >= self::MAX_DEPTH ) continue;

            $decoded = self::elementor_data_array( $source_id );
            if ( ! is_array( $decoded ) ) continue;
            $references = [];
            self::collect_document_references( $decoded, [], $references );

            foreach ( $references as $reference ) {
                $target_id = absint( $reference['post_id'] );
                if ( ! $target_id || $target_id === $source_id ) continue;
                $key = $source_id . ':' . $target_id . ':' . $reference['path'];
                $edges[ $key ] = [
                    'source_post_id' => $source_id,
                    'target_post_id' => $target_id,
                    'path' => $reference['path'],
                    'value' => $reference['value'],
                    'depth' => $depth + 1,
                ];
                if ( ! isset( $seen[ $target_id ] ) && count( $seen ) < self::MAX_NODES ) {
                    $seen[ $target_id ] = true;
                    $queue[] = [ 'post_id' => $target_id, 'depth' => $depth + 1 ];
                }
            }
        }

        $graph_ids = array_map( 'absint', array_keys( $seen ) );
        $embedded_ids = array_values( array_filter( $graph_ids, static fn( $id ) => $id !== $page_id ) );

        foreach ( $embedded_ids as $embedded_id ) {
            $post = get_post( $embedded_id );
            if ( ! $post ) continue;
            $elementor_raw = self::elementor_data_raw( $embedded_id );
            $post_content = (string) $post->post_content;
            $reverse = self::reverse_references( $embedded_id );
            $graph_references = [];
            $historical_references = [];
            $independent_external_references = [];

            foreach ( $reverse as $reference ) {
                $source_id = absint( $reference['source_post_id'] ?? 0 );
                if ( in_array( $source_id, $graph_ids, true ) ) {
                    $graph_references[] = $reference;
                    continue;
                }
                if ( ! empty( $reference['is_revision'] ) || ! empty( $reference['is_autosave'] ) ) {
                    $historical_references[] = $reference;
                    continue;
                }
                $independent_external_references[] = $reference;
            }

            $owner = absint( get_post_meta( $embedded_id, self::META_EMBEDDED_OWNER, true ) );
            $managed_clone = '1' === (string) get_post_meta( $embedded_id, self::META_EMBEDDED_MANAGED, true ) && $owner === $page_id;
            $documents[] = [
                'post_id' => $embedded_id,
                'post_type' => (string) $post->post_type,
                'post_status' => (string) $post->post_status,
                'post_title' => (string) $post->post_title,
                'elementor_edit_mode' => (string) get_post_meta( $embedded_id, '_elementor_edit_mode', true ),
                'elementor_template_type' => (string) get_post_meta( $embedded_id, '_elementor_template_type', true ),
                'elementor_data_present' => '' !== $elementor_raw,
                'elementor_data_length' => strlen( $elementor_raw ),
                'elementor_data_hash' => '' !== $elementor_raw ? hash( 'sha256', $elementor_raw ) : '',
                'query_match_counts' => [
                    'post_content' => self::count_query( $post_content, $query ),
                    'elementor_data' => self::count_query( $elementor_raw, $query ),
                ],
                'reverse_reference_count' => count( $reverse ),
                'current_graph_reference_count' => count( $graph_references ),
                'historical_revision_reference_count' => count( $historical_references ),
                'independent_external_reference_count' => count( $independent_external_references ),
                'exclusive_to_current_page' => 0 === count( $independent_external_references ),
                'elementize_managed_clone' => $managed_clone,
                'elementize_owner_page_id' => $owner,
                'elementize_source_template_id' => absint( get_post_meta( $embedded_id, self::META_EMBEDDED_SOURCE, true ) ),
                'recommended_write_strategy' => $managed_clone ? 'write_owned_clone' : 'clone_and_relink',
                'reverse_references' => $reverse,
                'current_graph_references' => $graph_references,
                'historical_revision_references' => $historical_references,
                'independent_external_references' => $independent_external_references,
            ];
        }

        return new WP_REST_Response( [
            'page_id' => $page_id,
            'page_title' => (string) get_the_title( $page_id ),
            'page_status' => (string) get_post_status( $page_id ),
            'query' => $query,
            'embedded_document_count' => count( $documents ),
            'embedded_documents' => $documents,
            'reference_edge_count' => count( $edges ),
            'reference_edges' => array_values( $edges ),
            'graph_post_ids' => $graph_ids,
            'revision_references_are_historical_only' => true,
            'direct_template_mutation_recommended' => false,
            'recommended_write_strategy' => 'clone_and_relink',
            'max_depth' => self::MAX_DEPTH,
            'read_only' => true,
        ], 200 );
    }

    public static function clone_relink( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $guard = self::guard_page_write( $request, 'content_hash', 'confirm_clone_and_relink' );
        if ( is_wp_error( $guard ) ) return $guard;
        $page_doc = $guard['document'];
        $page_elements = $guard['elements'];
        $original_page_elements = $page_elements;

        $refs = [];
        self::collect_direct_pixfort_references( $page_elements, [], $refs );
        if ( ! $refs ) {
            return new WP_REST_Response( [
                'saved' => false,
                'page_id' => $page_id,
                'already_isolated' => true,
                'cloned_count' => 0,
                'content_hash' => self::hash_elements( $page_elements ),
                'message' => 'No direct Pixfort template references require clone-and-relink.',
            ], 200 );
        }

        $source_ids = array_values( array_unique( array_map( static fn( $r ) => absint( $r['post_id'] ), $refs ) ) );
        $expected = $request->get_param( 'expected_documents' );
        $expected_map = self::validate_expected_documents( $expected );
        if ( is_wp_error( $expected_map ) ) return $expected_map;
        $expected_ids = array_map( 'absint', array_keys( $expected_map ) );
        sort( $source_ids );
        sort( $expected_ids );
        if ( $source_ids !== $expected_ids ) {
            return new WP_Error( 'elementize_embedded_document_set_changed', 'The page embedded-document set changed after it was read. Read embedded documents again before clone-and-relink.', [
                'status' => 409,
                'embedded_document_ids_now' => $source_ids,
            ] );
        }

        foreach ( $source_ids as $source_id ) {
            $post = get_post( $source_id );
            if ( ! $post || 'pixfort_template' !== $post->post_type ) {
                return new WP_Error( 'elementize_embedded_source_invalid', 'A referenced Pixfort template no longer exists.', [ 'status' => 409, 'post_id' => $source_id ] );
            }
            if ( '1' === (string) get_post_meta( $source_id, self::META_EMBEDDED_MANAGED, true ) && absint( get_post_meta( $source_id, self::META_EMBEDDED_OWNER, true ) ) === $page_id ) {
                return new WP_Error( 'elementize_embedded_already_isolated', 'This page already references an Elementize-owned embedded clone. Read embedded documents again before continuing.', [ 'status' => 409, 'post_id' => $source_id ] );
            }
            $raw = self::elementor_data_raw( $source_id );
            $hash = '' !== $raw ? hash( 'sha256', $raw ) : '';
            $expected_hash = (string) ( $expected_map[ $source_id ]['elementor_data_hash'] ?? '' );
            if ( '' === $expected_hash || '' === $hash || ! hash_equals( $hash, $expected_hash ) ) {
                return new WP_Error( 'elementize_embedded_source_changed', 'An embedded Pixfort template changed after it was read. Read embedded documents again before clone-and-relink.', [
                    'status' => 409,
                    'post_id' => $source_id,
                    'elementor_data_hash' => $hash,
                ] );
            }
            if ( isset( $expected_map[ $source_id ]['post_status'] ) && (string) $expected_map[ $source_id ]['post_status'] !== (string) $post->post_status ) {
                return new WP_Error( 'elementize_embedded_source_status_changed', 'An embedded Pixfort template status changed after it was read.', [ 'status' => 409, 'post_id' => $source_id, 'status_now' => $post->post_status ] );
            }
            if ( isset( $expected_map[ $source_id ]['post_title'] ) && (string) $expected_map[ $source_id ]['post_title'] !== (string) $post->post_title ) {
                return new WP_Error( 'elementize_embedded_source_title_changed', 'An embedded Pixfort template title changed after it was read.', [ 'status' => 409, 'post_id' => $source_id, 'title_now' => $post->post_title ] );
            }
        }

        $created = [];
        $mapping = [];
        $source_hashes = [];
        foreach ( $source_ids as $source_id ) $source_hashes[ $source_id ] = hash( 'sha256', self::elementor_data_raw( $source_id ) );

        foreach ( $source_ids as $source_id ) {
            $clone_id = self::clone_template_for_page( $source_id, $page_id );
            if ( is_wp_error( $clone_id ) ) {
                self::delete_temporary_clones( $created );
                return $clone_id;
            }
            $created[] = $clone_id;
            $mapping[ $source_id ] = $clone_id;
        }

        foreach ( $refs as $ref ) {
            $source_id = absint( $ref['post_id'] );
            if ( empty( $mapping[ $source_id ] ) || ! self::set_path_value( $page_elements, $ref['path_segments'], $mapping[ $source_id ], $source_id ) ) {
                self::delete_temporary_clones( $created );
                return new WP_Error( 'elementize_embedded_relink_prepare_failed', 'Elementize could not safely prepare the page reference relink. No page changes were saved.', [ 'status' => 500, 'source_post_id' => $source_id ] );
            }
        }

        $revision = self::save_revision( $page_id );
        if ( is_wp_error( $revision ) ) {
            self::delete_temporary_clones( $created );
            return $revision;
        }

        try {
            $saved = $page_doc->save( [ 'elements' => $page_elements ] );
        } catch ( Throwable $ex ) {
            self::delete_temporary_clones( $created );
            return new WP_Error( 'elementize_embedded_relink_save_failed', 'Elementor could not save the cloned template references: ' . $ex->getMessage(), [ 'status' => 500 ] );
        }
        if ( ! $saved ) {
            self::delete_temporary_clones( $created );
            return new WP_Error( 'elementize_embedded_relink_save_failed', 'Elementor refused to save the cloned template references.', [ 'status' => 500 ] );
        }

        $fresh_doc = self::doc( $page_id );
        $fresh_elements = $fresh_doc ? $fresh_doc->get_elements_data() : null;
        $verify_refs = [];
        if ( is_array( $fresh_elements ) ) self::collect_direct_pixfort_references( $fresh_elements, [], $verify_refs );
        $verify_ids = array_values( array_unique( array_map( static fn( $r ) => absint( $r['post_id'] ), $verify_refs ) ) );
        $clone_ids = array_values( array_map( 'absint', $mapping ) );
        sort( $verify_ids );
        sort( $clone_ids );

        $sources_unchanged = true;
        foreach ( $source_hashes as $source_id => $source_hash ) {
            if ( ! hash_equals( $source_hash, hash( 'sha256', self::elementor_data_raw( $source_id ) ) ) ) {
                $sources_unchanged = false;
                break;
            }
        }

        if ( ! is_array( $fresh_elements ) || $verify_ids !== $clone_ids || ! $sources_unchanged ) {
            try { $page_doc->save( [ 'elements' => $original_page_elements ] ); } catch ( Throwable $ignored ) {}
            self::delete_temporary_clones( $created );
            return new WP_Error( 'elementize_embedded_relink_verification_failed', 'Clone-and-relink could not be verified, so Elementize attempted to restore the original page references and removed the temporary clones.', [ 'status' => 500 ] );
        }

        foreach ( $clone_ids as $clone_id ) self::regenerate_css( $clone_id );
        self::regenerate_css( $page_id );

        $mappings = [];
        foreach ( $mapping as $source_id => $clone_id ) {
            $clone = get_post( $clone_id );
            $clone_doc = self::doc( $clone_id );
            $clone_elements = $clone_doc ? $clone_doc->get_elements_data() : null;
            $mappings[] = [
                'source_post_id' => absint( $source_id ),
                'source_post_title' => (string) get_the_title( $source_id ),
                'clone_post_id' => absint( $clone_id ),
                'clone_post_title' => $clone ? (string) $clone->post_title : '',
                'clone_status' => $clone ? (string) $clone->post_status : '',
                'clone_owned_by_page' => absint( get_post_meta( $clone_id, self::META_EMBEDDED_OWNER, true ) ) === $page_id,
                'clone_elementor_content_hash' => is_array( $clone_elements ) ? self::hash_elements( $clone_elements ) : '',
            ];
        }

        return new WP_REST_Response( [
            'saved' => true,
            'page_id' => $page_id,
            'revision_id' => $revision,
            'cloned_count' => count( $mappings ),
            'mappings' => $mappings,
            'original_templates_modified' => false,
            'original_templates_verified_unchanged' => true,
            'direct_template_mutation' => false,
            'page_content_hash' => self::hash_elements( $fresh_elements ),
            'next_step' => 'Read /embedded-text and edit only the Elementize-owned clones.',
        ], 200 );
    }

    public static function embedded_text( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $page_doc = self::doc( $page_id );
        if ( ! $page_doc || ! $page_doc->is_built_with_elementor() ) return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        $page_elements = $page_doc->get_elements_data();
        if ( ! is_array( $page_elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );

        $refs = [];
        self::collect_direct_pixfort_references( $page_elements, [], $refs );
        $ids = array_values( array_unique( array_map( static fn( $r ) => absint( $r['post_id'] ), $refs ) ) );
        $documents = [];
        $all_writable = true;
        $total_items = 0;

        foreach ( $ids as $document_id ) {
            $post = get_post( $document_id );
            $doc = self::doc( $document_id );
            if ( ! $post || ! $doc || ! $doc->is_built_with_elementor() ) continue;
            $elements = $doc->get_elements_data();
            if ( ! is_array( $elements ) ) continue;
            $owner = absint( get_post_meta( $document_id, self::META_EMBEDDED_OWNER, true ) );
            $writable = '1' === (string) get_post_meta( $document_id, self::META_EMBEDDED_MANAGED, true ) && $owner === $page_id;
            if ( ! $writable ) $all_writable = false;
            $items = [];
            self::collect_text( $elements, $items );
            foreach ( $items as &$item ) {
                $item['document_id'] = $document_id;
                $item['document_writable'] = $writable;
            }
            unset( $item );
            $total_items += count( $items );
            $documents[] = [
                'document_id' => $document_id,
                'post_type' => (string) $post->post_type,
                'status' => (string) $post->post_status,
                'title' => (string) $post->post_title,
                'elementize_managed_clone' => $writable,
                'owner_page_id' => $owner,
                'source_template_id' => absint( get_post_meta( $document_id, self::META_EMBEDDED_SOURCE, true ) ),
                'content_hash' => self::hash_elements( $elements ),
                'text_item_count' => count( $items ),
                'text_items' => $items,
            ];
        }

        return new WP_REST_Response( [
            'page_id' => $page_id,
            'page_title' => (string) get_the_title( $page_id ),
            'page_status' => (string) get_post_status( $page_id ),
            'page_content_hash' => self::hash_elements( $page_elements ),
            'embedded_document_count' => count( $documents ),
            'embedded_text_item_count' => $total_items,
            'all_embedded_documents_writable' => $all_writable,
            'prepare_required' => ! $all_writable && count( $documents ) > 0,
            'documents' => $documents,
        ], 200 );
    }

    public static function update_embedded_text( WP_REST_Request $request ) {
        $page_id = absint( $request['id'] );
        $guard = self::guard_page_write( $request, 'page_content_hash', 'confirm_embedded_text_write' );
        if ( is_wp_error( $guard ) ) return $guard;
        $page_elements = $guard['elements'];

        $refs = [];
        self::collect_direct_pixfort_references( $page_elements, [], $refs );
        $allowed_ids = array_values( array_unique( array_map( static fn( $r ) => absint( $r['post_id'] ), $refs ) ) );
        $updates = $request->get_param( 'updates' );
        if ( ! is_array( $updates ) || ! $updates ) return new WP_Error( 'elementize_updates_required', 'At least one embedded text update is required.', [ 'status' => 400 ] );
        if ( count( $updates ) > self::MAX_UPDATES ) return new WP_Error( 'elementize_too_many_updates', 'Send at most ' . self::MAX_UPDATES . ' embedded text updates per request.', [ 'status' => 400 ] );

        $groups = [];
        foreach ( $updates as $index => $update ) {
            $valid = self::validate_embedded_update( $update, $index );
            if ( is_wp_error( $valid ) ) return $valid;
            $document_id = absint( $update['document_id'] );
            if ( ! in_array( $document_id, $allowed_ids, true ) ) {
                return new WP_Error( 'elementize_embedded_document_not_current', 'An update targets a document that is no longer referenced by this page.', [ 'status' => 409, 'document_id' => $document_id ] );
            }
            if ( '1' !== (string) get_post_meta( $document_id, self::META_EMBEDDED_MANAGED, true ) || absint( get_post_meta( $document_id, self::META_EMBEDDED_OWNER, true ) ) !== $page_id ) {
                return new WP_Error( 'elementize_embedded_direct_template_write_blocked', 'Direct writes to original/shared Pixfort templates are blocked. Clone-and-relink this page first.', [ 'status' => 409, 'document_id' => $document_id ] );
            }
            $groups[ $document_id ][] = $update;
        }

        $prepared = [];
        foreach ( $groups as $document_id => $document_updates ) {
            $doc = self::doc( $document_id );
            if ( ! $doc || ! $doc->is_built_with_elementor() ) return new WP_Error( 'elementize_embedded_document_not_found', 'Elementor could not load an embedded clone as a document.', [ 'status' => 404, 'document_id' => $document_id ] );
            $elements = $doc->get_elements_data();
            if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid embedded document data.', [ 'status' => 500, 'document_id' => $document_id ] );
            $current_hash = self::hash_elements( $elements );
            foreach ( $document_updates as $update ) {
                if ( ! hash_equals( $current_hash, (string) $update['document_hash'] ) ) {
                    return new WP_Error( 'elementize_embedded_document_changed', 'An embedded document changed after it was read. Read embedded text again before editing.', [ 'status' => 409, 'document_id' => $document_id, 'content_hash' => $current_hash ] );
                }
            }
            $working = $elements;
            $changes = [];
            foreach ( $document_updates as $update ) {
                $old = null;
                $new = wp_kses_post( (string) $update['value'] );
                $result = self::apply_text( $working, (string) $update['element_id'], $update['setting_path'], $new, (string) $update['expected_value'], $old );
                if ( is_wp_error( $result ) ) return $result;
                if ( false === $result ) return new WP_Error( 'elementize_element_not_found', 'The requested Elementor element no longer exists in the embedded clone.', [ 'status' => 409, 'document_id' => $document_id ] );
                $changes[] = [
                    'document_id' => $document_id,
                    'element_id' => (string) $update['element_id'],
                    'setting_path' => array_values( $update['setting_path'] ),
                    'old_value' => $old,
                    'new_value' => $new,
                ];
            }
            $prepared[ $document_id ] = [
                'document' => $doc,
                'original' => $elements,
                'working' => $working,
                'changes' => $changes,
            ];
        }

        $saved_ids = [];
        $revision_ids = [];
        foreach ( $prepared as $document_id => $state ) {
            $revision = self::save_revision( $document_id );
            if ( is_wp_error( $revision ) ) {
                self::rollback_documents( $prepared, $saved_ids );
                return $revision;
            }
            $revision_ids[ $document_id ] = $revision;
            try {
                $saved = $state['document']->save( [ 'elements' => $state['working'] ] );
            } catch ( Throwable $ex ) {
                self::rollback_documents( $prepared, $saved_ids );
                return new WP_Error( 'elementize_embedded_text_save_failed', 'Elementor could not save an embedded clone: ' . $ex->getMessage(), [ 'status' => 500, 'document_id' => $document_id ] );
            }
            if ( ! $saved ) {
                self::rollback_documents( $prepared, $saved_ids );
                return new WP_Error( 'elementize_embedded_text_save_failed', 'Elementor refused to save an embedded clone.', [ 'status' => 500, 'document_id' => $document_id ] );
            }
            $saved_ids[] = absint( $document_id );
        }

        $document_results = [];
        $all_changes = [];
        foreach ( $prepared as $document_id => $state ) {
            $fresh = self::doc( $document_id );
            $fresh_elements = $fresh ? $fresh->get_elements_data() : null;
            if ( ! is_array( $fresh_elements ) ) {
                self::rollback_documents( $prepared, $saved_ids );
                return new WP_Error( 'elementize_embedded_text_verification_failed', 'Elementize could not re-read an embedded clone after saving, so it attempted to restore all changed clones.', [ 'status' => 500, 'document_id' => $document_id ] );
            }
            foreach ( $state['changes'] as $change ) {
                $now = self::read_text_value( $fresh_elements, $change['element_id'], $change['setting_path'] );
                if ( ! is_string( $now ) || $now !== $change['new_value'] ) {
                    self::rollback_documents( $prepared, $saved_ids );
                    return new WP_Error( 'elementize_embedded_text_verification_failed', 'A saved embedded text value could not be verified, so Elementize attempted to restore all changed clones.', [ 'status' => 500, 'document_id' => $document_id, 'element_id' => $change['element_id'] ] );
                }
                $all_changes[] = $change;
            }
            self::regenerate_css( $document_id );
            $document_results[] = [
                'document_id' => absint( $document_id ),
                'revision_id' => $revision_ids[ $document_id ] ?? null,
                'updated_count' => count( $state['changes'] ),
                'content_hash' => self::hash_elements( $fresh_elements ),
            ];
        }

        return new WP_REST_Response( [
            'saved' => true,
            'page_id' => $page_id,
            'updated_count' => count( $all_changes ),
            'updated_document_count' => count( $document_results ),
            'page_content_hash' => self::hash_elements( $page_elements ),
            'documents' => $document_results,
            'changes' => $all_changes,
            'direct_original_template_writes' => 0,
        ], 200 );
    }

    private static function guard_page_write( WP_REST_Request $request, string $hash_param, string $confirm_param ) {
        $page_id = absint( $request['id'] );
        $post = get_post( $page_id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( '1' !== (string) get_post_meta( $page_id, self::META_MANAGED_PAGE, true ) ) return new WP_Error( 'elementize_unmanaged_page', 'This page is not marked as Elementize-managed.', [ 'status' => 409 ] );
        if ( 'draft' !== $post->post_status || 'draft' !== (string) $request->get_param( 'expected_status' ) ) {
            return new WP_Error( 'elementize_status_changed', 'The page is no longer in the expected draft state.', [ 'status' => 409, 'status_now' => $post->post_status ] );
        }
        if ( (string) $request->get_param( 'expected_title' ) !== (string) $post->post_title ) {
            return new WP_Error( 'elementize_title_changed', 'The page title changed after it was read.', [ 'status' => 409, 'title_now' => $post->post_title ] );
        }
        if ( true !== $request->get_param( $confirm_param ) ) return new WP_Error( 'elementize_confirmation_required', 'Explicit ' . $confirm_param . '=true is required.', [ 'status' => 400 ] );
        $doc = self::doc( $page_id );
        if ( ! $doc || ! $doc->is_built_with_elementor() ) return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
        $elements = $doc->get_elements_data();
        if ( ! is_array( $elements ) ) return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
        $hash = self::hash_elements( $elements );
        $expected_hash = (string) $request->get_param( $hash_param );
        if ( '' === $expected_hash || ! hash_equals( $hash, $expected_hash ) ) {
            return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read. Read the page again before applying this action.', [ 'status' => 409, 'content_hash' => $hash ] );
        }
        return [ 'post' => $post, 'document' => $doc, 'elements' => $elements, 'content_hash' => $hash ];
    }

    private static function validate_expected_documents( $expected ) {
        if ( ! is_array( $expected ) || ! $expected ) return new WP_Error( 'elementize_expected_documents_required', 'expected_documents must contain the fresh embedded document identities.', [ 'status' => 400 ] );
        $map = [];
        foreach ( $expected as $index => $item ) {
            if ( ! is_array( $item ) || empty( $item['post_id'] ) || empty( $item['elementor_data_hash'] ) ) {
                return new WP_Error( 'elementize_invalid_expected_document', 'Each expected_documents item requires post_id and elementor_data_hash.', [ 'status' => 400, 'index' => $index ] );
            }
            $id = absint( $item['post_id'] );
            if ( ! $id || isset( $map[ $id ] ) ) return new WP_Error( 'elementize_invalid_expected_document', 'expected_documents contains an invalid or duplicate post_id.', [ 'status' => 400, 'index' => $index ] );
            $map[ $id ] = [
                'elementor_data_hash' => (string) $item['elementor_data_hash'],
            ];
            if ( array_key_exists( 'post_status', $item ) ) $map[ $id ]['post_status'] = (string) $item['post_status'];
            if ( array_key_exists( 'post_title', $item ) ) $map[ $id ]['post_title'] = (string) $item['post_title'];
        }
        return $map;
    }

    private static function validate_embedded_update( $update, int $index ) {
        if ( ! is_array( $update ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': each update must be an object.', [ 'status' => 400 ] );
        if ( empty( $update['document_id'] ) || ! is_numeric( $update['document_id'] ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': document_id is required.', [ 'status' => 400 ] );
        if ( empty( $update['document_hash'] ) || ! is_string( $update['document_hash'] ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': document_hash is required.', [ 'status' => 400 ] );
        if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': element_id is required.', [ 'status' => 400 ] );
        if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': setting_path must be a non-empty array.', [ 'status' => 400 ] );
        if ( ! array_key_exists( 'expected_value', $update ) || ! is_string( $update['expected_value'] ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': expected_value is required.', [ 'status' => 400 ] );
        if ( ! array_key_exists( 'value', $update ) || ! is_string( $update['value'] ) ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': value must be a string.', [ 'status' => 400 ] );
        if ( strlen( $update['value'] ) > self::MAX_TEXT ) return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': value is too long.', [ 'status' => 400 ] );
        return true;
    }

    private static function clone_template_for_page( int $source_id, int $page_id ) {
        $source = get_post( $source_id );
        if ( ! $source || 'pixfort_template' !== $source->post_type ) return new WP_Error( 'elementize_embedded_source_invalid', 'The Pixfort source template could not be loaded.', [ 'status' => 404, 'post_id' => $source_id ] );

        $clone_id = wp_insert_post( [
            'post_type' => 'pixfort_template',
            'post_status' => 'publish',
            'post_title' => $source->post_title . ' — Elementize page ' . $page_id,
            'post_content' => $source->post_content,
            'post_excerpt' => $source->post_excerpt,
            'post_author' => get_current_user_id(),
            'post_parent' => 0,
            'menu_order' => (int) $source->menu_order,
            'comment_status' => 'closed',
            'ping_status' => 'closed',
        ], true );
        if ( is_wp_error( $clone_id ) ) return new WP_Error( 'elementize_embedded_clone_failed', 'WordPress could not create an isolated Pixfort template clone: ' . $clone_id->get_error_message(), [ 'status' => 500, 'source_post_id' => $source_id ] );
        $clone_id = absint( $clone_id );

        $skip = [ '_edit_lock', '_edit_last', '_wp_old_slug', '_elementor_css', '_elementor_element_cache', self::META_EMBEDDED_MANAGED, self::META_EMBEDDED_OWNER, self::META_EMBEDDED_SOURCE, self::META_EMBEDDED_CREATED ];
        $meta = get_post_meta( $source_id );
        foreach ( $meta as $key => $values ) {
            if ( in_array( $key, $skip, true ) ) continue;
            foreach ( (array) $values as $value ) add_post_meta( $clone_id, $key, maybe_unserialize( $value ) );
        }
        update_post_meta( $clone_id, self::META_EMBEDDED_MANAGED, '1' );
        update_post_meta( $clone_id, self::META_EMBEDDED_OWNER, (string) $page_id );
        update_post_meta( $clone_id, self::META_EMBEDDED_SOURCE, (string) $source_id );
        update_post_meta( $clone_id, self::META_EMBEDDED_CREATED, gmdate( 'Y-m-d H:i:s' ) );

        foreach ( get_object_taxonomies( 'pixfort_template' ) as $taxonomy ) {
            $term_ids = wp_get_object_terms( $source_id, $taxonomy, [ 'fields' => 'ids' ] );
            if ( ! is_wp_error( $term_ids ) && $term_ids ) wp_set_object_terms( $clone_id, array_map( 'intval', $term_ids ), $taxonomy );
        }

        $source_raw = self::elementor_data_raw( $source_id );
        $clone_raw = self::elementor_data_raw( $clone_id );
        if ( '' === $clone_raw || ! hash_equals( hash( 'sha256', $source_raw ), hash( 'sha256', $clone_raw ) ) ) {
            wp_delete_post( $clone_id, true );
            return new WP_Error( 'elementize_embedded_clone_verification_failed', 'The isolated Pixfort clone did not preserve the source Elementor data exactly, so the clone was removed.', [ 'status' => 500, 'source_post_id' => $source_id ] );
        }
        $doc = self::doc( $clone_id );
        if ( ! $doc || ! $doc->is_built_with_elementor() ) {
            wp_delete_post( $clone_id, true );
            return new WP_Error( 'elementize_embedded_clone_verification_failed', 'Elementor did not recognize the isolated Pixfort clone as an Elementor document, so the clone was removed.', [ 'status' => 500, 'source_post_id' => $source_id ] );
        }
        return $clone_id;
    }

    private static function delete_temporary_clones( array $ids ): void {
        foreach ( array_unique( array_map( 'absint', $ids ) ) as $id ) {
            if ( ! $id ) continue;
            if ( '1' !== (string) get_post_meta( $id, self::META_EMBEDDED_MANAGED, true ) ) continue;
            wp_delete_post( $id, true );
        }
    }

    private static function rollback_documents( array $prepared, array $saved_ids ): void {
        foreach ( array_reverse( array_unique( array_map( 'absint', $saved_ids ) ) ) as $document_id ) {
            if ( empty( $prepared[ $document_id ]['document'] ) || ! isset( $prepared[ $document_id ]['original'] ) ) continue;
            try { $prepared[ $document_id ]['document']->save( [ 'elements' => $prepared[ $document_id ]['original'] ] ); } catch ( Throwable $ignored ) {}
            self::regenerate_css( $document_id );
        }
    }

    private static function collect_direct_pixfort_references( $value, array $path, array &$references ): void {
        if ( ! is_array( $value ) ) return;
        foreach ( $value as $key => $child ) {
            $next = $path;
            $next[] = is_int( $key ) ? $key : (string) $key;
            if ( 'pix_template_id' === (string) $key && is_scalar( $child ) ) {
                $candidate = absint( $child );
                $post = $candidate ? get_post( $candidate ) : null;
                if ( $post && 'pixfort_template' === $post->post_type && trim( (string) $child ) === (string) $candidate ) {
                    $references[] = [
                        'post_id' => $candidate,
                        'path_segments' => $next,
                        'path' => wp_json_encode( $next ),
                        'value' => (string) $child,
                    ];
                }
            }
            if ( is_array( $child ) ) self::collect_direct_pixfort_references( $child, $next, $references );
        }
    }

    private static function set_path_value( array &$root, array $path, int $new_id, int $expected_old_id ): bool {
        $cursor =& $root;
        foreach ( $path as $segment ) {
            if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return false;
            $cursor =& $cursor[ $segment ];
        }
        if ( ! is_scalar( $cursor ) || absint( $cursor ) !== $expected_old_id ) return false;
        $cursor = is_int( $cursor ) ? $new_id : (string) $new_id;
        return true;
    }

    private static function elementor_data_raw( int $post_id ): string {
        $raw = get_post_meta( $post_id, '_elementor_data', true );
        if ( is_string( $raw ) ) return $raw;
        if ( is_array( $raw ) || is_object( $raw ) ) {
            $json = wp_json_encode( $raw );
            return is_string( $json ) ? $json : '';
        }
        return is_scalar( $raw ) ? (string) $raw : '';
    }

    private static function elementor_data_array( int $post_id ): ?array {
        $raw = get_post_meta( $post_id, '_elementor_data', true );
        if ( is_array( $raw ) ) return $raw;
        if ( ! is_string( $raw ) || '' === $raw ) return null;
        $decoded = json_decode( $raw, true );
        return is_array( $decoded ) ? $decoded : null;
    }

    private static function collect_document_references( $value, array $path, array &$references ): void {
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $child ) {
                $next = $path;
                $next[] = is_int( $key ) ? $key : (string) $key;
                self::collect_document_references( $child, $next, $references );
            }
            return;
        }
        if ( ! is_scalar( $value ) ) return;
        $text = trim( (string) $value );
        if ( '' === $text ) return;
        $candidates = [];
        if ( ctype_digit( $text ) ) $candidates[] = absint( $text );
        elseif ( preg_match_all( '/(?<!\\d)(\\d{4,})(?!\\d)/', $text, $matches ) ) foreach ( $matches[1] as $match ) $candidates[] = absint( $match );
        foreach ( array_unique( array_filter( $candidates ) ) as $candidate ) {
            $post = get_post( $candidate );
            if ( ! $post || ! in_array( $post->post_type, [ 'pixfort_template', 'elementor_library' ], true ) ) continue;
            $references[] = [
                'post_id' => $candidate,
                'path' => wp_json_encode( $path ),
                'value' => mb_substr( $text, 0, 500 ),
            ];
        }
    }

    private static function reverse_references( int $target_id ): array {
        global $wpdb;
        $like = '%' . $wpdb->esc_like( (string) $target_id ) . '%';
        $candidate_ids = $wpdb->get_col( $wpdb->prepare(
            "SELECT DISTINCT post_id FROM {$wpdb->postmeta} WHERE meta_key = '_elementor_data' AND meta_value LIKE %s LIMIT 500",
            $like
        ) );
        $result = [];
        foreach ( array_map( 'absint', (array) $candidate_ids ) as $source_id ) {
            if ( ! $source_id || $source_id === $target_id ) continue;
            $decoded = self::elementor_data_array( $source_id );
            if ( ! is_array( $decoded ) ) continue;
            $references = [];
            self::collect_document_references( $decoded, [], $references );
            foreach ( $references as $reference ) {
                if ( absint( $reference['post_id'] ) !== $target_id ) continue;
                $source = get_post( $source_id );
                if ( ! $source ) continue;
                $revision_parent = wp_is_post_revision( $source_id );
                $autosave_parent = wp_is_post_autosave( $source_id );
                $key = $source_id . ':' . $reference['path'];
                $result[ $key ] = [
                    'source_post_id' => $source_id,
                    'source_post_type' => (string) $source->post_type,
                    'source_post_status' => (string) $source->post_status,
                    'source_post_title' => (string) $source->post_title,
                    'is_revision' => false !== $revision_parent,
                    'revision_parent' => false !== $revision_parent ? absint( $revision_parent ) : 0,
                    'is_autosave' => false !== $autosave_parent,
                    'autosave_parent' => false !== $autosave_parent ? absint( $autosave_parent ) : 0,
                    'path' => $reference['path'],
                ];
            }
        }
        return array_values( $result );
    }

    private static function collect_text( array $elements, array &$items ): void {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $id = (string) ( $element['id'] ?? '' );
            $element_type = isset( $element['elType'] ) ? (string) $element['elType'] : null;
            $widget_type = isset( $element['widgetType'] ) ? (string) $element['widgetType'] : null;
            if ( '' !== $id && ! empty( $element['settings'] ) && is_array( $element['settings'] ) ) self::collect_text_settings( $element['settings'], [], $id, $element_type, $widget_type, $items );
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) self::collect_text( $element['elements'], $items );
        }
    }

    private static function collect_text_settings( $value, array $path, string $id, ?string $element_type, ?string $widget_type, array &$items ): void {
        if ( is_array( $value ) ) {
            foreach ( $value as $key => $child ) self::collect_text_settings( $child, array_merge( $path, [ $key ] ), $id, $element_type, $widget_type, $items );
            return;
        }
        if ( ! is_string( $value ) || ! self::copy_field( $path, $value ) ) return;
        $items[] = [
            'element_id' => $id,
            'element_type' => $element_type,
            'widget_type' => $widget_type,
            'setting_path' => array_values( $path ),
            'format' => wp_strip_all_tags( $value ) === $value ? 'text' : 'html',
            'value' => $value,
        ];
    }

    private static function apply_text( array &$elements, string $element_id, array $path, string $new, string $expected, ?string &$old ) {
        foreach ( $elements as &$element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) return new WP_Error( 'elementize_setting_not_found', 'The target embedded Elementor element has no editable settings.', [ 'status' => 400 ] );
                $cursor =& $element['settings'];
                foreach ( $path as $segment ) {
                    if ( ! is_string( $segment ) && ! is_int( $segment ) ) return new WP_Error( 'elementize_invalid_setting_path', 'Each setting_path segment must be a string or integer.', [ 'status' => 400 ] );
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return new WP_Error( 'elementize_setting_not_found', 'The requested embedded text setting no longer exists.', [ 'status' => 409 ] );
                    $cursor =& $cursor[ $segment ];
                }
                if ( ! is_string( $cursor ) || ! self::copy_field( $path, $cursor ) ) return new WP_Error( 'elementize_not_copy_field', 'Elementize only permits embedded text updates to recognized copy fields.', [ 'status' => 400 ] );
                if ( $cursor !== $expected ) return new WP_Error( 'elementize_value_changed', 'The target embedded text changed after it was read.', [ 'status' => 409 ] );
                $old = $cursor;
                $cursor = $new;
                return true;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $result = self::apply_text( $element['elements'], $element_id, $path, $new, $expected, $old );
                if ( true === $result || is_wp_error( $result ) ) return $result;
            }
        }
        unset( $element );
        return false;
    }

    private static function read_text_value( array $elements, string $element_id, array $path ): ?string {
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
                $cursor = $element['settings'] ?? null;
                if ( ! is_array( $cursor ) ) return null;
                foreach ( $path as $segment ) {
                    if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) return null;
                    $cursor = $cursor[ $segment ];
                }
                return is_string( $cursor ) ? $cursor : null;
            }
            if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
                $value = self::read_text_value( $element['elements'], $element_id, $path );
                if ( null !== $value ) return $value;
            }
        }
        return null;
    }

    private static function copy_field( array $path, string $value ): bool {
        if ( '' === trim( $value ) || strlen( $value ) > self::MAX_TEXT || str_starts_with( ltrim( $value ), '[elementor-tag' ) ) return false;
        $key = '';
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) {
            if ( is_string( $path[ $i ] ) ) { $key = strtolower( $path[ $i ] ); break; }
        }
        if ( '' === $key ) return false;
        if ( preg_match( '/(^|_)(url|link|href|src|id|class|selector|shortcode|icon|image|media|color|background|gradient|font|typography|size|width|height|margin|padding|border|radius|opacity|align|justify|position|offset|z_index|animation|transition|transform|query|taxonomy|post_id|flex|grid|layout|display|direction|gap|wrap|grow|shrink|basis|order|overflow)(_|$)/i', $key ) ) return false;
        return (bool) preg_match( '/(^|_)(title|text|content|editor|description|heading|headline|subtitle|sub_title|label|caption|button_text|btn_text|placeholder|prefix|suffix|message)(_|$)/i', $key );
    }

    private static function doc( int $id ) {
        if ( ! class_exists( '\\Elementor\\Plugin' ) ) return null;
        try { return \\Elementor\\Plugin::$instance->documents->get( $id ); }
        catch ( Throwable $ignored ) { return null; }
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
        return is_int( $revision ) && $revision > 0 ? $revision : new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the document was not modified.', [ 'status' => 500, 'post_id' => $id ] );
    }

    private static function regenerate_css( int $id ): void {
        try {
            if ( class_exists( '\\Elementor\\Core\\Files\\CSS\\Post' ) ) {
                $css = new \\Elementor\\Core\\Files\\CSS\\Post( $id );
                $css->update();
            }
        } catch ( Throwable $ignored ) {}
    }

    private static function count_query( string $haystack, string $query ): int {
        if ( '' === $query || '' === $haystack ) return 0;
        return substr_count( strtolower( $haystack ), strtolower( $query ) );
    }

    public static function after_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['embedded_documents_diagnostic'] = true;
        $data['embedded_documents_revision_classification'] = true;
        $data['embedded_documents_clone_relink'] = true;
        $data['embedded_documents_direct_template_mutation'] = false;
        $data['embedded_text_read'] = true;
        $data['embedded_text_write_owned_clones_only'] = true;
        $data['embedded_documents_recommended_write_strategy'] = 'clone_and_relink';
        $response->set_data( $data );
        return $response;
    }
}

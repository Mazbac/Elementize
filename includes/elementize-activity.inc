<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Activity {
    private const OPT_LOG = 'elementize_activity_log';
    private const SNAPSHOT_META = '_elementize_activity_snapshot';
    private const MAX_LOG = 100;
    private static array $pending = [];

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 30 );
        add_filter( 'rest_request_before_callbacks', [ self::class, 'capture_before_write' ], 10, 3 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'capture_after_write' ], 10, 3 );
        add_action( 'wp_ajax_elementize_get_activity', [ self::class, 'ajax_list_activity' ] );
        add_action( 'wp_ajax_elementize_undo_activity', [ self::class, 'ajax_undo_activity' ] );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/activity', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'list_activity' ],
            'permission_callback' => [ self::class, 'can_manage' ],
            'args' => [
                'limit' => [ 'type' => 'integer', 'default' => 20, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
            ],
        ] );

        register_rest_route( Elementize_Core::NS, '/activity/(?P<activity_id>[A-Fa-f0-9-]{36})/undo', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'undo' ],
            'permission_callback' => [ self::class, 'can_manage' ],
            'args' => [
                'activity_id' => [ 'type' => 'string', 'required' => true, 'sanitize_callback' => 'sanitize_text_field' ],
                'confirm_undo' => [ 'type' => 'boolean', 'required' => true ],
                'confirm_published_page' => [ 'type' => 'boolean', 'default' => false ],
            ],
        ] );
    }

    public static function can_manage() {
        if ( ! is_user_logged_in() ) return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
        if ( ! current_user_can( 'manage_options' ) ) return new WP_Error( 'elementize_activity_forbidden', 'Activity history and undo require an administrator.', [ 'status' => 403 ] );
        return true;
    }

    public static function ajax_list_activity(): void {
        self::require_ajax_access();
        $request = new WP_REST_Request( 'GET' );
        $request->set_param( 'limit', min( 100, max( 1, absint( $_POST['limit'] ?? 20 ) ) ) );
        self::send_ajax_result( self::list_activity( $request ) );
    }

    public static function ajax_undo_activity(): void {
        self::require_ajax_access();
        $request = new WP_REST_Request( 'POST' );
        $request->set_param( 'activity_id', sanitize_text_field( (string) wp_unslash( $_POST['activity_id'] ?? '' ) ) );
        $request->set_param( 'confirm_undo', true );
        $request->set_param( 'confirm_published_page', '1' === (string) ( $_POST['confirm_published_page'] ?? '' ) );
        self::send_ajax_result( self::undo( $request ) );
    }

    public static function capture_before_write( $response, array $handler, WP_REST_Request $request ) {
        if ( null !== $response || 'POST' !== strtoupper( $request->get_method() ) ) return $response;
        $page_id = self::content_write_page_id( $request );
        if ( ! $page_id ) return $response;

        $loaded = Elementize_Core::elements( $page_id );
        if ( is_wp_error( $loaded ) ) return $response;

        self::$pending[ spl_object_hash( $request ) ] = [
            'page_id' => $page_id,
            'elements' => $loaded['elements'],
            'before_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
        ];
        return $response;
    }

    public static function capture_after_write( $response, array $handler, WP_REST_Request $request ) {
        $key = spl_object_hash( $request );
        $pending = self::$pending[ $key ] ?? null;
        unset( self::$pending[ $key ] );
        if ( ! is_array( $pending ) || is_wp_error( $response ) ) return $response;

        $rest_response = rest_ensure_response( $response );
        $data = $rest_response->get_data();
        if ( ! is_array( $data ) || empty( $data['saved'] ) || empty( $data['persisted_verification'] ) ) return $response;

        $revision_id = absint( $data['revision_id'] ?? 0 );
        $after_hash = (string) ( $data['content_hash'] ?? '' );
        if ( ! $revision_id || '' === $after_hash ) return $response;

        $snapshot = self::encode_snapshot( $pending['elements'] );
        if ( '' !== $snapshot ) {
            // update_post_meta() redirects revision IDs to their parent post. Use the
            // generic metadata API so this snapshot stays attached to this revision.
            update_metadata( 'post', $revision_id, self::SNAPSHOT_META, $snapshot );
        }

        $post = get_post( $pending['page_id'] );
        $user = wp_get_current_user();
        $changes = [];
        $kinds = [];
        foreach ( (array) ( $data['changes'] ?? [] ) as $change ) {
            if ( ! is_array( $change ) ) continue;
            $kind = sanitize_key( (string) ( $change['kind'] ?? '' ) );
            if ( '' !== $kind ) $kinds[ $kind ] = ( $kinds[ $kind ] ?? 0 ) + 1;
            $changes[] = [
                'kind' => $kind,
                'element_id' => sanitize_text_field( (string) ( $change['element_id'] ?? '' ) ),
                'setting_path' => array_values( array_map( static fn( $part ) => is_int( $part ) ? $part : sanitize_text_field( (string) $part ), (array) ( $change['setting_path'] ?? [] ) ) ),
            ];
        }

        $record = [
            'id' => wp_generate_uuid4(),
            'created_gmt' => gmdate( 'Y-m-d H:i:s' ),
            'page_id' => (int) $pending['page_id'],
            'page_title' => $post ? (string) $post->post_title : '',
            'page_status' => $post ? (string) $post->post_status : '',
            'user_id' => $user && $user->exists() ? (int) $user->ID : 0,
            'user_name' => $user && $user->exists() ? (string) $user->display_name : '',
            'revision_id' => $revision_id,
            'before_hash' => (string) $pending['before_hash'],
            'after_hash' => $after_hash,
            'updated_count' => absint( $data['updated_count'] ?? count( $changes ) ),
            'kinds' => $kinds,
            'changes' => array_slice( $changes, 0, 50 ),
            'undone_at_gmt' => '',
            'undo_revision_id' => 0,
        ];
        self::prepend_record( $record );
        return $response;
    }

    public static function list_activity( WP_REST_Request $request ): WP_REST_Response {
        $limit = min( 100, max( 1, absint( $request->get_param( 'limit' ) ) ) );
        $records = array_slice( self::records(), 0, $limit );
        $current_hashes = [];
        $items = [];

        foreach ( $records as $record ) {
            if ( ! is_array( $record ) ) continue;
            $page_id = absint( $record['page_id'] ?? 0 );
            $revision_id = absint( $record['revision_id'] ?? 0 );
            $snapshot_available = $revision_id > 0 && '' !== self::snapshot_payload( $revision_id );
            $undone = '' !== (string) ( $record['undone_at_gmt'] ?? '' );

            if ( $page_id && ! array_key_exists( $page_id, $current_hashes ) ) {
                $persisted = Elementize_Core::persisted_elements( $page_id );
                $current_hashes[ $page_id ] = is_wp_error( $persisted ) ? '' : Elementize_Core::hash_elements( $persisted );
            }
            $matches_after = $page_id && '' !== (string) ( $current_hashes[ $page_id ] ?? '' ) && hash_equals( (string) ( $record['after_hash'] ?? '' ), (string) $current_hashes[ $page_id ] );

            $record['snapshot_available'] = $snapshot_available;
            $record['undo_available'] = ! $undone && $snapshot_available && $matches_after;
            $record['page'] = $page_id ? Elementize_Core::page_identity( $page_id ) : null;
            $items[] = $record;
        }

        return new WP_REST_Response( [
            'items' => $items,
            'returned_count' => count( $items ),
            'max_history' => self::MAX_LOG,
        ], 200 );
    }

    public static function undo( WP_REST_Request $request ) {
        if ( true !== $request->get_param( 'confirm_undo' ) ) return new WP_Error( 'elementize_undo_confirmation_required', 'Explicit confirm_undo=true is required.', [ 'status' => 400 ] );
        $activity_id = sanitize_text_field( (string) $request->get_param( 'activity_id' ) );
        $records = self::records();
        $index = self::find_record_index( $records, $activity_id );
        if ( null === $index ) return new WP_Error( 'elementize_activity_not_found', 'Activity record not found.', [ 'status' => 404 ] );

        $record = $records[ $index ];
        if ( '' !== (string) ( $record['undone_at_gmt'] ?? '' ) ) return new WP_Error( 'elementize_activity_already_undone', 'This activity has already been undone.', [ 'status' => 409 ] );

        $page_id = absint( $record['page_id'] ?? 0 );
        $post = get_post( $page_id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
        if ( ! current_user_can( 'edit_post', $page_id ) ) return new WP_Error( 'elementize_forbidden', 'You cannot edit this page.', [ 'status' => 403 ] );
        if ( 'publish' === $post->post_status && true !== $request->get_param( 'confirm_published_page' ) ) return new WP_Error( 'elementize_published_confirmation_required', 'This is a published page. Confirm the live-page undo before continuing.', [ 'status' => 400 ] );

        $revision_id = absint( $record['revision_id'] ?? 0 );
        $snapshot = self::decode_snapshot( $revision_id );
        if ( ! is_array( $snapshot ) ) return new WP_Error( 'elementize_activity_snapshot_missing', 'The pre-change snapshot is no longer available, so this change cannot be undone safely.', [ 'status' => 409 ] );
        if ( ! hash_equals( (string) ( $record['before_hash'] ?? '' ), Elementize_Core::hash_elements( $snapshot ) ) ) return new WP_Error( 'elementize_activity_snapshot_invalid', 'The stored pre-change snapshot did not pass integrity verification.', [ 'status' => 409 ] );

        $current = Elementize_Core::persisted_elements( $page_id );
        if ( is_wp_error( $current ) ) return $current;
        $current_hash = Elementize_Core::hash_elements( $current );
        if ( ! hash_equals( (string) ( $record['after_hash'] ?? '' ), $current_hash ) ) return new WP_Error( 'elementize_activity_page_changed', 'The page changed after this activity. Undo the newest matching change first or review the page manually.', [ 'status' => 409, 'content_hash' => $current_hash ] );

        $undo_revision = Elementize_Core::save_revision( $page_id );
        if ( is_wp_error( $undo_revision ) ) return $undo_revision;
        $document = Elementize_Core::need_document( $page_id );
        if ( is_wp_error( $document ) ) return $document;

        $saved = self::save_verified( $document, $page_id, $snapshot, (string) $record['before_hash'] );
        if ( is_wp_error( $saved ) ) {
            $rollback = self::save_verified( $document, $page_id, $current, $current_hash );
            return new WP_Error( 'elementize_undo_failed', 'Elementize could not safely restore the pre-change content.', [
                'status' => 500,
                'undo_revision_id' => $undo_revision,
                'rollback_verified' => ! is_wp_error( $rollback ),
            ] );
        }

        $records[ $index ]['undone_at_gmt'] = gmdate( 'Y-m-d H:i:s' );
        $records[ $index ]['undo_revision_id'] = (int) $undo_revision;
        self::save_records( $records );
        Elementize_Core::regenerate_css( $page_id );
        clean_post_cache( $page_id );

        return new WP_REST_Response( [
            'undone' => true,
            'activity_id' => $activity_id,
            'page' => Elementize_Core::page_identity( $page_id ),
            'undo_revision_id' => (int) $undo_revision,
            'content_hash' => (string) $record['before_hash'],
            'persisted_verification' => true,
        ], 200 );
    }

    private static function require_ajax_access(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_send_json_error( [ 'message' => 'Administrator permission is required.' ], 403 );
        check_ajax_referer( 'elementize_onboarding', 'nonce' );
    }

    private static function send_ajax_result( $result ): void {
        if ( is_wp_error( $result ) ) {
            $data = $result->get_error_data();
            $status = is_array( $data ) && isset( $data['status'] ) ? absint( $data['status'] ) : 400;
            wp_send_json_error( [ 'message' => $result->get_error_message(), 'code' => $result->get_error_code(), 'data' => $data ], $status ?: 400 );
        }
        $response = rest_ensure_response( $result );
        wp_send_json_success( $response->get_data(), $response->get_status() );
    }

    private static function content_write_page_id( WP_REST_Request $request ): int {
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/(\\d+)/content$#', $request->get_route(), $matches ) ) return 0;
        return absint( $matches[1] ?? 0 );
    }

    private static function encode_snapshot( array $elements ): string {
        $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) && '' !== $json ? base64_encode( $json ) : '';
    }

    private static function snapshot_payload( int $revision_id ): string {
        if ( $revision_id <= 0 ) return '';
        return (string) get_metadata( 'post', $revision_id, self::SNAPSHOT_META, true );
    }

    private static function decode_snapshot( int $revision_id ): ?array {
        $payload = self::snapshot_payload( $revision_id );
        if ( '' === $payload ) return null;
        $json = base64_decode( $payload, true );
        if ( ! is_string( $json ) || '' === $json ) return null;
        $decoded = json_decode( $json, true );
        return is_array( $decoded ) ? $decoded : null;
    }

    private static function save_verified( $document, int $page_id, array $elements, string $expected_hash ) {
        try { $saved = $document->save( [ 'elements' => $elements ] ); }
        catch ( Throwable $e ) { return new WP_Error( 'elementize_activity_save_failed', $e->getMessage() ); }
        if ( ! $saved ) return new WP_Error( 'elementize_activity_save_failed', 'Elementor refused to save the page.' );
        $persisted = Elementize_Core::persisted_elements( $page_id );
        if ( is_wp_error( $persisted ) ) return $persisted;
        return hash_equals( $expected_hash, Elementize_Core::hash_elements( $persisted ) ) ? true : new WP_Error( 'elementize_activity_verification_failed', 'Persisted Elementor data did not match the expected snapshot.' );
    }

    private static function records(): array {
        $records = get_option( self::OPT_LOG, [] );
        return is_array( $records ) ? array_values( array_filter( $records, 'is_array' ) ) : [];
    }

    private static function prepend_record( array $record ): void {
        $records = self::records();
        array_unshift( $records, $record );
        self::save_records( array_slice( $records, 0, self::MAX_LOG ) );
    }

    private static function save_records( array $records ): void {
        update_option( self::OPT_LOG, array_values( array_slice( $records, 0, self::MAX_LOG ) ), false );
    }

    private static function find_record_index( array $records, string $activity_id ): ?int {
        foreach ( $records as $index => $record ) {
            if ( is_array( $record ) && '' !== $activity_id && hash_equals( (string) ( $record['id'] ?? '' ), $activity_id ) ) return (int) $index;
        }
        return null;
    }
}

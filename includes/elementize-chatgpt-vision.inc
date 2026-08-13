<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Native ChatGPT vision handoff for Rendered Visual QA.
 *
 * GPT Actions cannot return image/video files directly. When the existing
 * Visual QA action is called with analyze=true, this handoff reuses the proven
 * analyze=false Chromium capture job, wraps the completed PNG in a one-file ZIP
 * and returns it through openaiFileResponse. ChatGPT extracts screenshot.png from
 * the returned conversation ZIP and inspects the actual image itself.
 *
 * This handoff is read-only. It never changes page content, never exposes the
 * internal preview URL and never claims visual_analysis_verified on the server.
 */
final class Elementize_ChatGPT_Vision {
    private const VERSION = '2';
    private const SCREENSHOT_ENTRY = 'screenshot.png';
    private const MAX_SCREENSHOT_BYTES = 10485760;
    private const FILE_TTL = 180;
    private const FILE_PREFIX = 'elementize_chatgpt_vision_file_v2_';
    private const FILE_NAME_PREFIX = 'elementize-chatgpt-vision-';

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 30 );
        add_filter( 'rest_pre_serve_request', [ self::class, 'serve_returned_file' ], 10, 4 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/visual-qa-files/(?P<token>[a-f0-9]{64})', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'returned_file_marker' ],
            'permission_callback' => '__return_true',
            'args' => [ 'token' => [ 'type' => 'string', 'required' => true ] ],
        ] );
    }

    public static function returned_file_marker( WP_REST_Request $request ) {
        $meta = self::returned_file_meta( (string) $request['token'] );
        if ( is_wp_error( $meta ) ) return $meta;
        return new WP_REST_Response( [ 'download_ready' => true ], 200 );
    }

    public static function serve_returned_file( $served, $result, WP_REST_Request $request, $server ) {
        if ( $served ) return true;
        $route = (string) $request->get_route();
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/visual-qa-files/(?P<token>[a-f0-9]{64})$#', $route, $match ) ) return $served;
        if ( ! $result instanceof WP_REST_Response || 200 !== $result->get_status() ) return $served;
        $meta = self::returned_file_meta( (string) ( $match['token'] ?? '' ) );
        if ( is_wp_error( $meta ) ) return $served;

        $path = (string) ( $meta['path'] ?? '' );
        $name = (string) ( $meta['name'] ?? 'elementize-visual.zip' );
        $size = absint( $meta['size'] ?? 0 );
        if ( headers_sent() || '' === $path || $size < 1 ) return $served;
        header( 'Content-Type: application/zip' );
        header( 'Content-Disposition: attachment; filename="' . str_replace( '"', '', basename( $name ) ) . '"' );
        header( 'Content-Length: ' . $size );
        header( 'Cache-Control: private, no-store, max-age=0' );
        header( 'Pragma: no-cache' );
        header( 'X-Content-Type-Options: nosniff' );
        if ( 'HEAD' !== strtoupper( (string) $request->get_method() ) ) readfile( $path );
        return true;
    }

    public static function handle( WP_REST_Request $request ) {
        $route = (string) $request->get_route();
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/(?P<id>\\d+)/visual-qa$#', $route ) ) return new WP_Error( 'elementize_visual_qa_route_invalid', 'Native ChatGPT vision received an unexpected route.', [ 'status' => 400 ] );

        $raw_analyze = $request->get_param( 'analyze' );
        $analyze = null === $raw_analyze ? true : (bool) filter_var( $raw_analyze, FILTER_VALIDATE_BOOLEAN );
        if ( ! $analyze ) return new WP_Error( 'elementize_chatgpt_vision_not_requested', 'Native ChatGPT vision requires analyze=true.', [ 'status' => 400 ] );

        $allowed = Elementize_Core::can_edit_page( $request );
        if ( is_wp_error( $allowed ) ) return $allowed;
        if ( true !== $allowed ) return new WP_Error( 'elementize_forbidden', 'You cannot edit this page.', [ 'status' => 403 ] );

        return self::handoff( absint( $request['id'] ), (string) $request->get_param( 'viewport' ), (string) $request->get_param( 'motion' ), (string) $request->get_param( 'interaction_probe' ) );
    }

    private static function handoff( int $id, string $viewport = 'desktop', string $motion = 'settled', string $interaction = 'passive' ) {
        $state = Elementize_Capabilities::state();
        if ( empty( $state['creative_enabled'] ) || absint( $state['scope_page_id'] ) !== $id ) {
            return new WP_Error( 'elementize_visual_qa_scope_mismatch', 'Rendered Visual QA is currently limited to the active Creative Control page.', [ 'status' => 403, 'capability_state' => $state ] );
        }

        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );

        $hash = Elementize_Core::hash_elements( $loaded['elements'] );
        $revision = absint( $state['revision'] );
        $viewport = in_array( strtolower( trim( $viewport ) ), [ 'desktop', 'tablet', 'mobile' ], true ) ? strtolower( trim( $viewport ) ) : 'desktop';
        $motion = 'live' === strtolower( trim( $motion ) ) ? 'live' : 'settled';
        $interaction = 'safe' === strtolower( trim( $interaction ) ) ? 'safe' : 'passive';
        $context = Elementize_Visual_QA::capture_context( $id, $hash, $revision, $viewport, $motion, $interaction );
        $capture_version = (string) $context['version'];
        $job_id = (string) $context['job_id'];
        $job_key = (string) $context['job_key'];
        $paths = (array) $context['paths'];
        $base = self::base_payload( $id, $loaded, $hash, $revision, $capture_version, $viewport, $motion, $interaction );
        $job = get_transient( $job_key );

        if ( is_array( $job ) && Elementize_Visual_QA::capture_job_complete( $paths, $job_id ) ) {
            return self::complete_payload( $base, $job_id, $paths['screenshot'], $job );
        }

        if ( is_array( $job ) ) {
            return new WP_REST_Response( self::pending_payload( $base, $job_id, $paths, $job ), 200 );
        }

        // No matching capture is active yet. Start the proven Chromium pipeline
        // directly with analyze=false. Later analyze=true calls poll this handoff
        // before the original Visual QA collector can consume/clean the PNG.
        $capture_request = new WP_REST_Request( 'GET', '/' . Elementize_Core::NS . '/pages/' . $id . '/visual-qa' );
        $capture_request->set_param( 'id', $id );
        $capture_request->set_param( 'analyze', false );
        $capture_request->set_param( 'viewport', $viewport );
        $capture_request->set_param( 'motion', $motion );
        $capture_request->set_param( 'interaction_probe', $interaction );
        $started = Elementize_Visual_QA::read( $capture_request );
        if ( is_wp_error( $started ) ) return $started;

        $data = $started instanceof WP_REST_Response ? $started->get_data() : $started;
        if ( ! is_array( $data ) ) {
            $base['capture_state'] = 'failed';
            $base['stage'] = 'capture_launch';
            $base['reason'] = 'Rendered Visual QA returned an unexpected capture response.';
            return new WP_REST_Response( $base, 200 );
        }

        $capture_state = (string) ( $data['capture_state'] ?? '' );
        if ( 'pending' === $capture_state ) {
            $job = get_transient( $job_key );
            return new WP_REST_Response( self::pending_payload( $base, $job_id, $paths, is_array( $job ) ? $job : [] ), 200 );
        }

        // A complete response here would indicate an unusual orphan/race where
        // the original collector consumed the PNG. Fail closed rather than claim
        // a native visual handoff that was not actually returned.
        $base['capture_state'] = 'failed';
        $base['stage'] = 'capture_launch';
        $base['reason'] = isset( $data['reason'] ) && is_scalar( $data['reason'] )
            ? self::clip( (string) $data['reason'], 500 )
            : 'The native ChatGPT vision capture could not be prepared safely. Retry once from fresh page state.';
        return new WP_REST_Response( $base, 200 );
    }

    private static function base_payload( int $id, array $loaded, string $hash, int $revision, string $capture_version, string $viewport, string $motion, string $interaction ): array {
        return [
            'visual_qa_version' => $capture_version,
            'chatgpt_vision_handoff_version' => self::VERSION,
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => $hash,
            'capability_revision' => $revision,
            'scope' => 'active_creative_control_page',
            'read_only' => true,
            'advisory_only' => true,
            'provider' => 'chatgpt_native_vision',
            'paid_services_required' => false,
            'external_account_required' => false,
            'capture_backend' => 'local_chromium_async',
            'viewport' => $viewport,
            'motion' => $motion,
            'interaction_probe' => $interaction,
            'analysis_backend' => 'chatgpt_native_vision',
            'analysis_requested' => true,
            'analysis_ready' => false,
            'visual_render_verified' => false,
            'visual_analysis_verified' => false,
            'screenshot_persisted' => false,
            'preview_url_exposed' => false,
            'chatgpt_vision_handoff_requested' => true,
            'chatgpt_vision_handoff_ready' => false,
            'chatgpt_vision_handoff_format' => 'zip',
            'chatgpt_vision_handoff_entry' => self::SCREENSHOT_ENTRY,
        ];
    }

    private static function pending_payload( array $base, string $job_id, array $paths, array $job ): array {
        $base['capture_state'] = 'pending';
        $base['capture_job_id'] = $job_id;
        $base['capture_backend'] = (string) ( $job['capture_backend'] ?? $base['capture_backend'] ?? 'local_chromium_async' );
        $base['viewport_exact'] = ! empty( $job['viewport_exact'] );
        $base['retry_after_ms'] = 1500;
        $base['capture_elapsed_ms'] = isset( $job['started_microtime'] )
            ? max( 0, (int) round( ( microtime( true ) - (float) $job['started_microtime'] ) * 1000 ) )
            : 0;
        $base['capture_artifact_state'] = Elementize_Visual_QA::capture_artifact_state( $paths );
        $base['note'] = 'The real browser capture is running. Call the same read-only Visual QA action again with analyze=true. Do not switch to analyze=false while collecting the ChatGPT vision handoff.';
        return $base;
    }

    private static function complete_payload( array $base, string $job_id, string $screenshot, array $job = [] ) {
        $exact = ! empty( $job['viewport_exact'] );
        $bounds = $exact
            ? [ 'available' => true, 'trimmed' => false, 'original_height' => null, 'bounded_height' => null ]
            : ( class_exists( 'Elementize_Visual_QA' )
                ? Elementize_Visual_QA::bound_screenshot( $screenshot )
                : [ 'available' => false, 'trimmed' => false, 'original_height' => null, 'bounded_height' => null ] );

        $bytes = @file_get_contents( $screenshot );
        if ( false === $bytes || '' === $bytes ) {
            $base['capture_state'] = 'failed';
            $base['stage'] = 'chatgpt_vision_handoff';
            $base['reason'] = 'The completed rendered screenshot could not be read for ChatGPT vision handoff.';
            return new WP_REST_Response( $base, 200 );
        }
        $byte_count = strlen( $bytes );
        if ( $byte_count > self::MAX_SCREENSHOT_BYTES ) {
            $base['capture_state'] = 'failed';
            $base['stage'] = 'chatgpt_vision_handoff';
            $base['reason'] = 'The rendered screenshot exceeded the 10 MB ChatGPT handoff safety limit.';
            return new WP_REST_Response( $base, 200 );
        }

        $width = null;
        $height = null;
        if ( function_exists( 'getimagesizefromstring' ) ) {
            $size = @getimagesizefromstring( $bytes );
            if ( is_array( $size ) ) {
                $width = absint( $size[0] ?? 0 ) ?: null;
                $height = absint( $size[1] ?? 0 ) ?: null;
            }
        }

        $archive = self::zip_single_file( self::SCREENSHOT_ENTRY, $bytes );
        if ( is_wp_error( $archive ) ) {
            $base['capture_state'] = 'complete';
            $base['visual_render_verified'] = true;
            $base['stage'] = 'chatgpt_vision_handoff';
            $base['reason'] = self::clip( (string) $archive->get_error_message(), 500 );
            return new WP_REST_Response( $base, 200 );
        }
        if ( strlen( $archive ) > self::MAX_SCREENSHOT_BYTES ) {
            $base['capture_state'] = 'complete';
            $base['visual_render_verified'] = true;
            $base['stage'] = 'chatgpt_vision_handoff';
            $base['reason'] = 'The ZIP handoff exceeded the 10 MB returned-file safety limit.';
            return new WP_REST_Response( $base, 200 );
        }

        $probe = Elementize_Visual_QA::browser_probe_for_job( $job_id );
        $scroll_height = absint( $probe['diagnostics']['document']['scrollHeight'] ?? 0 ) ?: null;
        $document_complete = $exact && null !== $height && null !== $scroll_height ? $height + 2 >= $scroll_height : null;
        if ( $exact ) {
            $bounds['original_height'] = $height;
            $bounds['bounded_height'] = $height;
        }
        $base['capture_state'] = 'complete';
        $base['capture_job_id'] = $job_id;
        $base['capture_backend'] = (string) ( $job['capture_backend'] ?? $base['capture_backend'] ?? 'local_chromium_async' );
        $base['viewport_exact'] = $exact;
        $base['document_scroll_height'] = $scroll_height;
        $base['document_capture_complete'] = $document_complete;
        $base['visual_render_verified'] = true;
        $base['chatgpt_vision_handoff_ready'] = true;
        $base['chatgpt_vision_bounds_version'] = $exact ? '3' : '2';
        $base['capture_bounds_ready'] = $exact ? true === $document_complete : ! empty( $bounds['available'] );
        $base['capture_bounds_trimmed'] = (bool) ( $bounds['trimmed'] ?? false );
        $base['capture_original_height'] = $bounds['original_height'] ?? null;
        $base['capture_bounded_height'] = $bounds['bounded_height'] ?? null;
        $base['capture_mode'] = $exact ? 'content_full_' . (string) ( $base['viewport'] ?? 'desktop' ) : ( ! empty( $bounds['available'] ) ? 'content_bounded_tall_' . (string) ( $base['viewport'] ?? 'desktop' ) : 'tall_' . (string) ( $base['viewport'] ?? 'desktop' ) );
        if ( ! $exact && empty( $bounds['available'] ) ) {
            $base['capture_bounds_reason'] = 'Content-bounds normalization was unavailable; native visual analysis must not claim bounded capture verification.';
        } elseif ( $exact && true !== $document_complete ) {
            $base['capture_bounds_reason'] = 'Exact viewport capture did not prove the full document height was included.';
        }
        $base['browser_probe_verified'] = (bool) ( $probe['verified'] ?? false );
        $base['browser_diagnostics'] = $probe['diagnostics'] ?? null;
        $base['screenshot'] = [
            'captured' => true,
            'mime_type' => 'image/png',
            'byte_count' => $byte_count,
            'sha256' => hash( 'sha256', $bytes ),
            'width' => $width,
            'height' => $height,
            'viewport_exact' => $exact,
            'document_capture_complete' => $document_complete,
            'persisted' => false,
        ];
        $quality = Elementize_Visual_QA::quality_report( $base );
        $base['quality_gate_evaluation'] = $quality['quality_gate_evaluation'];
        $base['repair_signal_summary'] = $quality['repair_signal_summary'];
        $base['repair_signals'] = $quality['repair_signals'];
        $returned_file = self::create_returned_file( $archive, $job_id, (int) ( $base['page']['id'] ?? 0 ), (string) ( $base['content_hash'] ?? '' ), absint( $base['capability_revision'] ?? 0 ) );
        if ( is_string( $returned_file ) && '' !== $returned_file ) {
            $base['openaiFileResponse'] = [ $returned_file ];
            $base['chatgpt_vision_handoff_transport'] = 'signed_url';
        } else {
            $base['openaiFileResponse'] = [ [
                'name' => 'elementize-visual-' . $job_id . '.zip',
                'mime_type' => 'application/zip',
                'content' => base64_encode( $archive ),
            ] ];
            $base['chatgpt_vision_handoff_transport'] = 'inline_fallback';
        }
        $base['chatgpt_vision_handoff_file_bytes'] = strlen( $archive );
        $base['chatgpt_vision_handoff_file_sha256'] = hash( 'sha256', $archive );
        $base['note'] = 'Rendered capture succeeded. The returned ZIP contains screenshot.png. Extract screenshot.png from the returned conversation file, then inspect the PNG itself with ChatGPT vision. Do not claim visual analysis from metadata or ZIP existence alone.';

        return new WP_REST_Response( $base, 200 );
    }

    private static function create_returned_file( string $archive, string $job_id, int $page_id, string $content_hash, int $capability_revision ): string {
        if ( ! class_exists( 'Elementize_Onboarding' ) || ! method_exists( 'Elementize_Onboarding', 'public_api_origin' ) ) return '';
        $origin = trim( (string) Elementize_Onboarding::public_api_origin() );
        if ( '' === $origin || 'https://' !== strtolower( substr( $origin, 0, 8 ) ) ) return '';
        $size = strlen( $archive );
        if ( $size < 1 || $size > self::MAX_SCREENSHOT_BYTES ) return '';
        try { $token = bin2hex( random_bytes( 32 ) ); } catch ( Throwable $e ) { return ''; }
        if ( 64 !== strlen( $token ) ) return '';

        self::cleanup_returned_files();
        $path = trailingslashit( get_temp_dir() ) . self::FILE_NAME_PREFIX . $token . '.zip';
        $written = @file_put_contents( $path, $archive, LOCK_EX );
        if ( $size !== $written || ! is_file( $path ) ) { @unlink( $path ); return ''; }
        @chmod( $path, 0600 );
        $name = 'elementize-visual-' . preg_replace( '/[^A-Za-z0-9_-]/', '-', $job_id ) . '.zip';
        $meta = [
            'path' => $path,
            'name' => $name,
            'size' => $size,
            'sha256' => hash( 'sha256', $archive ),
            'page_id' => $page_id,
            'content_hash' => $content_hash,
            'capability_revision' => $capability_revision,
            'expires_at' => time() + self::FILE_TTL,
        ];
        set_transient( self::FILE_PREFIX . $token, $meta, self::FILE_TTL );
        return rtrim( $origin, '/' ) . '/wp-json/' . trim( Elementize_Core::NS, '/' ) . '/visual-qa-files/' . $token;
    }

    private static function returned_file_meta( string $token ) {
        $token = strtolower( trim( $token ) );
        if ( ! preg_match( '/^[a-f0-9]{64}$/', $token ) ) return new WP_Error( 'elementize_visual_file_invalid', 'Returned Visual QA file token is invalid.', [ 'status' => 404 ] );
        $key = self::FILE_PREFIX . $token;
        $meta = get_transient( $key );
        if ( ! is_array( $meta ) ) return new WP_Error( 'elementize_visual_file_expired', 'Returned Visual QA file is unavailable or expired.', [ 'status' => 404 ] );
        $path = (string) ( $meta['path'] ?? '' );
        if ( time() > absint( $meta['expires_at'] ?? 0 ) ) { self::delete_returned_file( $key, $path ); return new WP_Error( 'elementize_visual_file_expired', 'Returned Visual QA file expired.', [ 'status' => 410 ] ); }
        if ( ! class_exists( 'Elementize_Capabilities' ) || ! method_exists( 'Elementize_Capabilities', 'state' ) || ! method_exists( 'Elementize_Core', 'elements' ) || ! method_exists( 'Elementize_Core', 'hash_elements' ) ) { self::delete_returned_file( $key, $path ); return new WP_Error( 'elementize_visual_file_state_unavailable', 'Returned Visual QA file freshness could not be verified.', [ 'status' => 503 ] ); }
        $page_id = absint( $meta['page_id'] ?? 0 );
        $expected_hash = (string) ( $meta['content_hash'] ?? '' );
        $expected_revision = absint( $meta['capability_revision'] ?? 0 );
        $state = Elementize_Capabilities::state();
        $loaded = $page_id > 0 ? Elementize_Core::elements( $page_id ) : new WP_Error( 'elementize_visual_file_page_invalid' );
        $current_hash = is_wp_error( $loaded ) ? '' : Elementize_Core::hash_elements( (array) ( $loaded['elements'] ?? [] ) );
        if ( empty( $state['creative_enabled'] ) || absint( $state['scope_page_id'] ?? 0 ) !== $page_id || absint( $state['revision'] ?? 0 ) !== $expected_revision || '' === $expected_hash || '' === $current_hash || ! hash_equals( $expected_hash, $current_hash ) ) { self::delete_returned_file( $key, $path ); return new WP_Error( 'elementize_visual_file_stale', 'Returned Visual QA file no longer matches fresh Creative page state.', [ 'status' => 410 ] ); }
        $temp = wp_normalize_path( trailingslashit( get_temp_dir() ) );
        $normalized = wp_normalize_path( $path );
        if ( '' === $normalized || 0 !== strpos( strtolower( $normalized ), strtolower( $temp . self::FILE_NAME_PREFIX ) ) || ! is_file( $path ) ) return new WP_Error( 'elementize_visual_file_missing', 'Returned Visual QA file is unavailable.', [ 'status' => 404 ] );
        $size = filesize( $path );
        if ( false === $size || (int) $size !== absint( $meta['size'] ?? 0 ) || (string) ( $meta['sha256'] ?? '' ) !== hash_file( 'sha256', $path ) ) { self::delete_returned_file( $key, $path ); return new WP_Error( 'elementize_visual_file_changed', 'Returned Visual QA file failed integrity verification.', [ 'status' => 410 ] ); }
        return $meta;
    }

    private static function delete_returned_file( string $key, string $path ): void {
        if ( '' !== $path && is_file( $path ) ) @unlink( $path );
        delete_transient( $key );
    }

    private static function cleanup_returned_files(): void {
        $pattern = trailingslashit( get_temp_dir() ) . self::FILE_NAME_PREFIX . '*.zip';
        $cutoff = time() - self::FILE_TTL - 60;
        foreach ( (array) glob( $pattern ) as $path ) if ( is_file( $path ) && (int) @filemtime( $path ) < $cutoff ) @unlink( $path );
    }

    private static function zip_single_file( string $filename, string $bytes ) {
        $filename = basename( trim( $filename ) );
        $filename = preg_replace( '/[^A-Za-z0-9._-]/', '-', $filename ) ?: '';
        if ( '' === $filename || strlen( $filename ) > 120 ) return new WP_Error( 'elementize_chatgpt_vision_zip_name_invalid', 'The ChatGPT vision archive entry name is invalid.' );
        $size = strlen( $bytes );
        if ( $size < 1 || $size > self::MAX_SCREENSHOT_BYTES ) return new WP_Error( 'elementize_chatgpt_vision_zip_size_invalid', 'The ChatGPT vision screenshot size is invalid.' );

        $crc = (int) hexdec( hash( 'crc32b', $bytes ) );
        $now = getdate();
        $year = max( 1980, min( 2107, (int) ( $now['year'] ?? 1980 ) ) );
        $month = max( 1, min( 12, (int) ( $now['mon'] ?? 1 ) ) );
        $day = max( 1, min( 31, (int) ( $now['mday'] ?? 1 ) ) );
        $hour = max( 0, min( 23, (int) ( $now['hours'] ?? 0 ) ) );
        $minute = max( 0, min( 59, (int) ( $now['minutes'] ?? 0 ) ) );
        $second = max( 0, min( 59, (int) ( $now['seconds'] ?? 0 ) ) );
        $dos_time = ( $hour << 11 ) | ( $minute << 5 ) | intdiv( $second, 2 );
        $dos_date = ( ( $year - 1980 ) << 9 ) | ( $month << 5 ) | $day;
        $name_length = strlen( $filename );

        $local_header = pack( 'VvvvvvVVVvv', 0x04034b50, 20, 0, 0, $dos_time, $dos_date, $crc, $size, $size, $name_length, 0 );
        $local_record = $local_header . $filename . $bytes;
        $central_header = pack( 'VvvvvvvVVVvvvvvVV', 0x02014b50, 20, 20, 0, 0, $dos_time, $dos_date, $crc, $size, $size, $name_length, 0, 0, 0, 0, 0, 0 );
        $central_record = $central_header . $filename;
        $end_record = pack( 'VvvvvVVv', 0x06054b50, 0, 0, 1, 1, strlen( $central_record ), strlen( $local_record ), 0 );

        return $local_record . $central_record . $end_record;
    }

    private static function clip( string $value, int $limit ): string {
        $value = sanitize_textarea_field( $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $limit ? mb_substr( $value, 0, $limit ) . '…' : $value;
        return strlen( $value ) > $limit ? substr( $value, 0, $limit ) . '…' : $value;
    }
}

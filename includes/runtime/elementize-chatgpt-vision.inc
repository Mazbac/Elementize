<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Native ChatGPT vision handoff for Rendered Visual QA.
 *
 * GPT Actions cannot return image/video files directly. When the existing
 * Visual QA action is called with analyze=true, this adapter reuses the proven
 * analyze=false Chromium capture job, wraps the completed PNG in a one-file ZIP
 * and returns it through openaiFileResponse. ChatGPT can then use Code
 * Interpreter to extract screenshot.png and inspect the actual image itself.
 *
 * This adapter is read-only. It never changes page content, never exposes the
 * internal preview URL and never claims visual_analysis_verified on the server.
 */
final class Elementize_ChatGPT_Vision {
    private const VERSION = '1';
    private const SCREENSHOT_ENTRY = 'screenshot.png';
    private const MAX_SCREENSHOT_BYTES = 10485760;

    public static function init(): void {
        add_filter( 'rest_request_before_callbacks', [ self::class, 'before_callbacks' ], 10, 3 );
    }

    public static function before_callbacks( $response, $handler, WP_REST_Request $request ) {
        if ( null !== $response ) return $response;

        $route = (string) $request->get_route();
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/(?P<id>\\d+)/visual-qa$#', $route ) ) return $response;

        $raw_analyze = $request->get_param( 'analyze' );
        $analyze = null === $raw_analyze ? true : (bool) filter_var( $raw_analyze, FILTER_VALIDATE_BOOLEAN );
        if ( ! $analyze ) return $response;

        $allowed = Elementize_Core::can_edit_page( $request );
        if ( is_wp_error( $allowed ) ) return $allowed;
        if ( true !== $allowed ) return new WP_Error( 'elementize_forbidden', 'You cannot edit this page.', [ 'status' => 403 ] );

        return self::handoff( absint( $request['id'] ) );
    }

    private static function handoff( int $id ) {
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
        $capture_version = self::visual_qa_version();
        if ( '' === $capture_version ) {
            return new WP_REST_Response( self::base_payload( $id, $loaded, $hash, $revision, '' ) + [
                'capture_state' => 'failed',
                'stage' => 'chatgpt_vision_handoff',
                'reason' => 'The active Rendered Visual QA version could not be resolved safely.',
            ], 200 );
        }

        // Always reuse an analyze=false browser job. The local vision backend is
        // therefore never invoked for the native ChatGPT vision path.
        $job_id = self::job_id( $id, $hash, $revision, false );
        $paths = self::temp_paths( $job_id );
        $base = self::base_payload( $id, $loaded, $hash, $revision, $capture_version );

        if ( self::png_file_complete( $paths['screenshot'] ) ) {
            return self::complete_payload( $base, $job_id, $paths['screenshot'] );
        }

        $job_key = 'elementize_visual_qa_job_v' . $capture_version . '_' . $job_id;
        $job = get_transient( $job_key );
        if ( is_array( $job ) ) {
            return new WP_REST_Response( self::pending_payload( $base, $job_id, $paths, $job ), 200 );
        }

        // No matching capture is active yet. Start the proven Chromium pipeline
        // directly with analyze=false. Later analyze=true calls poll this adapter
        // before the original Visual QA collector can consume/clean the PNG.
        $capture_request = new WP_REST_Request( 'GET', '/' . Elementize_Core::NS . '/pages/' . $id . '/visual-qa' );
        $capture_request->set_param( 'id', $id );
        $capture_request->set_param( 'analyze', false );
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

    private static function base_payload( int $id, array $loaded, string $hash, int $revision, string $capture_version ): array {
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
            'analysis_backend' => 'chatgpt_native_vision',
            'analysis_requested' => true,
            'analysis_ready' => false,
            'local_vision_required' => false,
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
        $base['retry_after_ms'] = 1500;
        $base['capture_elapsed_ms'] = isset( $job['started_microtime'] )
            ? max( 0, (int) round( ( microtime( true ) - (float) $job['started_microtime'] ) * 1000 ) )
            : 0;
        $base['capture_artifact_state'] = self::capture_artifact_state( $paths );
        $base['note'] = 'The real browser capture is running. Call the same read-only Visual QA action again with analyze=true. Do not switch to analyze=false while collecting the ChatGPT vision handoff.';
        return $base;
    }

    private static function complete_payload( array $base, string $job_id, string $screenshot ) {
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

        $base['capture_state'] = 'complete';
        $base['capture_job_id'] = $job_id;
        $base['visual_render_verified'] = true;
        $base['chatgpt_vision_handoff_ready'] = true;
        $base['screenshot'] = [
            'captured' => true,
            'mime_type' => 'image/png',
            'byte_count' => $byte_count,
            'sha256' => hash( 'sha256', $bytes ),
            'width' => $width,
            'height' => $height,
            'persisted' => false,
        ];
        $base['openaiFileResponse'] = [ [
            'name' => 'elementize-visual-' . $job_id . '.zip',
            'mime_type' => 'application/zip',
            'content' => base64_encode( $archive ),
        ] ];
        $base['note'] = 'Rendered capture succeeded. The returned ZIP contains screenshot.png. Use Code Interpreter/Data Analysis to extract it, then inspect the extracted PNG itself with ChatGPT vision. Do not claim visual analysis from metadata or ZIP existence alone.';

        return new WP_REST_Response( $base, 200 );
    }

    private static function visual_qa_version(): string {
        if ( ! class_exists( 'Elementize_Visual_QA' ) ) return '';
        try {
            $reflection = new ReflectionClass( Elementize_Visual_QA::class );
            $constant = $reflection->getReflectionConstant( 'VERSION' );
            if ( ! $constant ) return '';
            $value = trim( (string) $constant->getValue() );
            return preg_match( '/^\\d+$/', $value ) ? $value : '';
        } catch ( Throwable $error ) {
            return '';
        }
    }

    private static function job_id( int $id, string $hash, int $revision, bool $analyze ): string {
        return substr( hash( 'sha256', $id . "\n" . $revision . "\n" . $hash . "\n" . ( $analyze ? '1' : '0' ) ), 0, 24 );
    }

    private static function temp_paths( string $job_id ): array {
        $base = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() );
        $prefix = $base . 'elementize-visual-' . $job_id;
        return [
            'screenshot' => $prefix . '.png',
            'log' => $prefix . '.log',
            'done' => $prefix . '.done',
            'runner' => $prefix . ( 'Windows' === PHP_OS_FAMILY ? '.cmd' : '.sh' ),
        ];
    }

    private static function png_file_complete( string $path ): bool {
        if ( '' === $path || ! is_file( $path ) ) return false;
        clearstatcache( true, $path );
        $size = (int) @filesize( $path );
        if ( $size < 1000 || $size > self::MAX_SCREENSHOT_BYTES ) return false;
        $handle = @fopen( $path, 'rb' );
        if ( ! is_resource( $handle ) ) return false;
        $tail = '';
        if ( 0 === @fseek( $handle, -12, SEEK_END ) ) $tail = (string) @fread( $handle, 12 );
        @fclose( $handle );
        return "\x00\x00\x00\x00IEND\xAE\x42\x60\x82" === $tail;
    }

    private static function capture_artifact_state( array $paths ): array {
        $screenshot = (string) ( $paths['screenshot'] ?? '' );
        clearstatcache( true, $screenshot );
        return [
            'runner_exists' => isset( $paths['runner'] ) && is_string( $paths['runner'] ) && is_file( $paths['runner'] ),
            'log_exists' => isset( $paths['log'] ) && is_string( $paths['log'] ) && is_file( $paths['log'] ),
            'done_exists' => isset( $paths['done'] ) && is_string( $paths['done'] ) && is_file( $paths['done'] ),
            'screenshot_exists' => '' !== $screenshot && is_file( $screenshot ),
            'screenshot_bytes' => '' !== $screenshot && is_file( $screenshot ) ? max( 0, (int) @filesize( $screenshot ) ) : 0,
            'screenshot_complete' => self::png_file_complete( $screenshot ),
        ];
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

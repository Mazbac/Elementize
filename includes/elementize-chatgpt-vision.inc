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
    private const VERSION = '1';
    private const SCREENSHOT_ENTRY = 'screenshot.png';
    private const MAX_SCREENSHOT_BYTES = 10485760;

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
        $base['openaiFileResponse'] = [ [
            'name' => 'elementize-visual-' . $job_id . '.zip',
            'mime_type' => 'application/zip',
            'content' => base64_encode( $archive ),
        ] ];
        $base['note'] = 'Rendered capture succeeded. The returned ZIP contains screenshot.png. Extract screenshot.png from that conversation ZIP, then inspect the PNG itself with ChatGPT vision. Do not claim visual analysis from metadata or ZIP existence alone.';

        return new WP_REST_Response( $base, 200 );
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

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Visual_Localization {
    private const VERSION = '0.15.0';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const CHROME_PATH_CONST = 'ELEMENTIZE_CHROME_PATH';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const VIEWPORT_WIDTH = 1280;
    private const CAPTURE_HEIGHT = 9000;
    private const PAGE_TIMEOUT_MS = 6500;
    private const CAPTURE_TIMEOUT_SECONDS = 30;
    private const SCREENSHOT_STABLE_SECONDS = 0.7;
    private const MAX_SCREENSHOT_BYTES = 10485760;
    private const MAX_MARKERS = 24;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 335, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual']['available'] ) || empty( $data['visual']['screenshot']['captured'] ) ) return $response;
        $findings = is_array( $data['visual']['critique']['findings'] ?? null ) ? $data['visual']['critique']['findings'] : [];
        if ( ! $findings ) return $response;

        $data['visual']['localization'] = self::localize( absint( $m[1] ?? 0 ), $findings );
        $response->set_data( $data );
        return $response;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function localize( int $id, array $findings ): array {
        $marker_map = self::top_level_marker_map( $id );
        $base = [
            'localization_version' => self::VERSION,
            'available' => false,
            'advisory_only' => true,
            'clean_and_annotated_separation' => true,
            'annotated_second_pass' => true,
            'exact_top_level_id_mapping' => true,
            'marker_count' => count( $marker_map ),
            'marker_map' => $marker_map,
            'annotated_screenshot_persisted' => false,
            'annotated_screenshot_exposed' => false,
        ];
        if ( ! $marker_map ) { $base['reason'] = 'No top-level Elementor elements were available for visual localization.'; return $base; }

        $preview = self::fresh_signed_preview( $id );
        if ( is_wp_error( $preview ) ) { $base['reason'] = self::safe_error( $preview, 'Could not issue a fresh signed preview for localization.' ); return $base; }
        if ( empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) { $base['reason'] = self::clip( (string) ( $preview['reason'] ?? 'Signed visual preview unavailable.' ), 400 ); return $base; }

        $url = add_query_arg( 'elementize_locators', '1', (string) $preview['signed_url'] );
        $capture = self::capture_screenshot( $url, self::browser_path() );
        if ( is_wp_error( $capture ) ) { $base['reason'] = self::safe_error( $capture, 'Annotated screenshot capture failed.' ); $base['stage'] = 'capture'; return $base; }

        $base['annotated_screenshot'] = [
            'captured' => true,
            'mime_type' => 'image/png',
            'byte_count' => $capture['byte_count'],
            'sha256' => $capture['sha256'],
            'width' => $capture['width'],
            'height' => $capture['height'],
            'capture_wait_seconds' => $capture['wait_seconds'],
            'persisted' => false,
            'exposed' => false,
        ];

        $localized = self::analyze_localization( $capture['bytes'], $findings, $marker_map );
        @unlink( $capture['path'] );
        unset( $capture['bytes'] );
        if ( is_wp_error( $localized ) ) { $base['reason'] = self::safe_error( $localized, 'Visual localization failed.' ); $base['stage'] = 'vision'; return $base; }

        $lookup = [];
        foreach ( $marker_map as $entry ) $lookup[(string) $entry['marker']] = (string) $entry['top_level_element_id'];
        $out = [];
        foreach ( $localized as $item ) {
            $index = absint( $item['finding_index'] ?? -1 );
            $markers = [];
            foreach ( is_array( $item['section_markers'] ?? null ) ? $item['section_markers'] : [] as $marker ) {
                $marker = strtoupper( trim( (string) $marker ) );
                if ( isset( $lookup[$marker] ) ) $markers[] = $marker;
            }
            $markers = array_values( array_unique( array_slice( $markers, 0, 3 ) ) );
            $ids = [];
            foreach ( $markers as $marker ) $ids[] = $lookup[$marker];
            $out[] = [
                'finding_index' => $index,
                'finding_category' => self::clip( (string) ( $findings[$index]['category'] ?? 'visual' ), 80 ),
                'section_markers' => $markers,
                'top_level_element_ids' => $ids,
                'confidence' => in_array( (string) ( $item['confidence'] ?? '' ), [ 'low', 'medium', 'high' ], true ) ? (string) $item['confidence'] : 'low',
                'rationale' => self::clip( (string) ( $item['rationale'] ?? '' ), 500 ),
            ];
        }

        $base['available'] = true;
        $base['localized_findings'] = $out;
        $base['note'] = 'Markers are internal visual locators mapped to exact top-level Elementor IDs. Localization is advisory and does not authorize a write.';
        return $base;
    }

    private static function top_level_marker_map( int $id ): array {
        $elements = null;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $doc = \Elementor\Plugin::$instance->documents->get( $id );
                $elements = $doc ? $doc->get_elements_data() : null;
            } catch ( Throwable $ignored ) {}
        }
        if ( ! is_array( $elements ) ) {
            $raw = get_post_meta( $id, '_elementor_data', true );
            $elements = is_string( $raw ) ? json_decode( $raw, true ) : null;
        }
        if ( ! is_array( $elements ) ) return [];

        $out = [];
        $n = 0;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $eid = trim( (string) ( $element['id'] ?? '' ) );
            if ( '' === $eid || ! preg_match( '/^[A-Za-z0-9_-]+$/', $eid ) ) continue;
            $n++;
            if ( $n > self::MAX_MARKERS ) break;
            $out[] = [ 'marker' => 'S' . $n, 'top_level_element_id' => $eid ];
        }
        return $out;
    }

    private static function fresh_signed_preview( int $id ) {
        $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/state' );
        $request->set_param( 'id', $id );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_localization_preview_failed', 'Fresh page state could not be read.' );
        $data = $response->get_data();
        return is_array( $data ) && is_array( $data['visual_preview'] ?? null ) ? $data['visual_preview'] : new WP_Error( 'elementize_localization_preview_missing', 'Fresh page state did not provide a signed visual preview.' );
    }

    private static function browser_path(): string {
        if ( defined( self::CHROME_PATH_CONST ) ) {
            $path = trim( (string) constant( self::CHROME_PATH_CONST ) );
            if ( '' !== $path && is_file( $path ) ) return $path;
        }
        $candidates = [];
        foreach ( [ getenv( 'LOCALAPPDATA' ), getenv( 'ProgramFiles' ), getenv( 'ProgramFiles(x86)' ) ] as $base ) {
            if ( ! is_string( $base ) || '' === $base ) continue;
            $candidates[] = $base . '\\Google\\Chrome\\Application\\chrome.exe';
            $candidates[] = $base . '\\Microsoft\\Edge\\Application\\msedge.exe';
        }
        $candidates = array_merge( $candidates, [
            '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
            '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
            '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium', '/usr/bin/chromium-browser',
            '/usr/bin/microsoft-edge', '/usr/bin/microsoft-edge-stable',
        ] );
        foreach ( array_unique( $candidates ) as $candidate ) if ( is_file( $candidate ) ) return $candidate;
        return '';
    }

    private static function temp_path( string $suffix ): string {
        $dir = function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir();
        return trailingslashit( $dir ) . 'elementize-locator-' . wp_generate_uuid4() . $suffix;
    }

    private static function temp_profile_dir(): string {
        $dir = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() ) . 'elementize-locator-profile-' . wp_generate_uuid4();
        return wp_mkdir_p( $dir ) && is_dir( $dir ) ? $dir : '';
    }

    private static function remove_tree( string $path ): void {
        if ( '' === $path || ! is_dir( $path ) ) return;
        foreach ( @scandir( $path ) ?: [] as $item ) {
            if ( '.' === $item || '..' === $item ) continue;
            $child = $path . DIRECTORY_SEPARATOR . $item;
            if ( is_dir( $child ) && ! is_link( $child ) ) self::remove_tree( $child ); else @unlink( $child );
        }
        @rmdir( $path );
    }

    private static function stop_process( $proc ): void {
        if ( ! is_resource( $proc ) ) return;
        $status = @proc_get_status( $proc );
        if ( is_array( $status ) && ! empty( $status['running'] ) ) @proc_terminate( $proc );
        usleep( 200000 );
        $status = @proc_get_status( $proc );
        if ( is_array( $status ) && ! empty( $status['running'] ) ) @proc_terminate( $proc, 9 );
        @proc_close( $proc );
    }

    private static function capture_screenshot( string $url, string $browser ) {
        if ( ! function_exists( 'proc_open' ) || '' === $browser || ! is_file( $browser ) ) return new WP_Error( 'elementize_locator_browser_unavailable', 'Local Chrome/Chromium/Edge capture is unavailable.' );
        $path = self::temp_path( '.png' );
        $stdout = self::temp_path( '.stdout.log' );
        $stderr = self::temp_path( '.stderr.log' );
        $profile = self::temp_profile_dir();
        if ( '' === $profile ) return new WP_Error( 'elementize_locator_profile_failed', 'Could not create an isolated browser profile.' );

        $command = [
            $browser, '--headless', '--user-data-dir=' . $profile, '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars', '--mute-audio',
            '--timeout=' . self::PAGE_TIMEOUT_MS, '--window-size=' . self::VIEWPORT_WIDTH . ',' . self::CAPTURE_HEIGHT, '--screenshot=' . $path, $url,
        ];
        $spec = [ 0 => [ 'file', DIRECTORY_SEPARATOR === '\\' ? 'NUL' : '/dev/null', 'r' ], 1 => [ 'file', $stdout, 'a' ], 2 => [ 'file', $stderr, 'a' ] ];
        $pipes = [];
        $proc = @proc_open( $command, $spec, $pipes, null, null, [ 'bypass_shell' => true ] );
        if ( ! is_resource( $proc ) ) { @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile ); return new WP_Error( 'elementize_locator_browser_start_failed', 'Could not start the local headless browser.' ); }

        $started = microtime( true );
        $last_size = -1; $stable_since = null; $captured = false;
        while ( microtime( true ) - $started <= self::CAPTURE_TIMEOUT_SECONDS ) {
            clearstatcache( true, $path );
            if ( is_file( $path ) ) {
                $size = (int) @filesize( $path );
                if ( $size >= 1000 ) {
                    if ( $size === $last_size ) {
                        if ( null === $stable_since ) $stable_since = microtime( true );
                        if ( microtime( true ) - $stable_since >= self::SCREENSHOT_STABLE_SECONDS ) { $captured = true; break; }
                    } else { $last_size = $size; $stable_since = microtime( true ); }
                }
            }
            $status = @proc_get_status( $proc );
            if ( ! is_array( $status ) || empty( $status['running'] ) ) break;
            usleep( 100000 );
        }
        $wait = round( microtime( true ) - $started, 2 );
        self::stop_process( $proc );
        @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile );
        clearstatcache( true, $path );
        if ( ! $captured && is_file( $path ) && (int) @filesize( $path ) >= 1000 ) $captured = true;
        if ( ! $captured ) { @unlink( $path ); return new WP_Error( 'elementize_locator_no_screenshot', 'Chrome did not create the annotated screenshot.' ); }

        $bytes = @file_get_contents( $path );
        if ( false === $bytes || '' === $bytes ) { @unlink( $path ); return new WP_Error( 'elementize_locator_read_failed', 'The annotated screenshot could not be read.' ); }
        if ( strlen( $bytes ) > self::MAX_SCREENSHOT_BYTES ) { @unlink( $path ); return new WP_Error( 'elementize_locator_too_large', 'The annotated screenshot exceeded the 10 MB safety limit.' ); }
        $width = null; $height = null;
        if ( function_exists( 'getimagesizefromstring' ) ) {
            $size = @getimagesizefromstring( $bytes );
            if ( is_array( $size ) ) { $width = absint( $size[0] ?? 0 ) ?: null; $height = absint( $size[1] ?? 0 ) ?: null; }
        }
        return [ 'path' => $path, 'bytes' => $bytes, 'byte_count' => strlen( $bytes ), 'sha256' => hash( 'sha256', $bytes ), 'width' => $width, 'height' => $height, 'wait_seconds' => $wait ];
    }

    private static function ollama_url(): string {
        $url = defined( self::OLLAMA_URL_CONST ) ? trim( (string) constant( self::OLLAMA_URL_CONST ) ) : self::DEFAULT_OLLAMA_URL;
        $url = untrailingslashit( $url );
        return preg_match( '#^https?://(?:127\.0\.0\.1|localhost)(?::\d+)?$#i', $url ) ? $url : self::DEFAULT_OLLAMA_URL;
    }

    private static function model(): string {
        $model = defined( self::MODEL_CONST ) ? trim( (string) constant( self::MODEL_CONST ) ) : self::DEFAULT_MODEL;
        return '' !== $model ? $model : self::DEFAULT_MODEL;
    }

    private static function localization_schema( array $markers, int $count ): array {
        return [
            'type' => 'object', 'additionalProperties' => false,
            'properties' => [
                'localizations' => [
                    'type' => 'array', 'minItems' => $count, 'maxItems' => $count,
                    'items' => [
                        'type' => 'object', 'additionalProperties' => false,
                        'properties' => [
                            'finding_index' => [ 'type' => 'integer', 'minimum' => 0, 'maximum' => max( 0, $count - 1 ) ],
                            'section_markers' => [ 'type' => 'array', 'maxItems' => 3, 'items' => [ 'type' => 'string', 'enum' => $markers ] ],
                            'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                            'rationale' => [ 'type' => 'string', 'minLength' => 1 ],
                        ],
                        'required' => [ 'finding_index', 'section_markers', 'confidence', 'rationale' ],
                    ],
                ],
            ],
            'required' => [ 'localizations' ],
        ];
    }

    private static function analyze_localization( string $bytes, array $findings, array $marker_map ) {
        $markers = array_values( array_map( static fn( $entry ) => (string) $entry['marker'], $marker_map ) );
        $count = min( 8, count( $findings ) );
        $findings = array_slice( $findings, 0, $count );
        $schema = self::localization_schema( $markers, $count );
        $compact = [];
        foreach ( $findings as $i => $finding ) {
            $compact[] = [ 'finding_index' => $i, 'category' => (string) ( $finding['category'] ?? 'visual' ), 'severity' => (string) ( $finding['severity'] ?? 'warning' ), 'evidence' => self::clip( (string) ( $finding['evidence'] ?? '' ), 350 ) ];
        }
        $prompt = 'This is an annotated screenshot of the same landing page already critiqued. Bright section labels S1, S2, etc. mark exact top-level Elementor sections. Localize each supplied visual finding to up to three visible section markers. Return [] when a finding is genuinely page-wide or no exact section is defensible. Do not infer a marker from ordering alone; use visible evidence. Return exactly one item for every finding_index. Findings: ' . wp_json_encode( $compact, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        $payload = [ 'model' => self::model(), 'messages' => [ [ 'role' => 'user', 'content' => $prompt, 'images' => [ base64_encode( $bytes ) ] ] ], 'stream' => false, 'format' => $schema, 'options' => [ 'temperature' => 0 ] ];
        $json = wp_json_encode( $payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        unset( $payload );
        if ( false === $json ) return new WP_Error( 'elementize_locator_encode_failed', 'Could not encode the localization request.' );
        $response = wp_remote_post( self::ollama_url() . '/api/chat', [ 'headers' => [ 'Content-Type' => 'application/json' ], 'body' => $json, 'timeout' => 180, 'redirection' => 0, 'sslverify' => false, 'limit_response_size' => 1048576, 'data_format' => 'body' ] );
        unset( $json );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_locator_http_failed', self::clip( $response->get_error_message(), 300 ) );
        if ( 200 !== (int) wp_remote_retrieve_response_code( $response ) ) return new WP_Error( 'elementize_locator_model_failed', 'Ollama returned HTTP ' . (int) wp_remote_retrieve_response_code( $response ) . ' during localization.' );
        $decoded = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        $content = is_array( $decoded ) ? trim( (string) ( $decoded['message']['content'] ?? '' ) ) : '';
        $parsed = '' !== $content ? json_decode( $content, true ) : null;
        $items = is_array( $parsed['localizations'] ?? null ) ? $parsed['localizations'] : null;
        if ( ! is_array( $items ) || count( $items ) !== $count ) return new WP_Error( 'elementize_locator_incomplete', 'The local vision model did not return one localization for every finding.' );

        $seen = [];
        foreach ( $items as $item ) {
            if ( ! is_array( $item ) ) return new WP_Error( 'elementize_locator_invalid', 'The localization response was malformed.' );
            $idx = absint( $item['finding_index'] ?? -1 );
            if ( $idx >= $count || isset( $seen[$idx] ) ) return new WP_Error( 'elementize_locator_indices_invalid', 'The localization response contained duplicate or invalid finding indexes.' );
            $seen[$idx] = true;
        }
        if ( count( $seen ) !== $count ) return new WP_Error( 'elementize_locator_indices_missing', 'The localization response omitted a finding index.' );
        usort( $items, static fn( $a, $b ) => (int) $a['finding_index'] <=> (int) $b['finding_index'] );
        return $items;
    }

    private static function safe_error( WP_Error $error, string $fallback ): string {
        $message = trim( (string) $error->get_error_message() );
        return self::clip( '' !== $message ? $message : $fallback, 500 );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\s+/', ' ', $value ) ?? $value );
        return strlen( $value ) <= $max ? $value : substr( $value, 0, max( 0, $max - 1 ) ) . '…';
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_localization'] = true;
        $data['rendered_visual_localization_advisory_only'] = true;
        $data['rendered_visual_localization_annotated_second_pass'] = true;
        $data['rendered_visual_localization_clean_and_annotated_separation'] = true;
        $data['rendered_visual_localization_exact_top_level_id_mapping'] = true;
        $data['rendered_visual_localization_max_markers'] = self::MAX_MARKERS;
        $data['rendered_visual_localization_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

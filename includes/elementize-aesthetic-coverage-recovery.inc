<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Brain', false ) ) return;

/**
 * Recovery pass for incomplete Aesthetic Brain section coverage.
 *
 * This is intentionally read-only. When the first art-direction pass returns
 * insufficient section coverage, this pass captures a fresh locator-annotated
 * render and asks the local vision model for exactly one evidence-bearing ledger
 * row per visible top-level section. A complete ledger may restore reviewability,
 * but never grants mutation authority.
 */
final class Elementize_Aesthetic_Coverage_Recovery {
    private const VERSION = '0.26.1-r1';
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
    private const VISION_TIMEOUT_SECONDS = 150;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 533, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) ) return $response;
        $visual = $data['visual'];
        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];
        $ratio = is_numeric( $coherence['section_coverage_ratio'] ?? null ) ? (float) $coherence['section_coverage_ratio'] : 0.0;

        if ( ! empty( $coherence['assessment_complete'] ) && $ratio >= 0.60 ) {
            $visual['aesthetic_coverage_recovery'] = [
                'version' => self::VERSION,
                'available' => true,
                'needed' => false,
                'read_only' => true,
                'writes_performed' => false,
                'automatic_write_allowed' => false,
                'reason' => 'The primary aesthetic pass already had sufficient section coverage.',
            ];
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $id = absint( $m[1] ?? 0 );
        $marker_map = self::top_level_marker_map( $id );
        $diag = [
            'version' => self::VERSION,
            'available' => false,
            'needed' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'provider' => 'local_ollama',
            'model' => self::model(),
            'exact_section_contract' => true,
            'marker_count' => count( $marker_map ),
            'covered_section_count' => 0,
            'coverage_ratio' => 0.0,
            'screenshot_persisted' => false,
            'screenshot_exposed' => false,
            'signed_preview_url_exposed' => false,
            'action_slot_cost' => 0,
        ];

        if ( $id < 1 || ! $marker_map ) {
            $diag['reason'] = 'No exact top-level section marker map is available.';
            $visual['aesthetic_coverage_recovery'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $browser = self::browser_path();
        if ( '' === $browser || ! function_exists( 'proc_open' ) || ! self::ollama_ready() ) {
            $diag['reason'] = 'Local Chrome/Chromium/Edge and Ollama are required for exact aesthetic coverage recovery.';
            $visual['aesthetic_coverage_recovery'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $preview = self::fresh_signed_preview( $id );
        if ( is_wp_error( $preview ) || empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) {
            $diag['reason'] = is_wp_error( $preview ) ? self::safe_error( $preview, 'A fresh signed preview is unavailable.' ) : 'A fresh signed preview is unavailable.';
            $visual['aesthetic_coverage_recovery'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $capture = self::capture_screenshot( add_query_arg( 'elementize_locators', '1', (string) $preview['signed_url'] ), $browser );
        unset( $preview );
        if ( is_wp_error( $capture ) ) {
            $diag['reason'] = self::safe_error( $capture, 'Exact-section aesthetic recovery capture failed.' );
            $visual['aesthetic_coverage_recovery'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        try {
            $ledger = self::analyze( $capture['bytes'], $marker_map, $id );
        } finally {
            @unlink( $capture['path'] );
            unset( $capture['bytes'] );
        }
        if ( is_wp_error( $ledger ) ) {
            $diag['reason'] = self::safe_error( $ledger, 'The exact-section aesthetic ledger was incomplete.' );
            $visual['aesthetic_coverage_recovery'] = $diag;
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $lookup = [];
        foreach ( $marker_map as $entry ) $lookup[(string) $entry['marker']] = (string) $entry['top_level_element_id'];
        $expected = array_keys( $lookup );
        $rows = [];
        $issues = [];
        $violations = [];
        $seen = [];
        foreach ( is_array( $ledger['sections'] ?? null ) ? $ledger['sections'] : [] as $index => $row ) {
            if ( ! is_array( $row ) ) continue;
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            if ( ! isset( $lookup[$marker] ) || isset( $seen[$marker] ) ) continue;
            if ( ! isset( $expected[$index] ) || $expected[$index] !== $marker ) continue;
            $seen[$marker] = true;
            $fit = self::enum( $row['coherence_fit'] ?? '', [ 'fits', 'mixed', 'violates' ], 'mixed' );
            $severity = self::enum( $row['severity'] ?? '', [ 'none', 'low', 'medium', 'high' ], 'none' );
            $category = self::enum( $row['issue_category'] ?? '', [ 'none', 'hierarchy', 'spacing', 'density', 'typography', 'imagery', 'component_style', 'composition', 'transition', 'conversion', 'section_choice', 'other' ], 'other' );
            $rows[] = [
                'marker' => $marker,
                'top_level_element_id' => $lookup[$marker],
                'visual_intensity' => self::score( $row['visual_intensity'] ?? 1 ),
                'role' => self::clip( (string) ( $row['role'] ?? '' ), 140 ),
                'transition_from_previous' => self::enum( $row['transition_from_previous'] ?? '', [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ], 'neutral' ),
                'coherence_fit' => $fit,
                'evidence' => self::clip( (string) ( $row['evidence'] ?? '' ), 500 ),
            ];
            if ( 'violates' === $fit ) {
                $violations[] = [
                    'marker' => $marker,
                    'top_level_element_id' => $lookup[$marker],
                    'mismatch_type' => self::mismatch_type( $category ),
                    'reason' => self::clip( (string) ( $row['evidence'] ?? '' ), 450 ),
                    'automatic_write_allowed' => false,
                ];
            }
            if ( in_array( $severity, [ 'medium', 'high' ], true ) && 'none' !== $category ) {
                $issues[] = [
                    'category' => $category,
                    'severity' => $severity,
                    'section_markers' => [ $marker ],
                    'top_level_element_ids' => [ $lookup[$marker] ],
                    'evidence' => self::clip( (string) ( $row['evidence'] ?? '' ), 500 ),
                    'recommendation' => self::clip( (string) ( $row['recommendation'] ?? '' ), 500 ),
                    'intervention_level' => self::enum( $row['intervention_level'] ?? '', [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ], 'none' ),
                    'source' => 'exact_section_aesthetic_coverage_recovery',
                    'automatic_write_allowed' => false,
                ];
            }
        }

        $complete = count( $rows ) === count( $marker_map ) && count( $seen ) === count( $marker_map );
        $diag['covered_section_count'] = count( $rows );
        $diag['coverage_ratio'] = count( $marker_map ) ? round( count( $rows ) / count( $marker_map ), 3 ) : 0.0;
        $diag['exact_section_coverage_complete'] = $complete;
        $diag['overall_approval'] = self::enum( $ledger['overall_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' );
        $diag['confidence'] = self::enum( $ledger['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' );
        $diag['overall_rationale'] = self::clip( (string) ( $ledger['overall_rationale'] ?? '' ), 800 );
        $diag['section_ledger'] = $rows;
        $diag['issues'] = array_slice( $issues, 0, 6 );
        $diag['available'] = $complete;
        if ( ! $complete ) $diag['reason'] = 'The local model did not return exactly one valid, ordered section ledger row for every visible marker.';

        if ( $complete ) {
            $coherence['rhythm_sequence'] = array_map( static function ( array $row ): array {
                return [
                    'marker' => $row['marker'],
                    'top_level_element_id' => $row['top_level_element_id'],
                    'visual_intensity' => $row['visual_intensity'],
                    'role' => $row['role'],
                    'transition_from_previous' => $row['transition_from_previous'],
                ];
            }, $rows );
            $coherence['covered_section_count'] = count( $rows );
            $coherence['section_coverage_ratio'] = 1.0;
            $coherence['assessment_complete'] = true;
            if ( $violations ) $coherence['violating_sections'] = array_slice( $violations, 0, 5 );
            $coherence['coverage_recovery_version'] = self::VERSION;

            $base_approval = self::enum( $art['professional_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' );
            $recovery_approval = $diag['overall_approval'];
            $effective = self::more_conservative_approval( $base_approval, $recovery_approval );
            $art['model_professional_approval'] = $base_approval;
            $art['coverage_recovery_professional_approval'] = $recovery_approval;
            $art['professional_approval'] = $effective;
            $art['section_coverage_ratio'] = 1.0;
            $art['assessment_complete'] = true;
            $art['approval_usable'] = in_array( $diag['confidence'], [ 'medium', 'high' ], true );
            $art['coverage_recovery_version'] = self::VERSION;
            $art['coverage_recovery_rationale'] = $diag['overall_rationale'];
            if ( $issues ) {
                $existing = is_array( $art['top_issues'] ?? null ) ? $art['top_issues'] : [];
                $art['top_issues'] = array_slice( array_merge( $existing, $issues ), 0, 4 );
            }

            $design = is_array( $visual['design_dna'] ?? null ) ? $visual['design_dna'] : [];
            $design['assessment_complete'] = true;
            $design['usable_as_page_design_context'] = in_array( (string) ( $design['confidence'] ?? '' ), [ 'medium', 'high' ], true );
            $design['coverage_recovery_version'] = self::VERSION;
            $visual['design_dna'] = $design;
        }

        $visual['page_art_direction'] = $art;
        $visual['section_coherence'] = $coherence;
        $visual['aesthetic_coverage_recovery'] = $diag;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function analyze( string $bytes, array $marker_map, int $id ) {
        $markers = array_map( static fn ( array $entry ): string => (string) $entry['marker'], $marker_map );
        $count = count( $markers );
        $prompt = "You are performing a strict second-pass section-by-section art-direction audit. The first page-level pass did not cover enough sections. Inspect the attached locator-annotated landing page and return EXACTLY one section row for every marker, in this exact order: " . implode( ', ', $markers ) . ".\n\n" .
            "Do not skip sections. Do not invent markers. For each section, describe its actual visible role, visual intensity, transition quality, whether it fits the page-wide design language, and the strongest visible aesthetic issue if one exists. Judge aesthetics like a demanding senior SaaS web designer: optical balance, typography, whitespace rhythm, imagery, density, component language, composition, conversion emphasis, and whether the section feels like it belongs.\n\n" .
            "Do not approve merely because the page is clean. 'approve' means the complete page would be client-presentable without another aesthetic pass. If important sections feel generic, inconsistent, visually weak, poorly art-directed, or structurally mismatched, choose revise or reject. Do not recommend exact CSS pixel values. Return JSON only. Page title for context: " . self::clip( (string) get_the_title( $id ), 160 );

        $section_item = [
            'type' => 'object',
            'additionalProperties' => false,
            'required' => [ 'marker', 'visual_intensity', 'role', 'transition_from_previous', 'coherence_fit', 'issue_category', 'severity', 'evidence', 'recommendation', 'intervention_level' ],
            'properties' => [
                'marker' => [ 'type' => 'string' ],
                'visual_intensity' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ],
                'role' => [ 'type' => 'string' ],
                'transition_from_previous' => [ 'type' => 'string', 'enum' => [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ] ],
                'coherence_fit' => [ 'type' => 'string', 'enum' => [ 'fits', 'mixed', 'violates' ] ],
                'issue_category' => [ 'type' => 'string', 'enum' => [ 'none', 'hierarchy', 'spacing', 'density', 'typography', 'imagery', 'component_style', 'composition', 'transition', 'conversion', 'section_choice', 'other' ] ],
                'severity' => [ 'type' => 'string', 'enum' => [ 'none', 'low', 'medium', 'high' ] ],
                'evidence' => [ 'type' => 'string' ],
                'recommendation' => [ 'type' => 'string' ],
                'intervention_level' => [ 'type' => 'string', 'enum' => [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ] ],
            ],
        ];
        $schema = [
            'type' => 'object',
            'additionalProperties' => false,
            'required' => [ 'sections', 'overall_approval', 'confidence', 'overall_rationale' ],
            'properties' => [
                'sections' => [ 'type' => 'array', 'minItems' => $count, 'maxItems' => $count, 'items' => $section_item ],
                'overall_approval' => [ 'type' => 'string', 'enum' => [ 'approve', 'revise', 'reject' ] ],
                'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                'overall_rationale' => [ 'type' => 'string' ],
            ],
        ];
        $body = [
            'model' => self::model(),
            'prompt' => $prompt,
            'images' => [ base64_encode( $bytes ) ],
            'stream' => false,
            'format' => $schema,
            'options' => [ 'temperature' => 0 ],
        ];
        $response = wp_remote_post( self::ollama_url() . '/api/generate', [
            'timeout' => self::VISION_TIMEOUT_SECONDS,
            'redirection' => 0,
            'sslverify' => false,
            'headers' => [ 'Content-Type' => 'application/json', 'Accept' => 'application/json' ],
            'body' => wp_json_encode( $body, JSON_UNESCAPED_SLASHES ),
            'limit_response_size' => 4194304,
        ] );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_aesthetic_recovery_ollama_failed', 'The local Ollama recovery request failed.' );
        if ( 200 !== (int) wp_remote_retrieve_response_code( $response ) ) return new WP_Error( 'elementize_aesthetic_recovery_ollama_http', 'Local Ollama returned a non-200 recovery response.' );
        $outer = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_aesthetic_recovery_envelope_invalid', 'The local recovery response envelope was invalid.' );
        $decoded = json_decode( trim( (string) $outer['response'] ), true );
        if ( ! is_array( $decoded ) || ! is_array( $decoded['sections'] ?? null ) || count( $decoded['sections'] ) !== $count ) {
            return new WP_Error( 'elementize_aesthetic_recovery_schema_incomplete', 'The local model did not satisfy the exact section-ledger contract.' );
        }
        return $decoded;
    }

    private static function more_conservative_approval( string $a, string $b ): string {
        $rank = [ 'approve' => 0, 'revise' => 1, 'reject' => 2 ];
        return ( $rank[$a] ?? 1 ) >= ( $rank[$b] ?? 1 ) ? $a : $b;
    }

    private static function mismatch_type( string $category ): string {
        $map = [
            'hierarchy' => 'composition', 'spacing' => 'spacing', 'density' => 'density', 'typography' => 'typography',
            'imagery' => 'imagery', 'component_style' => 'component_style', 'composition' => 'composition',
            'transition' => 'composition', 'conversion' => 'composition', 'section_choice' => 'section_choice', 'other' => 'other', 'none' => 'other',
        ];
        return $map[$category] ?? 'other';
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
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_aesthetic_recovery_preview_failed', 'Fresh page state could not be read.' );
        $data = $response->get_data();
        return is_array( $data ) && is_array( $data['visual_preview'] ?? null ) ? $data['visual_preview'] : new WP_Error( 'elementize_aesthetic_recovery_preview_missing', 'Fresh page state did not provide a signed visual preview.' );
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

    private static function ollama_url(): string {
        $url = defined( self::OLLAMA_URL_CONST ) ? trim( (string) constant( self::OLLAMA_URL_CONST ) ) : self::DEFAULT_OLLAMA_URL;
        $url = untrailingslashit( $url );
        return preg_match( '#^https?://(?:127\\.0\\.0\\.1|localhost)(?::\\d+)?$#i', $url ) ? $url : self::DEFAULT_OLLAMA_URL;
    }

    private static function model(): string {
        $model = defined( self::MODEL_CONST ) ? trim( (string) constant( self::MODEL_CONST ) ) : self::DEFAULT_MODEL;
        return '' !== $model ? $model : self::DEFAULT_MODEL;
    }

    private static function ollama_ready(): bool {
        $response = wp_remote_get( self::ollama_url() . '/api/tags', [ 'timeout' => 2, 'redirection' => 0, 'sslverify' => false, 'limit_response_size' => 1048576 ] );
        if ( is_wp_error( $response ) || 200 !== (int) wp_remote_retrieve_response_code( $response ) ) return false;
        $decoded = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        foreach ( is_array( $decoded['models'] ?? null ) ? $decoded['models'] : [] as $entry ) {
            if ( ! is_array( $entry ) ) continue;
            $name = (string) ( $entry['name'] ?? $entry['model'] ?? '' );
            if ( $name === self::model() || ( 'gemma3:4b' === self::model() && 'gemma3:latest' === $name ) ) return true;
        }
        return false;
    }

    private static function temp_path( string $suffix ): string {
        $dir = function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir();
        return trailingslashit( $dir ) . 'elementize-aesthetic-recovery-' . wp_generate_uuid4() . $suffix;
    }

    private static function temp_profile_dir(): string {
        $dir = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() ) . 'elementize-aesthetic-recovery-profile-' . wp_generate_uuid4();
        return wp_mkdir_p( $dir ) && is_dir( $dir ) ? $dir : '';
    }

    private static function capture_screenshot( string $url, string $browser ) {
        $path = self::temp_path( '.png' );
        $stdout = self::temp_path( '.stdout.log' );
        $stderr = self::temp_path( '.stderr.log' );
        $profile = self::temp_profile_dir();
        if ( '' === $profile ) return new WP_Error( 'elementize_aesthetic_recovery_profile_failed', 'Could not create an isolated browser profile.' );
        $command = [
            $browser, '--headless', '--user-data-dir=' . $profile, '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars', '--mute-audio',
            '--timeout=' . self::PAGE_TIMEOUT_MS, '--window-size=' . self::VIEWPORT_WIDTH . ',' . self::CAPTURE_HEIGHT, '--screenshot=' . $path, $url,
        ];
        $spec = [ 0 => [ 'file', DIRECTORY_SEPARATOR === '\\' ? 'NUL' : '/dev/null', 'r' ], 1 => [ 'file', $stdout, 'a' ], 2 => [ 'file', $stderr, 'a' ] ];
        $pipes = [];
        $proc = @proc_open( $command, $spec, $pipes, null, null, [ 'bypass_shell' => true ] );
        if ( ! is_resource( $proc ) ) {
            @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile );
            return new WP_Error( 'elementize_aesthetic_recovery_browser_start_failed', 'Could not start the local headless browser.' );
        }
        $started = microtime( true );
        $last_size = -1;
        $stable_since = null;
        $captured = false;
        while ( microtime( true ) - $started <= self::CAPTURE_TIMEOUT_SECONDS ) {
            clearstatcache( true, $path );
            if ( is_file( $path ) ) {
                $size = (int) @filesize( $path );
                if ( $size >= 1000 ) {
                    if ( $size === $last_size ) {
                        if ( null === $stable_since ) $stable_since = microtime( true );
                        if ( microtime( true ) - $stable_since >= self::SCREENSHOT_STABLE_SECONDS ) { $captured = true; break; }
                    } else {
                        $last_size = $size;
                        $stable_since = microtime( true );
                    }
                }
            }
            $status = @proc_get_status( $proc );
            if ( ! is_array( $status ) || empty( $status['running'] ) ) break;
            usleep( 100000 );
        }
        self::stop_process( $proc );
        @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile );
        clearstatcache( true, $path );
        if ( ! $captured && is_file( $path ) && (int) @filesize( $path ) >= 1000 ) $captured = true;
        if ( ! $captured ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_recovery_no_screenshot', 'Chrome did not create the recovery screenshot.' ); }
        $bytes = @file_get_contents( $path );
        if ( false === $bytes || '' === $bytes ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_recovery_read_failed', 'The recovery screenshot could not be read.' ); }
        if ( strlen( $bytes ) > self::MAX_SCREENSHOT_BYTES ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_recovery_too_large', 'The recovery screenshot exceeded the 10 MB safety limit.' ); }
        return [ 'path' => $path, 'bytes' => $bytes ];
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

    private static function remove_tree( string $path ): void {
        if ( '' === $path || ! is_dir( $path ) ) return;
        foreach ( @scandir( $path ) ?: [] as $item ) {
            if ( '.' === $item || '..' === $item ) continue;
            $child = $path . DIRECTORY_SEPARATOR . $item;
            if ( is_dir( $child ) && ! is_link( $child ) ) self::remove_tree( $child ); else @unlink( $child );
        }
        @rmdir( $path );
    }

    private static function enum( $value, array $allowed, string $fallback ): string {
        $value = strtolower( trim( (string) $value ) );
        return in_array( $value, $allowed, true ) ? $value : $fallback;
    }

    private static function score( $value ): int {
        return max( 1, min( 10, absint( $value ) ) );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( wp_strip_all_tags( $value ) );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function safe_error( WP_Error $error, string $fallback ): string {
        $message = trim( (string) $error->get_error_message() );
        return self::clip( '' !== $message ? $message : $fallback, 600 );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_coverage_recovery'] = true;
        $data['aesthetic_coverage_recovery_version'] = self::VERSION;
        $data['aesthetic_coverage_recovery_read_only'] = true;
        $data['aesthetic_coverage_recovery_automatic_writes'] = false;
        $response->set_data( $data );
        return $response;
    }
}

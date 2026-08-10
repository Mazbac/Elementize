<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Judgment', false ) ) return;

/**
 * Narrow transport recovery for contextual Pixfort A/B judgment.
 *
 * 0.27.0-a1 used Ollama /api/chat with raw Pixfort thumbnail bytes. Pixfort can
 * legitimately return WebP/GIF thumbnails while the local multimodal decoder may
 * reject those bytes. This hardening runs only after that exact HTTP-400 failure,
 * preserves the grounded context and candidate set, normalizes unsupported thumbnail
 * formats to PNG locally, and retries through the already runtime-proven /api/generate
 * multimodal contract. It never mutates page state.
 */
final class Elementize_Aesthetic_AB_Transport_Hardening {
    private const VERSION = '0.27.0-h1';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const VISION_TIMEOUT_SECONDS = 150;
    private const MAX_PROMPT_CONTEXT_BYTES = 18000;
    private const MAX_IMAGE_BYTES = 2097152;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 580, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/visual-probe' !== $route || 'POST' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'aesthetic_compare' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['aesthetic_comparison'] ?? null ) ) return $response;
        $prior = $data['aesthetic_comparison'];
        if ( ! empty( $prior['available'] ) || 'Local Ollama A/B comparison returned HTTP 400.' !== trim( (string) ( $prior['reason'] ?? '' ) ) ) return $response;

        $recovered = self::recover( $data, $request, $prior );
        if ( is_wp_error( $recovered ) ) {
            $prior['transport_recovery'] = [
                'version' => self::VERSION,
                'attempted' => true,
                'succeeded' => false,
                'read_only' => true,
                'writes_performed' => false,
                'automatic_write_allowed' => false,
                'reason' => $recovered->get_error_message(),
            ];
            $data['aesthetic_comparison'] = $prior;
        } else {
            $data['aesthetic_comparison'] = $recovered;
        }
        $response->set_data( $data );
        return $response;
    }

    private static function recover( array $data, WP_REST_Request $request, array $prior ) {
        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) ) return new WP_Error( 'elementize_ab_hardening_context_invalid', 'Transport recovery requires the same valid context_page_id and target_marker.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_ab_hardening_hash_unavailable', 'The current Elementor page state could not be fingerprinted during transport recovery.' );
        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) return new WP_Error( 'elementize_ab_hardening_context_stale', 'The fresh grounded A/B context is unavailable or stale during transport recovery.' );

        $context = self::target_context( $snapshot, $marker );
        if ( is_wp_error( $context ) ) return $context;
        $candidates = self::extract_candidates( $data );
        if ( is_wp_error( $candidates ) ) return $candidates;

        $normalized = self::normalize_images( $candidates );
        if ( is_wp_error( $normalized ) ) return $normalized;
        $result = self::analyze_candidates( $normalized['candidates'], $context );
        if ( is_wp_error( $result ) ) return $result;
        $judgment = self::normalize_result( $result, $normalized['candidates'] );
        if ( is_wp_error( $judgment ) ) return $judgment;

        $diag = array_merge( $prior, $judgment );
        $diag['available'] = true;
        $diag['assessment_complete'] = true;
        $diag['page_state_hash'] = $state_hash;
        $diag['context_captured_gmt'] = (string) ( $snapshot['captured_gmt'] ?? '' );
        $diag['context_grounding_source'] = (string) ( $snapshot['grounding_source'] ?? '' );
        $diag['target_context'] = $context['target'];
        $diag['usable_for_selection'] = 'none' !== $diag['winner_slot'] && in_array( $diag['confidence'], [ 'medium', 'high' ], true );
        $diag['selection_policy'] = $diag['usable_for_selection']
            ? 'The winner may be used as a read-only recommendation for section selection. It still does not authorize insertion or replacement.'
            : 'No candidate should be selected automatically from this comparison; obtain stronger candidates or a higher-confidence comparison.';
        unset( $diag['reason'] );
        $diag['transport_recovery'] = [
            'version' => self::VERSION,
            'attempted' => true,
            'succeeded' => true,
            'trigger' => 'ollama_chat_http_400_media_decode',
            'original_reason' => (string) ( $prior['reason'] ?? '' ),
            'retry_transport' => 'ollama_api_generate',
            'candidate_image_normalization' => $normalized['diagnostics'],
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
        ];
        return $diag;
    }

    private static function extract_candidates( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_ab_hardening_visual_proof_missing', 'The visual-probe candidate proof file is unavailable during transport recovery.' );
        $encoded = (string) ( $files[0]['content'] ?? '' );
        $json = base64_decode( $encoded, true );
        if ( false === $json || '' === $json ) return new WP_Error( 'elementize_ab_hardening_visual_proof_invalid', 'The visual-probe proof file could not be decoded during transport recovery.' );
        $proof = json_decode( $json, true );
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_ab_hardening_visual_proof_invalid', 'The visual-probe proof file is not valid candidate JSON during transport recovery.' );

        $out = [];
        foreach ( $proof['candidates'] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            $template_id = sanitize_key( (string) ( $row['template_id'] ?? '' ) );
            $image = trim( (string) ( $row['thumbnail_base64'] ?? '' ) );
            $mime = strtolower( trim( (string) ( $row['thumbnail_mime'] ?? '' ) ) );
            if ( ! in_array( $slot, [ 'A', 'B', 'C', 'D' ], true ) || '' === $template_id || '' === $image ) continue;
            $out[] = [
                'slot' => $slot,
                'template_id' => $template_id,
                'title' => self::clip( (string) ( $row['title'] ?? $template_id ), 180 ),
                'identity_hash' => self::clip( (string) ( $row['identity_hash'] ?? '' ), 80 ),
                'thumbnail_mime' => $mime,
                'thumbnail_base64' => $image,
            ];
        }
        if ( count( $out ) < 2 ) return new WP_Error( 'elementize_ab_hardening_visual_proof_incomplete', 'Fewer than two usable candidate thumbnails were available during transport recovery.' );
        return $out;
    }

    private static function normalize_images( array $candidates ) {
        $out = [];
        $diag = [];
        foreach ( $candidates as $candidate ) {
            $bytes = base64_decode( (string) $candidate['thumbnail_base64'], true );
            if ( false === $bytes || '' === $bytes ) return new WP_Error( 'elementize_ab_hardening_image_decode_failed', 'Candidate ' . $candidate['slot'] . ' could not be base64-decoded locally.' );
            if ( strlen( $bytes ) > self::MAX_IMAGE_BYTES ) return new WP_Error( 'elementize_ab_hardening_image_too_large', 'Candidate ' . $candidate['slot'] . ' exceeds the local normalization size limit.' );

            $source_mime = self::detect_mime( $bytes, (string) $candidate['thumbnail_mime'] );
            $transport_bytes = $bytes;
            $transport_mime = $source_mime;
            $transcoded = false;

            if ( ! in_array( $source_mime, [ 'image/png', 'image/jpeg' ], true ) ) {
                $png = self::to_png( $bytes );
                if ( is_wp_error( $png ) ) return new WP_Error( 'elementize_ab_hardening_image_transcode_failed', 'Candidate ' . $candidate['slot'] . ' could not be normalized for the local vision model: ' . $png->get_error_message() );
                $transport_bytes = $png;
                $transport_mime = 'image/png';
                $transcoded = true;
            }

            $candidate['thumbnail_base64'] = base64_encode( $transport_bytes );
            $candidate['transport_mime'] = $transport_mime;
            $out[] = $candidate;
            $diag[] = [
                'slot' => $candidate['slot'],
                'source_mime' => $source_mime,
                'transport_mime' => $transport_mime,
                'transcoded' => $transcoded,
                'source_bytes' => strlen( $bytes ),
                'transport_bytes' => strlen( $transport_bytes ),
            ];
        }
        return [ 'candidates' => $out, 'diagnostics' => $diag ];
    }

    private static function detect_mime( string $bytes, string $hint ): string {
        if ( str_starts_with( $bytes, "\x89PNG\r\n\x1a\n" ) ) return 'image/png';
        if ( str_starts_with( $bytes, "\xff\xd8\xff" ) ) return 'image/jpeg';
        if ( str_starts_with( $bytes, 'GIF87a' ) || str_starts_with( $bytes, 'GIF89a' ) ) return 'image/gif';
        if ( strlen( $bytes ) >= 12 && 'RIFF' === substr( $bytes, 0, 4 ) && 'WEBP' === substr( $bytes, 8, 4 ) ) return 'image/webp';
        return in_array( $hint, [ 'image/png', 'image/jpeg', 'image/gif', 'image/webp' ], true ) ? $hint : 'application/octet-stream';
    }

    private static function to_png( string $bytes ) {
        if ( function_exists( 'imagecreatefromstring' ) && function_exists( 'imagepng' ) ) {
            $image = @imagecreatefromstring( $bytes );
            if ( false !== $image ) {
                ob_start();
                $ok = @imagepng( $image, null, 6 );
                $png = ob_get_clean();
                if ( is_resource( $image ) || $image instanceof GdImage ) @imagedestroy( $image );
                if ( $ok && is_string( $png ) && str_starts_with( $png, "\x89PNG\r\n\x1a\n" ) ) return $png;
            }
        }

        if ( ! function_exists( 'wp_get_image_editor' ) ) require_once ABSPATH . 'wp-admin/includes/image.php';
        if ( ! function_exists( 'wp_get_image_editor' ) ) return new WP_Error( 'elementize_ab_hardening_image_editor_missing', 'No local WordPress image editor is available.' );
        $input = wp_tempnam( 'elementize-ab-input' );
        $output = wp_tempnam( 'elementize-ab-output.png' );
        if ( ! is_string( $input ) || ! is_string( $output ) || '' === $input || '' === $output ) return new WP_Error( 'elementize_ab_hardening_temp_failed', 'Temporary image files could not be created.' );
        try {
            if ( false === @file_put_contents( $input, $bytes ) ) return new WP_Error( 'elementize_ab_hardening_temp_write_failed', 'The candidate image could not be written to a temporary local file.' );
            $editor = wp_get_image_editor( $input );
            if ( is_wp_error( $editor ) ) return new WP_Error( 'elementize_ab_hardening_editor_decode_failed', self::clip( $editor->get_error_message(), 180 ) );
            $saved = $editor->save( $output, 'image/png' );
            if ( is_wp_error( $saved ) ) return new WP_Error( 'elementize_ab_hardening_editor_save_failed', self::clip( $saved->get_error_message(), 180 ) );
            $png = @file_get_contents( $output );
            if ( ! is_string( $png ) || ! str_starts_with( $png, "\x89PNG\r\n\x1a\n" ) ) return new WP_Error( 'elementize_ab_hardening_editor_output_invalid', 'The local image editor did not produce a valid PNG.' );
            return $png;
        } finally {
            @unlink( $input );
            @unlink( $output );
        }
    }

    private static function target_context( array $snapshot, string $marker ) {
        $ledger = array_values( array_filter( (array) ( $snapshot['section_ledger'] ?? [] ), 'is_array' ) );
        $index = null;
        foreach ( $ledger as $i => $row ) {
            if ( $marker === strtoupper( (string) ( $row['marker'] ?? '' ) ) ) { $index = $i; break; }
        }
        if ( null === $index ) return new WP_Error( 'elementize_ab_hardening_target_not_found', 'The requested target marker is missing from the grounded section ledger.' );
        return [
            'design_dna' => $snapshot['design_dna'] ?? [],
            'art_direction' => $snapshot['art_direction'] ?? [],
            'section_coherence' => $snapshot['section_coherence'] ?? [],
            'previous_section' => $index > 0 ? $ledger[ $index - 1 ] : null,
            'target' => $ledger[ $index ],
            'next_section' => isset( $ledger[ $index + 1 ] ) ? $ledger[ $index + 1 ] : null,
        ];
    }

    private static function analyze_candidates( array $candidates, array $context ) {
        $labels = [];
        $images = [];
        foreach ( $candidates as $candidate ) {
            $labels[] = $candidate['slot'] . ' = template_id ' . $candidate['template_id'] . '; title "' . $candidate['title'] . '"';
            $images[] = $candidate['thumbnail_base64'];
        }
        $context_json = wp_json_encode( $context, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $context_json ) ) return new WP_Error( 'elementize_ab_hardening_context_encode_failed', 'The grounded page context could not be encoded for transport recovery.' );
        if ( strlen( $context_json ) > self::MAX_PROMPT_CONTEXT_BYTES ) $context_json = substr( $context_json, 0, self::MAX_PROMPT_CONTEXT_BYTES );

        $prompt = "You are a demanding senior SaaS web designer performing a RELATIVE A/B/C/D aesthetic comparison of Pixfort section candidates.\n\n" .
            "The attached images are candidate thumbnails in this exact order: " . implode( ', ', array_column( $candidates, 'slot' ) ) . ".\n" . implode( "\n", $labels ) . "\n\n" .
            "The page context below came from a fresh fact-conditioned grounded aesthetic audit of the exact current Elementor page state. Treat it as the source of truth for the existing page's design language, rhythm, target section, and neighboring sections.\n" . $context_json . "\n\n" .
            "Judge candidates RELATIVELY, not by generic prettiness. Compare composition, whitespace, density, typography hierarchy, geometry, imagery balance, CTA prominence, visual intensity, restraint, and likely transition compatibility with the surrounding page. Ignore candidate demo copy, brand names, logos, testimonial claims, stats, and other semantic promises; template titles are metadata only. Do not invent facts about the current page beyond the grounded context. Do not claim that a thumbnail proves final post-insertion appearance. Return exactly one judgment row per supplied candidate slot.";

        $schema = self::schema();
        $payload = [
            'model' => self::model(),
            'prompt' => $prompt,
            'images' => $images,
            'stream' => false,
            'format' => $schema,
            'options' => [ 'temperature' => 0.1 ],
        ];
        $response = wp_remote_post( self::ollama_generate_url(), [
            'timeout' => self::VISION_TIMEOUT_SECONDS,
            'redirection' => 0,
            'headers' => [ 'Content-Type' => 'application/json' ],
            'body' => wp_json_encode( $payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ),
            'limit_response_size' => 4194304,
        ] );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_ab_hardening_ollama_failed', 'Local Ollama transport recovery failed: ' . $response->get_error_message() );
        $code = (int) wp_remote_retrieve_response_code( $response );
        $raw_body = (string) wp_remote_retrieve_body( $response );
        if ( 200 !== $code ) {
            $error = json_decode( $raw_body, true );
            $detail = is_array( $error ) ? self::clip( (string) ( $error['error'] ?? '' ), 240 ) : '';
            return new WP_Error( 'elementize_ab_hardening_ollama_http', 'Local Ollama /api/generate recovery returned HTTP ' . $code . ( '' !== $detail ? ': ' . $detail : '.' ) );
        }
        $outer = json_decode( $raw_body, true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_ab_hardening_ollama_envelope_invalid', 'Local Ollama /api/generate returned an invalid comparison envelope.' );
        $decoded = json_decode( self::strip_json_fence( (string) $outer['response'] ), true );
        if ( ! is_array( $decoded ) ) return new WP_Error( 'elementize_ab_hardening_ollama_json_invalid', 'Local Ollama /api/generate returned invalid comparison JSON.' );
        return $decoded;
    }

    private static function schema(): array {
        $fit = [ 'strong', 'acceptable', 'weak' ];
        $tri = [ 'strong', 'mixed', 'weak' ];
        return [
            'type' => 'object',
            'required' => [ 'winner', 'confidence', 'winner_margin', 'professional_rationale', 'candidate_judgments', 'decision_basis' ],
            'properties' => [
                'winner' => [ 'type' => 'string', 'enum' => [ 'A', 'B', 'C', 'D', 'none' ] ],
                'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                'winner_margin' => [ 'type' => 'string', 'enum' => [ 'slight', 'clear', 'decisive' ] ],
                'professional_rationale' => [ 'type' => 'string' ],
                'candidate_judgments' => [
                    'type' => 'array',
                    'items' => [
                        'type' => 'object',
                        'required' => [ 'slot', 'score', 'overall_fit', 'design_dna_fit', 'rhythm_fit', 'hierarchy_fit', 'conversion_fit', 'strength', 'risk' ],
                        'properties' => [
                            'slot' => [ 'type' => 'string', 'enum' => [ 'A', 'B', 'C', 'D' ] ],
                            'score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ],
                            'overall_fit' => [ 'type' => 'string', 'enum' => $fit ],
                            'design_dna_fit' => [ 'type' => 'string', 'enum' => $tri ],
                            'rhythm_fit' => [ 'type' => 'string', 'enum' => $tri ],
                            'hierarchy_fit' => [ 'type' => 'string', 'enum' => $tri ],
                            'conversion_fit' => [ 'type' => 'string', 'enum' => $tri ],
                            'strength' => [ 'type' => 'string' ],
                            'risk' => [ 'type' => 'string' ],
                        ],
                    ],
                ],
                'decision_basis' => [ 'type' => 'array', 'items' => [ 'type' => 'string' ] ],
            ],
        ];
    }

    private static function normalize_result( array $result, array $candidates ) {
        $expected_slots = array_values( array_map( static fn ( array $row ): string => (string) $row['slot'], $candidates ) );
        $rows = is_array( $result['candidate_judgments'] ?? null ) ? $result['candidate_judgments'] : [];
        if ( count( $rows ) !== count( $expected_slots ) ) return new WP_Error( 'elementize_ab_hardening_candidate_coverage_failed', 'The recovered comparison did not return exactly one judgment per candidate.' );

        $by_slot = [];
        foreach ( $rows as $row ) {
            if ( ! is_array( $row ) ) return new WP_Error( 'elementize_ab_hardening_candidate_row_invalid', 'The recovered comparison returned an invalid candidate row.' );
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            if ( ! in_array( $slot, $expected_slots, true ) || isset( $by_slot[$slot] ) ) return new WP_Error( 'elementize_ab_hardening_candidate_slot_invalid', 'The recovered comparison returned an unknown or duplicate candidate slot.' );
            $by_slot[$slot] = [
                'slot' => $slot,
                'template_id' => self::template_id_for_slot( $candidates, $slot ),
                'score' => self::clamp_score( $row['score'] ?? 1 ),
                'overall_fit' => self::enum( $row['overall_fit'] ?? '', [ 'strong', 'acceptable', 'weak' ], 'weak' ),
                'design_dna_fit' => self::enum( $row['design_dna_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'rhythm_fit' => self::enum( $row['rhythm_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'hierarchy_fit' => self::enum( $row['hierarchy_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'conversion_fit' => self::enum( $row['conversion_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'strength' => self::clip( (string) ( $row['strength'] ?? '' ), 320 ),
                'risk' => self::clip( (string) ( $row['risk'] ?? '' ), 320 ),
            ];
        }
        $ordered = [];
        foreach ( $expected_slots as $slot ) {
            if ( ! isset( $by_slot[$slot] ) ) return new WP_Error( 'elementize_ab_hardening_candidate_coverage_failed', 'A candidate slot is missing from the recovered comparison.' );
            $ordered[] = $by_slot[$slot];
        }

        $winner = strtolower( trim( (string) ( $result['winner'] ?? 'none' ) ) );
        $winner = 'none' === $winner ? 'none' : strtoupper( $winner );
        if ( 'none' !== $winner && ! in_array( $winner, $expected_slots, true ) ) return new WP_Error( 'elementize_ab_hardening_winner_invalid', 'The recovered comparison selected a winner outside the supplied slots.' );
        $max_score = 0; $max_count = 0; $winner_score = 0;
        foreach ( $ordered as $row ) {
            $score = (int) $row['score'];
            if ( $score > $max_score ) { $max_score = $score; $max_count = 1; }
            elseif ( $score === $max_score ) $max_count++;
            if ( $row['slot'] === $winner ) $winner_score = $score;
        }
        if ( 'none' !== $winner && $winner_score !== $max_score ) return new WP_Error( 'elementize_ab_hardening_winner_score_conflict', 'The recovered winner does not have the highest candidate score.' );

        $margin = self::enum( $result['winner_margin'] ?? '', [ 'slight', 'clear', 'decisive' ], 'slight' );
        if ( 'none' === $winner || $max_count > 1 ) $margin = 'slight';
        $confidence = self::enum( $result['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' );
        $basis = [];
        foreach ( (array) ( $result['decision_basis'] ?? [] ) as $item ) {
            if ( ! is_scalar( $item ) ) continue;
            $text = self::clip( (string) $item, 260 );
            if ( '' !== $text ) $basis[] = $text;
            if ( count( $basis ) >= 6 ) break;
        }
        return [
            'candidate_count' => count( $ordered ),
            'exact_candidate_coverage' => true,
            'winner_slot' => $winner,
            'winner_template_id' => 'none' === $winner ? null : self::template_id_for_slot( $candidates, $winner ),
            'confidence' => $confidence,
            'winner_margin' => $margin,
            'professional_rationale' => self::clip( (string) ( $result['professional_rationale'] ?? '' ), 900 ),
            'candidate_judgments' => $ordered,
            'decision_basis' => $basis,
        ];
    }

    private static function page_state_hash( int $page_id ): string {
        $raw = get_post_meta( $page_id, '_elementor_data', true );
        if ( is_string( $raw ) ) return hash( 'sha256', $raw );
        $json = wp_json_encode( $raw, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) ? hash( 'sha256', $json ) : '';
    }
    private static function cache_key( int $page_id, string $hash ): string { return 'elementize_ab_' . $page_id . '_' . substr( preg_replace( '/[^a-f0-9]/i', '', $hash ), 0, 24 ); }
    private static function template_id_for_slot( array $candidates, string $slot ): ?string { foreach ( $candidates as $candidate ) if ( $slot === (string) ( $candidate['slot'] ?? '' ) ) return (string) ( $candidate['template_id'] ?? '' ); return null; }
    private static function model(): string { if ( defined( self::MODEL_CONST ) ) { $value = trim( (string) constant( self::MODEL_CONST ) ); if ( '' !== $value ) return $value; } return self::DEFAULT_MODEL; }
    private static function ollama_generate_url(): string { $base = self::DEFAULT_OLLAMA_URL; if ( defined( self::OLLAMA_URL_CONST ) ) { $value = trim( (string) constant( self::OLLAMA_URL_CONST ) ); if ( '' !== $value ) $base = $value; } return rtrim( $base, '/' ) . '/api/generate'; }
    private static function strip_json_fence( string $content ): string { $content = trim( $content ); if ( preg_match( '/^```(?:json)?\s*(.*?)\s*```$/is', $content, $m ) ) return trim( (string) $m[1] ); $start = strpos( $content, '{' ); $end = strrpos( $content, '}' ); return false !== $start && false !== $end && $end >= $start ? substr( $content, $start, $end - $start + 1 ) : $content; }
    private static function enum( $value, array $allowed, string $fallback ): string { $value = strtolower( trim( (string) $value ) ); return in_array( $value, $allowed, true ) ? $value : $fallback; }
    private static function clamp_score( $value ): int { $score = is_numeric( $value ) ? (int) round( (float) $value ) : 1; return max( 1, min( 10, $score ) ); }
    private static function clip( string $value, int $max ): string { $value = trim( preg_replace( '/\s+/u', ' ', $value ) ?? $value ); if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value; return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value; }
    private static function truthy( $value ): bool { if ( is_bool( $value ) ) return $value; if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value; return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true ); }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_ab_transport_hardening_version'] = self::VERSION;
        $data['aesthetic_ab_transport_hardening'] = true;
        $data['aesthetic_ab_transport_retry'] = 'ollama_api_generate';
        $data['aesthetic_ab_candidate_image_normalization'] = true;
        $data['aesthetic_ab_transport_hardening_read_only'] = true;
        $data['aesthetic_ab_transport_hardening_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

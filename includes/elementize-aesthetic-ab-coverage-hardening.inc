<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Transport_Hardening', false ) ) return;

/**
 * Strict candidate-coverage recovery for contextual A/B aesthetic judgment.
 *
 * The 0.27.0 transport recovery proved that normalized candidate images can be
 * decoded by the local Ollama /api/generate path, but its array schema did not
 * require the number of candidate_judgments rows to match the supplied candidate
 * count. This layer runs only after that exact downstream coverage failure and
 * retries with a slot-keyed JSON schema whose required keys are generated from the
 * actual supplied candidates. Missing and duplicate candidate rows are therefore
 * structurally impossible in a schema-conforming response.
 *
 * Read-only: this class never inserts, replaces, edits, publishes, or otherwise
 * mutates Elementor/page state.
 */
final class Elementize_Aesthetic_AB_Coverage_Hardening {
    private const VERSION = '0.27.1-c1';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const VISION_TIMEOUT_SECONDS = 150;
    private const MAX_PROMPT_CONTEXT_BYTES = 18000;
    private const MAX_IMAGE_BYTES = 2097152;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 590, 3 );
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
        if ( ! empty( $prior['available'] ) ) return $response;

        $transport = is_array( $prior['transport_recovery'] ?? null ) ? $prior['transport_recovery'] : [];
        if ( empty( $transport['attempted'] ) || ! empty( $transport['succeeded'] ) ) return $response;
        if ( 'The recovered comparison did not return exactly one judgment per candidate.' !== trim( (string) ( $transport['reason'] ?? '' ) ) ) return $response;

        $recovered = self::recover( $data, $request, $prior );
        if ( is_wp_error( $recovered ) ) {
            $prior['coverage_recovery'] = [
                'version' => self::VERSION,
                'attempted' => true,
                'succeeded' => false,
                'strict_slot_keyed_contract' => true,
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
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) ) return new WP_Error( 'elementize_ab_coverage_context_invalid', 'Coverage recovery requires the same valid context_page_id and target_marker.' );

        $post = get_post( $page_id );
        if ( ! $post || 'page' !== $post->post_type || ! current_user_can( 'edit_post', $page_id ) ) return new WP_Error( 'elementize_ab_coverage_page_unavailable', 'The contextual page is unavailable or not editable during coverage recovery.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_ab_coverage_hash_unavailable', 'The current Elementor page state could not be fingerprinted during coverage recovery.' );

        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) return new WP_Error( 'elementize_ab_coverage_context_stale', 'The fresh grounded A/B context is unavailable or stale during coverage recovery.' );

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

        $previous_transport = is_array( $prior['transport_recovery'] ?? null ) ? $prior['transport_recovery'] : [];
        $diag['transport_recovery'] = array_merge( $previous_transport, [
            'succeeded' => true,
            'transport_succeeded' => true,
            'completed_by_coverage_recovery' => true,
            'initial_downstream_reason' => (string) ( $previous_transport['reason'] ?? '' ),
            'retry_transport' => 'ollama_api_generate',
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
        ] );
        unset( $diag['transport_recovery']['reason'] );

        $diag['coverage_recovery'] = [
            'version' => self::VERSION,
            'attempted' => true,
            'succeeded' => true,
            'strict_slot_keyed_contract' => true,
            'required_slots' => array_values( array_map( static fn ( array $row ): string => (string) $row['slot'], $normalized['candidates'] ) ),
            'candidate_count' => count( $normalized['candidates'] ),
            'candidate_image_normalization' => $normalized['diagnostics'],
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
        ];

        return $diag;
    }

    private static function extract_candidates( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_ab_coverage_visual_proof_missing', 'The visual-probe candidate proof file is unavailable during coverage recovery.' );

        $encoded = (string) ( $files[0]['content'] ?? '' );
        $json = base64_decode( $encoded, true );
        if ( false === $json || '' === $json ) return new WP_Error( 'elementize_ab_coverage_visual_proof_invalid', 'The visual-probe proof file could not be decoded during coverage recovery.' );

        $proof = json_decode( $json, true );
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_ab_coverage_visual_proof_invalid', 'The visual-probe proof file is not valid candidate JSON during coverage recovery.' );

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

        if ( count( $out ) < 2 ) return new WP_Error( 'elementize_ab_coverage_visual_proof_incomplete', 'Fewer than two usable candidate thumbnails were available during coverage recovery.' );
        return $out;
    }

    private static function normalize_images( array $candidates ) {
        $out = [];
        $diag = [];

        foreach ( $candidates as $candidate ) {
            $bytes = base64_decode( (string) $candidate['thumbnail_base64'], true );
            if ( false === $bytes || '' === $bytes ) return new WP_Error( 'elementize_ab_coverage_image_decode_failed', 'Candidate ' . $candidate['slot'] . ' could not be base64-decoded locally.' );
            if ( strlen( $bytes ) > self::MAX_IMAGE_BYTES ) return new WP_Error( 'elementize_ab_coverage_image_too_large', 'Candidate ' . $candidate['slot'] . ' exceeds the local normalization size limit.' );

            $source_mime = self::detect_mime( $bytes, (string) $candidate['thumbnail_mime'] );
            $transport_bytes = $bytes;
            $transport_mime = $source_mime;
            $transcoded = false;

            if ( ! in_array( $source_mime, [ 'image/png', 'image/jpeg' ], true ) ) {
                $png = self::to_png( $bytes );
                if ( is_wp_error( $png ) ) return new WP_Error( 'elementize_ab_coverage_image_transcode_failed', 'Candidate ' . $candidate['slot'] . ' could not be normalized for the local vision model: ' . $png->get_error_message() );
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
                if ( is_resource( $image ) || is_object( $image ) ) @imagedestroy( $image );
                if ( $ok && is_string( $png ) && str_starts_with( $png, "\x89PNG\r\n\x1a\n" ) ) return $png;
            }
        }

        if ( ! function_exists( 'wp_get_image_editor' ) ) require_once ABSPATH . 'wp-admin/includes/image.php';
        if ( ! function_exists( 'wp_get_image_editor' ) ) return new WP_Error( 'elementize_ab_coverage_image_editor_missing', 'No local WordPress image editor is available.' );

        $input = wp_tempnam( 'elementize-ab-coverage-input' );
        $output = wp_tempnam( 'elementize-ab-coverage-output.png' );
        if ( ! is_string( $input ) || ! is_string( $output ) || '' === $input || '' === $output ) return new WP_Error( 'elementize_ab_coverage_temp_failed', 'Temporary image files could not be created.' );

        try {
            if ( false === @file_put_contents( $input, $bytes ) ) return new WP_Error( 'elementize_ab_coverage_temp_write_failed', 'The candidate image could not be written to a temporary local file.' );
            $editor = wp_get_image_editor( $input );
            if ( is_wp_error( $editor ) ) return new WP_Error( 'elementize_ab_coverage_editor_decode_failed', self::clip( $editor->get_error_message(), 180 ) );
            $saved = $editor->save( $output, 'image/png' );
            if ( is_wp_error( $saved ) ) return new WP_Error( 'elementize_ab_coverage_editor_save_failed', self::clip( $saved->get_error_message(), 180 ) );
            $png = @file_get_contents( $output );
            if ( ! is_string( $png ) || ! str_starts_with( $png, "\x89PNG\r\n\x1a\n" ) ) return new WP_Error( 'elementize_ab_coverage_editor_output_invalid', 'The local image editor did not produce a valid PNG.' );
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
            if ( $marker === strtoupper( (string) ( $row['marker'] ?? '' ) ) ) {
                $index = $i;
                break;
            }
        }
        if ( null === $index ) return new WP_Error( 'elementize_ab_coverage_target_not_found', 'The requested target marker is missing from the grounded section ledger.' );

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
        $slots = array_values( array_map( static fn ( array $row ): string => (string) $row['slot'], $candidates ) );
        $labels = [];
        $images = [];
        foreach ( $candidates as $candidate ) {
            $labels[] = $candidate['slot'] . ' = template_id ' . $candidate['template_id'] . '; title "' . $candidate['title'] . '"';
            $images[] = $candidate['thumbnail_base64'];
        }

        $context_json = wp_json_encode( $context, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $context_json ) ) return new WP_Error( 'elementize_ab_coverage_context_encode_failed', 'The grounded page context could not be encoded for strict coverage recovery.' );
        if ( strlen( $context_json ) > self::MAX_PROMPT_CONTEXT_BYTES ) $context_json = substr( $context_json, 0, self::MAX_PROMPT_CONTEXT_BYTES );

        $prompt = "You are a demanding senior SaaS web designer performing a RELATIVE aesthetic comparison of Pixfort section candidates.\n\n" .
            "Attached candidate images are in this exact order: " . implode( ', ', $slots ) . ".\n" .
            implode( "\n", $labels ) . "\n\n" .
            "The page context below is from the fresh fact-conditioned grounded aesthetic audit of the exact current Elementor page state. Treat it as truth for the existing page design language, rhythm, target section, and neighboring sections.\n" .
            $context_json . "\n\n" .
            "Judge candidates RELATIVELY, not by generic prettiness. Compare composition, whitespace, density, typography hierarchy, geometry, imagery balance, CTA prominence, visual intensity, restraint, and likely transition compatibility. Ignore demo copy, brand names, logos, testimonial claims, stats, and semantic promises. Do not invent current-page facts. Do not claim the thumbnail proves final post-insertion appearance.\n\n" .
            "The candidate_judgments object MUST contain every supplied slot exactly once as a required key: " . implode( ', ', $slots ) . ".";

        $payload = [
            'model' => self::model(),
            'prompt' => $prompt,
            'images' => $images,
            'stream' => false,
            'format' => self::schema( $slots ),
            'options' => [ 'temperature' => 0 ],
        ];

        $response = wp_remote_post( self::ollama_generate_url(), [
            'timeout' => self::VISION_TIMEOUT_SECONDS,
            'redirection' => 0,
            'headers' => [ 'Content-Type' => 'application/json' ],
            'body' => wp_json_encode( $payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ),
            'limit_response_size' => 4194304,
        ] );

        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_ab_coverage_ollama_failed', 'Local Ollama strict coverage recovery failed: ' . $response->get_error_message() );
        $code = (int) wp_remote_retrieve_response_code( $response );
        $raw_body = (string) wp_remote_retrieve_body( $response );
        if ( 200 !== $code ) {
            $error = json_decode( $raw_body, true );
            $detail = is_array( $error ) ? self::clip( (string) ( $error['error'] ?? '' ), 240 ) : '';
            return new WP_Error( 'elementize_ab_coverage_ollama_http', 'Local Ollama strict /api/generate recovery returned HTTP ' . $code . ( '' !== $detail ? ': ' . $detail : '.' ) );
        }

        $outer = json_decode( $raw_body, true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_ab_coverage_ollama_envelope_invalid', 'Local Ollama strict /api/generate recovery returned an invalid envelope.' );

        $decoded = json_decode( self::strip_json_fence( (string) $outer['response'] ), true );
        if ( ! is_array( $decoded ) ) return new WP_Error( 'elementize_ab_coverage_ollama_json_invalid', 'Local Ollama strict /api/generate recovery returned invalid JSON.' );
        return $decoded;
    }

    private static function schema( array $slots ): array {
        $fit = [ 'strong', 'acceptable', 'weak' ];
        $tri = [ 'strong', 'mixed', 'weak' ];
        $judgment = [
            'type' => 'object',
            'required' => [ 'score', 'overall_fit', 'design_dna_fit', 'rhythm_fit', 'hierarchy_fit', 'conversion_fit', 'strength', 'risk' ],
            'properties' => [
                'score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ],
                'overall_fit' => [ 'type' => 'string', 'enum' => $fit ],
                'design_dna_fit' => [ 'type' => 'string', 'enum' => $tri ],
                'rhythm_fit' => [ 'type' => 'string', 'enum' => $tri ],
                'hierarchy_fit' => [ 'type' => 'string', 'enum' => $tri ],
                'conversion_fit' => [ 'type' => 'string', 'enum' => $tri ],
                'strength' => [ 'type' => 'string' ],
                'risk' => [ 'type' => 'string' ],
            ],
            'additionalProperties' => false,
        ];

        $candidate_properties = [];
        foreach ( $slots as $slot ) $candidate_properties[ $slot ] = $judgment;

        return [
            'type' => 'object',
            'required' => [ 'winner', 'confidence', 'winner_margin', 'professional_rationale', 'candidate_judgments', 'decision_basis' ],
            'properties' => [
                'winner' => [ 'type' => 'string', 'enum' => array_merge( $slots, [ 'none' ] ) ],
                'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                'winner_margin' => [ 'type' => 'string', 'enum' => [ 'slight', 'clear', 'decisive' ] ],
                'professional_rationale' => [ 'type' => 'string' ],
                'candidate_judgments' => [
                    'type' => 'object',
                    'required' => $slots,
                    'properties' => $candidate_properties,
                    'additionalProperties' => false,
                ],
                'decision_basis' => [
                    'type' => 'array',
                    'items' => [ 'type' => 'string' ],
                    'minItems' => 1,
                    'maxItems' => 6,
                ],
            ],
            'additionalProperties' => false,
        ];
    }

    private static function normalize_result( array $result, array $candidates ) {
        $expected_slots = array_values( array_map( static fn ( array $row ): string => (string) $row['slot'], $candidates ) );
        $rows = is_array( $result['candidate_judgments'] ?? null ) ? $result['candidate_judgments'] : [];

        $actual_slots = array_keys( $rows );
        sort( $actual_slots );
        $sorted_expected = $expected_slots;
        sort( $sorted_expected );
        if ( $actual_slots !== $sorted_expected ) return new WP_Error( 'elementize_ab_coverage_contract_failed', 'The strict slot-keyed comparison did not return exactly the supplied candidate slots.' );

        $ordered = [];
        foreach ( $expected_slots as $slot ) {
            $row = $rows[ $slot ] ?? null;
            if ( ! is_array( $row ) ) return new WP_Error( 'elementize_ab_coverage_row_invalid', 'The strict comparison returned an invalid judgment for candidate ' . $slot . '.' );
            $ordered[] = [
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

        $winner = trim( (string) ( $result['winner'] ?? 'none' ) );
        $winner = 'none' === strtolower( $winner ) ? 'none' : strtoupper( $winner );
        if ( 'none' !== $winner && ! in_array( $winner, $expected_slots, true ) ) return new WP_Error( 'elementize_ab_coverage_winner_invalid', 'The strict comparison selected a winner outside the supplied candidate slots.' );

        $max_score = 0;
        $max_count = 0;
        $winner_score = 0;
        foreach ( $ordered as $row ) {
            $score = (int) $row['score'];
            if ( $score > $max_score ) {
                $max_score = $score;
                $max_count = 1;
            } elseif ( $score === $max_score ) {
                $max_count++;
            }
            if ( $row['slot'] === $winner ) $winner_score = $score;
        }

        if ( 'none' !== $winner && $winner_score !== $max_score ) return new WP_Error( 'elementize_ab_coverage_winner_score_conflict', 'The strict comparison winner does not have the highest candidate score.' );

        $margin = self::enum( $result['winner_margin'] ?? '', [ 'slight', 'clear', 'decisive' ], 'slight' );
        $confidence = self::enum( $result['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' );

        if ( $max_count > 1 ) {
            $winner = 'none';
            $margin = 'slight';
            $confidence = 'low';
        } elseif ( 'none' === $winner ) {
            $margin = 'slight';
            $confidence = 'low';
        }

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

    private static function cache_key( int $page_id, string $hash ): string {
        return 'elementize_ab_' . $page_id . '_' . substr( preg_replace( '/[^a-f0-9]/i', '', $hash ), 0, 24 );
    }

    private static function template_id_for_slot( array $candidates, string $slot ): ?string {
        foreach ( $candidates as $candidate ) if ( $slot === (string) ( $candidate['slot'] ?? '' ) ) return (string) ( $candidate['template_id'] ?? '' );
        return null;
    }

    private static function model(): string {
        if ( defined( self::MODEL_CONST ) ) {
            $value = trim( (string) constant( self::MODEL_CONST ) );
            if ( '' !== $value ) return $value;
        }
        return self::DEFAULT_MODEL;
    }

    private static function ollama_generate_url(): string {
        $base = self::DEFAULT_OLLAMA_URL;
        if ( defined( self::OLLAMA_URL_CONST ) ) {
            $value = trim( (string) constant( self::OLLAMA_URL_CONST ) );
            if ( '' !== $value ) $base = $value;
        }
        $base = rtrim( $base, '/' );
        if ( ! preg_match( '#^https?://(?:127\.0\.0\.1|localhost)(?::\d+)?$#i', $base ) ) $base = self::DEFAULT_OLLAMA_URL;
        return $base . '/api/generate';
    }

    private static function strip_json_fence( string $content ): string {
        $content = trim( $content );
        if ( preg_match( '/^```(?:json)?\s*(.*?)\s*```$/is', $content, $m ) ) return trim( (string) $m[1] );
        $start = strpos( $content, '{' );
        $end = strrpos( $content, '}' );
        if ( false !== $start && false !== $end && $end >= $start ) return substr( $content, $start, $end - $start + 1 );
        return $content;
    }

    private static function enum( $value, array $allowed, string $fallback ): string {
        $value = strtolower( trim( (string) $value ) );
        return in_array( $value, $allowed, true ) ? $value : $fallback;
    }

    private static function clamp_score( $value ): int {
        $score = is_numeric( $value ) ? (int) round( (float) $value ) : 1;
        return max( 1, min( 10, $score ) );
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\s+/u', ' ', $value ) ?? $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value;
        return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value;
    }

    private static function truthy( $value ): bool {
        if ( is_bool( $value ) ) return $value;
        if ( is_int( $value ) || is_float( $value ) ) return 0 !== (int) $value;
        return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_ab_coverage_hardening_version'] = self::VERSION;
        $data['aesthetic_ab_coverage_hardening'] = true;
        $data['aesthetic_ab_coverage_slot_keyed_contract'] = true;
        $data['aesthetic_ab_coverage_read_only'] = true;
        $data['aesthetic_ab_coverage_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

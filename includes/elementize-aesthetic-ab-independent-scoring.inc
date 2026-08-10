<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Judgment', false ) ) return;

/**
 * Slot-blind independent candidate scoring for contextual Pixfort A/B judgment.
 *
 * Small local vision models can exhibit strong positional bias when two candidate
 * images are judged side-by-side. This layer removes slot position from the scoring
 * decision itself: each candidate thumbnail is scored in a separate Ollama request
 * against the exact same grounded page context. The model is not told the Pixfort
 * template ID, title, or A/B/C/D slot. PHP then ranks the independent absolute-fit
 * scores deterministically. Existing judgment calibration runs afterward and may
 * only preserve or lower confidence.
 *
 * Read-only: no Elementor/page mutation capability exists in this class.
 */
final class Elementize_Aesthetic_AB_Independent_Scoring {
    private const VERSION = '0.27.6-i1';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const VISION_TIMEOUT_SECONDS = 150;
    private const MAX_PROMPT_CONTEXT_BYTES = 18000;
    private const MAX_IMAGE_BYTES = 2097152;

    public static function init(): void {
        // After transport/coverage recovery (580/590), before deterministic calibration (610).
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 600, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/visual-probe' !== $route || 'POST' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'aesthetic_compare' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $prior = is_array( $data['aesthetic_comparison'] ?? null ) ? $data['aesthetic_comparison'] : [];

        $diag = [
            'version' => self::VERSION,
            'attempted' => true,
            'applied' => false,
            'method' => 'independent_absolute_context_fit',
            'slot_order_blind' => true,
            'template_identity_hidden_from_model' => true,
            'one_image_per_model_request' => true,
            'shared_context_for_every_candidate' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
            'pairwise_baseline' => self::pairwise_baseline( $prior ),
        ];

        $result = self::score_request_candidates( $data, $request );
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = self::clip( $result->get_error_message(), 500 );
            if ( $prior ) {
                $prior['independent_candidate_scoring'] = $diag;
                $data['aesthetic_comparison'] = $prior;
                $response->set_data( $data );
            }
            return $response;
        }

        $diag = array_merge( $diag, $result['diagnostics'], [ 'applied' => true ] );

        $comparison = $prior;
        if ( ! $comparison ) {
            $comparison = [
                'version' => '0.27.0-a1',
                'read_only' => true,
                'advisory_only' => true,
                'writes_performed' => false,
                'automatic_write_allowed' => false,
                'action_slot_cost' => 0,
                'reuses_existing_visual_probe_action' => true,
                'provider' => 'local_ollama',
                'model' => self::model(),
                'requires_post_insert_visual_verification' => true,
            ];
        }

        $comparison['available'] = true;
        $comparison['assessment_complete'] = true;
        $comparison['exact_candidate_coverage'] = true;
        $comparison['candidate_count'] = count( $result['candidate_judgments'] );
        $comparison['winner_slot'] = $result['winner_slot'];
        $comparison['winner_template_id'] = $result['winner_template_id'];
        $comparison['confidence'] = $result['confidence'];
        $comparison['winner_margin'] = $result['winner_margin'];
        $comparison['professional_rationale'] = $result['professional_rationale'];
        $comparison['candidate_judgments'] = $result['candidate_judgments'];
        $comparison['decision_basis'] = $result['decision_basis'];
        $comparison['page_state_hash'] = $result['page_state_hash'];
        $comparison['context_captured_gmt'] = $result['context_captured_gmt'];
        $comparison['context_grounding_source'] = $result['context_grounding_source'];
        $comparison['target_context'] = $result['target_context'];
        $comparison['comparison_method'] = 'slot_blind_independent_candidate_scoring';
        $comparison['usable_for_selection'] = 'none' !== $result['winner_slot'] && in_array( $result['confidence'], [ 'medium', 'high' ], true );
        $comparison['selection_policy'] = $comparison['usable_for_selection']
            ? 'The independently scored winner may be used as a read-only recommendation for section selection. It still does not authorize insertion or replacement.'
            : 'Independent scoring produced only an advisory result. Do not select a candidate automatically unless later deterministic calibration also considers it usable.';
        $comparison['independent_candidate_scoring'] = $diag;
        unset( $comparison['reason'] );

        $data['aesthetic_comparison'] = $comparison;
        $response->set_data( $data );
        return $response;
    }

    private static function score_request_candidates( array $data, WP_REST_Request $request ) {
        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) ) return new WP_Error( 'elementize_ab_independent_context_invalid', 'Independent scoring requires a valid context_page_id and target_marker.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_ab_independent_hash_unavailable', 'The current Elementor page state could not be fingerprinted during independent scoring.' );

        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) return new WP_Error( 'elementize_ab_independent_context_stale', 'The exact-state grounded A/B context is unavailable or stale during independent scoring.' );

        $context = self::target_context( $snapshot, $marker );
        if ( is_wp_error( $context ) ) return $context;

        $candidates = self::extract_candidates( $data );
        if ( is_wp_error( $candidates ) ) return $candidates;
        if ( count( $candidates ) < 2 || count( $candidates ) > 4 ) return new WP_Error( 'elementize_ab_independent_candidate_count', 'Independent scoring requires between two and four candidate thumbnails.' );

        $normalized = self::normalize_images( $candidates );
        if ( is_wp_error( $normalized ) ) return $normalized;

        $judgments = [];
        $candidate_diagnostics = [];
        foreach ( $normalized['candidates'] as $candidate ) {
            $judgment = self::score_one( (string) $candidate['thumbnail_base64'], $context );
            if ( is_wp_error( $judgment ) ) {
                return new WP_Error(
                    'elementize_ab_independent_candidate_failed',
                    'Independent scoring failed for candidate ' . (string) $candidate['slot'] . ': ' . $judgment->get_error_message()
                );
            }

            $row = [
                'slot' => (string) $candidate['slot'],
                'template_id' => (string) $candidate['template_id'],
                'score' => self::clamp_score( $judgment['score'] ?? 1 ),
                'overall_fit' => self::enum( $judgment['overall_fit'] ?? '', [ 'strong', 'acceptable', 'weak' ], 'weak' ),
                'design_dna_fit' => self::enum( $judgment['design_dna_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'rhythm_fit' => self::enum( $judgment['rhythm_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'hierarchy_fit' => self::enum( $judgment['hierarchy_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'conversion_fit' => self::enum( $judgment['conversion_fit'] ?? '', [ 'strong', 'mixed', 'weak' ], 'weak' ),
                'strength' => self::clip( (string) ( $judgment['strength'] ?? '' ), 320 ),
                'risk' => self::clip( (string) ( $judgment['risk'] ?? '' ), 320 ),
            ];
            $judgments[] = $row;
            $candidate_diagnostics[] = [
                'slot' => (string) $candidate['slot'],
                'template_id' => (string) $candidate['template_id'],
                'model_confidence' => self::confidence( $judgment['confidence'] ?? 'low' ),
                'score' => $row['score'],
            ];
        }

        $ranked = $judgments;
        usort( $ranked, static function ( array $a, array $b ): int {
            if ( (int) $a['score'] === (int) $b['score'] ) return strcmp( (string) $a['template_id'], (string) $b['template_id'] );
            return (int) $b['score'] <=> (int) $a['score'];
        } );

        $top = $ranked[0];
        $runner = $ranked[1];
        $top_score = (int) $top['score'];
        $runner_score = (int) $runner['score'];
        $gap = max( 0, $top_score - $runner_score );
        $tie = $top_score === $runner_score;

        $winner_slot = $tie ? 'none' : (string) $top['slot'];
        $winner_template_id = $tie ? null : (string) $top['template_id'];
        $winner_model_confidence = 'low';
        if ( ! $tie ) {
            foreach ( $candidate_diagnostics as $candidate_diag ) {
                if ( (string) $candidate_diag['template_id'] === (string) $top['template_id'] ) {
                    $winner_model_confidence = (string) $candidate_diag['model_confidence'];
                    break;
                }
            }
        }

        $margin = 'slight';
        if ( ! $tie ) {
            if ( $gap >= 5 ) $margin = 'decisive';
            elseif ( $gap >= 3 ) $margin = 'clear';
        }
        $confidence = $tie ? 'low' : $winner_model_confidence;

        $winner_strength = self::clip( (string) ( $top['strength'] ?? '' ), 260 );
        $runner_risk = self::clip( (string) ( $runner['risk'] ?? '' ), 260 );
        $rationale = $tie
            ? 'Independent absolute contextual scoring produced a top-score tie, so no relative winner is retained.'
            : 'Independent absolute contextual scoring ranked ' . (string) $top['template_id'] . ' at ' . $top_score . '/10 versus ' . (string) $runner['template_id'] . ' at ' . $runner_score . '/10.'
                . ( '' !== $winner_strength ? ' Winner strength: ' . $winner_strength . '.' : '' )
                . ( '' !== $runner_risk ? ' Runner-up risk: ' . $runner_risk . '.' : '' );

        return [
            'candidate_judgments' => $judgments,
            'winner_slot' => $winner_slot,
            'winner_template_id' => $winner_template_id,
            'confidence' => $confidence,
            'winner_margin' => $margin,
            'professional_rationale' => self::clip( $rationale, 900 ),
            'decision_basis' => [
                'Each candidate was scored in a separate one-image model request against the same grounded page context.',
                'The model was not shown candidate slot, template ID, or template title while scoring.',
                'Winner ranking is deterministic from independent absolute-fit scores; score gap=' . $gap . '.',
            ],
            'page_state_hash' => $state_hash,
            'context_captured_gmt' => (string) ( $snapshot['captured_gmt'] ?? '' ),
            'context_grounding_source' => (string) ( $snapshot['grounding_source'] ?? '' ),
            'target_context' => $context['target'],
            'diagnostics' => [
                'candidate_count' => count( $judgments ),
                'candidate_results' => $candidate_diagnostics,
                'candidate_image_normalization' => $normalized['diagnostics'],
                'top_score' => $top_score,
                'runner_up_score' => $runner_score,
                'score_gap' => $gap,
                'tied_top_score' => $tie,
                'effective_winner_template_id' => $winner_template_id,
            ],
        ];
    }

    private static function score_one( string $thumbnail_base64, array $context ) {
        $context_json = wp_json_encode( $context, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $context_json ) ) return new WP_Error( 'elementize_ab_independent_context_encode_failed', 'The grounded page context could not be encoded for independent scoring.' );
        if ( strlen( $context_json ) > self::MAX_PROMPT_CONTEXT_BYTES ) $context_json = substr( $context_json, 0, self::MAX_PROMPT_CONTEXT_BYTES );

        $prompt = "You are a demanding senior SaaS web designer scoring ONE anonymous Pixfort section candidate for ABSOLUTE contextual fit.\n\n" .
            "There is exactly one attached candidate image. You are intentionally NOT given its slot, template ID, template title, or any competing candidate. Judge only what is visible in this image against the grounded current-page context below.\n\n" .
            "GROUNDED PAGE CONTEXT:\n" . $context_json . "\n\n" .
            "Score absolute contextual fit from 1 to 10. Judge composition, whitespace, density, typography hierarchy, geometry, imagery balance, CTA prominence, visual intensity, restraint, conversion emphasis, and likely transition compatibility with neighboring sections. Ignore demo copy, brand names, logos, testimonial claims, stats, and semantic promises. Do not reward or punish the candidate for what product the demo content appears to describe. Do not invent current-page facts. Do not claim the thumbnail proves final post-insertion appearance.\n\n" .
            "Use overall_fit=strong only when the numeric score is normally 7-10, acceptable normally 4-6, and weak normally 1-3. Keep the categorical dimensions consistent with the numeric score. Return JSON only.";

        $schema = [
            'type' => 'object',
            'required' => [ 'score', 'overall_fit', 'design_dna_fit', 'rhythm_fit', 'hierarchy_fit', 'conversion_fit', 'strength', 'risk', 'confidence' ],
            'properties' => [
                'score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ],
                'overall_fit' => [ 'type' => 'string', 'enum' => [ 'strong', 'acceptable', 'weak' ] ],
                'design_dna_fit' => [ 'type' => 'string', 'enum' => [ 'strong', 'mixed', 'weak' ] ],
                'rhythm_fit' => [ 'type' => 'string', 'enum' => [ 'strong', 'mixed', 'weak' ] ],
                'hierarchy_fit' => [ 'type' => 'string', 'enum' => [ 'strong', 'mixed', 'weak' ] ],
                'conversion_fit' => [ 'type' => 'string', 'enum' => [ 'strong', 'mixed', 'weak' ] ],
                'strength' => [ 'type' => 'string' ],
                'risk' => [ 'type' => 'string' ],
                'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
            ],
            'additionalProperties' => false,
        ];

        $payload = [
            'model' => self::model(),
            'prompt' => $prompt,
            'images' => [ $thumbnail_base64 ],
            'stream' => false,
            'format' => $schema,
            'options' => [ 'temperature' => 0 ],
        ];

        $response = wp_remote_post( self::ollama_generate_url(), [
            'timeout' => self::VISION_TIMEOUT_SECONDS,
            'redirection' => 0,
            'sslverify' => false,
            'headers' => [ 'Content-Type' => 'application/json', 'Accept' => 'application/json' ],
            'body' => wp_json_encode( $payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ),
            'limit_response_size' => 2097152,
        ] );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_ab_independent_ollama_failed', 'Local Ollama independent scoring request failed.' );
        $code = (int) wp_remote_retrieve_response_code( $response );
        if ( 200 !== $code ) return new WP_Error( 'elementize_ab_independent_ollama_http', 'Local Ollama independent scoring returned HTTP ' . $code . '.' );

        $outer = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_ab_independent_envelope_invalid', 'Local Ollama independent scoring returned an invalid envelope.' );
        $decoded = json_decode( self::strip_json_fence( (string) $outer['response'] ), true );
        if ( ! is_array( $decoded ) ) return new WP_Error( 'elementize_ab_independent_json_invalid', 'Local Ollama independent scoring returned invalid JSON.' );
        return $decoded;
    }

    private static function pairwise_baseline( array $prior ): array {
        $scores = [];
        foreach ( is_array( $prior['candidate_judgments'] ?? null ) ? $prior['candidate_judgments'] : [] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $scores[] = [
                'slot' => (string) ( $row['slot'] ?? '' ),
                'template_id' => (string) ( $row['template_id'] ?? '' ),
                'score' => self::clamp_score( $row['score'] ?? 1 ),
            ];
        }
        return [
            'available' => ! empty( $prior['available'] ),
            'winner_slot' => (string) ( $prior['winner_slot'] ?? '' ),
            'winner_template_id' => isset( $prior['winner_template_id'] ) ? (string) $prior['winner_template_id'] : null,
            'confidence' => (string) ( $prior['confidence'] ?? '' ),
            'winner_margin' => (string) ( $prior['winner_margin'] ?? '' ),
            'usable_for_selection' => ! empty( $prior['usable_for_selection'] ),
            'reason' => self::clip( (string) ( $prior['reason'] ?? '' ), 300 ),
            'candidate_scores' => $scores,
        ];
    }

    private static function extract_candidates( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_ab_independent_visual_proof_missing', 'The visual-probe candidate proof file is unavailable during independent scoring.' );
        $encoded = (string) ( $files[0]['content'] ?? '' );
        $json = base64_decode( $encoded, true );
        if ( false === $json || '' === $json ) return new WP_Error( 'elementize_ab_independent_visual_proof_invalid', 'The visual-probe proof file could not be decoded during independent scoring.' );
        $proof = json_decode( $json, true );
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_ab_independent_visual_proof_invalid', 'The visual-probe proof file is not valid candidate JSON during independent scoring.' );

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
                'thumbnail_mime' => $mime,
                'thumbnail_base64' => $image,
            ];
        }
        return count( $out ) >= 2 ? $out : new WP_Error( 'elementize_ab_independent_visual_proof_incomplete', 'Fewer than two usable candidate thumbnails were available during independent scoring.' );
    }

    private static function normalize_images( array $candidates ) {
        $out = [];
        $diag = [];
        foreach ( $candidates as $candidate ) {
            $bytes = base64_decode( (string) $candidate['thumbnail_base64'], true );
            if ( false === $bytes || '' === $bytes ) return new WP_Error( 'elementize_ab_independent_image_decode_failed', 'A candidate thumbnail could not be base64-decoded locally.' );
            if ( strlen( $bytes ) > self::MAX_IMAGE_BYTES ) return new WP_Error( 'elementize_ab_independent_image_too_large', 'A candidate thumbnail exceeds the local normalization size limit.' );

            $source_mime = self::detect_mime( $bytes, (string) $candidate['thumbnail_mime'] );
            $transport_bytes = $bytes;
            $transport_mime = $source_mime;
            $transcoded = false;
            if ( ! in_array( $source_mime, [ 'image/png', 'image/jpeg' ], true ) ) {
                $png = self::to_png( $bytes );
                if ( is_wp_error( $png ) ) return $png;
                $transport_bytes = $png;
                $transport_mime = 'image/png';
                $transcoded = true;
            }
            $candidate['thumbnail_base64'] = base64_encode( $transport_bytes );
            $out[] = $candidate;
            $diag[] = [
                'slot' => (string) $candidate['slot'],
                'template_id' => (string) $candidate['template_id'],
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
        if ( ! function_exists( 'wp_get_image_editor' ) ) return new WP_Error( 'elementize_ab_independent_image_editor_missing', 'No local WordPress image editor is available.' );

        $input = wp_tempnam( 'elementize-ab-independent-input' );
        $output = wp_tempnam( 'elementize-ab-independent-output.png' );
        if ( ! is_string( $input ) || ! is_string( $output ) || '' === $input || '' === $output ) return new WP_Error( 'elementize_ab_independent_temp_failed', 'Temporary image files could not be created.' );
        try {
            if ( false === @file_put_contents( $input, $bytes ) ) return new WP_Error( 'elementize_ab_independent_temp_write_failed', 'The candidate image could not be written to a temporary local file.' );
            $editor = wp_get_image_editor( $input );
            if ( is_wp_error( $editor ) ) return new WP_Error( 'elementize_ab_independent_editor_decode_failed', self::clip( $editor->get_error_message(), 180 ) );
            $saved = $editor->save( $output, 'image/png' );
            if ( is_wp_error( $saved ) ) return new WP_Error( 'elementize_ab_independent_editor_save_failed', self::clip( $saved->get_error_message(), 180 ) );
            $png = @file_get_contents( $output );
            return is_string( $png ) && str_starts_with( $png, "\x89PNG\r\n\x1a\n" ) ? $png : new WP_Error( 'elementize_ab_independent_editor_output_invalid', 'The local image editor did not produce a valid PNG.' );
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
        if ( null === $index ) return new WP_Error( 'elementize_ab_independent_target_missing', 'The requested target marker is missing from the grounded context ledger.' );
        return [
            'design_dna' => $snapshot['design_dna'] ?? [],
            'art_direction' => $snapshot['art_direction'] ?? [],
            'section_coherence' => $snapshot['section_coherence'] ?? [],
            'previous_section' => $index > 0 ? $ledger[ $index - 1 ] : null,
            'target' => $ledger[ $index ],
            'next_section' => isset( $ledger[ $index + 1 ] ) ? $ledger[ $index + 1 ] : null,
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

    private static function confidence( $value ): string {
        return self::enum( $value, [ 'low', 'medium', 'high' ], 'low' );
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
        if ( ! is_array( $data ) ) $data = [];
        $data['aesthetic_ab_independent_scoring_version'] = self::VERSION;
        $data['aesthetic_ab_independent_scoring'] = true;
        $data['aesthetic_ab_independent_scoring_read_only'] = true;
        $data['aesthetic_ab_independent_scoring_slot_order_blind'] = true;
        $data['aesthetic_ab_independent_scoring_template_identity_hidden_from_model'] = true;
        $data['aesthetic_ab_independent_scoring_action_slot_cost'] = 0;
        $data['aesthetic_ab_independent_scoring_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

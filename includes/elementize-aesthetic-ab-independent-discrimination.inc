<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Independent_Scoring', false ) ) return;

/**
 * Anchored discrimination for slot-blind independent candidate scoring.
 *
 * The first independent scorer deliberately removes A/B positional bias, but a small
 * local vision model may still collapse visually different candidates onto the same
 * generous absolute score. This layer runs only when the independent top scores are
 * tied or separated by at most one point. Each candidate is again judged alone, with
 * no slot/template identity exposed to the model, using explicit 1-5 visual-fit axes.
 * PHP derives the final 1-10 score deterministically from those axes plus bounded
 * visual-intensity and mismatch-severity penalties. Existing calibration still runs
 * afterward and may only lower confidence/usability.
 *
 * Read-only: this class never inserts, replaces, edits, publishes, or otherwise
 * mutates Elementor/page state.
 */
final class Elementize_Aesthetic_AB_Independent_Discrimination {
    private const VERSION = '0.27.7-d1';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const VISION_TIMEOUT_SECONDS = 150;
    private const MAX_PROMPT_CONTEXT_BYTES = 18000;
    private const MAX_IMAGE_BYTES = 2097152;
    private const TRIGGER_MAX_GAP = 1;

    public static function init(): void {
        // Independent scorer is 600; deterministic calibration is 610.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 605, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/visual-probe' !== $route || 'POST' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'aesthetic_compare' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['aesthetic_comparison'] ?? null ) ) return $response;
        $comparison = $data['aesthetic_comparison'];
        $independent = is_array( $comparison['independent_candidate_scoring'] ?? null ) ? $comparison['independent_candidate_scoring'] : [];
        if ( empty( $independent['applied'] ) ) return $response;

        $top = absint( $independent['top_score'] ?? 0 );
        $runner = absint( $independent['runner_up_score'] ?? 0 );
        $gap = abs( $top - $runner );
        $tied = ! empty( $independent['tied_top_score'] ) || $gap === 0 || 'none' === strtolower( trim( (string) ( $comparison['winner_slot'] ?? '' ) ) );
        if ( ! $tied && $gap > self::TRIGGER_MAX_GAP ) return $response;

        $diag = [
            'version' => self::VERSION,
            'attempted' => true,
            'applied' => false,
            'trigger' => $tied ? 'independent_top_score_tie' : 'independent_score_gap_at_most_' . self::TRIGGER_MAX_GAP,
            'initial_top_score' => $top,
            'initial_runner_up_score' => $runner,
            'initial_score_gap' => $gap,
            'slot_order_blind' => true,
            'template_identity_hidden_from_model' => true,
            'one_image_per_model_request' => true,
            'dimension_anchored' => true,
            'overall_score_derived_by_php' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
        ];

        $result = self::discriminate( $data, $request );
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = self::clip( $result->get_error_message(), 500 );
            $comparison['independent_discrimination'] = $diag;
            $data['aesthetic_comparison'] = $comparison;
            $response->set_data( $data );
            return $response;
        }

        $diag = array_merge( $diag, $result['diagnostics'], [ 'applied' => true ] );

        $comparison['candidate_judgments'] = $result['candidate_judgments'];
        $comparison['winner_slot'] = $result['winner_slot'];
        $comparison['winner_template_id'] = $result['winner_template_id'];
        $comparison['confidence'] = $result['confidence'];
        $comparison['winner_margin'] = $result['winner_margin'];
        $comparison['professional_rationale'] = $result['professional_rationale'];
        $comparison['decision_basis'] = $result['decision_basis'];
        $comparison['page_state_hash'] = $result['page_state_hash'];
        $comparison['context_captured_gmt'] = $result['context_captured_gmt'];
        $comparison['context_grounding_source'] = $result['context_grounding_source'];
        $comparison['target_context'] = $result['target_context'];
        $comparison['comparison_method'] = 'slot_blind_independent_candidate_scoring_with_anchored_discrimination';
        $comparison['usable_for_selection'] = 'none' !== $result['winner_slot'] && in_array( $result['confidence'], [ 'medium', 'high' ], true );
        $comparison['selection_policy'] = $comparison['usable_for_selection']
            ? 'The anchored independently scored winner may be used as a read-only recommendation for section selection. It still does not authorize insertion or replacement.'
            : 'Anchored independent scoring remains advisory. Do not select automatically unless deterministic calibration also considers the result usable.';
        $comparison['independent_discrimination'] = $diag;
        unset( $comparison['reason'] );

        $data['aesthetic_comparison'] = $comparison;
        $response->set_data( $data );
        return $response;
    }

    private static function discriminate( array $data, WP_REST_Request $request ) {
        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) ) return new WP_Error( 'elementize_ab_discrimination_context_invalid', 'Anchored discrimination requires a valid context_page_id and target_marker.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_ab_discrimination_hash_unavailable', 'The current Elementor page state could not be fingerprinted during anchored discrimination.' );

        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) {
            return new WP_Error( 'elementize_ab_discrimination_context_stale', 'The exact-state grounded A/B context is unavailable or stale during anchored discrimination.' );
        }

        $context = self::target_context( $snapshot, $marker );
        if ( is_wp_error( $context ) ) return $context;

        $candidates = self::extract_candidates( $data );
        if ( is_wp_error( $candidates ) ) return $candidates;
        if ( count( $candidates ) < 2 || count( $candidates ) > 4 ) return new WP_Error( 'elementize_ab_discrimination_candidate_count', 'Anchored discrimination requires between two and four candidate thumbnails.' );

        $normalized = self::normalize_images( $candidates );
        if ( is_wp_error( $normalized ) ) return $normalized;

        $target_intensity = self::clamp_score( $context['target']['visual_intensity'] ?? 5 );
        $rows = [];
        $candidate_diagnostics = [];

        foreach ( $normalized['candidates'] as $candidate ) {
            $model = self::score_dimensions( (string) $candidate['thumbnail_base64'], $context );
            if ( is_wp_error( $model ) ) {
                return new WP_Error(
                    'elementize_ab_discrimination_candidate_failed',
                    'Anchored discrimination failed for candidate ' . (string) $candidate['slot'] . ': ' . $model->get_error_message()
                );
            }

            $dimensions = [
                'composition' => self::axis( $model['composition_fit_score'] ?? 1 ),
                'whitespace' => self::axis( $model['whitespace_fit_score'] ?? 1 ),
                'density' => self::axis( $model['density_fit_score'] ?? 1 ),
                'hierarchy' => self::axis( $model['hierarchy_fit_score'] ?? 1 ),
                'design_dna' => self::axis( $model['design_dna_fit_score'] ?? 1 ),
                'transition' => self::axis( $model['transition_fit_score'] ?? 1 ),
                'conversion' => self::axis( $model['conversion_fit_score'] ?? 1 ),
                'restraint' => self::axis( $model['restraint_fit_score'] ?? 1 ),
            ];
            $estimated_intensity = self::clamp_score( $model['estimated_visual_intensity'] ?? 5 );
            $mismatch_severity = self::enum( $model['mismatch_severity'] ?? '', [ 'none', 'low', 'medium', 'high' ], 'medium' );
            $derived = self::derive_score( $dimensions, $estimated_intensity, $target_intensity, $mismatch_severity );

            $rhythm_axis = (int) round( ( $dimensions['whitespace'] + $dimensions['density'] + $dimensions['transition'] ) / 3 );
            $row = [
                'slot' => (string) $candidate['slot'],
                'template_id' => (string) $candidate['template_id'],
                'score' => $derived['score'],
                'overall_fit' => self::overall_fit_from_score( $derived['score'] ),
                'design_dna_fit' => self::fit_from_axis( $dimensions['design_dna'] ),
                'rhythm_fit' => self::fit_from_axis( $rhythm_axis ),
                'hierarchy_fit' => self::fit_from_axis( $dimensions['hierarchy'] ),
                'conversion_fit' => self::fit_from_axis( $dimensions['conversion'] ),
                'strength' => self::clip( (string) ( $model['strength'] ?? '' ), 320 ),
                'risk' => self::clip( (string) ( $model['risk'] ?? '' ), 320 ),
            ];
            $rows[] = $row;
            $candidate_diagnostics[] = [
                'slot' => (string) $candidate['slot'],
                'template_id' => (string) $candidate['template_id'],
                'dimension_scores' => $dimensions,
                'dimension_sum' => array_sum( $dimensions ),
                'estimated_visual_intensity' => $estimated_intensity,
                'target_visual_intensity' => $target_intensity,
                'visual_intensity_delta' => abs( $estimated_intensity - $target_intensity ),
                'mismatch_severity' => $mismatch_severity,
                'model_confidence' => self::confidence( $model['confidence'] ?? 'low' ),
                'raw_axis_score' => $derived['raw_axis_score'],
                'intensity_penalty' => $derived['intensity_penalty'],
                'mismatch_penalty' => $derived['mismatch_penalty'],
                'derived_score' => $derived['score'],
            ];
        }

        $ranked = $rows;
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

        $confidence = 'low';
        if ( ! $tie ) {
            if ( $top_score >= 8 && $gap >= 3 ) $confidence = 'high';
            elseif ( $top_score >= 6 && $gap >= 1 ) $confidence = 'medium';
        }
        $margin = 'slight';
        if ( ! $tie && $gap >= 5 && $top_score >= 9 ) $margin = 'decisive';
        elseif ( ! $tie && $gap >= 3 ) $margin = 'clear';

        $rationale = $tie
            ? 'Anchored independent dimension scoring still produced a top-score tie, so no winner is retained.'
            : 'Anchored independent dimension scoring ranked ' . (string) $top['template_id'] . ' at ' . $top_score . '/10 versus ' . (string) $runner['template_id'] . ' at ' . $runner_score . '/10. The final scores were derived deterministically from anonymous per-candidate visual-fit axes and bounded penalties.';

        return [
            'candidate_judgments' => $rows,
            'winner_slot' => $winner_slot,
            'winner_template_id' => $winner_template_id,
            'confidence' => $confidence,
            'winner_margin' => $margin,
            'professional_rationale' => self::clip( $rationale, 900 ),
            'decision_basis' => [
                'Each candidate was judged alone with slot/template identity hidden from the model.',
                'Eight explicit 1-5 visual-fit axes were scored against the exact same grounded page context.',
                'PHP derived the 1-10 candidate score from the axis scores; the model did not choose the final numeric score.',
                'A bounded penalty was applied for visual-intensity mismatch against the grounded target section and for material mismatch severity.',
                'Winner confidence and margin were derived deterministically from absolute winner score and score gap.',
            ],
            'page_state_hash' => $state_hash,
            'context_captured_gmt' => (string) ( $snapshot['captured_gmt'] ?? '' ),
            'context_grounding_source' => (string) ( $snapshot['grounding_source'] ?? '' ),
            'target_context' => $context['target'],
            'diagnostics' => [
                'candidate_count' => count( $rows ),
                'candidate_results' => $candidate_diagnostics,
                'candidate_image_normalization' => $normalized['diagnostics'],
                'target_visual_intensity' => $target_intensity,
                'top_score' => $top_score,
                'runner_up_score' => $runner_score,
                'score_gap' => $gap,
                'tied_top_score' => $tie,
                'effective_winner_template_id' => $winner_template_id,
            ],
        ];
    }

    private static function score_dimensions( string $thumbnail_base64, array $context ) {
        $context_json = wp_json_encode( $context, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $context_json ) ) return new WP_Error( 'elementize_ab_discrimination_context_encode_failed', 'The grounded page context could not be encoded for anchored discrimination.' );
        if ( strlen( $context_json ) > self::MAX_PROMPT_CONTEXT_BYTES ) $context_json = substr( $context_json, 0, self::MAX_PROMPT_CONTEXT_BYTES );

        $prompt = "You are a demanding senior SaaS web designer evaluating ONE anonymous section candidate against an exact grounded page context. There is exactly one attached image. You are NOT given its Pixfort template name, ID, request slot, or any competing image.\n\n" .
            "GROUNDED PAGE CONTEXT:\n" . $context_json . "\n\n" .
            "Score the candidate on EACH visual-fit axis from 1 to 5 using these strict anchors: 5=exceptionally aligned with this exact page context with essentially no visible mismatch; 4=good fit with minor visible mismatch; 3=mixed/average fit with meaningful compromises; 2=material mismatch that would need substantial correction; 1=poor contextual fit. Do not default to 4 or 5. Use the full scale when warranted.\n\n" .
            "Axes: composition fit; whitespace fit; density fit; typography/hierarchy fit; design-DNA fit; likely transition fit to the neighboring page sections; conversion-emphasis fit; visual restraint fit. Also estimate the candidate's visual intensity from 1-10, where 1 is extremely quiet/minimal and 10 is extremely dense/loud/visually intense. Set mismatch_severity to none, low, medium, or high for the largest visible contextual mismatch.\n\n" .
            "Ignore demo copy meaning, product category, brand names, logos, testimonial claims, stats, and semantic promises. Judge only visible design behavior. Do not reward a candidate simply for looking polished in isolation; score its fit to THIS grounded page. Do not compare against an unseen alternative. Return JSON only.";

        $schema = [
            'type' => 'object',
            'required' => [
                'composition_fit_score', 'whitespace_fit_score', 'density_fit_score', 'hierarchy_fit_score',
                'design_dna_fit_score', 'transition_fit_score', 'conversion_fit_score', 'restraint_fit_score',
                'estimated_visual_intensity', 'mismatch_severity', 'strength', 'risk', 'confidence'
            ],
            'properties' => [
                'composition_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'whitespace_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'density_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'hierarchy_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'design_dna_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'transition_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'conversion_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'restraint_fit_score' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 5 ],
                'estimated_visual_intensity' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ],
                'mismatch_severity' => [ 'type' => 'string', 'enum' => [ 'none', 'low', 'medium', 'high' ] ],
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
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_ab_discrimination_ollama_failed', 'Local Ollama anchored discrimination request failed.' );
        $code = (int) wp_remote_retrieve_response_code( $response );
        if ( 200 !== $code ) return new WP_Error( 'elementize_ab_discrimination_ollama_http', 'Local Ollama anchored discrimination returned HTTP ' . $code . '.' );
        $outer = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_ab_discrimination_envelope_invalid', 'Local Ollama anchored discrimination returned an invalid envelope.' );
        $decoded = json_decode( self::strip_json_fence( (string) $outer['response'] ), true );
        return is_array( $decoded ) ? $decoded : new WP_Error( 'elementize_ab_discrimination_json_invalid', 'Local Ollama anchored discrimination returned invalid JSON.' );
    }

    private static function derive_score( array $dimensions, int $estimated_intensity, int $target_intensity, string $mismatch_severity ): array {
        $sum = array_sum( $dimensions );
        // Eight axes, each 1..5. Map 8..40 linearly onto 1..10.
        $raw = 1.0 + 9.0 * ( ( $sum - 8.0 ) / 32.0 );

        $delta = abs( $estimated_intensity - $target_intensity );
        $intensity_penalty = 0.0;
        if ( $delta >= 5 ) $intensity_penalty = 2.0;
        elseif ( $delta >= 4 ) $intensity_penalty = 1.5;
        elseif ( $delta >= 3 ) $intensity_penalty = 1.0;
        elseif ( $delta >= 2 ) $intensity_penalty = 0.5;

        $mismatch_penalty = [ 'none' => 0.0, 'low' => 0.0, 'medium' => 0.5, 'high' => 1.0 ][ $mismatch_severity ] ?? 0.5;
        $score = self::clamp_score( round( $raw - $intensity_penalty - $mismatch_penalty ) );

        return [
            'raw_axis_score' => round( $raw, 2 ),
            'intensity_penalty' => $intensity_penalty,
            'mismatch_penalty' => $mismatch_penalty,
            'score' => $score,
        ];
    }

    private static function fit_from_axis( int $axis ): string {
        if ( $axis >= 4 ) return 'strong';
        if ( 3 === $axis ) return 'mixed';
        return 'weak';
    }

    private static function overall_fit_from_score( int $score ): string {
        if ( $score >= 7 ) return 'strong';
        if ( $score >= 4 ) return 'acceptable';
        return 'weak';
    }

    private static function extract_candidates( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_ab_discrimination_visual_proof_missing', 'The visual-probe candidate proof file is unavailable during anchored discrimination.' );
        $encoded = (string) ( $files[0]['content'] ?? '' );
        $json = base64_decode( $encoded, true );
        if ( false === $json || '' === $json ) return new WP_Error( 'elementize_ab_discrimination_visual_proof_invalid', 'The visual-probe proof file could not be decoded during anchored discrimination.' );
        $proof = json_decode( $json, true );
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_ab_discrimination_visual_proof_invalid', 'The visual-probe proof file is not valid candidate JSON during anchored discrimination.' );

        $out = [];
        foreach ( $proof['candidates'] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            $template_id = sanitize_key( (string) ( $row['template_id'] ?? '' ) );
            $image = trim( (string) ( $row['thumbnail_base64'] ?? '' ) );
            $mime = strtolower( trim( (string) ( $row['thumbnail_mime'] ?? '' ) ) );
            if ( ! in_array( $slot, [ 'A', 'B', 'C', 'D' ], true ) || '' === $template_id || '' === $image ) continue;
            $out[] = [ 'slot' => $slot, 'template_id' => $template_id, 'thumbnail_mime' => $mime, 'thumbnail_base64' => $image ];
        }
        return count( $out ) >= 2 ? $out : new WP_Error( 'elementize_ab_discrimination_visual_proof_incomplete', 'Fewer than two usable candidate thumbnails were available during anchored discrimination.' );
    }

    private static function normalize_images( array $candidates ) {
        $out = [];
        $diag = [];
        foreach ( $candidates as $candidate ) {
            $bytes = base64_decode( (string) $candidate['thumbnail_base64'], true );
            if ( false === $bytes || '' === $bytes ) return new WP_Error( 'elementize_ab_discrimination_image_decode_failed', 'A candidate thumbnail could not be base64-decoded locally.' );
            if ( strlen( $bytes ) > self::MAX_IMAGE_BYTES ) return new WP_Error( 'elementize_ab_discrimination_image_too_large', 'A candidate thumbnail exceeds the local normalization size limit.' );

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
        if ( ! function_exists( 'wp_get_image_editor' ) ) return new WP_Error( 'elementize_ab_discrimination_image_editor_missing', 'No local WordPress image editor is available.' );

        $input = wp_tempnam( 'elementize-ab-discrimination-input' );
        $output = wp_tempnam( 'elementize-ab-discrimination-output.png' );
        if ( ! is_string( $input ) || ! is_string( $output ) || '' === $input || '' === $output ) return new WP_Error( 'elementize_ab_discrimination_temp_failed', 'Temporary image files could not be created.' );
        try {
            if ( false === @file_put_contents( $input, $bytes ) ) return new WP_Error( 'elementize_ab_discrimination_temp_write_failed', 'The candidate image could not be written to a temporary local file.' );
            $editor = wp_get_image_editor( $input );
            if ( is_wp_error( $editor ) ) return new WP_Error( 'elementize_ab_discrimination_editor_decode_failed', self::clip( $editor->get_error_message(), 180 ) );
            $saved = $editor->save( $output, 'image/png' );
            if ( is_wp_error( $saved ) ) return new WP_Error( 'elementize_ab_discrimination_editor_save_failed', self::clip( $saved->get_error_message(), 180 ) );
            $png = @file_get_contents( $output );
            return is_string( $png ) && str_starts_with( $png, "\x89PNG\r\n\x1a\n" ) ? $png : new WP_Error( 'elementize_ab_discrimination_editor_output_invalid', 'The local image editor did not produce a valid PNG.' );
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
        if ( null === $index ) return new WP_Error( 'elementize_ab_discrimination_target_missing', 'The requested target marker is missing from the grounded context ledger.' );
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

    private static function axis( $value ): int {
        $score = is_numeric( $value ) ? (int) round( (float) $value ) : 1;
        return max( 1, min( 5, $score ) );
    }

    private static function clamp_score( $value ): int {
        $score = is_numeric( $value ) ? (int) round( (float) $value ) : 1;
        return max( 1, min( 10, $score ) );
    }

    private static function confidence( $value ): string {
        return self::enum( $value, [ 'low', 'medium', 'high' ], 'low' );
    }

    private static function enum( $value, array $allowed, string $fallback ): string {
        $value = strtolower( trim( (string) $value ) );
        return in_array( $value, $allowed, true ) ? $value : $fallback;
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
        $data['aesthetic_ab_independent_discrimination_version'] = self::VERSION;
        $data['aesthetic_ab_independent_discrimination'] = true;
        $data['aesthetic_ab_independent_discrimination_trigger_max_gap'] = self::TRIGGER_MAX_GAP;
        $data['aesthetic_ab_independent_discrimination_slot_order_blind'] = true;
        $data['aesthetic_ab_independent_discrimination_template_identity_hidden_from_model'] = true;
        $data['aesthetic_ab_independent_discrimination_dimension_anchored'] = true;
        $data['aesthetic_ab_independent_discrimination_php_derived_overall_score'] = true;
        $data['aesthetic_ab_independent_discrimination_read_only'] = true;
        $data['aesthetic_ab_independent_discrimination_action_slot_cost'] = 0;
        $data['aesthetic_ab_independent_discrimination_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

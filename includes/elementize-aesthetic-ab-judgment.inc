<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Grounded_Reassessment_Hardening', false ) ) return;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

/**
 * Read-only contextual A/B aesthetic judgment for Pixfort visual-probe candidates.
 *
 * This layer deliberately reuses the existing /pixfort/visual-probe action so it costs
 * no additional Custom GPT action slot. A successful grounded completion audit caches
 * a small page-design context snapshot keyed to the exact current Elementor state.
 * A later visual-probe request may opt into aesthetic_compare=true and name the page
 * plus target section marker. The local Ollama vision model then compares the existing
 * Pixfort candidate thumbnails against that grounded context. It never inserts,
 * removes, edits, publishes, or otherwise mutates page state.
 */
final class Elementize_Aesthetic_AB_Judgment {
    private const VERSION = '0.27.0-a1';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const CONTEXT_TTL = 900;
    private const VISION_TIMEOUT_SECONDS = 150;
    private const MAX_PROMPT_CONTEXT_BYTES = 18000;

    public static function init(): void {
        add_filter( 'rest_endpoints', [ self::class, 'augment_route_schema' ], 1000 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 570, 3 );
    }

    public static function augment_route_schema( array $endpoints ): array {
        $route = '/' . self::NS . '/pixfort/visual-probe';
        if ( empty( $endpoints[ $route ] ) || ! is_array( $endpoints[ $route ] ) ) return $endpoints;

        foreach ( $endpoints[ $route ] as &$endpoint ) {
            if ( ! is_array( $endpoint ) || ! self::endpoint_supports_post( $endpoint['methods'] ?? [] ) ) continue;
            if ( ! isset( $endpoint['args'] ) || ! is_array( $endpoint['args'] ) ) $endpoint['args'] = [];
            $endpoint['args']['aesthetic_compare'] = [
                'type' => 'boolean',
                'required' => false,
                'default' => false,
                'description' => 'When true, compare the probed Pixfort candidates against a fresh grounded page-design context. This remains read-only.',
            ];
            $endpoint['args']['context_page_id'] = [
                'type' => 'integer',
                'required' => false,
                'minimum' => 1,
                'sanitize_callback' => 'absint',
                'description' => 'Editable Elementor page whose grounded aesthetic context should guide the comparison.',
            ];
            $endpoint['args']['target_marker'] = [
                'type' => 'string',
                'required' => false,
                'sanitize_callback' => 'sanitize_text_field',
                'description' => 'Grounded section marker such as S1, S2, or S3 that the candidates are being considered for.',
            ];
        }
        unset( $endpoint );
        return $endpoints;
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();

        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );

        if ( 'GET' === strtoupper( $request->get_method() ) && preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\\d+)/completion-audit$#', $route, $m ) ) {
            if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;
            return self::cache_grounded_context( $response, absint( $m[1] ?? 0 ) );
        }

        if ( '/' . self::NS . '/pixfort/visual-probe' !== $route || 'POST' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'aesthetic_compare' ) ) ) return $response;

        return self::augment_visual_probe_with_comparison( $response, $request );
    }

    private static function cache_grounded_context( WP_REST_Response $response, int $page_id ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['visual'] ?? null ) || $page_id < 1 ) return $response;

        $visual = $data['visual'];
        $snapshot = self::build_context_snapshot( $page_id, $visual );
        $context_diag = [
            'version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
            'context_ttl_seconds' => self::CONTEXT_TTL,
        ];

        if ( is_wp_error( $snapshot ) ) {
            $context_diag['reason'] = $snapshot->get_error_message();
        } else {
            $key = self::cache_key( $page_id, (string) $snapshot['page_state_hash'] );
            set_transient( $key, $snapshot, self::CONTEXT_TTL );
            $context_diag['available'] = true;
            $context_diag['page_id'] = $page_id;
            $context_diag['page_state_hash'] = (string) $snapshot['page_state_hash'];
            $context_diag['captured_gmt'] = (string) $snapshot['captured_gmt'];
            $context_diag['grounding_source'] = (string) $snapshot['grounding_source'];
            $context_diag['marker_count'] = count( (array) ( $snapshot['section_ledger'] ?? [] ) );
            $context_diag['note'] = 'This compact grounded context is cached only for the exact current Elementor state and is used by the existing Pixfort visual-probe action for read-only A/B judgment.';
        }

        $visual['aesthetic_ab_context'] = $context_diag;
        $data['visual'] = $visual;
        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_ab_context_ready'] = ! empty( $context_diag['available'] );
        $response->set_data( $data );
        return $response;
    }

    private static function build_context_snapshot( int $page_id, array $visual ) {
        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];
        $dna = is_array( $visual['design_dna'] ?? null ) ? $visual['design_dna'] : [];
        $reassessment = is_array( $visual['aesthetic_grounded_reassessment'] ?? null ) ? $visual['aesthetic_grounded_reassessment'] : [];
        $semantic = is_array( $visual['aesthetic_semantic_grounding'] ?? null ) ? $visual['aesthetic_semantic_grounding'] : [];
        $coverage = is_array( $visual['aesthetic_coverage_recovery'] ?? null ) ? $visual['aesthetic_coverage_recovery'] : [];

        if ( empty( $art['approval_grounding_complete'] ) || empty( $art['approval_usable'] ) ) {
            return new WP_Error( 'elementize_ab_context_not_grounded', 'Professional visual approval is not yet fully grounded and usable.' );
        }
        if ( empty( $coherence['rhythm_judgment_usable'] ) || empty( $coherence['transition_contract_valid'] ) ) {
            return new WP_Error( 'elementize_ab_context_rhythm_unavailable', 'Grounded section rhythm is not yet usable.' );
        }

        $ledger = [];
        $source = '';
        if ( ! empty( $reassessment['assessment_complete'] ) && is_array( $reassessment['section_ledger'] ?? null ) ) {
            $ledger = $reassessment['section_ledger'];
            $source = 'fact_conditioned_grounded_reassessment';
        } elseif ( is_array( $semantic['section_grounding'] ?? null ) && ! empty( $semantic['section_grounding'] ) ) {
            $ledger = $semantic['section_grounding'];
            $source = 'semantic_grounding';
        } elseif ( is_array( $coverage['section_ledger'] ?? null ) && ! empty( $coverage['section_ledger'] ) ) {
            $ledger = $coverage['section_ledger'];
            $source = 'aesthetic_coverage_recovery';
        }
        if ( ! $ledger ) return new WP_Error( 'elementize_ab_context_sections_unavailable', 'No grounded section ledger is available for contextual comparison.' );

        $compact_ledger = [];
        foreach ( $ledger as $row ) {
            if ( ! is_array( $row ) ) continue;
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            if ( ! preg_match( '/^S\\d{1,2}$/', $marker ) ) continue;
            $compact_ledger[] = [
                'marker' => $marker,
                'top_level_element_id' => self::clip( (string) ( $row['top_level_element_id'] ?? '' ), 32 ),
                'visual_intensity' => self::clamp_score( $row['visual_intensity'] ?? 1 ),
                'transition_from_previous' => self::enum( $row['transition_from_previous'] ?? '', [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ], 'neutral' ),
                'coherence_fit' => self::enum( $row['coherence_fit'] ?? $row['effective_coherence_fit'] ?? '', [ 'fits', 'mixed', 'violates' ], 'mixed' ),
                'issue_category' => self::enum( $row['issue_category'] ?? '', [ 'none', 'hierarchy', 'spacing', 'density', 'typography', 'imagery', 'component_style', 'composition', 'transition', 'conversion', 'section_choice', 'other' ], 'none' ),
                'severity' => self::enum( $row['severity'] ?? '', [ 'none', 'low', 'medium', 'high' ], 'none' ),
                'evidence' => self::clip( (string) ( $row['evidence'] ?? '' ), 320 ),
                'recommendation' => self::clip( (string) ( $row['recommendation'] ?? '' ), 320 ),
                'intervention_level' => self::enum( $row['grounded_intervention_level'] ?? $row['intervention_level'] ?? '', [ 'none', 'tweak', 'content', 'imagery', 'restructure', 'replace_section' ], 'none' ),
            ];
        }
        if ( ! $compact_ledger ) return new WP_Error( 'elementize_ab_context_sections_invalid', 'The grounded section ledger did not contain usable section markers.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_ab_context_hash_unavailable', 'The current Elementor page state could not be fingerprinted.' );

        return [
            'version' => self::VERSION,
            'page_id' => $page_id,
            'page_state_hash' => $state_hash,
            'captured_gmt' => gmdate( 'Y-m-d H:i:s' ),
            'grounding_source' => $source,
            'design_dna' => self::compact_design_dna( $dna ),
            'art_direction' => [
                'grounded_professional_approval' => self::enum( $art['grounded_effective_professional_approval'] ?? $art['effective_professional_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' ),
                'aesthetic_score' => self::clamp_score( $art['calibrated_aesthetic_score'] ?? $art['aesthetic_score'] ?? 1 ),
                'coherence_score' => self::clamp_score( $art['calibrated_coherence_score'] ?? $art['coherence_score'] ?? 1 ),
                'hierarchy_score' => self::clamp_score( $art['calibrated_hierarchy_score'] ?? $art['hierarchy_score'] ?? 1 ),
                'rhythm_score' => self::clamp_score( $art['calibrated_rhythm_score'] ?? $art['rhythm_score'] ?? 1 ),
                'polish_score' => self::clamp_score( $art['calibrated_polish_score'] ?? $art['polish_score'] ?? 1 ),
                'conversion_focus_score' => self::clamp_score( $art['calibrated_conversion_focus_score'] ?? $art['conversion_focus_score'] ?? 1 ),
                'first_impression' => self::clip( (string) ( $art['first_impression'] ?? '' ), 120 ),
                'grounded_issues' => self::compact_issues( (array) ( $art['grounded_issues'] ?? [] ) ),
            ],
            'section_coherence' => [
                'effective_verdict' => self::enum( $coherence['effective_coherence_verdict'] ?? $coherence['coherence_verdict'] ?? '', [ 'strong', 'mixed', 'weak' ], 'mixed' ),
                'rhythm_quality' => self::enum( $coherence['rhythm_quality'] ?? '', [ 'strong', 'acceptable', 'weak' ], 'acceptable' ),
            ],
            'section_ledger' => $compact_ledger,
        ];
    }

    private static function augment_visual_probe_with_comparison( WP_REST_Response $response, WP_REST_Request $request ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;

        $diag = [
            'version' => self::VERSION,
            'available' => false,
            'assessment_complete' => false,
            'usable_for_selection' => false,
            'read_only' => true,
            'advisory_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
            'reuses_existing_visual_probe_action' => true,
            'provider' => 'local_ollama',
            'model' => self::model(),
            'requires_post_insert_visual_verification' => true,
            'page_screenshot_used_in_this_pass' => false,
            'candidate_visual_source' => 'existing_pixfort_visual_probe_thumbnails',
            'page_context_source' => 'fresh_grounded_context_cache_keyed_to_current_elementor_state',
        ];

        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        $diag['context_page_id'] = $page_id;
        $diag['target_marker'] = $marker;

        if ( $page_id < 1 || ! preg_match( '/^S\\d{1,2}$/', $marker ) ) {
            $diag['reason'] = 'aesthetic_compare=true requires context_page_id and a target_marker such as S1.';
            return self::finish_probe( $response, $data, $diag );
        }
        $post = get_post( $page_id );
        if ( ! $post || 'page' !== $post->post_type || ! current_user_can( 'edit_post', $page_id ) ) {
            $diag['reason'] = 'The requested contextual page is unavailable or not editable by the authenticated user.';
            return self::finish_probe( $response, $data, $diag );
        }

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) {
            $diag['reason'] = 'The current Elementor page state could not be fingerprinted.';
            return self::finish_probe( $response, $data, $diag );
        }
        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) {
            $diag['reason'] = 'No fresh grounded aesthetic context exists for the current Elementor state. Run the page completion audit with include_visual=true immediately before contextual visual probing.';
            $diag['required_preflight'] = 'GET /elementize/v1/pages/' . $page_id . '/completion-audit?include_visual=true';
            return self::finish_probe( $response, $data, $diag );
        }

        $context = self::target_context( $snapshot, $marker );
        if ( is_wp_error( $context ) ) {
            $diag['reason'] = $context->get_error_message();
            return self::finish_probe( $response, $data, $diag );
        }

        $proof = self::extract_visual_proof( $data );
        if ( is_wp_error( $proof ) ) {
            $diag['reason'] = $proof->get_error_message();
            return self::finish_probe( $response, $data, $diag );
        }
        if ( count( $proof['candidates'] ) < 2 ) {
            $diag['reason'] = 'Contextual aesthetic judgment requires at least two Pixfort candidates.';
            return self::finish_probe( $response, $data, $diag );
        }

        $result = self::analyze_candidates( $proof['candidates'], $context );
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = $result->get_error_message();
            return self::finish_probe( $response, $data, $diag );
        }

        $normalized = self::normalize_result( $result, $proof['candidates'] );
        if ( is_wp_error( $normalized ) ) {
            $diag['reason'] = $normalized->get_error_message();
            return self::finish_probe( $response, $data, $diag );
        }

        $diag = array_merge( $diag, $normalized );
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

        return self::finish_probe( $response, $data, $diag );
    }

    private static function finish_probe( WP_REST_Response $response, array $data, array $diag ): WP_REST_Response {
        $data['aesthetic_comparison'] = $diag;
        $response->set_data( $data );
        return $response;
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
        if ( null === $index ) return new WP_Error( 'elementize_ab_target_not_found', 'The requested target marker is not present in the fresh grounded section ledger.' );

        $payload = [
            'design_dna' => $snapshot['design_dna'] ?? [],
            'art_direction' => $snapshot['art_direction'] ?? [],
            'section_coherence' => $snapshot['section_coherence'] ?? [],
            'previous_section' => $index > 0 ? $ledger[ $index - 1 ] : null,
            'target' => $ledger[ $index ],
            'next_section' => isset( $ledger[ $index + 1 ] ) ? $ledger[ $index + 1 ] : null,
        ];
        return $payload;
    }

    private static function extract_visual_proof( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_ab_visual_proof_missing', 'The existing visual-probe response did not contain its candidate proof file.' );
        $encoded = (string) ( $files[0]['content'] ?? '' );
        $json = base64_decode( $encoded, true );
        if ( false === $json || '' === $json ) return new WP_Error( 'elementize_ab_visual_proof_invalid', 'The visual-probe proof file could not be decoded.' );
        $proof = json_decode( $json, true );
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_ab_visual_proof_invalid', 'The visual-probe proof file is not valid candidate JSON.' );

        $candidates = [];
        foreach ( $proof['candidates'] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            $image = trim( (string) ( $row['thumbnail_base64'] ?? '' ) );
            $template_id = sanitize_key( (string) ( $row['template_id'] ?? '' ) );
            if ( ! in_array( $slot, [ 'A', 'B', 'C', 'D' ], true ) || '' === $image || '' === $template_id ) continue;
            $candidates[] = [
                'slot' => $slot,
                'template_id' => $template_id,
                'title' => self::clip( (string) ( $row['title'] ?? $template_id ), 180 ),
                'identity_hash' => self::clip( (string) ( $row['identity_hash'] ?? '' ), 80 ),
                'thumbnail_base64' => $image,
            ];
        }
        if ( count( $candidates ) < 2 ) return new WP_Error( 'elementize_ab_visual_proof_incomplete', 'Fewer than two usable candidate thumbnails were present in the visual proof.' );
        return [ 'candidates' => $candidates ];
    }

    private static function analyze_candidates( array $candidates, array $context ) {
        $labels = [];
        $images = [];
        foreach ( $candidates as $candidate ) {
            $labels[] = $candidate['slot'] . ' = template_id ' . $candidate['template_id'] . '; title "' . $candidate['title'] . '"';
            $images[] = $candidate['thumbnail_base64'];
        }

        $context_json = wp_json_encode( $context, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $context_json ) ) return new WP_Error( 'elementize_ab_context_encode_failed', 'The grounded page context could not be encoded for local comparison.' );
        if ( strlen( $context_json ) > self::MAX_PROMPT_CONTEXT_BYTES ) $context_json = substr( $context_json, 0, self::MAX_PROMPT_CONTEXT_BYTES );

        $prompt = "You are a demanding senior SaaS web designer performing a RELATIVE A/B/C/D aesthetic comparison of Pixfort section candidates.\n\n" .
            "The attached images are candidate thumbnails in this exact order: " . implode( ', ', array_column( $candidates, 'slot' ) ) . ".\n" .
            implode( "\n", $labels ) . "\n\n" .
            "The page context below came from a fresh fact-conditioned grounded aesthetic audit of the exact current Elementor page state. Treat it as the source of truth for the existing page's design language, rhythm, target section, and neighboring sections.\n" .
            $context_json . "\n\n" .
            "Judge candidates RELATIVELY, not by generic prettiness. Compare composition, whitespace, density, typography hierarchy, geometry, imagery balance, CTA prominence, visual intensity, restraint, and likely transition compatibility with the surrounding page. Ignore candidate demo copy, brand names, logos, testimonial claims, stats, and other semantic promises; template titles are metadata only. Do not invent facts about the current page beyond the grounded context. Do not claim that a thumbnail proves final post-insertion appearance.\n\n" .
            "Return JSON only with this exact shape: {\"winner\":\"A|B|C|D|none\",\"confidence\":\"low|medium|high\",\"winner_margin\":\"slight|clear|decisive\",\"professional_rationale\":\"...\",\"candidate_judgments\":[{\"slot\":\"A\",\"score\":1,\"overall_fit\":\"strong|acceptable|weak\",\"design_dna_fit\":\"strong|mixed|weak\",\"rhythm_fit\":\"strong|mixed|weak\",\"hierarchy_fit\":\"strong|mixed|weak\",\"conversion_fit\":\"strong|mixed|weak\",\"strength\":\"...\",\"risk\":\"...\"}],\"decision_basis\":[\"...\"]}. Return EXACTLY one candidate_judgments row per supplied candidate slot and no extra slots.";

        $payload = [
            'model' => self::model(),
            'stream' => false,
            'format' => 'json',
            'messages' => [
                [
                    'role' => 'user',
                    'content' => $prompt,
                    'images' => $images,
                ],
            ],
            'options' => [
                'temperature' => 0.1,
            ],
        ];

        $response = wp_remote_post( self::ollama_chat_url(), [
            'timeout' => self::VISION_TIMEOUT_SECONDS,
            'redirection' => 0,
            'headers' => [ 'Content-Type' => 'application/json' ],
            'body' => wp_json_encode( $payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ),
        ] );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_ab_ollama_failed', 'Local Ollama A/B comparison failed: ' . $response->get_error_message() );
        $code = (int) wp_remote_retrieve_response_code( $response );
        if ( $code < 200 || $code >= 300 ) return new WP_Error( 'elementize_ab_ollama_failed', 'Local Ollama A/B comparison returned HTTP ' . $code . '.' );
        $body = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        $content = is_array( $body ) ? (string) ( $body['message']['content'] ?? '' ) : '';
        $content = self::strip_json_fence( $content );
        $result = json_decode( $content, true );
        if ( ! is_array( $result ) ) return new WP_Error( 'elementize_ab_ollama_invalid_json', 'Local Ollama returned an invalid A/B comparison JSON object.' );
        return $result;
    }

    private static function normalize_result( array $result, array $candidates ) {
        $expected_slots = array_values( array_map( static fn ( array $row ): string => (string) $row['slot'], $candidates ) );
        $rows = is_array( $result['candidate_judgments'] ?? null ) ? $result['candidate_judgments'] : [];
        if ( count( $rows ) !== count( $expected_slots ) ) return new WP_Error( 'elementize_ab_candidate_coverage_failed', 'The local comparison did not return exactly one judgment per candidate.' );

        $by_slot = [];
        foreach ( $rows as $row ) {
            if ( ! is_array( $row ) ) return new WP_Error( 'elementize_ab_candidate_row_invalid', 'The local comparison returned an invalid candidate judgment row.' );
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            if ( ! in_array( $slot, $expected_slots, true ) || isset( $by_slot[ $slot ] ) ) return new WP_Error( 'elementize_ab_candidate_slot_invalid', 'The local comparison returned an unknown or duplicate candidate slot.' );
            $by_slot[ $slot ] = [
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
        if ( array_keys( $by_slot ) !== array_values( array_filter( $expected_slots, static fn ( $slot ): bool => isset( $by_slot[ $slot ] ) ) ) ) {
            $ordered = [];
            foreach ( $expected_slots as $slot ) {
                if ( ! isset( $by_slot[ $slot ] ) ) return new WP_Error( 'elementize_ab_candidate_coverage_failed', 'A candidate slot is missing from the local comparison.' );
                $ordered[] = $by_slot[ $slot ];
            }
        } else {
            $ordered = array_values( $by_slot );
        }

        $winner = strtolower( trim( (string) ( $result['winner'] ?? 'none' ) ) );
        $winner = 'none' === $winner ? 'none' : strtoupper( $winner );
        if ( 'none' !== $winner && ! in_array( $winner, $expected_slots, true ) ) return new WP_Error( 'elementize_ab_winner_invalid', 'The local comparison selected a winner outside the supplied candidate slots.' );

        if ( 'none' !== $winner ) {
            $winner_score = 0;
            $max_score = 0;
            $max_count = 0;
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
            if ( $winner_score !== $max_score ) return new WP_Error( 'elementize_ab_winner_score_conflict', 'The selected winner does not have the highest candidate score.' );
        } else {
            $max_count = 0;
        }

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

    private static function compact_design_dna( array $dna ): array {
        $out = [];
        foreach ( [ 'aesthetic_direction', 'visual_density', 'whitespace_character', 'typography_character', 'geometry_language', 'color_behavior', 'imagery_language', 'layout_language', 'component_language', 'restraint_level', 'effective_design_system_coherence' ] as $key ) {
            if ( ! array_key_exists( $key, $dna ) ) continue;
            $value = $dna[ $key ];
            if ( is_array( $value ) ) {
                $out[ $key ] = array_values( array_slice( array_filter( array_map( static fn ( $v ): string => self::clip( (string) $v, 100 ), $value ) ), 0, 8 ) );
            } elseif ( is_scalar( $value ) ) {
                $out[ $key ] = self::clip( (string) $value, 180 );
            }
        }
        return $out;
    }

    private static function compact_issues( array $issues ): array {
        $out = [];
        foreach ( $issues as $issue ) {
            if ( ! is_array( $issue ) ) continue;
            $out[] = [
                'category' => self::clip( (string) ( $issue['category'] ?? '' ), 40 ),
                'severity' => self::enum( $issue['severity'] ?? '', [ 'low', 'medium', 'high' ], 'medium' ),
                'section_markers' => array_values( array_slice( array_filter( array_map( 'strval', (array) ( $issue['section_markers'] ?? [] ) ) ), 0, 4 ) ),
                'evidence' => self::clip( (string) ( $issue['evidence'] ?? '' ), 280 ),
                'recommendation' => self::clip( (string) ( $issue['recommendation'] ?? '' ), 280 ),
                'intervention_level' => self::enum( $issue['grounded_intervention_level'] ?? $issue['intervention_level'] ?? '', [ 'none', 'tweak', 'content', 'imagery', 'restructure', 'replace_section' ], 'none' ),
            ];
            if ( count( $out ) >= 8 ) break;
        }
        return $out;
    }

    private static function endpoint_supports_post( $methods ): bool {
        if ( is_string( $methods ) ) return false !== stripos( $methods, 'POST' );
        if ( ! is_array( $methods ) ) return false;
        if ( isset( $methods['POST'] ) && $methods['POST'] ) return true;
        return in_array( 'POST', $methods, true );
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
        foreach ( $candidates as $candidate ) {
            if ( $slot === (string) ( $candidate['slot'] ?? '' ) ) return (string) ( $candidate['template_id'] ?? '' );
        }
        return null;
    }

    private static function model(): string {
        if ( defined( self::MODEL_CONST ) ) {
            $value = trim( (string) constant( self::MODEL_CONST ) );
            if ( '' !== $value ) return $value;
        }
        return self::DEFAULT_MODEL;
    }

    private static function ollama_chat_url(): string {
        $base = self::DEFAULT_OLLAMA_URL;
        if ( defined( self::OLLAMA_URL_CONST ) ) {
            $value = trim( (string) constant( self::OLLAMA_URL_CONST ) );
            if ( '' !== $value ) $base = $value;
        }
        return rtrim( $base, '/' ) . '/api/chat';
    }

    private static function strip_json_fence( string $content ): string {
        $content = trim( $content );
        if ( preg_match( '/^```(?:json)?\\s*(.*?)\\s*```$/is', $content, $m ) ) return trim( (string) $m[1] );
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
        $value = trim( preg_replace( '/\\s+/u', ' ', $value ) ?? $value );
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
        $data['aesthetic_ab_judgment_version'] = self::VERSION;
        $data['aesthetic_ab_judgment'] = true;
        $data['aesthetic_ab_judgment_read_only'] = true;
        $data['aesthetic_ab_judgment_reuses_visual_probe'] = true;
        $data['aesthetic_ab_judgment_action_slot_cost'] = 0;
        $data['aesthetic_ab_judgment_requires_fresh_grounded_context'] = true;
        $data['aesthetic_ab_judgment_context_ttl_seconds'] = self::CONTEXT_TTL;
        $data['aesthetic_ab_automatic_write_allowed'] = false;
        $response->set_data( $data );
        return $response;
    }
}

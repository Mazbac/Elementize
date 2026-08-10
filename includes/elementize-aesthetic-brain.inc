<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

/**
 * Elementize Aesthetic Brain v1.
 *
 * Read-only senior art-direction pass over a fresh, locator-annotated render.
 * This deliberately grants no mutation authority. Its job is to judge whether
 * the page feels like one aesthetically coherent professional design, infer a
 * page-level design language, and identify section-to-section coherence issues.
 */
final class Elementize_Aesthetic_Brain {
    private const VERSION = '0.26.0';
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
        // Run after the established visual/measurement/repair-planning pipeline.
        // This pass is intentionally independent and read-only.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 530, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $visual = is_array( $data['visual'] ?? null ) ? $data['visual'] : [];

        $result = self::analyze_page( absint( $m[1] ?? 0 ), $visual );
        $visual['design_dna'] = $result['design_dna'];
        $visual['page_art_direction'] = $result['page_art_direction'];
        $visual['section_coherence'] = $result['section_coherence'];
        $data['visual'] = $visual;

        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_brain_requested'] = true;
        $data['summary']['aesthetic_brain_available'] = ! empty( $result['page_art_direction']['available'] );
        $data['summary']['aesthetic_brain_read_only'] = true;
        $data['summary']['aesthetic_brain_blocks_completion_by_default'] = false;
        $data['summary']['professional_visual_approval'] = (string) ( $result['page_art_direction']['professional_approval'] ?? 'unavailable' );
        $data['summary']['professional_visual_approval_usable'] = ! empty( $result['page_art_direction']['approval_usable'] );
        $response->set_data( $data );
        return $response;
    }

    private static function analyze_page( int $id, array $visual ): array {
        $marker_map = self::top_level_marker_map( $id );
        $base_meta = [
            'version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'advisory_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'provider' => 'local_ollama',
            'model' => self::model(),
            'source' => 'fresh_locator_annotated_full_page_render',
            'paid_services_required' => false,
            'external_account_required' => false,
            'screenshot_persisted' => false,
            'screenshot_exposed' => false,
            'signed_preview_url_exposed' => false,
            'action_slot_cost' => 0,
            'blocks_design_completion_by_default' => false,
            'marker_count' => count( $marker_map ),
        ];

        if ( $id < 1 ) return self::unavailable_result( $base_meta, 'A valid page ID is required.' );
        if ( empty( $visual['available'] ) || empty( $visual['screenshot']['captured'] ) ) {
            return self::unavailable_result( $base_meta, 'The established rendered visual audit is unavailable, so the aesthetic pass will not manufacture a judgment.' );
        }
        if ( ! $marker_map ) return self::unavailable_result( $base_meta, 'No top-level Elementor sections were available for page-level art direction.' );

        $browser = self::browser_path();
        if ( '' === $browser || ! function_exists( 'proc_open' ) ) {
            return self::unavailable_result( $base_meta, 'Local Chrome/Chromium/Edge capture is unavailable.' );
        }
        if ( ! self::ollama_ready() ) {
            return self::unavailable_result( $base_meta, 'Local Ollama visual analysis is unavailable or the configured model is not installed.' );
        }

        $preview = self::fresh_signed_preview( $id );
        if ( is_wp_error( $preview ) || empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) {
            $reason = is_wp_error( $preview ) ? self::safe_error( $preview, 'Could not issue a fresh signed preview.' ) : 'A fresh signed preview is unavailable.';
            return self::unavailable_result( $base_meta, $reason );
        }

        $url = add_query_arg( 'elementize_locators', '1', (string) $preview['signed_url'] );
        $capture = self::capture_screenshot( $url, $browser );
        unset( $url, $preview );
        if ( is_wp_error( $capture ) ) return self::unavailable_result( $base_meta, self::safe_error( $capture, 'Aesthetic screenshot capture failed.' ) );

        $analysis = null;
        try {
            $analysis = self::analyze_screenshot( $capture['bytes'], $marker_map, $id );
        } finally {
            @unlink( $capture['path'] );
            unset( $capture['bytes'] );
        }
        if ( is_wp_error( $analysis ) ) return self::unavailable_result( $base_meta, self::safe_error( $analysis, 'Local aesthetic analysis failed.' ) );

        $lookup = [];
        foreach ( $marker_map as $entry ) $lookup[(string) $entry['marker']] = (string) $entry['top_level_element_id'];

        $design_dna = self::sanitize_design_dna( is_array( $analysis['design_dna'] ?? null ) ? $analysis['design_dna'] : [], $base_meta );
        $page_art = self::sanitize_page_art_direction( is_array( $analysis['page_art_direction'] ?? null ) ? $analysis['page_art_direction'] : [], $base_meta, $lookup );
        $coherence = self::sanitize_section_coherence( is_array( $analysis['section_coherence'] ?? null ) ? $analysis['section_coherence'] : [], $base_meta, $lookup );

        $covered = absint( $coherence['covered_section_count'] ?? 0 );
        $total = max( 1, count( $marker_map ) );
        $coverage = min( 1, $covered / $total );
        $assessment_complete = count( $marker_map ) <= 3 ? $covered >= count( $marker_map ) : $coverage >= 0.60;
        $page_art['assessment_complete'] = $assessment_complete;
        $page_art['section_coverage_ratio'] = round( $coverage, 3 );
        $page_art['approval_usable'] = $assessment_complete && in_array( (string) ( $page_art['confidence'] ?? '' ), [ 'medium', 'high' ], true );
        $page_art['note'] = 'Professional approval is advisory. It is usable only when section coverage is sufficient and confidence is at least medium; it never grants write authority.';

        $design_dna['assessment_complete'] = $assessment_complete;
        $design_dna['usable_as_page_design_context'] = $assessment_complete && in_array( (string) ( $design_dna['confidence'] ?? '' ), [ 'medium', 'high' ], true );
        $design_dna['note'] = 'This is an inferred design language from the rendered page, not a user-authored brand specification. It is read-only context for future section selection and comparison.';

        $coherence['assessment_complete'] = $assessment_complete;
        $coherence['section_coverage_ratio'] = round( $coverage, 3 );
        $coherence['note'] = 'Section coherence evaluates whether the page feels intentionally composed as one system. It does not authorize a section replacement or any other write.';

        return [ 'design_dna' => $design_dna, 'page_art_direction' => $page_art, 'section_coherence' => $coherence ];
    }

    private static function unavailable_result( array $meta, string $reason ): array {
        $obj = $meta;
        $obj['reason'] = self::clip( $reason, 600 );
        return [
            'design_dna' => $obj + [ 'confidence' => 'low', 'usable_as_page_design_context' => false ],
            'page_art_direction' => $obj + [ 'professional_approval' => 'unavailable', 'confidence' => 'low', 'approval_usable' => false ],
            'section_coherence' => $obj + [ 'coherence_verdict' => 'unavailable', 'covered_section_count' => 0 ],
        ];
    }

    private static function analyze_screenshot( string $bytes, array $marker_map, int $id ) {
        $marker_names = array_map( static fn ( $e ): string => (string) ( $e['marker'] ?? '' ), $marker_map );
        $title = get_the_title( $id );
        $prompt = "You are the senior web-design art director inside Elementize. Evaluate the attached rendered landing-page screenshot as ONE complete visual composition, not as a checklist of isolated CSS defects.\n\n" .
            "Your standard is demanding professional web design: aesthetically pleasing, intentional, coherent, balanced, restrained, polished, and suitable to present to a client. A page can have technically valid sections and still fail if it feels like a collage of unrelated templates.\n\n" .
            "Judge optical balance, focal hierarchy, typography as architecture, spacing rhythm, density contrast between loud and quiet sections, component consistency, imagery art direction, section transitions, visual restraint, conversion emphasis, and whether exceptions feel purposeful rather than accidental. Distinguish a local tweak from a structural/content/imagery/section-choice problem. Do not reward novelty for its own sake. Do not treat uniformity as automatically good.\n\n" .
            "The screenshot contains internal section labels " . implode( ', ', $marker_names ) . ". Use ONLY labels that are visibly present and only from this list. Never invent a marker. Do not guess hidden interactions, analytics, or business facts. Do not recommend exact CSS pixel values. Be concise and evidence-based. 'approve' means you would be comfortable presenting this page as professionally designed without another aesthetic pass.\n\n" .
            "Page title for context only: " . self::clip( (string) $title, 160 ) . "\n\nReturn JSON only and match the supplied JSON schema exactly.";

        $schema = self::schema();
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
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_aesthetic_ollama_failed', 'The local Ollama request failed.' );
        if ( 200 !== (int) wp_remote_retrieve_response_code( $response ) ) return new WP_Error( 'elementize_aesthetic_ollama_http', 'Local Ollama returned a non-200 response.' );
        $outer = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        if ( ! is_array( $outer ) || ! isset( $outer['response'] ) || ! is_string( $outer['response'] ) ) return new WP_Error( 'elementize_aesthetic_ollama_invalid', 'Local Ollama returned an invalid response envelope.' );
        $decoded = json_decode( trim( $outer['response'] ), true );
        if ( ! is_array( $decoded ) || ! is_array( $decoded['design_dna'] ?? null ) || ! is_array( $decoded['page_art_direction'] ?? null ) || ! is_array( $decoded['section_coherence'] ?? null ) ) {
            return new WP_Error( 'elementize_aesthetic_schema_incomplete', 'The local aesthetic response did not satisfy the required top-level contract.' );
        }
        return $decoded;
    }

    private static function schema(): array {
        $string_array = [ 'type' => 'array', 'items' => [ 'type' => 'string' ], 'maxItems' => 6 ];
        $marker_array = [ 'type' => 'array', 'items' => [ 'type' => 'string' ], 'maxItems' => 4 ];
        $score = [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ];
        return [
            'type' => 'object',
            'additionalProperties' => false,
            'required' => [ 'design_dna', 'page_art_direction', 'section_coherence' ],
            'properties' => [
                'design_dna' => [
                    'type' => 'object', 'additionalProperties' => false,
                    'required' => [ 'aesthetic_direction', 'visual_density', 'whitespace_character', 'typography_character', 'geometry_language', 'color_behavior', 'imagery_language', 'layout_language', 'component_language', 'restraint_level', 'design_system_coherence', 'evidence', 'avoid', 'confidence' ],
                    'properties' => [
                        'aesthetic_direction' => $string_array,
                        'visual_density' => [ 'type' => 'string', 'enum' => [ 'low', 'medium-low', 'medium', 'medium-high', 'high' ] ],
                        'whitespace_character' => [ 'type' => 'string' ],
                        'typography_character' => [ 'type' => 'string' ],
                        'geometry_language' => [ 'type' => 'string' ],
                        'color_behavior' => [ 'type' => 'string' ],
                        'imagery_language' => [ 'type' => 'string' ],
                        'layout_language' => [ 'type' => 'string' ],
                        'component_language' => [ 'type' => 'string' ],
                        'restraint_level' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                        'design_system_coherence' => [ 'type' => 'string', 'enum' => [ 'weak', 'mixed', 'strong' ] ],
                        'evidence' => $string_array,
                        'avoid' => $string_array,
                        'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                    ],
                ],
                'page_art_direction' => [
                    'type' => 'object', 'additionalProperties' => false,
                    'required' => [ 'professional_approval', 'aesthetic_score', 'coherence_score', 'hierarchy_score', 'rhythm_score', 'polish_score', 'restraint_score', 'conversion_focus_score', 'template_collage_risk', 'first_impression', 'attention_path', 'top_strengths', 'top_issues', 'professional_rationale', 'confidence' ],
                    'properties' => [
                        'professional_approval' => [ 'type' => 'string', 'enum' => [ 'approve', 'revise', 'reject' ] ],
                        'aesthetic_score' => $score,
                        'coherence_score' => $score,
                        'hierarchy_score' => $score,
                        'rhythm_score' => $score,
                        'polish_score' => $score,
                        'restraint_score' => $score,
                        'conversion_focus_score' => $score,
                        'template_collage_risk' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                        'first_impression' => [ 'type' => 'string' ],
                        'attention_path' => $string_array,
                        'top_strengths' => $string_array,
                        'top_issues' => [
                            'type' => 'array', 'maxItems' => 4,
                            'items' => [
                                'type' => 'object', 'additionalProperties' => false,
                                'required' => [ 'category', 'severity', 'section_markers', 'evidence', 'recommendation', 'intervention_level' ],
                                'properties' => [
                                    'category' => [ 'type' => 'string' ],
                                    'severity' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                                    'section_markers' => $marker_array,
                                    'evidence' => [ 'type' => 'string' ],
                                    'recommendation' => [ 'type' => 'string' ],
                                    'intervention_level' => [ 'type' => 'string', 'enum' => [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ] ],
                                ],
                            ],
                        ],
                        'professional_rationale' => [ 'type' => 'string' ],
                        'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                    ],
                ],
                'section_coherence' => [
                    'type' => 'object', 'additionalProperties' => false,
                    'required' => [ 'coherence_verdict', 'rhythm_quality', 'rhythm_sequence', 'weakest_transitions', 'violating_sections', 'component_consistency', 'overall_rationale', 'confidence' ],
                    'properties' => [
                        'coherence_verdict' => [ 'type' => 'string', 'enum' => [ 'weak', 'mixed', 'strong' ] ],
                        'rhythm_quality' => [ 'type' => 'string', 'enum' => [ 'flat', 'erratic', 'acceptable', 'strong' ] ],
                        'rhythm_sequence' => [
                            'type' => 'array', 'maxItems' => self::MAX_MARKERS,
                            'items' => [
                                'type' => 'object', 'additionalProperties' => false,
                                'required' => [ 'marker', 'visual_intensity', 'role', 'transition_from_previous' ],
                                'properties' => [
                                    'marker' => [ 'type' => 'string' ],
                                    'visual_intensity' => $score,
                                    'role' => [ 'type' => 'string' ],
                                    'transition_from_previous' => [ 'type' => 'string', 'enum' => [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ] ],
                                ],
                            ],
                        ],
                        'weakest_transitions' => [
                            'type' => 'array', 'maxItems' => 3,
                            'items' => [
                                'type' => 'object', 'additionalProperties' => false,
                                'required' => [ 'from_marker', 'to_marker', 'issue', 'evidence' ],
                                'properties' => [
                                    'from_marker' => [ 'type' => 'string' ], 'to_marker' => [ 'type' => 'string' ],
                                    'issue' => [ 'type' => 'string' ], 'evidence' => [ 'type' => 'string' ],
                                ],
                            ],
                        ],
                        'violating_sections' => [
                            'type' => 'array', 'maxItems' => 5,
                            'items' => [
                                'type' => 'object', 'additionalProperties' => false,
                                'required' => [ 'marker', 'mismatch_type', 'reason' ],
                                'properties' => [
                                    'marker' => [ 'type' => 'string' ],
                                    'mismatch_type' => [ 'type' => 'string', 'enum' => [ 'typography', 'spacing', 'geometry', 'color', 'imagery', 'component_style', 'density', 'composition', 'section_choice', 'other' ] ],
                                    'reason' => [ 'type' => 'string' ],
                                ],
                            ],
                        ],
                        'component_consistency' => [
                            'type' => 'object', 'additionalProperties' => false,
                            'required' => [ 'cta_language', 'radius_geometry', 'typography', 'imagery', 'card_treatment', 'content_widths' ],
                            'properties' => [
                                'cta_language' => [ 'type' => 'string', 'enum' => [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ] ],
                                'radius_geometry' => [ 'type' => 'string', 'enum' => [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ] ],
                                'typography' => [ 'type' => 'string', 'enum' => [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ] ],
                                'imagery' => [ 'type' => 'string', 'enum' => [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ] ],
                                'card_treatment' => [ 'type' => 'string', 'enum' => [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ] ],
                                'content_widths' => [ 'type' => 'string', 'enum' => [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ] ],
                            ],
                        ],
                        'overall_rationale' => [ 'type' => 'string' ],
                        'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                    ],
                ],
            ],
        ];
    }

    private static function sanitize_design_dna( array $raw, array $meta ): array {
        return $meta + [
            'available' => true,
            'aesthetic_direction' => self::string_list( $raw['aesthetic_direction'] ?? [], 5, 80 ),
            'visual_density' => self::enum( $raw['visual_density'] ?? '', [ 'low', 'medium-low', 'medium', 'medium-high', 'high' ], 'medium' ),
            'whitespace_character' => self::clip( (string) ( $raw['whitespace_character'] ?? '' ), 350 ),
            'typography_character' => self::clip( (string) ( $raw['typography_character'] ?? '' ), 350 ),
            'geometry_language' => self::clip( (string) ( $raw['geometry_language'] ?? '' ), 350 ),
            'color_behavior' => self::clip( (string) ( $raw['color_behavior'] ?? '' ), 350 ),
            'imagery_language' => self::clip( (string) ( $raw['imagery_language'] ?? '' ), 350 ),
            'layout_language' => self::clip( (string) ( $raw['layout_language'] ?? '' ), 350 ),
            'component_language' => self::clip( (string) ( $raw['component_language'] ?? '' ), 350 ),
            'restraint_level' => self::enum( $raw['restraint_level'] ?? '', [ 'low', 'medium', 'high' ], 'medium' ),
            'design_system_coherence' => self::enum( $raw['design_system_coherence'] ?? '', [ 'weak', 'mixed', 'strong' ], 'mixed' ),
            'evidence' => self::string_list( $raw['evidence'] ?? [], 4, 300 ),
            'avoid' => self::string_list( $raw['avoid'] ?? [], 4, 220 ),
            'confidence' => self::enum( $raw['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' ),
        ];
    }

    private static function sanitize_page_art_direction( array $raw, array $meta, array $lookup ): array {
        $issues = [];
        foreach ( is_array( $raw['top_issues'] ?? null ) ? array_slice( $raw['top_issues'], 0, 4 ) : [] as $issue ) {
            if ( ! is_array( $issue ) ) continue;
            $markers = self::marker_list( $issue['section_markers'] ?? [], $lookup, 4 );
            $ids = [];
            foreach ( $markers as $marker ) $ids[] = $lookup[$marker];
            $issues[] = [
                'category' => self::clip( (string) ( $issue['category'] ?? 'aesthetic' ), 80 ),
                'severity' => self::enum( $issue['severity'] ?? '', [ 'low', 'medium', 'high' ], 'low' ),
                'section_markers' => $markers,
                'top_level_element_ids' => $ids,
                'evidence' => self::clip( (string) ( $issue['evidence'] ?? '' ), 500 ),
                'recommendation' => self::clip( (string) ( $issue['recommendation'] ?? '' ), 500 ),
                'intervention_level' => self::enum( $issue['intervention_level'] ?? '', [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ], 'none' ),
                'automatic_write_allowed' => false,
            ];
        }
        return $meta + [
            'available' => true,
            'professional_approval' => self::enum( $raw['professional_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' ),
            'aesthetic_score' => self::score( $raw['aesthetic_score'] ?? 1 ),
            'coherence_score' => self::score( $raw['coherence_score'] ?? 1 ),
            'hierarchy_score' => self::score( $raw['hierarchy_score'] ?? 1 ),
            'rhythm_score' => self::score( $raw['rhythm_score'] ?? 1 ),
            'polish_score' => self::score( $raw['polish_score'] ?? 1 ),
            'restraint_score' => self::score( $raw['restraint_score'] ?? 1 ),
            'conversion_focus_score' => self::score( $raw['conversion_focus_score'] ?? 1 ),
            'template_collage_risk' => self::enum( $raw['template_collage_risk'] ?? '', [ 'low', 'medium', 'high' ], 'medium' ),
            'first_impression' => self::clip( (string) ( $raw['first_impression'] ?? '' ), 600 ),
            'attention_path' => self::string_list( $raw['attention_path'] ?? [], 5, 180 ),
            'top_strengths' => self::string_list( $raw['top_strengths'] ?? [], 3, 300 ),
            'top_issues' => $issues,
            'professional_rationale' => self::clip( (string) ( $raw['professional_rationale'] ?? '' ), 800 ),
            'confidence' => self::enum( $raw['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' ),
        ];
    }

    private static function sanitize_section_coherence( array $raw, array $meta, array $lookup ): array {
        $rhythm = [];
        $seen = [];
        foreach ( is_array( $raw['rhythm_sequence'] ?? null ) ? $raw['rhythm_sequence'] : [] as $item ) {
            if ( ! is_array( $item ) ) continue;
            $marker = strtoupper( trim( (string) ( $item['marker'] ?? '' ) ) );
            if ( ! isset( $lookup[$marker] ) || isset( $seen[$marker] ) ) continue;
            $seen[$marker] = true;
            $rhythm[] = [
                'marker' => $marker,
                'top_level_element_id' => $lookup[$marker],
                'visual_intensity' => self::score( $item['visual_intensity'] ?? 1 ),
                'role' => self::clip( (string) ( $item['role'] ?? '' ), 120 ),
                'transition_from_previous' => self::enum( $item['transition_from_previous'] ?? '', [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ], 'neutral' ),
            ];
        }

        $transitions = [];
        foreach ( is_array( $raw['weakest_transitions'] ?? null ) ? array_slice( $raw['weakest_transitions'], 0, 3 ) : [] as $item ) {
            if ( ! is_array( $item ) ) continue;
            $from = strtoupper( trim( (string) ( $item['from_marker'] ?? '' ) ) );
            $to = strtoupper( trim( (string) ( $item['to_marker'] ?? '' ) ) );
            if ( ! isset( $lookup[$from], $lookup[$to] ) ) continue;
            $transitions[] = [
                'from_marker' => $from, 'from_top_level_element_id' => $lookup[$from],
                'to_marker' => $to, 'to_top_level_element_id' => $lookup[$to],
                'issue' => self::clip( (string) ( $item['issue'] ?? '' ), 300 ),
                'evidence' => self::clip( (string) ( $item['evidence'] ?? '' ), 450 ),
            ];
        }

        $violations = [];
        foreach ( is_array( $raw['violating_sections'] ?? null ) ? array_slice( $raw['violating_sections'], 0, 5 ) : [] as $item ) {
            if ( ! is_array( $item ) ) continue;
            $marker = strtoupper( trim( (string) ( $item['marker'] ?? '' ) ) );
            if ( ! isset( $lookup[$marker] ) ) continue;
            $violations[] = [
                'marker' => $marker,
                'top_level_element_id' => $lookup[$marker],
                'mismatch_type' => self::enum( $item['mismatch_type'] ?? '', [ 'typography', 'spacing', 'geometry', 'color', 'imagery', 'component_style', 'density', 'composition', 'section_choice', 'other' ], 'other' ),
                'reason' => self::clip( (string) ( $item['reason'] ?? '' ), 450 ),
                'automatic_write_allowed' => false,
            ];
        }

        $consistency_raw = is_array( $raw['component_consistency'] ?? null ) ? $raw['component_consistency'] : [];
        $consistency = [];
        foreach ( [ 'cta_language', 'radius_geometry', 'typography', 'imagery', 'card_treatment', 'content_widths' ] as $key ) {
            $consistency[$key] = self::enum( $consistency_raw[$key] ?? '', [ 'consistent', 'mixed', 'inconsistent', 'not_visible' ], 'not_visible' );
        }

        return $meta + [
            'available' => true,
            'coherence_verdict' => self::enum( $raw['coherence_verdict'] ?? '', [ 'weak', 'mixed', 'strong' ], 'mixed' ),
            'rhythm_quality' => self::enum( $raw['rhythm_quality'] ?? '', [ 'flat', 'erratic', 'acceptable', 'strong' ], 'acceptable' ),
            'rhythm_sequence' => $rhythm,
            'covered_section_count' => count( $rhythm ),
            'weakest_transitions' => $transitions,
            'violating_sections' => $violations,
            'component_consistency' => $consistency,
            'overall_rationale' => self::clip( (string) ( $raw['overall_rationale'] ?? '' ), 800 ),
            'confidence' => self::enum( $raw['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' ),
        ];
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
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_aesthetic_preview_failed', 'Fresh page state could not be read.' );
        $data = $response->get_data();
        return is_array( $data ) && is_array( $data['visual_preview'] ?? null ) ? $data['visual_preview'] : new WP_Error( 'elementize_aesthetic_preview_missing', 'Fresh page state did not provide a signed visual preview.' );
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
        return trailingslashit( $dir ) . 'elementize-aesthetic-' . wp_generate_uuid4() . $suffix;
    }

    private static function temp_profile_dir(): string {
        $dir = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() ) . 'elementize-aesthetic-profile-' . wp_generate_uuid4();
        return wp_mkdir_p( $dir ) && is_dir( $dir ) ? $dir : '';
    }

    private static function capture_screenshot( string $url, string $browser ) {
        if ( ! function_exists( 'proc_open' ) || '' === $browser || ! is_file( $browser ) ) return new WP_Error( 'elementize_aesthetic_browser_unavailable', 'Local Chrome/Chromium/Edge capture is unavailable.' );
        $path = self::temp_path( '.png' );
        $stdout = self::temp_path( '.stdout.log' );
        $stderr = self::temp_path( '.stderr.log' );
        $profile = self::temp_profile_dir();
        if ( '' === $profile ) return new WP_Error( 'elementize_aesthetic_profile_failed', 'Could not create an isolated browser profile.' );
        $command = [
            $browser, '--headless', '--user-data-dir=' . $profile, '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars', '--mute-audio',
            '--timeout=' . self::PAGE_TIMEOUT_MS, '--window-size=' . self::VIEWPORT_WIDTH . ',' . self::CAPTURE_HEIGHT, '--screenshot=' . $path, $url,
        ];
        $spec = [ 0 => [ 'file', DIRECTORY_SEPARATOR === '\\' ? 'NUL' : '/dev/null', 'r' ], 1 => [ 'file', $stdout, 'a' ], 2 => [ 'file', $stderr, 'a' ] ];
        $pipes = [];
        $proc = @proc_open( $command, $spec, $pipes, null, null, [ 'bypass_shell' => true ] );
        if ( ! is_resource( $proc ) ) {
            @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile );
            return new WP_Error( 'elementize_aesthetic_browser_start_failed', 'Could not start the local headless browser.' );
        }
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
        if ( ! $captured ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_no_screenshot', 'Chrome did not create the aesthetic screenshot.' ); }
        $bytes = @file_get_contents( $path );
        if ( false === $bytes || '' === $bytes ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_read_failed', 'The aesthetic screenshot could not be read.' ); }
        if ( strlen( $bytes ) > self::MAX_SCREENSHOT_BYTES ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_too_large', 'The aesthetic screenshot exceeded the 10 MB safety limit.' ); }
        $size = @getimagesize( $path );
        return [
            'path' => $path, 'bytes' => $bytes, 'byte_count' => strlen( $bytes ), 'sha256' => hash( 'sha256', $bytes ),
            'width' => is_array( $size ) ? absint( $size[0] ?? 0 ) : 0,
            'height' => is_array( $size ) ? absint( $size[1] ?? 0 ) : 0,
            'wait_seconds' => $wait,
        ];
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

    private static function marker_list( $items, array $lookup, int $max ): array {
        $out = [];
        foreach ( is_array( $items ) ? $items : [] as $item ) {
            $marker = strtoupper( trim( (string) $item ) );
            if ( isset( $lookup[$marker] ) && ! in_array( $marker, $out, true ) ) $out[] = $marker;
            if ( count( $out ) >= $max ) break;
        }
        return $out;
    }

    private static function string_list( $items, int $max, int $length ): array {
        $out = [];
        foreach ( is_array( $items ) ? $items : [] as $item ) {
            $value = self::clip( trim( (string) $item ), $length );
            if ( '' !== $value && ! in_array( $value, $out, true ) ) $out[] = $value;
            if ( count( $out ) >= $max ) break;
        }
        return $out;
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
        $data['aesthetic_brain_v1'] = true;
        $data['aesthetic_brain_version'] = self::VERSION;
        $data['aesthetic_brain_read_only'] = true;
        $data['aesthetic_brain_automatic_writes'] = false;
        $data['aesthetic_brain_local_only'] = true;
        $data['aesthetic_brain_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

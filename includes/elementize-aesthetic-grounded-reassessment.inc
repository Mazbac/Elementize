<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_Semantic_Grounding', false ) ) return;

/**
 * Fact-conditioned recovery for aesthetic judgments that failed semantic grounding.
 *
 * The first visual passes remain useful for taste, but small local vision models can
 * invent CTA labels, section roles, or transitions. This pass is deliberately
 * expensive and runs only when semantic grounding or the transition contract failed.
 * It supplies exact Elementor section facts to the local model, re-renders the page,
 * and accepts the reassessment only when every section is covered in order, later
 * sections actually judge their transition, and no material semantic claim conflicts
 * with the supplied facts. It never writes page state.
 */
final class Elementize_Aesthetic_Grounded_Reassessment {
    private const VERSION = '0.26.4-r2';
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
    private const MAX_TEXT_SAMPLES = 12;
    private const VISION_TIMEOUT_SECONDS = 150;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 560, 3 );
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
        $grounding = is_array( $visual['aesthetic_semantic_grounding'] ?? null ) ? $visual['aesthetic_semantic_grounding'] : [];
        if ( ! $grounding ) return $response;

        $needed = empty( $grounding['approval_grounding_complete'] ) || empty( $grounding['transition_contract_valid'] );
        if ( ! $needed ) {
            $visual['aesthetic_grounded_reassessment'] = [
                'version' => self::VERSION,
                'available' => true,
                'needed' => false,
                'read_only' => true,
                'writes_performed' => false,
                'automatic_write_allowed' => false,
                'reason' => 'The existing aesthetic judgment is already semantically grounded and transition-valid.',
            ];
            $data['visual'] = $visual;
            $response->set_data( $data );
            return $response;
        }

        $id = absint( $m[1] ?? 0 );
        $facts = self::section_facts( $id );
        $diag = [
            'version' => self::VERSION,
            'available' => false,
            'needed' => true,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'provider' => 'local_ollama',
            'model' => self::model(),
            'semantic_truth_source' => 'exact_elementor_section_facts_plus_fresh_locator_render',
            'section_fact_count' => count( $facts ),
            'exact_section_coverage_complete' => false,
            'transition_contract_valid' => false,
            'semantic_conflict_count' => 0,
            'assessment_complete' => false,
            'screenshot_persisted' => false,
            'screenshot_exposed' => false,
            'signed_preview_url_exposed' => false,
            'action_slot_cost' => 0,
        ];

        if ( $id < 1 || ! $facts ) {
            $diag['reason'] = 'Exact Elementor section facts are unavailable.';
            return self::finish( $response, $data, $visual, $diag );
        }
        $browser = self::browser_path();
        if ( '' === $browser || ! function_exists( 'proc_open' ) || ! self::ollama_ready() ) {
            $diag['reason'] = 'Local Chrome/Chromium/Edge and Ollama are required for grounded aesthetic reassessment.';
            return self::finish( $response, $data, $visual, $diag );
        }

        $preview = self::fresh_signed_preview( $id );
        if ( is_wp_error( $preview ) || empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) {
            $diag['reason'] = is_wp_error( $preview ) ? self::safe_error( $preview, 'Fresh signed preview unavailable.' ) : 'Fresh signed preview unavailable.';
            return self::finish( $response, $data, $visual, $diag );
        }
        $capture = self::capture_screenshot( add_query_arg( 'elementize_locators', '1', (string) $preview['signed_url'] ), $browser );
        unset( $preview );
        if ( is_wp_error( $capture ) ) {
            $diag['reason'] = self::safe_error( $capture, 'Grounded reassessment capture failed.' );
            return self::finish( $response, $data, $visual, $diag );
        }

        try {
            $result = self::analyze( $capture['bytes'], $facts, $id );
        } finally {
            @unlink( $capture['path'] );
            unset( $capture['bytes'] );
        }
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = self::safe_error( $result, 'Grounded local aesthetic reassessment was incomplete.' );
            return self::finish( $response, $data, $visual, $diag );
        }

        $expected = array_keys( $facts );
        $rows = [];
        $issues = [];
        $semantic_conflicts = [];
        $transition_valid = true;
        foreach ( $result['sections'] as $index => $row ) {
            if ( ! is_array( $row ) || ! isset( $expected[$index] ) ) continue;
            $marker = strtoupper( trim( (string) ( $row['marker'] ?? '' ) ) );
            if ( $marker !== $expected[$index] || ! isset( $facts[$marker] ) ) continue;
            $transition = self::enum( $row['transition_from_previous'] ?? '', [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ], 'neutral' );
            $transition_ok = 0 === $index ? 'n/a' === $transition : 'n/a' !== $transition;
            if ( ! $transition_ok ) $transition_valid = false;

            $conflicts = self::semantic_conflicts( $row, $facts[$marker] );
            foreach ( $conflicts as $conflict ) $semantic_conflicts[] = [ 'marker' => $marker, 'conflict' => $conflict ];
            $severity = self::enum( $row['severity'] ?? '', [ 'none', 'low', 'medium', 'high' ], 'none' );
            $category = self::enum( $row['issue_category'] ?? '', [ 'none', 'hierarchy', 'spacing', 'density', 'typography', 'imagery', 'component_style', 'composition', 'transition', 'conversion', 'section_choice', 'other' ], 'other' );
            $intervention = self::enum( $row['intervention_level'] ?? '', [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ], 'none' );

            $rows[] = [
                'marker' => $marker,
                'top_level_element_id' => $facts[$marker]['top_level_element_id'],
                'visual_intensity' => self::score( $row['visual_intensity'] ?? 1 ),
                'transition_from_previous' => $transition,
                'transition_contract_valid' => $transition_ok,
                'coherence_fit' => self::enum( $row['coherence_fit'] ?? '', [ 'fits', 'mixed', 'violates' ], 'mixed' ),
                'issue_category' => $category,
                'severity' => $severity,
                'evidence' => self::clip( (string) ( $row['evidence'] ?? '' ), 500 ),
                'recommendation' => self::clip( (string) ( $row['recommendation'] ?? '' ), 500 ),
                'intervention_level' => $intervention,
                'semantic_grounded' => empty( $conflicts ),
                'semantic_conflicts' => $conflicts,
                'section_fact_summary' => self::public_fact_summary( $facts[$marker] ),
                'automatic_write_allowed' => false,
            ];
            if ( in_array( $severity, [ 'medium', 'high' ], true ) && 'none' !== $category ) {
                $issues[] = [
                    'category' => $category,
                    'severity' => $severity,
                    'section_markers' => [ $marker ],
                    'top_level_element_ids' => [ $facts[$marker]['top_level_element_id'] ],
                    'evidence' => self::clip( (string) ( $row['evidence'] ?? '' ), 500 ),
                    'recommendation' => self::clip( (string) ( $row['recommendation'] ?? '' ), 500 ),
                    'intervention_level' => $intervention,
                    'semantic_grounded' => empty( $conflicts ),
                    'evidence_usable' => empty( $conflicts ),
                    'automatic_write_allowed' => false,
                ];
            }
        }

        $coverage_complete = count( $rows ) === count( $facts );
        $diag['exact_section_coverage_complete'] = $coverage_complete;
        $diag['covered_section_count'] = count( $rows );
        $diag['transition_contract_valid'] = $coverage_complete && $transition_valid;
        $diag['semantic_conflict_count'] = count( $semantic_conflicts );
        $diag['semantic_conflicts'] = array_slice( $semantic_conflicts, 0, 12 );
        $diag['overall_approval'] = self::enum( $result['overall_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' );
        $diag['confidence'] = self::enum( $result['confidence'] ?? '', [ 'low', 'medium', 'high' ], 'low' );
        $diag['overall_rationale'] = self::clip( (string) ( $result['overall_rationale'] ?? '' ), 900 );
        $diag['section_ledger'] = $rows;
        $diag['issues'] = array_slice( $issues, 0, 8 );
        $diag['assessment_complete'] = $coverage_complete && $transition_valid && empty( $semantic_conflicts );
        $diag['available'] = $diag['assessment_complete'];
        if ( ! $diag['assessment_complete'] ) {
            $diag['reason'] = 'The fact-conditioned reassessment did not satisfy exact coverage, transition, and semantic-grounding contracts.';
            return self::finish( $response, $data, $visual, $diag );
        }

        $art = is_array( $visual['page_art_direction'] ?? null ) ? $visual['page_art_direction'] : [];
        $coherence = is_array( $visual['section_coherence'] ?? null ) ? $visual['section_coherence'] : [];
        $prior = self::enum( $art['effective_professional_approval'] ?? '', [ 'approve', 'revise', 'reject' ], 'revise' );
        $grounded_approval = self::more_conservative_approval( $prior, $diag['overall_approval'] );

        $art['grounded_reassessment_version'] = self::VERSION;
        $art['grounded_reassessment_approval'] = $diag['overall_approval'];
        $art['grounded_effective_professional_approval'] = $grounded_approval;
        $art['approval_grounding_complete'] = true;
        $art['approval_usable'] = in_array( $diag['confidence'], [ 'medium', 'high' ], true );
        $art['grounded_issues'] = array_values( array_filter( $issues, static fn ( array $issue ): bool => ! empty( $issue['evidence_usable'] ) ) );
        $art['grounded_issue_count'] = count( $art['grounded_issues'] );
        $art['automatic_write_allowed'] = false;

        $coherence['grounded_reassessment_version'] = self::VERSION;
        $coherence['transition_contract_valid'] = true;
        $coherence['rhythm_judgment_usable'] = true;
        $coherence['rhythm_sequence'] = array_map( static function ( array $row ): array {
            return [
                'marker' => $row['marker'],
                'top_level_element_id' => $row['top_level_element_id'],
                'visual_intensity' => $row['visual_intensity'],
                'transition_from_previous' => $row['transition_from_previous'],
            ];
        }, $rows );
        $coherence['automatic_write_allowed'] = false;

        $visual['page_art_direction'] = $art;
        $visual['section_coherence'] = $coherence;
        $visual['aesthetic_grounded_reassessment'] = $diag;
        $data['visual'] = $visual;
        if ( ! isset( $data['summary'] ) || ! is_array( $data['summary'] ) ) $data['summary'] = [];
        $data['summary']['aesthetic_grounded_reassessment_available'] = true;
        $data['summary']['aesthetic_approval_grounding_complete'] = true;
        $data['summary']['grounded_professional_visual_approval'] = $grounded_approval;
        $data['summary']['professional_visual_approval_usable'] = ! empty( $art['approval_usable'] );
        $data['summary']['aesthetic_transition_contract_valid'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function finish( WP_REST_Response $response, array $data, array $visual, array $diag ): WP_REST_Response {
        $visual['aesthetic_grounded_reassessment'] = $diag;
        $data['visual'] = $visual;
        $response->set_data( $data );
        return $response;
    }

    private static function analyze( string $bytes, array $facts, int $id ) {
        $markers = array_keys( $facts );
        $fact_lines = [];
        foreach ( $facts as $marker => $fact ) {
            $texts = array_values( array_filter( array_map( static fn ( $v ): string => self::clip( (string) $v, 140 ), $fact['text_samples'] ?? [] ) ) );
            $fact_lines[] = $marker . ' FACTS: widgets=[' . implode( ',', $fact['widget_types'] ) . ']; has_button=' . ( $fact['has_button'] ? 'yes' : 'no' ) . '; has_form=' . ( $fact['has_form'] ? 'yes' : 'no' ) . '; has_image=' . ( $fact['has_image'] ? 'yes' : 'no' ) . '; exact_text=[' . implode( ' | ', $texts ) . ']';
        }
        $prompt = "You are a demanding senior SaaS web designer performing a FACT-CONDITIONED aesthetic reassessment of a locator-annotated landing page. The screenshot is the source of truth for appearance. The exact Elementor facts below are the source of truth for semantics. NEVER invent CTA labels, headings, forms, testimonials, stats, imagery, or section roles that contradict these facts. Do not quote visible text unless it appears verbatim in exact_text for that marker.\n\n" .
            "Return EXACTLY one row per marker in this exact order: " . implode( ', ', $markers ) . ". S1 transition_from_previous MUST be n/a. Every later section MUST choose smooth, neutral, abrupt, or repetitive by visually comparing it with the previous section; n/a is forbidden after S1. Judge optical balance, hierarchy, typography, whitespace rhythm, density, imagery quality, component consistency, transition quality, conversion emphasis, and whether the page feels intentionally art-directed rather than assembled from templates. Approve only if it is client-presentable without another aesthetic pass. Prefer the least-destructive intervention that could plausibly solve the visible issue. Return JSON only.\n\nEXACT SECTION FACTS:\n" . implode( "\n", $fact_lines ) . "\n\nPage title: " . self::clip( (string) get_the_title( $id ), 160 );

        $item = [
            'type' => 'object', 'additionalProperties' => false,
            'required' => [ 'marker', 'visual_intensity', 'transition_from_previous', 'coherence_fit', 'issue_category', 'severity', 'evidence', 'recommendation', 'intervention_level' ],
            'properties' => [
                'marker' => [ 'type' => 'string' ],
                'visual_intensity' => [ 'type' => 'integer', 'minimum' => 1, 'maximum' => 10 ],
                'transition_from_previous' => [ 'type' => 'string', 'enum' => [ 'n/a', 'smooth', 'neutral', 'abrupt', 'repetitive' ] ],
                'coherence_fit' => [ 'type' => 'string', 'enum' => [ 'fits', 'mixed', 'violates' ] ],
                'issue_category' => [ 'type' => 'string', 'enum' => [ 'none', 'hierarchy', 'spacing', 'density', 'typography', 'imagery', 'component_style', 'composition', 'transition', 'conversion', 'section_choice', 'other' ] ],
                'severity' => [ 'type' => 'string', 'enum' => [ 'none', 'low', 'medium', 'high' ] ],
                'evidence' => [ 'type' => 'string' ],
                'recommendation' => [ 'type' => 'string' ],
                'intervention_level' => [ 'type' => 'string', 'enum' => [ 'tweak', 'content', 'imagery', 'restructure', 'replace_section', 'none' ] ],
            ],
        ];
        $count = count( $markers );
        $schema = [
            'type' => 'object', 'additionalProperties' => false,
            'required' => [ 'sections', 'overall_approval', 'confidence', 'overall_rationale' ],
            'properties' => [
                'sections' => [ 'type' => 'array', 'minItems' => $count, 'maxItems' => $count, 'items' => $item ],
                'overall_approval' => [ 'type' => 'string', 'enum' => [ 'approve', 'revise', 'reject' ] ],
                'confidence' => [ 'type' => 'string', 'enum' => [ 'low', 'medium', 'high' ] ],
                'overall_rationale' => [ 'type' => 'string' ],
            ],
        ];
        $response = wp_remote_post( self::ollama_url() . '/api/generate', [
            'timeout' => self::VISION_TIMEOUT_SECONDS, 'redirection' => 0, 'sslverify' => false,
            'headers' => [ 'Content-Type' => 'application/json', 'Accept' => 'application/json' ],
            'body' => wp_json_encode( [ 'model' => self::model(), 'prompt' => $prompt, 'images' => [ base64_encode( $bytes ) ], 'stream' => false, 'format' => $schema, 'options' => [ 'temperature' => 0 ] ], JSON_UNESCAPED_SLASHES ),
            'limit_response_size' => 4194304,
        ] );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_aesthetic_grounded_ollama_failed', 'The local grounded reassessment request failed.' );
        if ( 200 !== (int) wp_remote_retrieve_response_code( $response ) ) return new WP_Error( 'elementize_aesthetic_grounded_ollama_http', 'Local Ollama returned a non-200 grounded reassessment response.' );
        $outer = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_aesthetic_grounded_envelope_invalid', 'The local grounded reassessment envelope was invalid.' );
        $decoded = json_decode( trim( (string) $outer['response'] ), true );
        if ( ! is_array( $decoded ) || ! is_array( $decoded['sections'] ?? null ) || count( $decoded['sections'] ) !== $count ) return new WP_Error( 'elementize_aesthetic_grounded_schema_incomplete', 'The local model did not satisfy the fact-conditioned section contract.' );
        return $decoded;
    }

    private static function semantic_conflicts( array $row, array $fact ): array {
        $text = strtolower( (string) ( $row['evidence'] ?? '' ) . ' ' . (string) ( $row['recommendation'] ?? '' ) );
        $conflicts = [];
        if ( ! $fact['has_button'] && preg_match( '/\b(button|cta|call-to-action|call to action)\b/i', $text ) ) $conflicts[] = 'Model referenced a button/CTA but the exact section has no button widget.';
        if ( ! $fact['has_form'] && preg_match( '/\b(form|field|fields|input|submit)\b/i', $text ) ) $conflicts[] = 'Model referenced a form or form fields but the exact section has no form widget.';
        foreach ( self::quoted_phrases( (string) ( $row['evidence'] ?? '' ) . ' ' . (string) ( $row['recommendation'] ?? '' ) ) as $quote ) {
            if ( strlen( $quote ) < 4 ) continue;
            if ( ! self::quote_present( $quote, $fact['text_samples'] ?? [] ) ) $conflicts[] = 'Quoted visible text is not present in the exact Elementor section: "' . self::clip( $quote, 120 ) . '".';
        }
        return array_values( array_unique( $conflicts ) );
    }

    private static function quoted_phrases( string $text ): array {
        $out = [];
        if ( preg_match_all( '/["“”\'‘’]([^"“”\'‘’]{3,120})["“”\'‘’]/u', $text, $m ) ) $out = $m[1];
        return $out;
    }

    private static function quote_present( string $quote, array $samples ): bool {
        $needle = strtolower( trim( wp_strip_all_tags( $quote ) ) );
        foreach ( $samples as $sample ) if ( false !== strpos( strtolower( (string) $sample ), $needle ) ) return true;
        return false;
    }

    private static function section_facts( int $id ): array {
        $elements = null;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try { $doc = \Elementor\Plugin::$instance->documents->get( $id ); $elements = $doc ? $doc->get_elements_data() : null; } catch ( Throwable $ignored ) {}
        }
        if ( ! is_array( $elements ) ) {
            $raw = get_post_meta( $id, '_elementor_data', true );
            $elements = is_string( $raw ) ? json_decode( $raw, true ) : null;
        }
        if ( ! is_array( $elements ) ) return [];
        $facts = [];
        $n = 0;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $eid = trim( (string) ( $element['id'] ?? '' ) );
            if ( '' === $eid || ! preg_match( '/^[A-Za-z0-9_-]+$/', $eid ) ) continue;
            $n++; if ( $n > self::MAX_MARKERS ) break;
            $acc = [ 'widget_types' => [], 'text_samples' => [], 'has_form' => false, 'has_button' => false, 'has_image' => false ];
            self::walk_element( $element, $acc );
            $facts['S' . $n] = [
                'top_level_element_id' => $eid,
                'widget_types' => array_values( array_unique( $acc['widget_types'] ) ),
                'text_samples' => array_slice( array_values( array_unique( array_filter( $acc['text_samples'] ) ) ), 0, self::MAX_TEXT_SAMPLES ),
                'has_form' => $acc['has_form'], 'has_button' => $acc['has_button'], 'has_image' => $acc['has_image'],
            ];
        }
        return $facts;
    }

    private static function walk_element( array $node, array &$acc ): void {
        $type = strtolower( trim( (string) ( $node['widgetType'] ?? $node['elType'] ?? '' ) ) );
        if ( '' !== $type ) $acc['widget_types'][] = $type;
        if ( false !== strpos( $type, 'form' ) ) $acc['has_form'] = true;
        if ( false !== strpos( $type, 'button' ) || false !== strpos( $type, 'popup' ) ) $acc['has_button'] = true;
        if ( false !== strpos( $type, 'img' ) || false !== strpos( $type, 'image' ) || false !== strpos( $type, 'feature' ) || false !== strpos( $type, 'mockup' ) ) $acc['has_image'] = true;
        self::collect_settings( is_array( $node['settings'] ?? null ) ? $node['settings'] : [], $acc );
        foreach ( is_array( $node['elements'] ?? null ) ? $node['elements'] : [] as $child ) if ( is_array( $child ) ) self::walk_element( $child, $acc );
    }

    private static function collect_settings( array $settings, array &$acc ): void {
        foreach ( $settings as $key => $value ) {
            $k = strtolower( (string) $key );
            if ( is_string( $value ) ) {
                $plain = self::clip( $value, 180 );
                if ( '' !== $plain && preg_match( '/(text|title|heading|description|content|label|button|badge|quote|name)/', $k ) ) $acc['text_samples'][] = $plain;
                if ( preg_match( '/(image|img|media|icon|mockup)/', $k ) && '' !== trim( $value ) ) $acc['has_image'] = true;
                if ( preg_match( '/(button|cta)/', $k ) && '' !== trim( $value ) ) $acc['has_button'] = true;
            } elseif ( is_array( $value ) ) {
                if ( preg_match( '/(image|img|media|icon|mockup)/', $k ) && ! empty( $value ) ) $acc['has_image'] = true;
                self::collect_settings( $value, $acc );
            }
        }
    }

    private static function public_fact_summary( array $fact ): array {
        return [
            'top_level_element_id' => (string) $fact['top_level_element_id'],
            'widget_types' => $fact['widget_types'],
            'text_sample_count' => count( $fact['text_samples'] ),
            'has_form' => (bool) $fact['has_form'], 'has_button' => (bool) $fact['has_button'], 'has_image' => (bool) $fact['has_image'],
        ];
    }

    private static function fresh_signed_preview( int $id ) {
        $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/state' );
        $request->set_param( 'id', $id );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_aesthetic_grounded_preview_failed', 'Fresh page state could not be read.' );
        $data = $response->get_data();
        return is_array( $data ) && is_array( $data['visual_preview'] ?? null ) ? $data['visual_preview'] : new WP_Error( 'elementize_aesthetic_grounded_preview_missing', 'Fresh page state did not provide a signed visual preview.' );
    }

    private static function browser_path(): string {
        if ( defined( self::CHROME_PATH_CONST ) ) { $path = trim( (string) constant( self::CHROME_PATH_CONST ) ); if ( '' !== $path && is_file( $path ) ) return $path; }
        $candidates = [];
        foreach ( [ getenv( 'LOCALAPPDATA' ), getenv( 'ProgramFiles' ), getenv( 'ProgramFiles(x86)' ) ] as $base ) {
            if ( ! is_string( $base ) || '' === $base ) continue;
            $candidates[] = $base . '\\Google\\Chrome\\Application\\chrome.exe';
            $candidates[] = $base . '\\Microsoft\\Edge\\Application\\msedge.exe';
        }
        $candidates = array_merge( $candidates, [ '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge', '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/microsoft-edge', '/usr/bin/microsoft-edge-stable' ] );
        foreach ( array_unique( $candidates ) as $candidate ) if ( is_file( $candidate ) ) return $candidate;
        return '';
    }

    private static function ollama_url(): string {
        $url = defined( self::OLLAMA_URL_CONST ) ? trim( (string) constant( self::OLLAMA_URL_CONST ) ) : self::DEFAULT_OLLAMA_URL;
        $url = untrailingslashit( $url );
        return preg_match( '#^https?://(?:127\\.0\\.0\\.1|localhost)(?::\\d+)?$#i', $url ) ? $url : self::DEFAULT_OLLAMA_URL;
    }
    private static function model(): string { $model = defined( self::MODEL_CONST ) ? trim( (string) constant( self::MODEL_CONST ) ) : self::DEFAULT_MODEL; return '' !== $model ? $model : self::DEFAULT_MODEL; }
    private static function ollama_ready(): bool {
        $response = wp_remote_get( self::ollama_url() . '/api/tags', [ 'timeout' => 2, 'redirection' => 0, 'sslverify' => false, 'limit_response_size' => 1048576 ] );
        if ( is_wp_error( $response ) || 200 !== (int) wp_remote_retrieve_response_code( $response ) ) return false;
        $decoded = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        foreach ( is_array( $decoded['models'] ?? null ) ? $decoded['models'] : [] as $entry ) {
            if ( ! is_array( $entry ) ) continue; $name = (string) ( $entry['name'] ?? $entry['model'] ?? '' );
            if ( $name === self::model() || ( 'gemma3:4b' === self::model() && 'gemma3:latest' === $name ) ) return true;
        }
        return false;
    }

    private static function capture_screenshot( string $url, string $browser ) {
        $path = self::temp_path( '.png' ); $stdout = self::temp_path( '.stdout.log' ); $stderr = self::temp_path( '.stderr.log' ); $profile = self::temp_profile_dir();
        if ( '' === $profile ) return new WP_Error( 'elementize_aesthetic_grounded_profile_failed', 'Could not create an isolated browser profile.' );
        $command = [ $browser, '--headless', '--user-data-dir=' . $profile, '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars', '--mute-audio', '--timeout=' . self::PAGE_TIMEOUT_MS, '--window-size=' . self::VIEWPORT_WIDTH . ',' . self::CAPTURE_HEIGHT, '--screenshot=' . $path, $url ];
        $spec = [ 0 => [ 'file', DIRECTORY_SEPARATOR === '\\' ? 'NUL' : '/dev/null', 'r' ], 1 => [ 'file', $stdout, 'a' ], 2 => [ 'file', $stderr, 'a' ] ];
        $pipes = []; $proc = @proc_open( $command, $spec, $pipes, null, null, [ 'bypass_shell' => true ] );
        if ( ! is_resource( $proc ) ) { @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile ); return new WP_Error( 'elementize_aesthetic_grounded_browser_start_failed', 'Could not start the local headless browser.' ); }
        $started = microtime( true ); $last_size = -1; $stable_since = null; $captured = false;
        while ( microtime( true ) - $started <= self::CAPTURE_TIMEOUT_SECONDS ) {
            clearstatcache( true, $path );
            if ( is_file( $path ) ) {
                $size = (int) @filesize( $path );
                if ( $size >= 1000 ) {
                    if ( $size === $last_size ) { if ( null === $stable_since ) $stable_since = microtime( true ); if ( microtime( true ) - $stable_since >= self::SCREENSHOT_STABLE_SECONDS ) { $captured = true; break; } }
                    else { $last_size = $size; $stable_since = microtime( true ); }
                }
            }
            $status = @proc_get_status( $proc ); if ( ! is_array( $status ) || empty( $status['running'] ) ) break; usleep( 100000 );
        }
        self::stop_process( $proc ); @unlink( $stdout ); @unlink( $stderr ); self::remove_tree( $profile ); clearstatcache( true, $path );
        if ( ! $captured && is_file( $path ) && (int) @filesize( $path ) >= 1000 ) $captured = true;
        if ( ! $captured ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_grounded_no_screenshot', 'Chrome did not create the reassessment screenshot.' ); }
        $bytes = @file_get_contents( $path );
        if ( false === $bytes || '' === $bytes ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_grounded_read_failed', 'The reassessment screenshot could not be read.' ); }
        if ( strlen( $bytes ) > self::MAX_SCREENSHOT_BYTES ) { @unlink( $path ); return new WP_Error( 'elementize_aesthetic_grounded_too_large', 'The reassessment screenshot exceeded the 10 MB safety limit.' ); }
        return [ 'path' => $path, 'bytes' => $bytes ];
    }

    private static function temp_path( string $suffix ): string { $dir = function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir(); return trailingslashit( $dir ) . 'elementize-aesthetic-grounded-' . wp_generate_uuid4() . $suffix; }
    private static function temp_profile_dir(): string { $dir = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() ) . 'elementize-aesthetic-grounded-profile-' . wp_generate_uuid4(); return wp_mkdir_p( $dir ) && is_dir( $dir ) ? $dir : ''; }
    private static function stop_process( $proc ): void { if ( ! is_resource( $proc ) ) return; $status = @proc_get_status( $proc ); if ( is_array( $status ) && ! empty( $status['running'] ) ) @proc_terminate( $proc ); @proc_close( $proc ); }
    private static function remove_tree( string $dir ): void { if ( '' === $dir || ! is_dir( $dir ) ) return; $items = @scandir( $dir ); if ( ! is_array( $items ) ) return; foreach ( $items as $item ) { if ( '.' === $item || '..' === $item ) continue; $path = $dir . DIRECTORY_SEPARATOR . $item; if ( is_dir( $path ) ) self::remove_tree( $path ); else @unlink( $path ); } @rmdir( $dir ); }
    private static function safe_error( WP_Error $error, string $fallback ): string { $message = self::clip( $error->get_error_message(), 300 ); return '' !== $message ? $message : $fallback; }
    private static function truthy( $value ): bool { if ( is_bool( $value ) ) return $value; return in_array( strtolower( trim( (string) $value ) ), [ '1', 'true', 'yes', 'on' ], true ); }
    private static function score( $value ): int { return max( 1, min( 10, absint( $value ) ) ); }
    private static function enum( $value, array $allowed, string $fallback ): string { $value = strtolower( trim( (string) $value ) ); return in_array( $value, $allowed, true ) ? $value : $fallback; }
    private static function clip( string $value, int $max ): string { $value = trim( wp_strip_all_tags( $value ) ); if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $max ? mb_substr( $value, 0, $max ) : $value; return strlen( $value ) > $max ? substr( $value, 0, $max ) : $value; }
    private static function more_conservative_approval( string $a, string $b ): string { $rank = [ 'approve' => 0, 'revise' => 1, 'reject' => 2 ]; return ( $rank[$a] ?? 1 ) >= ( $rank[$b] ?? 1 ) ? $a : $b; }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data(); if ( ! is_array( $data ) ) return $response;
        $data['aesthetic_grounded_reassessment'] = true;
        $data['aesthetic_grounded_reassessment_version'] = self::VERSION;
        $data['aesthetic_grounded_reassessment_read_only'] = true;
        $data['aesthetic_grounded_reassessment_fact_conditioned'] = true;
        $data['aesthetic_grounded_reassessment_automatic_writes'] = false;
        $response->set_data( $data ); return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Bounded, read-only page blueprint validation for Designer Agent planning.
 * ChatGPT authors the blueprint; Elementize validates continuity and section identity.
 */
final class Elementize_Blueprint {
    private const VERSION = '4';
    private const MAX_SECTIONS = 24;
    private const COMPOSITION_ROLES = [ 'hero_focus', 'split_media', 'card_grid', 'process_steps', 'proof_reassurance', 'faq', 'cta_band', 'editorial', 'mixed' ];
    private const VISUAL_SCALES = [ 'dominant', 'standard', 'compact' ];
    private const MEDIA_EMPHASIS = [ 'none', 'supporting', 'balanced', 'dominant' ];
    private const DENSITIES = [ 'airy', 'balanced', 'dense' ];
    private const MAX_LIST_ITEMS = 16;
    private const MAX_REFERENCE_OBSERVATIONS = 48;
    private const CONTRACTS = [ 'feature_grid', 'content_split', 'testimonial_grid', 'cta', 'generic' ];
    private const VIEWPORTS = [ 'desktop', 'tablet', 'mobile' ];
    private const EVIDENCE_VIEWPORTS = [ 'desktop', 'tablet', 'mobile', 'cross_viewport', 'unknown' ];
    private const EVIDENCE_CATEGORIES = [ 'hierarchy', 'section_sequence', 'layout', 'density', 'typography', 'spacing', 'palette', 'radius_border', 'imagery', 'cta', 'background', 'interaction', 'responsive' ];
    private const EVIDENCE_KINDS = [ 'observed', 'inferred' ];
    private const CONFIDENCE = [ 'high', 'medium', 'low' ];
    private const TRANSFERABILITY = [ 'structural', 'visual', 'behavioral', 'site_specific' ];
    private const QA_GATES = [ 'visual_render_verified', 'viewport_exact', 'document_capture_complete', 'capture_bounds_ready', 'browser_probe_verified', 'no_horizontal_overflow', 'no_bad_internal_anchors', 'no_hidden_animated_elements', 'safe_interactions_pass' ];
    private const CORE_QA_GATES = [ 'visual_render_verified', 'viewport_exact', 'document_capture_complete', 'capture_bounds_ready', 'browser_probe_verified', 'no_horizontal_overflow', 'no_bad_internal_anchors' ];

    public static function normalize( $raw, string $selected_section_id = '', string $expected_fingerprint = '' ) {
        if ( ! is_array( $raw ) ) return self::error( 'elementize_blueprint_invalid', 'blueprint must be an object.' );
        $required = [ 'page_goal', 'audience', 'conversion_goal', 'visual_direction', 'sections' ];
        foreach ( $required as $key ) if ( ! array_key_exists( $key, $raw ) ) return self::error( 'elementize_blueprint_required', 'Blueprint field is required: ' . $key );
        $sections = is_array( $raw['sections'] ?? null ) ? $raw['sections'] : [];
        if ( ! $sections || count( $sections ) > self::MAX_SECTIONS ) return self::error( 'elementize_blueprint_sections_invalid', 'blueprint.sections must contain 1-24 sections.' );

        $normalized = [
            'page_goal' => self::text( $raw['page_goal'], 600 ),
            'audience' => self::text( $raw['audience'], 500 ),
            'conversion_goal' => self::text( $raw['conversion_goal'], 500 ),
            'visual_direction' => self::text( $raw['visual_direction'], 800 ),
            'design_tokens' => self::string_map( $raw['design_tokens'] ?? [], 16, 80, 180 ),
            'responsive_strategy' => self::text( $raw['responsive_strategy'] ?? '', 700 ),
            'interaction_strategy' => self::text( $raw['interaction_strategy'] ?? '', 700 ),
            'acceptance_criteria' => self::text_list( $raw['acceptance_criteria'] ?? [], 16, 240 ),
            'reference_basis' => self::reference_basis( $raw['reference_basis'] ?? [] ),
            'reference_evidence' => [],
            'acceptance_plan' => [],
            'build_checkpoints' => [],
            'composition_rhythm' => [],
            'sections' => [],
        ];

        $reference_evidence = self::reference_evidence( $raw['reference_evidence'] ?? [], $normalized['reference_basis'] );
        if ( self::is_error( $reference_evidence ) ) return $reference_evidence;
        $normalized['reference_evidence'] = $reference_evidence;
        $acceptance_plan = self::acceptance_plan( $raw['acceptance_plan'] ?? [] );
        if ( self::is_error( $acceptance_plan ) ) return $acceptance_plan;
        $normalized['acceptance_plan'] = $acceptance_plan;

        foreach ( [ 'page_goal', 'audience', 'conversion_goal', 'visual_direction' ] as $key ) {
            if ( '' === $normalized[ $key ] ) return self::error( 'elementize_blueprint_text_required', 'Blueprint field cannot be empty: ' . $key );
        }

        $ids = [];
        foreach ( $sections as $index => $section ) {
            if ( ! is_array( $section ) ) return self::error( 'elementize_blueprint_section_invalid', 'Every blueprint section must be an object.' );
            $item = self::section( $section, (int) $index );
            if ( self::is_error( $item ) ) return $item;
            if ( isset( $ids[ $item['id'] ] ) ) return self::error( 'elementize_blueprint_section_duplicate', 'Blueprint section IDs must be unique.' );
            $ids[ $item['id'] ] = true;
            $normalized['sections'][] = $item;
        }
        $normalized['composition_rhythm'] = self::composition_rhythm( $normalized['sections'] );
        $normalized['build_checkpoints'] = self::build_checkpoints( $normalized['sections'] );

        $fingerprint = hash( 'sha256', self::json( $normalized ) );
        $expected_fingerprint = strtolower( trim( $expected_fingerprint ) );
        if ( '' !== $expected_fingerprint && ! hash_equals( $fingerprint, $expected_fingerprint ) ) {
            return self::error( 'elementize_blueprint_changed', 'The supplied blueprint no longer matches expected_blueprint_fingerprint.' );
        }

        $selected = null;
        $selected_section_id = self::slug( $selected_section_id, 80 );
        if ( '' !== $selected_section_id ) {
            foreach ( $normalized['sections'] as $section ) if ( $section['id'] === $selected_section_id ) { $selected = $section; break; }
            if ( null === $selected ) return self::error( 'elementize_blueprint_section_missing', 'selected_section_id does not exist in the supplied blueprint.' );
        }

        return [
            'blueprint_version' => self::VERSION,
            'fingerprint' => $fingerprint,
            'normalized' => $normalized,
            'selected_section' => $selected,
        ];
    }

    public static function contract(): array {
        return [
            'blueprint_version' => self::VERSION,
            'persistence' => 'conversation_owned_read_only',
            'max_sections' => self::MAX_SECTIONS,
            'required_fields' => [ 'page_goal', 'audience', 'conversion_goal', 'visual_direction', 'sections' ],
            'section_required_fields' => [ 'id', 'purpose', 'narrative_role', 'contract' ],
            'section_contracts' => self::CONTRACTS,
            'composition_intent' => [
                'roles' => self::COMPOSITION_ROLES,
                'visual_scales' => self::VISUAL_SCALES,
                'media_emphasis' => self::MEDIA_EMPHASIS,
                'densities' => self::DENSITIES,
                'rule' => 'Section composition intent is fingerprinted. Missing values are inferred conservatively from section purpose/contract; three or more identical consecutive roles are review evidence, not an automatic defect.',
            ],
            'reference_viewports' => self::VIEWPORTS,
            'reference_evidence' => [
                'max_observations' => self::MAX_REFERENCE_OBSERVATIONS,
                'categories' => self::EVIDENCE_CATEGORIES,
                'required_observation_fields' => [ 'id', 'category', 'statement', 'viewport', 'evidence_kind', 'confidence', 'transferability' ],
                'rule' => 'Observed viewport evidence must be backed by reference_basis.observed_viewports; unsupported behavior stays inferred or unresolved.',
            ],
            'acceptance_plan' => [
                'required_viewports' => self::VIEWPORTS,
                'quality_gates' => self::QA_GATES,
                'max_repair_cycles' => 3,
                'core_quality_gates' => self::CORE_QA_GATES,
                'rule' => 'Every required viewport also requires native vision. The normalized plan keeps core gates and derives settled/native rows plus requested live-motion and safe-interaction rows.',
            ],
            'build_checkpoints' => [
                'derived' => true,
                'max_checkpoints' => 4,
                'stages' => [ 'design_language_lock', 'early_narrative', 'mid_page_rhythm', 'whole_page_close' ],
                'rule' => 'After reaching each checkpoint section, stop further building, render the current page with native vision, judge the checkpoint rubric, repair only a defensible local blocker, then continue.',
            ],
            'continuity' => 'Reuse the exact blueprint across section ranking calls and pass its returned fingerprint as expected_blueprint_fingerprint.',
            'authority' => 'Validation proves blueprint/reference/acceptance continuity and bounded shape only. ChatGPT remains responsible for design judgment.',
        ];
    }

    private static function section( array $raw, int $index ) {
        foreach ( [ 'id', 'purpose', 'narrative_role', 'contract' ] as $key ) {
            if ( ! array_key_exists( $key, $raw ) ) return self::error( 'elementize_blueprint_section_required', 'Blueprint section field is required: ' . $key );
        }
        $id = self::slug( $raw['id'], 80 );
        $contract = self::slug( $raw['contract'], 40 );
        if ( '' === $id ) return self::error( 'elementize_blueprint_section_id_invalid', 'Blueprint section id must be a stable non-empty slug.' );
        if ( ! in_array( $contract, self::CONTRACTS, true ) ) return self::error( 'elementize_blueprint_section_contract_invalid', 'Unsupported blueprint section contract.' );
        $purpose = self::text( $raw['purpose'], 700 );
        $narrative = self::text( $raw['narrative_role'], 500 );
        $layout = self::text( $raw['layout_pattern'] ?? '', 400 );
        if ( '' === $purpose || '' === $narrative ) return self::error( 'elementize_blueprint_section_text_required', 'Blueprint section purpose and narrative_role cannot be empty.' );
        $composition = self::composition_intent( $raw, $contract, $purpose, $narrative, $layout );
        if ( self::is_error( $composition ) ) return $composition;
        return [
            'index' => $index,
            'id' => $id,
            'purpose' => $purpose,
            'narrative_role' => $narrative,
            'contract' => $contract,
            'composition_role' => $composition['composition_role'],
            'composition_role_source' => $composition['composition_role_source'],
            'visual_scale' => $composition['visual_scale'],
            'media_emphasis' => $composition['media_emphasis'],
            'density' => $composition['density'],
            'layout_pattern' => $layout,
            'content_requirements' => self::text_list( $raw['content_requirements'] ?? [], 12, 220 ),
            'component_constraints' => self::text_list( $raw['component_constraints'] ?? [], 12, 220 ),
            'visual_treatment' => self::text( $raw['visual_treatment'] ?? '', 500 ),
            'responsive_behavior' => self::text( $raw['responsive_behavior'] ?? '', 500 ),
            'interaction_behavior' => self::text( $raw['interaction_behavior'] ?? '', 500 ),
            'keywords' => self::text_list( $raw['keywords'] ?? [], 12, 80 ),
        ];
    }

    private static function composition_intent( array $raw, string $contract, string $purpose, string $narrative, string $layout ) {
        $role_raw = self::slug( $raw['composition_role'] ?? '', 40 );
        if ( '' !== $role_raw && ! in_array( $role_raw, self::COMPOSITION_ROLES, true ) ) return self::error( 'elementize_blueprint_composition_role_invalid', 'Unsupported blueprint section composition_role.' );
        $role = '' !== $role_raw ? $role_raw : self::infer_composition_role( $contract, $purpose, $narrative, $layout );
        $scale_default = in_array( $role, [ 'hero_focus', 'cta_band' ], true ) ? 'dominant' : ( 'faq' === $role ? 'compact' : 'standard' );
        $media_default = match ( $role ) {
            'hero_focus', 'split_media' => 'balanced',
            'card_grid', 'proof_reassurance' => 'supporting',
            default => 'none',
        };
        $density_default = in_array( $role, [ 'hero_focus', 'cta_band' ], true ) ? 'airy' : ( 'card_grid' === $role ? 'dense' : 'balanced' );
        $scale = self::optional_enum( $raw, 'visual_scale', self::VISUAL_SCALES, $scale_default, 'elementize_blueprint_visual_scale_invalid' );
        if ( self::is_error( $scale ) ) return $scale;
        $media = self::optional_enum( $raw, 'media_emphasis', self::MEDIA_EMPHASIS, $media_default, 'elementize_blueprint_media_emphasis_invalid' );
        if ( self::is_error( $media ) ) return $media;
        $density = self::optional_enum( $raw, 'density', self::DENSITIES, $density_default, 'elementize_blueprint_density_invalid' );
        if ( self::is_error( $density ) ) return $density;
        return [
            'composition_role' => $role,
            'composition_role_source' => '' !== $role_raw ? 'explicit' : 'inferred',
            'visual_scale' => $scale,
            'media_emphasis' => $media,
            'density' => $density,
        ];
    }

    private static function infer_composition_role( string $contract, string $purpose, string $narrative, string $layout ): string {
        $primary = strtolower( trim( $purpose . ' ' . $layout ) );
        $narrative_text = strtolower( $narrative );
        $has_primary = static function( array $terms ) use ( $primary ): bool { foreach ( $terms as $term ) if ( false !== strpos( $primary, $term ) ) return true; return false; };
        $has_narrative = static function( array $terms ) use ( $narrative_text ): bool { foreach ( $terms as $term ) if ( false !== strpos( $narrative_text, $term ) ) return true; return false; };
        if ( $has_primary( [ 'hero', 'opening', 'above fold', 'above-fold', 'first viewport', 'promise' ] ) || $has_narrative( [ 'opening frame', 'opening conversion', 'hero' ] ) ) return 'hero_focus';
        if ( $has_primary( [ 'faq', 'question', 'objection' ] ) || $has_narrative( [ 'objection handling', 'faq' ] ) ) return 'faq';
        if ( $has_primary( [ 'workflow', 'process', 'steps', 'step-by-step', 'mechanism', 'how it works' ] ) ) return 'process_steps';
        if ( $has_primary( [ 'final cta', 'final conversion', 'contact action', 'conversion close', 'close the page' ] ) || $has_narrative( [ 'final conversion close', 'final cta' ] ) ) return 'cta_band';
        if ( $has_primary( [ 'testimonial', 'review', 'social proof', 'reassurance', 'trust', 'confidence', 'proof' ] ) || $has_narrative( [ 'reassurance', 'trust', 'social proof', 'confidence' ] ) ) return 'proof_reassurance';
        if ( $has_primary( [ 'split', 'image', 'media', 'screenshot', 'showcase', 'story' ] ) ) return 'split_media';
        if ( $has_primary( [ 'feature', 'capabilit', 'service', 'card grid', 'card-grid', 'cards' ] ) ) return 'card_grid';
        return match ( $contract ) {
            'content_split' => 'split_media',
            'feature_grid' => 'card_grid',
            'testimonial_grid' => 'proof_reassurance',
            'cta' => 'cta_band',
            default => 'editorial',
        };
    }

    private static function optional_enum( array $raw, string $key, array $allowed, string $default, string $error_code ) {
        if ( ! array_key_exists( $key, $raw ) || '' === trim( (string) $raw[ $key ] ) ) return $default;
        $value = self::slug( $raw[ $key ], 40 );
        if ( ! in_array( $value, $allowed, true ) ) return self::error( $error_code, 'Unsupported blueprint section ' . $key . '.' );
        return $value;
    }

    private static function composition_rhythm( array $sections ): array {
        $roles = array_values( array_map( static fn( array $section ): string => (string) ( $section['composition_role'] ?? 'mixed' ), $sections ) );
        $runs = [];
        $start = 0;
        while ( $start < count( $roles ) ) {
            $role = $roles[ $start ];
            $end = $start + 1;
            while ( $end < count( $roles ) && $roles[ $end ] === $role ) $end++;
            $length = $end - $start;
            if ( $length >= 2 ) $runs[] = [ 'role' => $role, 'start_index' => $start, 'end_index' => $end - 1, 'length' => $length, 'review_required' => $length >= 3 ];
            $start = $end;
        }
        $review = array_values( array_filter( $runs, static fn( array $run ): bool => ! empty( $run['review_required'] ) ) );
        return [
            'role_sequence' => $roles,
            'role_variety_count' => count( array_unique( $roles ) ),
            'repeated_runs' => $runs,
            'review_runs' => $review,
            'review_required' => ! empty( $review ),
            'rule' => 'Three or more consecutive identical composition roles require native-vision review for monotony; repetition is evidence, not an automatic defect.',
        ];
    }

    private static function reference_basis( $raw ): array {
        if ( ! is_array( $raw ) ) $raw = [];
        $viewports = [];
        foreach ( is_array( $raw['observed_viewports'] ?? null ) ? $raw['observed_viewports'] : [] as $viewport ) {
            $viewport = self::slug( $viewport, 20 );
            if ( in_array( $viewport, self::VIEWPORTS, true ) ) $viewports[] = $viewport;
        }
        return [
            'source_count' => min( 20, max( 0, (int) ( $raw['source_count'] ?? 0 ) ) ),
            'observed_viewports' => array_values( array_unique( $viewports ) ),
            'unresolved_hypotheses' => self::text_list( $raw['unresolved_hypotheses'] ?? [], 12, 240 ),
        ];
    }

    private static function reference_evidence( $raw, array $reference_basis ) {
        if ( ! is_array( $raw ) ) $raw = [];
        $items = is_array( $raw['observations'] ?? null ) ? $raw['observations'] : [];
        if ( count( $items ) > self::MAX_REFERENCE_OBSERVATIONS ) return self::error( 'elementize_reference_observation_limit', 'reference_evidence.observations may contain at most 48 items.' );
        $observed_viewports = (array) ( $reference_basis['observed_viewports'] ?? [] );
        $normalized = [];
        $ids = [];
        foreach ( $items as $item ) {
            if ( ! is_array( $item ) ) return self::error( 'elementize_reference_observation_invalid', 'Every reference observation must be an object.' );
            foreach ( [ 'id', 'category', 'statement', 'viewport', 'evidence_kind', 'confidence', 'transferability' ] as $key ) {
                if ( ! array_key_exists( $key, $item ) ) return self::error( 'elementize_reference_observation_required', 'Reference observation field is required: ' . $key );
            }
            $entry = [
                'id' => self::slug( $item['id'], 80 ),
                'category' => self::enum_value( $item['category'], self::EVIDENCE_CATEGORIES, '' ),
                'statement' => self::text( $item['statement'], 360 ),
                'viewport' => self::enum_value( $item['viewport'], self::EVIDENCE_VIEWPORTS, 'unknown' ),
                'evidence_kind' => self::enum_value( $item['evidence_kind'], self::EVIDENCE_KINDS, '' ),
                'confidence' => self::enum_value( $item['confidence'], self::CONFIDENCE, '' ),
                'transferability' => self::enum_value( $item['transferability'], self::TRANSFERABILITY, '' ),
                'source_ref' => self::text( $item['source_ref'] ?? '', 120 ),
            ];
            if ( '' === $entry['id'] || '' === $entry['category'] || '' === $entry['statement'] || '' === $entry['evidence_kind'] || '' === $entry['confidence'] || '' === $entry['transferability'] ) return self::error( 'elementize_reference_observation_value_invalid', 'Reference observation contains an unsupported or empty value.' );
            if ( isset( $ids[ $entry['id'] ] ) ) return self::error( 'elementize_reference_observation_duplicate', 'Reference observation IDs must be unique.' );
            if ( 'observed' === $entry['evidence_kind'] && in_array( $entry['viewport'], self::VIEWPORTS, true ) && ! in_array( $entry['viewport'], $observed_viewports, true ) ) return self::error( 'elementize_reference_viewport_unobserved', 'Observed reference evidence names a viewport not present in reference_basis.observed_viewports.' );
            if ( 'observed' === $entry['evidence_kind'] && 'cross_viewport' === $entry['viewport'] && count( $observed_viewports ) < 2 ) return self::error( 'elementize_reference_cross_viewport_unproven', 'Cross-viewport observed evidence requires at least two observed reference viewports.' );
            $ids[ $entry['id'] ] = true;
            $normalized[] = $entry;
        }
        usort( $normalized, static fn( array $a, array $b ): int => strcmp( $a['id'], $b['id'] ) );
        $observed_categories = [];
        $inferred_categories = [];
        foreach ( $normalized as $entry ) {
            if ( 'observed' === $entry['evidence_kind'] ) $observed_categories[] = $entry['category']; else $inferred_categories[] = $entry['category'];
        }
        sort( $observed_categories ); sort( $inferred_categories );
        return [
            'observation_count' => count( $normalized ),
            'observed_categories' => array_values( array_unique( $observed_categories ) ),
            'inferred_categories' => array_values( array_unique( $inferred_categories ) ),
            'observations' => $normalized,
        ];
    }

    private static function acceptance_plan( $raw ) {
        if ( ! is_array( $raw ) ) $raw = [];
        $required = self::viewport_list( $raw['required_viewports'] ?? self::VIEWPORTS );
        if ( ! $required ) return self::error( 'elementize_acceptance_viewports_required', 'acceptance_plan.required_viewports must contain at least one supported viewport.' );
        $native = self::viewport_list( $raw['native_vision_viewports'] ?? $required );
        $live = self::viewport_list( $raw['live_motion_viewports'] ?? [] );
        $safe = self::viewport_list( $raw['safe_interaction_viewports'] ?? [] );
        foreach ( array_merge( $native, $live, $safe ) as $viewport ) if ( ! in_array( $viewport, $required, true ) ) return self::error( 'elementize_acceptance_viewport_outside_scope', 'Acceptance sub-check viewports must also exist in required_viewports.' );
        foreach ( $required as $viewport ) if ( ! in_array( $viewport, $native, true ) ) return self::error( 'elementize_acceptance_native_vision_required', 'Every required acceptance viewport must also be present in native_vision_viewports.' );
        $requested_gates = self::enum_list( $raw['quality_gates'] ?? [], self::QA_GATES, 12 );
        $gates = self::enum_list( array_merge( self::CORE_QA_GATES, $requested_gates ), self::QA_GATES, 12 );
        if ( in_array( 'safe_interactions_pass', $gates, true ) && ! $safe ) return self::error( 'elementize_acceptance_safe_probe_required', 'safe_interactions_pass requires at least one safe_interaction_viewport.' );
        $max_repairs = max( 0, min( 3, (int) ( $raw['max_repair_cycles'] ?? 3 ) ) );
        $matrix = [];
        foreach ( $required as $viewport ) $matrix[] = [ 'id' => $viewport . '-settled', 'viewport' => $viewport, 'motion' => 'settled', 'interaction_probe' => 'passive', 'analyze' => in_array( $viewport, $native, true ), 'purpose' => 'visual_acceptance' ];
        foreach ( $live as $viewport ) $matrix[] = [ 'id' => $viewport . '-live', 'viewport' => $viewport, 'motion' => 'live', 'interaction_probe' => 'passive', 'analyze' => false, 'purpose' => 'motion_integrity' ];
        foreach ( $safe as $viewport ) $matrix[] = [ 'id' => $viewport . '-safe', 'viewport' => $viewport, 'motion' => in_array( $viewport, $live, true ) ? 'live' : 'settled', 'interaction_probe' => 'safe', 'analyze' => false, 'purpose' => 'interaction_integrity' ];
        return [
            'required_viewports' => $required,
            'native_vision_viewports' => $native,
            'live_motion_viewports' => $live,
            'safe_interaction_viewports' => $safe,
            'quality_gates' => $gates,
            'max_repair_cycles' => $max_repairs,
            'bounded_exceptions' => self::text_list( $raw['bounded_exceptions'] ?? [], 8, 240 ),
            'qa_matrix' => $matrix,
        ];
    }

    private static function build_checkpoints( array $sections ): array {
        $count = count( $sections );
        if ( 0 === $count ) return [];
        $out = [];
        $add = static function( int $index, string $stage, string $purpose, array $focus ) use ( &$out, $sections, $count ): void {
            $index = max( 0, min( $count - 1, $index ) );
            $section = $sections[ $index ] ?? null;
            if ( ! is_array( $section ) || empty( $section['id'] ) ) return;
            foreach ( $out as $existing ) if ( (int) ( $existing['after_section_index'] ?? -1 ) === $index ) return;
            $out[] = [
                'id' => 'checkpoint-' . $stage,
                'stage' => $stage,
                'after_section_id' => (string) $section['id'],
                'after_section_index' => $index,
                'viewport' => 'desktop',
                'motion' => 'settled',
                'interaction_probe' => 'passive',
                'analyze' => true,
                'required' => true,
                'max_repair_cycles' => 1,
                'purpose' => $purpose,
                'critic_focus' => $focus,
            ];
        };

        $add( 0, 'design_language_lock', 'Validate the hero and initial design language before that choice propagates through the page.', [ 'hero_prominence', 'first_viewport_conversion_clarity', 'cta_hierarchy', 'media_scale', 'target_design_language' ] );
        if ( $count >= 4 ) $add( 2, 'early_narrative', 'Validate the first narrative group before repeating its spacing/composition patterns.', [ 'section_scale_consistency', 'heading_body_balance', 'vertical_spacing_rhythm', 'split_balance', 'background_transition' ] );
        if ( $count >= 7 ) {
            $mid = max( 3, min( $count - 2, (int) floor( ( $count - 1 ) * 0.65 ) ) );
            $add( $mid, 'mid_page_rhythm', 'Detect page-wide monotony or scale drift while enough of the middle page is still cheap to correct.', [ 'layout_repetition', 'card_grid_monotony', 'section_height_variance', 'imagery_prominence', 'accent_consistency', 'narrative_pacing' ] );
        }
        if ( $count >= 2 ) $add( $count - 1, 'whole_page_close', 'Judge the complete desktop composition before final multi-viewport acceptance.', [ 'whole_page_hierarchy', 'cta_dominance', 'whitespace_outliers', 'footer_transition', 'background_rhythm', 'typography_progression', 'conversion_path' ] );
        return $out;
    }

    private static function viewport_list( $raw ): array {
        if ( ! is_array( $raw ) ) return [];
        $out = [];
        foreach ( $raw as $value ) { $viewport = self::slug( $value, 20 ); if ( in_array( $viewport, self::VIEWPORTS, true ) ) $out[] = $viewport; }
        return array_values( array_unique( $out ) );
    }

    private static function enum_list( $raw, array $allowed, int $max_items ): array {
        if ( ! is_array( $raw ) ) return [];
        $out = [];
        foreach ( array_slice( $raw, 0, $max_items ) as $value ) { $clean = self::enum_value( $value, $allowed, '' ); if ( '' !== $clean ) $out[] = $clean; }
        return array_values( array_unique( $out ) );
    }

    private static function enum_value( $value, array $allowed, string $default ): string {
        $clean = self::slug( $value, 80 );
        return in_array( $clean, $allowed, true ) ? $clean : $default;
    }

    private static function string_map( $raw, int $max_items, int $max_key, int $max_value ): array {
        if ( ! is_array( $raw ) ) return [];
        $out = [];
        foreach ( array_slice( $raw, 0, $max_items, true ) as $key => $value ) {
            if ( ! is_scalar( $value ) ) continue;
            $clean_key = self::slug( $key, $max_key );
            $clean_value = self::text( $value, $max_value );
            if ( '' !== $clean_key && '' !== $clean_value ) $out[ $clean_key ] = $clean_value;
        }
        ksort( $out );
        return $out;
    }

    private static function text_list( $raw, int $max_items, int $max_length ): array {
        if ( ! is_array( $raw ) ) return [];
        $out = [];
        foreach ( array_slice( $raw, 0, min( self::MAX_LIST_ITEMS, $max_items ) ) as $value ) {
            $clean = self::text( $value, $max_length );
            if ( '' !== $clean ) $out[] = $clean;
        }
        return array_values( array_unique( $out ) );
    }

    private static function text( $value, int $max_length ): string {
        if ( ! is_scalar( $value ) ) return '';
        $text = trim( preg_replace( '/\s+/', ' ', strip_tags( (string) $value ) ) ?: '' );
        if ( '' === $text ) return '';
        if ( function_exists( 'mb_substr' ) ) return mb_substr( $text, 0, $max_length, 'UTF-8' );
        return substr( $text, 0, $max_length );
    }

    private static function slug( $value, int $max_length ): string {
        if ( ! is_scalar( $value ) ) return '';
        $value = strtolower( trim( (string) $value ) );
        $value = preg_replace( '/[^a-z0-9_-]+/', '-', $value ) ?: '';
        $value = trim( $value, '-_' );
        return substr( $value, 0, $max_length );
    }

    private static function json( $value ): string {
        $json = function_exists( 'wp_json_encode' ) ? wp_json_encode( $value, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE ) : json_encode( $value, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) ? $json : '';
    }

    private static function is_error( $value ): bool {
        return function_exists( 'is_wp_error' ) ? is_wp_error( $value ) : $value instanceof WP_Error;
    }

    private static function error( string $code, string $message ) {
        if ( class_exists( 'WP_Error' ) ) return new WP_Error( $code, $message, [ 'status' => 400 ] );
        throw new InvalidArgumentException( $code . ': ' . $message );
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Bounded, read-only page blueprint validation for Designer Agent planning.
 * ChatGPT authors the blueprint; Elementize validates continuity and section identity.
 */
final class Elementize_Blueprint {
    private const VERSION = '1';
    private const MAX_SECTIONS = 24;
    private const MAX_LIST_ITEMS = 16;
    private const CONTRACTS = [ 'feature_grid', 'content_split', 'testimonial_grid', 'cta', 'generic' ];
    private const VIEWPORTS = [ 'desktop', 'tablet', 'mobile' ];

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
            'sections' => [],
        ];

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
            'reference_viewports' => self::VIEWPORTS,
            'continuity' => 'Reuse the exact blueprint across section ranking calls and pass its returned fingerprint as expected_blueprint_fingerprint.',
            'authority' => 'Validation proves blueprint continuity and bounded shape only. ChatGPT remains responsible for design judgment.',
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
        $item = [
            'index' => $index,
            'id' => $id,
            'purpose' => self::text( $raw['purpose'], 700 ),
            'narrative_role' => self::text( $raw['narrative_role'], 500 ),
            'contract' => $contract,
            'layout_pattern' => self::text( $raw['layout_pattern'] ?? '', 400 ),
            'content_requirements' => self::text_list( $raw['content_requirements'] ?? [], 12, 220 ),
            'component_constraints' => self::text_list( $raw['component_constraints'] ?? [], 12, 220 ),
            'visual_treatment' => self::text( $raw['visual_treatment'] ?? '', 500 ),
            'responsive_behavior' => self::text( $raw['responsive_behavior'] ?? '', 500 ),
            'interaction_behavior' => self::text( $raw['interaction_behavior'] ?? '', 500 ),
            'keywords' => self::text_list( $raw['keywords'] ?? [], 12, 80 ),
        ];
        if ( '' === $item['purpose'] || '' === $item['narrative_role'] ) return self::error( 'elementize_blueprint_section_text_required', 'Blueprint section purpose and narrative_role cannot be empty.' );
        return $item;
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

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Validates conversation-owned blueprint intent against a Creative transaction.
 * The full blueprint is never persisted; only the compact verified trace is returned.
 */
final class Elementize_Execution_Trace {
    private const VERSION = '2';
    private const MAX_SECTIONS = 24;
    private const MAX_CANDIDATES = 24;
    private const PHASES = [ 'build', 'repair', 'polish' ];
    private const RECOMMENDATIONS = [ 'strong_candidate', 'inspect_and_compare', 'possible_but_normalization_heavy', 'weak_fit' ];

    public static function normalize( $raw, array $operations ) {
        if ( null === $raw || [] === $raw ) return null;
        if ( ! is_array( $raw ) ) return self::error( 'elementize_plan_context_invalid', 'plan_context must be an object.' );

        $blueprint = $raw['blueprint'] ?? null;
        $expected = strtolower( trim( self::scalar( $raw['expected_blueprint_fingerprint'] ?? '' ) ) );
        if ( ! is_array( $blueprint ) || ! preg_match( '/^[a-f0-9]{64}$/', $expected ) ) {
            return self::error( 'elementize_plan_context_blueprint_required', 'plan_context requires blueprint and its exact expected_blueprint_fingerprint.' );
        }

        $validated = Elementize_Blueprint::normalize( $blueprint, '', $expected );
        if ( self::is_error( $validated ) ) return $validated;
        $normalized = (array) ( $validated['normalized'] ?? [] );
        $section_map = [];
        foreach ( (array) ( $normalized['sections'] ?? [] ) as $section ) {
            if ( is_array( $section ) && '' !== (string) ( $section['id'] ?? '' ) ) $section_map[ (string) $section['id'] ] = $section;
        }

        $section_ids = self::section_ids( $raw['section_ids'] ?? [], $section_map );
        if ( self::is_error( $section_ids ) ) return $section_ids;
        if ( ! $section_ids ) return self::error( 'elementize_plan_context_sections_required', 'plan_context.section_ids must identify at least one blueprint section.' );

        $phase = self::slug( $raw['phase'] ?? 'build', 20 );
        if ( ! in_array( $phase, self::PHASES, true ) ) return self::error( 'elementize_plan_context_phase_invalid', 'plan_context.phase must be build, repair, or polish.' );

        $candidates = self::candidates( $raw['selected_candidates'] ?? [], $section_map, $section_ids );
        if ( self::is_error( $candidates ) ) return $candidates;
        $checked = self::verify_insertions( $operations, $candidates, $section_map );
        if ( self::is_error( $checked ) ) return $checked;

        $sections = [];
        foreach ( $section_ids as $id ) {
            $section = $section_map[ $id ];
            $sections[] = [
                'id' => $id,
                'contract' => (string) ( $section['contract'] ?? 'generic' ),
                'composition_role' => (string) ( $section['composition_role'] ?? 'mixed' ),
            ];
        }

        return [
            'trace_version' => self::VERSION,
            'trace_verified' => true,
            'phase' => $phase,
            'blueprint_version' => (string) ( $validated['blueprint_version'] ?? '' ),
            'blueprint_fingerprint' => (string) ( $validated['fingerprint'] ?? '' ),
            'sections' => $sections,
            'selected_candidates' => $candidates,
            'insertion_count_verified' => (int) $checked,
        ];
    }

    public static function contract(): array {
        return [
            'trace_version' => self::VERSION,
            'phases' => self::PHASES,
            'max_sections' => self::MAX_SECTIONS,
            'max_candidates' => self::MAX_CANDIDATES,
            'persistence' => 'compact_verified_trace_only',
            'rule' => 'The full blueprint is validated for this transaction but is never stored in Activity.',
        ];
    }

    private static function section_ids( $raw, array $section_map ) {
        if ( ! is_array( $raw ) || count( $raw ) > self::MAX_SECTIONS ) return self::error( 'elementize_plan_context_sections_invalid', 'plan_context.section_ids must contain at most 24 IDs.' );
        $out = [];
        foreach ( $raw as $value ) {
            $id = self::slug( $value, 80 );
            if ( '' === $id || ! isset( $section_map[ $id ] ) ) return self::error( 'elementize_plan_context_section_missing', 'A plan_context section ID does not exist in the validated blueprint.' );
            $out[] = $id;
        }
        return array_values( array_unique( $out ) );
    }

    private static function candidates( $raw, array $section_map, array $section_ids ) {
        if ( ! is_array( $raw ) || count( $raw ) > self::MAX_CANDIDATES ) return self::error( 'elementize_plan_context_candidates_invalid', 'selected_candidates must contain at most 24 items.' );
        $out = [];
        foreach ( $raw as $candidate ) {
            if ( ! is_array( $candidate ) ) return self::error( 'elementize_plan_context_candidate_invalid', 'Each selected candidate must be an object.' );
            $section_id = self::slug( $candidate['section_id'] ?? '', 80 );
            if ( '' === $section_id || ! in_array( $section_id, $section_ids, true ) || ! isset( $section_map[ $section_id ] ) ) return self::error( 'elementize_plan_context_candidate_section_invalid', 'Each selected candidate must reference one declared blueprint section.' );
            $source_id = self::slug( $candidate['source_id'] ?? '', 100 );
            $template_id = trim( self::scalar( $candidate['template_id'] ?? '' ) );
            if ( '' === $source_id || '' === $template_id || strlen( $template_id ) > 200 ) return self::error( 'elementize_plan_context_candidate_template_invalid', 'Selected candidates require source_id and template_id.' );
            $recommendation = self::slug( $candidate['recommendation'] ?? '', 60 );
            if ( '' !== $recommendation && ! in_array( $recommendation, self::RECOMMENDATIONS, true ) ) return self::error( 'elementize_plan_context_candidate_recommendation_invalid', 'Selected candidate recommendation is unsupported.' );
            $score = isset( $candidate['score'] ) ? (int) $candidate['score'] : null;
            if ( null !== $score && ( $score < 0 || $score > 100 ) ) return self::error( 'elementize_plan_context_candidate_score_invalid', 'Selected candidate score must be 0-100.' );
            $out[] = [
                'section_id' => $section_id,
                'section_contract' => (string) ( $section_map[ $section_id ]['contract'] ?? 'generic' ),
                'section_composition_role' => (string) ( $section_map[ $section_id ]['composition_role'] ?? 'mixed' ),
                'source_id' => $source_id,
                'template_id' => $template_id,
                'score' => $score,
                'recommendation' => $recommendation,
            ];
        }
        return $out;
    }

    private static function verify_insertions( array $operations, array $candidates, array $section_map ) {
        $insertions = [];
        foreach ( $operations as $index => $operation ) {
            if ( ! is_array( $operation ) || 'insert_template' !== self::slug( $operation['type'] ?? '', 40 ) ) continue;
            $insertions[] = [
                'index' => (int) $index,
                'source_id' => self::slug( $operation['source_id'] ?? '', 100 ),
                'template_id' => trim( self::scalar( $operation['template_id'] ?? '' ) ),
                'contract' => self::slug( $operation['contract'] ?? 'generic', 40 ) ?: 'generic',
                'matched' => false,
            ];
        }

        if ( $insertions && ! $candidates ) return self::error( 'elementize_plan_context_candidates_required', 'Blueprint-grounded transactions that insert templates must declare selected_candidates.' );
        foreach ( $candidates as $candidate ) {
            $matched = false;
            foreach ( $insertions as &$insertion ) {
                if ( $insertion['matched'] ) continue;
                if ( $insertion['source_id'] !== $candidate['source_id'] || $insertion['template_id'] !== $candidate['template_id'] ) continue;
                if ( $insertion['contract'] !== $candidate['section_contract'] ) return self::error( 'elementize_plan_context_contract_mismatch', 'An insertion contract does not match its selected blueprint section contract.' );
                $insertion['matched'] = true;
                $matched = true;
                break;
            }
            unset( $insertion );
            if ( ! $matched ) return self::error( 'elementize_plan_context_candidate_not_inserted', 'A selected candidate is not present in the Creative insertion operations.' );
        }
        foreach ( $insertions as $insertion ) if ( ! $insertion['matched'] ) return self::error( 'elementize_plan_context_insertion_unmapped', 'Every template insertion in a blueprint-grounded transaction must map to a selected candidate.' );
        return count( $insertions );
    }

    private static function scalar( $value ): string {
        return is_scalar( $value ) ? (string) $value : '';
    }

    private static function slug( $value, int $max ): string {
        $value = strtolower( trim( self::scalar( $value ) ) );
        $value = preg_replace( '/[^a-z0-9_-]+/', '-', $value ) ?: '';
        return substr( trim( $value, '-_' ), 0, $max );
    }

    private static function is_error( $value ): bool {
        return function_exists( 'is_wp_error' ) ? is_wp_error( $value ) : $value instanceof WP_Error;
    }

    private static function error( string $code, string $message ) {
        if ( class_exists( 'WP_Error' ) ) return new WP_Error( $code, $message, [ 'status' => 400 ] );
        throw new InvalidArgumentException( $code . ': ' . $message );
    }
}

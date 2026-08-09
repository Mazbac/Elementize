<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Design_Intelligence_Hardening {
    private const VERSION = '0.7.1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 185, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( $response->get_status() < 200 || $response->get_status() >= 300 ) return $response;
        $route = $request->get_route();

        if ( '/elementize/v1/status' === $route ) return self::status( $response );
        if ( '/elementize/v1/pixfort/catalogue-summary' === $route ) return self::catalogue_summary( $response );
        if ( '/elementize/v1/pixfort/templates' === $route ) return self::template_search( $response );
        if ( preg_match( '#^/elementize/v1/pages/\d+/pixfort/template-structure$#', $route ) ) return self::template_structure( $response );
        return $response;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['design_intelligence_hardening'] = true;
        $data['design_name_family_is_advisory'] = true;
        $data['design_structural_composition_family'] = true;
        $data['design_structural_alignment_confidence'] = true;
        $data['design_structural_media_hints'] = true;
        $data['design_dependency_risk_split'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function catalogue_summary( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['design_intelligence_version'] = self::VERSION;
        $data['family_interpretation'] = [
            'catalogue_family_signal' => 'name_based_advisory',
            'warning' => 'Catalogue design_family values are derived mainly from normalized Pixfort titles and should not be treated as proof of visual uniqueness.',
            'preferred_finalist_signal' => 'composition_family_hash from read-only template structure inspection',
        ];
        $response->set_data( $data );
        return $response;
    }

    private static function template_search( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        if ( ! isset( $data['design_intelligence'] ) || ! is_array( $data['design_intelligence'] ) ) $data['design_intelligence'] = [];
        $data['design_intelligence']['family_signal_type'] = 'name_based_advisory';
        $data['design_intelligence']['family_signal_warning'] = 'Different design_family values may still be visually or structurally similar. Use visual probes and structure inspection for serious finalists.';
        $data['design_intelligence']['preferred_finalist_family_signal'] = 'composition_family_hash';
        $response->set_data( $data );
        return $response;
    }

    private static function template_structure( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) || ! isset( $data['templates'] ) || ! is_array( $data['templates'] ) ) return $response;

        foreach ( $data['templates'] as &$template ) {
            if ( ! is_array( $template ) || empty( $template['ok'] ) ) continue;
            $widgets = is_array( $template['widget_type_counts'] ?? null ) ? $template['widget_type_counts'] : [];
            $elements = is_array( $template['element_type_counts'] ?? null ) ? $template['element_type_counts'] : [];
            $alignments = is_array( $template['alignment_counts'] ?? null ) ? $template['alignment_counts'] : [];
            $old_roles = is_array( $template['role_counts'] ?? null ) ? $template['role_counts'] : [];

            $widget_count = array_sum( array_map( 'intval', $widgets ) );
            $button_count = self::count_widgets( $widgets, '/button|(^|[-_])btn([-_]|$)|(^|[-_])cta([-_]|$)/' );
            $video_count = self::count_widgets( $widgets, '/video/' );
            $image_count = self::count_widgets( $widgets, '/image|(^|[-_])img([-_]|$)|gallery|picture|photo/' );
            $visual_media_count = $image_count + $video_count;
            $interactive = self::widget_names( $widgets, '/carousel|slider|tabs|accordion|video/' );
            $external_like = self::widget_names( $widgets, '/form|contact-form|newsletter|subscribe|wpforms|gravity|shortcode|posts|loop|feed|products/' );

            $alignment_evidence = array_sum( array_map( 'intval', $alignments ) );
            $alignment_confidence = self::confidence( $alignment_evidence );
            $dominant_alignment = (string) ( $template['dominant_alignment'] ?? 'unknown' );

            $total_elements = max( 0, (int) ( $template['total_elements'] ?? 0 ) );
            $non_widgets = max( 0, $total_elements - $widget_count );
            $max_depth = max( 0, (int) ( $template['max_depth'] ?? 0 ) );
            $complexity_score = round( $widget_count + ( $non_widgets * 0.35 ) + ( $max_depth * 0.25 ), 2 );
            $structural_density = $complexity_score >= 13 ? 'high' : ( $complexity_score >= 5 ? 'medium' : 'low' );

            $media_ratio = $widget_count > 0 ? $visual_media_count / $widget_count : 0;
            $media_prominence = 0 === $visual_media_count ? 'none' : ( $media_ratio >= 0.35 ? 'high' : ( $media_ratio >= 0.15 ? 'medium' : 'low' ) );
            if ( $video_count > 0 && 'low' === $media_prominence ) $media_prominence = 'medium';

            $roles = $old_roles;
            $roles['button_cta'] = $button_count;
            $roles['video'] = $video_count;
            $roles['visual_media'] = $visual_media_count;

            $root_shape = is_array( $template['top_level_shape'] ?? null ) ? implode( ',', $template['top_level_shape'] ) : 'unknown';
            $columns = (int) ( $elements['column'] ?? 0 );
            $containers = (int) ( $elements['container'] ?? 0 );
            $features = self::count_widgets( $widgets, '/feature|card|box/' );
            $family_alignment = in_array( $alignment_confidence, [ 'medium', 'high' ], true ) ? $dominant_alignment : 'unknown';
            $composition = implode( ';', [
                'root=' . $root_shape,
                'columns=' . self::bucket( $columns ),
                'containers=' . self::bucket( $containers ),
                'features=' . self::bucket( $features ),
                'media=' . self::bucket( $visual_media_count ),
                'cta=' . self::bucket( $button_count ),
                'align=' . $family_alignment,
                'density=' . $structural_density,
            ] );

            $template['role_counts_corrected'] = $roles;
            $template['alignment_evidence_count'] = $alignment_evidence;
            $template['alignment_confidence'] = $alignment_confidence;
            $template['structural_complexity_score'] = $complexity_score;
            $template['structural_density'] = $structural_density;
            $template['visual_media_hint'] = [
                'image_widget_count' => $image_count,
                'video_widget_count' => $video_count,
                'visual_media_widget_count' => $visual_media_count,
                'widget_media_prominence' => $media_prominence,
                'background_media_resolved' => false,
                'note' => 'Background photography/video and some decorative media are not reliably represented by widget counts. Use the visual probe as the source of truth for actual media prominence.',
            ];
            $template['dependency_risk'] = [
                'external_or_data_dependent_widgets' => $external_like,
                'interactive_but_not_automatically_external_widgets' => $interactive,
                'hard_dependency_signal' => ! empty( $external_like ) || ! empty( $template['dependency_indicators']['has_shortcode_like_widget'] ) || ! empty( $template['dependency_indicators']['has_form_like_widget'] ),
            ];
            $template['composition_family_signature'] = $composition;
            $template['composition_family_hash'] = hash( 'sha256', $composition );
            $template['comparison_guidance'] = 'Use visual inspection for appearance and composition_family_hash/signature for coarse structural similarity. Do not treat exact structural_family_hash differences as proof that two templates are meaningfully different designs.';
        }
        unset( $template );

        $data['design_intelligence_hardening_version'] = self::VERSION;
        $data['structure_interpretation'] = [
            'exact_structural_family_hash' => 'high-detail fingerprint; useful for exact/near-exact structure, not the primary diversity signal',
            'composition_family_hash' => 'coarser structural family intended for shortlist diversity comparisons',
            'visual_probe' => 'source of truth for actual appearance, background imagery, whitespace, and perceived density',
        ];
        $response->set_data( $data );
        return $response;
    }

    private static function count_widgets( array $widgets, string $pattern ): int {
        $count = 0;
        foreach ( $widgets as $name => $n ) if ( preg_match( $pattern, strtolower( (string) $name ) ) ) $count += (int) $n;
        return $count;
    }

    private static function widget_names( array $widgets, string $pattern ): array {
        $out = [];
        foreach ( $widgets as $name => $n ) if ( (int) $n > 0 && preg_match( $pattern, strtolower( (string) $name ) ) ) $out[] = (string) $name;
        sort( $out );
        return array_values( array_unique( $out ) );
    }

    private static function confidence( int $evidence ): string {
        if ( $evidence < 1 ) return 'none';
        if ( 1 === $evidence ) return 'low';
        if ( $evidence <= 3 ) return 'medium';
        return 'high';
    }

    private static function bucket( int $n ): string {
        if ( $n < 1 ) return '0';
        if ( 1 === $n ) return '1';
        if ( 2 === $n ) return '2';
        return '3+';
    }
}

Elementize_Design_Intelligence_Hardening::init();

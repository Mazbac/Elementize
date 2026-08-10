<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Visual_Localization_Hardening {
    private const VERSION = '0.15.1';
    private const NS = 'elementize/v1';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 345, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual']['localization']['available'] ) ) return $response;
        $loc = $data['visual']['localization'];
        $map = is_array( $loc['marker_map'] ?? null ) ? $loc['marker_map'] : [];
        $items = is_array( $loc['localized_findings'] ?? null ) ? $loc['localized_findings'] : [];
        if ( ! $map || ! $items ) return $response;

        $lookup = [];
        foreach ( $map as $entry ) {
            if ( ! is_array( $entry ) ) continue;
            $marker = strtoupper( trim( (string) ( $entry['marker'] ?? '' ) ) );
            $id = trim( (string) ( $entry['top_level_element_id'] ?? '' ) );
            if ( '' !== $marker && '' !== $id ) $lookup[$marker] = $id;
        }

        $localized = 0; $unlocalized = 0; $rejected = 0;
        $hardened = [];
        foreach ( $items as $item ) {
            if ( ! is_array( $item ) ) continue;
            $markers = [];
            foreach ( is_array( $item['section_markers'] ?? null ) ? $item['section_markers'] : [] as $marker ) {
                $marker = strtoupper( trim( (string) $marker ) );
                if ( isset( $lookup[$marker] ) ) $markers[] = $marker;
            }
            $markers = array_values( array_unique( array_slice( $markers, 0, 3 ) ) );
            $rationale = trim( (string) ( $item['rationale'] ?? '' ) );
            $mentioned = self::markers_in_text( $rationale, $lookup );
            $page_wide_language = self::page_wide_language( $rationale );
            $missing_from_explicit = array_values( array_diff( $mentioned, $markers ) );
            $contradiction = ! empty( $missing_from_explicit ) || ( $page_wide_language && ! empty( $markers ) );

            if ( $contradiction ) {
                $rejected++;
                $item['rejected_section_markers'] = $markers;
                $item['section_markers'] = [];
                $item['top_level_element_ids'] = [];
                $item['confidence'] = 'low';
                $item['localization_status'] = 'rejected_inconsistent_model_output';
                $item['usable_for_repair_discovery'] = false;
                $item['consistency_reason'] = $page_wide_language && $markers
                    ? 'The rationale describes a page-wide or unlocalized issue while also assigning section markers.'
                    : 'The rationale names section marker(s) that were not returned in section_markers: ' . implode( ', ', $missing_from_explicit ) . '.';
            } elseif ( $markers ) {
                $localized++;
                $ids = [];
                foreach ( $markers as $marker ) $ids[] = $lookup[$marker];
                $item['section_markers'] = $markers;
                $item['top_level_element_ids'] = $ids;
                $item['localization_status'] = 'localized';
                $item['usable_for_repair_discovery'] = in_array( (string) ( $item['confidence'] ?? '' ), [ 'medium', 'high' ], true );
                $item['consistency_reason'] = 'Returned markers are valid and do not contradict marker references in the rationale.';
            } else {
                $unlocalized++;
                $item['section_markers'] = [];
                $item['top_level_element_ids'] = [];
                $item['localization_status'] = 'page_wide_or_unlocalized';
                $item['usable_for_repair_discovery'] = false;
                $item['consistency_reason'] = 'No exact section mapping was returned; treat this as page-wide or unresolved.';
            }
            $item['rationale_markers_detected'] = $mentioned;
            $hardened[] = $item;
        }

        $loc['localization_version'] = self::VERSION;
        $loc['localized_findings'] = $hardened;
        $loc['consistency_hardening'] = [
            'enabled' => true,
            'rationale_marker_crosscheck' => true,
            'contradictory_mapping_rejected' => true,
            'localized_count' => $localized,
            'page_wide_or_unlocalized_count' => $unlocalized,
            'rejected_count' => $rejected,
            'all_returned_mappings_consistent' => 0 === $rejected,
        ];
        $loc['note'] = 'Markers map to exact top-level Elementor IDs, but only medium/high-confidence mappings with localization_status=localized are eligible for repair discovery. Contradictory model output is rejected.';
        $data['visual']['localization'] = $loc;
        $response->set_data( $data );
        return $response;
    }

    private static function markers_in_text( string $text, array $lookup ): array {
        if ( '' === $text ) return [];
        preg_match_all( '/\bS\d{1,2}\b/i', $text, $matches );
        $out = [];
        foreach ( $matches[0] ?? [] as $marker ) {
            $marker = strtoupper( (string) $marker );
            if ( isset( $lookup[$marker] ) ) $out[] = $marker;
        }
        return array_values( array_unique( $out ) );
    }

    private static function page_wide_language( string $text ): bool {
        return '' !== $text && (bool) preg_match( '/\b(page[- ]?wide|no exact section|cannot localize|not localized|across the page|throughout the page)\b/i', $text );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_visual_localization_consistency_hardening'] = true;
        $data['rendered_visual_localization_rationale_marker_crosscheck'] = true;
        $data['rendered_visual_localization_contradictory_mapping_rejected'] = true;
        $data['rendered_visual_localization_repair_discovery_medium_high_only'] = true;
        $response->set_data( $data );
        return $response;
    }
}

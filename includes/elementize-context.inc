<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Context {
    private const MAX_NODES = 1500;
    private const MAX_STRUCTURE_RETURN = 300;
    private const MAX_FIELDS_RETURN = 250;
    private const MAX_CONTEXT_STRINGS = 24;
    private const MAX_CLUES = 12;
    private const MAX_CONTEXT_IDS = 30;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 25 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/structure', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'structure' ],
            'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'element_id' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'limit' => [ 'type' => 'integer', 'default' => 120, 'minimum' => 1, 'maximum' => self::MAX_STRUCTURE_RETURN, 'sanitize_callback' => 'absint' ],
                'include_fields' => [ 'type' => 'boolean', 'default' => false ],
            ],
        ] );

        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/resolve', [
            'methods' => WP_REST_Server::EDITABLE,
            'callback' => [ self::class, 'resolve' ],
            'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function structure( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;

        $map = self::build_map( $loaded['elements'] );
        $query = self::normalize( (string) $request->get_param( 'q' ) );
        $element_id = trim( (string) $request->get_param( 'element_id' ) );
        $limit = min( self::MAX_STRUCTURE_RETURN, max( 1, absint( $request->get_param( 'limit' ) ) ) );
        $include_fields = true === $request->get_param( 'include_fields' );

        $nodes = [];
        foreach ( $map['order'] as $node_id ) {
            $node = $map['nodes'][ $node_id ];
            if ( '' !== $element_id && $node_id !== $element_id ) continue;
            if ( '' !== $query && false === strpos( $node['_search_text'], $query ) ) {
                $tokens = self::tokens( $query );
                $matched = false;
                foreach ( $tokens as $token ) {
                    if ( false !== strpos( $node['_search_text'], $token ) ) { $matched = true; break; }
                }
                if ( ! $matched ) continue;
            }
            $nodes[] = self::public_node( $node, $include_fields ? ( $map['editable_by_element'][ $node_id ] ?? [] ) : [] );
            if ( count( $nodes ) >= $limit ) break;
        }

        return new WP_REST_Response( [
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'scope' => 'main_elementor_document_only',
            'semantic_structure' => true,
            'returned_node_count' => count( $nodes ),
            'total_node_count' => count( $map['nodes'] ),
            'groups' => array_values( $map['groups'] ),
            'nodes' => $nodes,
        ], 200 );
    }

    public static function resolve( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;

        $description = trim( (string) $request->get_param( 'description' ) );
        if ( '' === $description || strlen( $description ) > 1000 ) {
            return new WP_Error( 'elementize_resolver_description_required', 'description is required and must be at most 1000 characters.', [ 'status' => 400 ] );
        }

        $visual_clues = self::string_array_param( $request->get_param( 'visual_clues' ), self::MAX_CLUES, 300 );
        if ( is_wp_error( $visual_clues ) ) return $visual_clues;
        $context_ids = self::string_array_param( $request->get_param( 'context_element_ids' ), self::MAX_CONTEXT_IDS, 100 );
        if ( is_wp_error( $context_ids ) ) return $context_ids;

        $scope_id = trim( (string) $request->get_param( 'scope_element_id' ) );
        if ( strlen( $scope_id ) > 100 ) return new WP_Error( 'elementize_resolver_invalid_scope', 'scope_element_id is too long.', [ 'status' => 400 ] );

        $expand = strtolower( trim( (string) $request->get_param( 'expand' ) ) ) ?: 'auto';
        if ( ! in_array( $expand, [ 'auto', 'none', 'siblings', 'group', 'section' ], true ) ) {
            return new WP_Error( 'elementize_resolver_invalid_expand', 'expand must be auto, none, siblings, group, or section.', [ 'status' => 400 ] );
        }

        $expected_count = absint( $request->get_param( 'expected_count' ) );
        if ( $expected_count > 50 ) return new WP_Error( 'elementize_resolver_invalid_count', 'expected_count must be between 1 and 50.', [ 'status' => 400 ] );

        $map = self::build_map( $loaded['elements'] );
        if ( '' !== $scope_id && ! isset( $map['nodes'][ $scope_id ] ) ) {
            return new WP_Error( 'elementize_resolver_scope_not_found', 'The requested scope element does not exist on this fresh page.', [ 'status' => 409 ] );
        }
        foreach ( $context_ids as $context_id ) {
            if ( ! isset( $map['nodes'][ $context_id ] ) ) continue;
            $map['context_ids'][ $context_id ] = true;
        }

        $query_parts = array_merge( [ $description ], $visual_clues );
        $query = self::normalize( implode( ' ', $query_parts ) );
        $query_tokens = self::tokens( $query );
        $scored = [];
        foreach ( $map['order'] as $node_id ) {
            $node = $map['nodes'][ $node_id ];
            if ( '' !== $scope_id && ! self::node_within( $node, $scope_id ) ) continue;
            $score = self::score_node( $node, $query, $query_tokens, $visual_clues, $context_ids, $map );
            if ( $score <= 0 ) continue;
            $scored[] = [ 'id' => $node_id, 'score' => $score ];
        }
        usort( $scored, static fn( array $a, array $b ): int => $b['score'] <=> $a['score'] );

        if ( ! $scored && $context_ids ) {
            foreach ( $context_ids as $context_id ) if ( isset( $map['nodes'][ $context_id ] ) ) $scored[] = [ 'id' => $context_id, 'score' => 5 ];
        }

        if ( ! $scored ) {
            return new WP_REST_Response( [
                'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
                'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
                'resolved' => false,
                'confidence' => 'low',
                'requires_clarification' => true,
                'clarification_reason' => 'No page element matched the supplied description or visual clues.',
                'candidates' => [],
                'selection' => null,
                'editable_fields' => [],
            ], 200 );
        }

        $anchor_id = $scored[0]['id'];
        $anchor = $map['nodes'][ $anchor_id ];
        $effective_expand = self::effective_expand( $expand, $description, $expected_count, $context_ids );
        $selection = self::expand_selection( $anchor_id, $effective_expand, $expected_count, $map );
        $selected_roots = $selection['root_ids'];
        if ( ! $selected_roots ) $selected_roots = [ $anchor_id ];

        $fields = self::fields_for_roots( $selected_roots, $map );
        $field_truncated = count( $fields ) > self::MAX_FIELDS_RETURN;
        if ( $field_truncated ) $fields = array_slice( $fields, 0, self::MAX_FIELDS_RETURN );

        $candidate_payload = [];
        foreach ( array_slice( $scored, 0, 5 ) as $candidate ) {
            $candidate_node = $map['nodes'][ $candidate['id'] ];
            $candidate_payload[] = [
                'element_id' => $candidate['id'],
                'score' => $candidate['score'],
                'semantic_role' => $candidate_node['semantic_role'],
                'widget_type' => $candidate_node['widget_type'],
                'section_id' => $candidate_node['top_level_id'],
                'snippets' => $candidate_node['snippets'],
            ];
        }

        $confidence = self::confidence( $scored, $anchor_id, $map );
        $requires_clarification = 'low' === $confidence;
        $reason = $requires_clarification ? 'Several locations are plausible or the match is weak.' : '';
        if ( $expected_count > 0 && count( $selected_roots ) !== $expected_count ) {
            $requires_clarification = true;
            $reason = 'The resolved group contains ' . count( $selected_roots ) . ' item(s), but the request expected ' . $expected_count . '.';
        }
        if ( ! $fields ) {
            $requires_clarification = true;
            $reason = 'The location was found, but no safely writable content fields were found inside the selected target.';
        }

        $selection_payload = [
            'anchor_element_id' => $anchor_id,
            'anchor_semantic_role' => $anchor['semantic_role'],
            'section_id' => $anchor['top_level_id'],
            'group_id' => $selection['group_id'],
            'expand' => $effective_expand,
            'selected_root_ids' => array_values( $selected_roots ),
            'selected_count' => count( $selected_roots ),
            'context_element_ids' => array_values( array_unique( array_merge( [ $anchor_id ], $selected_roots ) ) ),
        ];

        return new WP_REST_Response( [
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => Elementize_Core::hash_elements( $loaded['elements'] ),
            'scope' => 'main_elementor_document_only',
            'resolved' => true,
            'confidence' => $confidence,
            'requires_clarification' => $requires_clarification,
            'clarification_reason' => $reason,
            'candidates' => $candidate_payload,
            'selection' => $selection_payload,
            'editable_field_count' => count( $fields ),
            'editable_fields_truncated' => $field_truncated,
            'editable_fields' => $fields,
        ], 200 );
    }

    private static function build_map( array $elements ): array {
        $map = [
            'nodes' => [],
            'order' => [],
            'groups' => [],
            'member_groups' => [],
            'editable_by_element' => [],
            'context_ids' => [],
        ];

        $editable = Elementize_Content::discover_items( $elements );
        foreach ( $editable as $item ) {
            $element_id = (string) ( $item['element_id'] ?? '' );
            if ( '' === $element_id ) continue;
            $map['editable_by_element'][ $element_id ][] = $item;
        }

        self::walk_elements( $elements, '', '', 0, [], 0, $map );
        self::build_groups( $map );

        foreach ( $map['nodes'] as $node_id => &$node ) {
            if ( ! empty( $map['member_groups'][ $node_id ] ) ) {
                foreach ( $map['member_groups'][ $node_id ] as $group_id ) {
                    $group = $map['groups'][ $group_id ] ?? null;
                    if ( is_array( $group ) && count( $group['member_ids'] ) >= 3 && 'widget' !== $node['base_role'] ) {
                        $node['semantic_role'] = 'card';
                        break;
                    }
                }
            }
        }
        unset( $node );

        return $map;
    }

    private static function walk_elements( array $elements, string $parent_id, string $top_id, int $depth, array $ancestors, int $top_index, array &$map ): void {
        if ( count( $map['nodes'] ) >= self::MAX_NODES ) return;
        $sibling_count = count( $elements );
        foreach ( array_values( $elements ) as $index => $element ) {
            if ( count( $map['nodes'] ) >= self::MAX_NODES ) return;
            if ( ! is_array( $element ) ) continue;
            $id = trim( (string) ( $element['id'] ?? '' ) );
            $current_top = '' !== $top_id ? $top_id : $id;
            $current_top_index = '' !== $top_id ? $top_index : $index;
            $type = is_string( $element['elType'] ?? null ) ? strtolower( $element['elType'] ) : '';
            $widget = is_string( $element['widgetType'] ?? null ) ? strtolower( $element['widgetType'] ) : '';
            $children = is_array( $element['elements'] ?? null ) ? $element['elements'] : [];

            if ( '' === $id ) {
                self::walk_elements( $children, $parent_id, $top_id, $depth, $ancestors, $top_index, $map );
                continue;
            }

            $base_role = self::base_role( $type, $widget, $depth );
            $snippets = self::context_strings( is_array( $element['settings'] ?? null ) ? $element['settings'] : [], $map['editable_by_element'][ $id ] ?? [] );
            $shape = self::shape_signature( $element );
            $semantic_words = self::semantic_words( $base_role, $widget, $type );
            $search_parts = array_merge( [ $id, $type, $widget, $base_role, $semantic_words ], $snippets );
            foreach ( (array) ( $map['editable_by_element'][ $id ] ?? [] ) as $item ) {
                $search_parts[] = (string) ( $item['setting_key'] ?? '' );
            }

            $child_ids = [];
            foreach ( $children as $child ) {
                if ( is_array( $child ) && '' !== trim( (string) ( $child['id'] ?? '' ) ) ) $child_ids[] = trim( (string) $child['id'] );
            }

            $map['nodes'][ $id ] = [
                'element_id' => $id,
                'parent_id' => $parent_id,
                'top_level_id' => $current_top,
                'top_level_index' => $current_top_index,
                'depth' => $depth,
                'index' => $index,
                'sibling_count' => $sibling_count,
                'ancestor_ids' => $ancestors,
                'child_ids' => $child_ids,
                'el_type' => $type,
                'widget_type' => $widget,
                'base_role' => $base_role,
                'semantic_role' => $base_role,
                'snippets' => $snippets,
                'editable_field_count' => count( $map['editable_by_element'][ $id ] ?? [] ),
                '_shape' => $shape,
                '_search_text' => self::normalize( implode( ' ', $search_parts ) ),
            ];
            $map['order'][] = $id;

            self::walk_elements( $children, $id, $current_top, $depth + 1, array_merge( $ancestors, [ $id ] ), $current_top_index, $map );
        }
    }

    private static function build_groups( array &$map ): void {
        $parents = [];
        foreach ( $map['nodes'] as $node_id => $node ) {
            $parent = (string) $node['parent_id'];
            if ( '' === $parent ) continue;
            $parents[ $parent ][] = $node_id;
        }

        foreach ( $parents as $parent_id => $child_ids ) {
            if ( count( $child_ids ) < 2 ) continue;
            $clusters = [];
            foreach ( $child_ids as $child_id ) {
                $node = $map['nodes'][ $child_id ];
                $key = (string) $node['_shape'];
                $clusters[ $key ][] = $child_id;
            }

            foreach ( $clusters as $shape => $members ) {
                if ( count( $members ) < 2 ) continue;
                self::add_group( $parent_id, $shape, $members, $map );
            }

            if ( count( $child_ids ) < 3 ) continue;
            $types = [];
            $all_structural = true;
            foreach ( $child_ids as $child_id ) {
                $node = $map['nodes'][ $child_id ];
                $types[ $node['el_type'] . '|' . $node['widget_type'] ] = true;
                if ( 'widget' === $node['base_role'] ) $all_structural = false;
            }
            if ( $all_structural && 1 === count( $types ) ) self::add_group( $parent_id, 'structural-siblings', $child_ids, $map );
        }
    }

    private static function add_group( string $parent_id, string $shape, array $members, array &$map ): void {
        $members = array_values( array_unique( $members ) );
        if ( count( $members ) < 2 ) return;
        $group_id = 'group:' . $parent_id . ':' . substr( hash( 'sha256', $shape . '|' . implode( ',', $members ) ), 0, 12 );
        $first = $map['nodes'][ $members[0] ];
        $map['groups'][ $group_id ] = [
            'group_id' => $group_id,
            'parent_id' => $parent_id,
            'section_id' => $first['top_level_id'],
            'member_ids' => $members,
            'size' => count( $members ),
            'relationship' => 'repeated_siblings',
        ];
        foreach ( $members as $member ) $map['member_groups'][ $member ][] = $group_id;
    }

    private static function context_strings( array $settings, array $editable_items ): array {
        $strings = [];
        foreach ( $editable_items as $item ) {
            $value = $item['value'] ?? '';
            if ( is_array( $value ) ) {
                foreach ( [ 'alt', 'title', 'url' ] as $key ) if ( is_string( $value[ $key ] ?? null ) ) self::add_context_string( $strings, (string) $value[ $key ] );
            } elseif ( is_scalar( $value ) ) self::add_context_string( $strings, (string) $value );
        }
        self::scan_context_settings( $settings, [], $strings );
        return array_slice( array_values( array_unique( $strings ) ), 0, self::MAX_CONTEXT_STRINGS );
    }

    private static function scan_context_settings( $value, array $path, array &$strings ): void {
        if ( count( $strings ) >= self::MAX_CONTEXT_STRINGS ) return;
        if ( is_array( $value ) ) {
            if ( isset( $value['url'] ) && is_string( $value['url'] ) ) {
                if ( is_string( $value['alt'] ?? null ) ) self::add_context_string( $strings, (string) $value['alt'] );
                if ( is_string( $value['title'] ?? null ) ) self::add_context_string( $strings, (string) $value['title'] );
                $attachment_id = absint( $value['id'] ?? 0 );
                if ( $attachment_id > 0 ) {
                    $attachment_title = get_the_title( $attachment_id );
                    $attachment_alt = get_post_meta( $attachment_id, '_wp_attachment_image_alt', true );
                    if ( is_string( $attachment_title ) ) self::add_context_string( $strings, $attachment_title );
                    if ( is_string( $attachment_alt ) ) self::add_context_string( $strings, $attachment_alt );
                }
                $basename = wp_basename( (string) $value['url'] );
                if ( '' !== $basename ) self::add_context_string( $strings, $basename );
            }
            foreach ( $value as $key => $child ) {
                self::scan_context_settings( $child, array_merge( $path, [ is_int( $key ) ? $key : (string) $key ] ), $strings );
                if ( count( $strings ) >= self::MAX_CONTEXT_STRINGS ) return;
            }
            return;
        }
        if ( ! is_string( $value ) ) return;
        $trimmed = trim( $value );
        if ( '' === $trimmed || str_starts_with( ltrim( $trimmed ), '[elementor-tag' ) ) return;
        $key = self::last_string_key( $path );
        if ( preg_match( '#^(Line|Duotone|Solid)/pixfort-icon-([A-Za-z0-9_-]+)$#', $trimmed, $matches ) ) {
            self::add_context_string( $strings, $trimmed );
            self::add_context_string( $strings, str_replace( [ '-', '_' ], ' ', (string) $matches[2] ) . ' icon' );
            return;
        }
        if ( self::looks_contextual( $key, $trimmed ) ) self::add_context_string( $strings, $trimmed );
    }

    private static function add_context_string( array &$strings, string $value ): void {
        $plain = trim( preg_replace( '/\\s+/', ' ', html_entity_decode( wp_strip_all_tags( $value ), ENT_QUOTES | ENT_HTML5, 'UTF-8' ) ) );
        if ( '' === $plain ) return;
        if ( strlen( $plain ) > 220 ) $plain = substr( $plain, 0, 217 ) . '...';
        $strings[] = $plain;
    }

    private static function looks_contextual( string $key, string $value ): bool {
        if ( preg_match( '/(^|_)(title|text|content|editor|description|heading|headline|subtitle|label|caption|button|btn|cta|message|quote|author|name|question|answer|tagline|eyebrow|kicker|overline|feature|service|testimonial|review)(_|$)/i', $key ) ) return true;
        $plain = trim( wp_strip_all_tags( $value ) );
        if ( strlen( $plain ) < 4 ) return false;
        if ( preg_match( '#^(https?://|mailto:|tel:)#i', $plain ) ) return false;
        if ( preg_match( '/^#[0-9a-f]{3,8}$/i', $plain ) ) return false;
        if ( preg_match( '/^-?\\d+(?:\\.\\d+)?(?:px|em|rem|%|vh|vw)?$/i', $plain ) ) return false;
        return false !== strpos( $plain, ' ' ) || strlen( $plain ) >= 24;
    }

    private static function shape_signature( array $element ): string {
        $type = strtolower( (string) ( $element['elType'] ?? '' ) );
        $widget = strtolower( (string) ( $element['widgetType'] ?? '' ) );
        $widgets = [];
        self::descendant_widget_types( is_array( $element['elements'] ?? null ) ? $element['elements'] : [], $widgets, 24 );
        sort( $widgets );
        return hash( 'sha256', $type . '|' . $widget . '|' . implode( ',', $widgets ) );
    }

    private static function descendant_widget_types( array $elements, array &$widgets, int $limit ): void {
        foreach ( $elements as $element ) {
            if ( count( $widgets ) >= $limit ) return;
            if ( ! is_array( $element ) ) continue;
            $widget = strtolower( (string) ( $element['widgetType'] ?? '' ) );
            if ( '' !== $widget ) $widgets[] = $widget;
            if ( is_array( $element['elements'] ?? null ) ) self::descendant_widget_types( $element['elements'], $widgets, $limit );
        }
    }

    private static function base_role( string $type, string $widget, int $depth ): string {
        if ( 0 === $depth ) return 'section';
        if ( '' !== $widget || 'widget' === $type ) return 'widget';
        if ( 'column' === $type ) return 'column';
        if ( 'container' === $type ) return 'container';
        if ( 'section' === $type ) return 'section';
        return '' !== $type ? $type : 'element';
    }

    private static function semantic_words( string $role, string $widget, string $type ): string {
        $words = [ $role, $widget, $type ];
        if ( 'section' === $role ) $words[] = 'section sectie block blok';
        if ( in_array( $role, [ 'container', 'column' ], true ) ) $words[] = 'container column kolom card kaart item';
        if ( 'widget' === $role ) $words[] = 'widget element';
        if ( false !== strpos( $widget, 'button' ) ) $words[] = 'button knop cta';
        if ( false !== strpos( $widget, 'image' ) ) $words[] = 'image afbeelding photo foto';
        if ( preg_match( '/heading|headline|title/', $widget ) ) $words[] = 'heading title titel kop';
        if ( preg_match( '/text|editor/', $widget ) ) $words[] = 'text tekst copy paragraph';
        return implode( ' ', $words );
    }

    private static function score_node( array $node, string $query, array $tokens, array $visual_clues, array $context_ids, array $map ): int {
        $score = 0;
        $haystack = $node['_search_text'];
        if ( '' !== $query && strlen( $query ) >= 4 && false !== strpos( $haystack, $query ) ) $score += 18;
        foreach ( $visual_clues as $clue ) {
            $normalized = self::normalize( $clue );
            if ( strlen( $normalized ) >= 3 && false !== strpos( $haystack, $normalized ) ) $score += 12;
        }
        foreach ( $tokens as $token ) {
            if ( false !== strpos( $haystack, $token ) ) $score += strlen( $token ) >= 7 ? 4 : 3;
        }

        if ( in_array( $node['element_id'], $context_ids, true ) ) $score += 24;
        foreach ( $context_ids as $context_id ) {
            if ( in_array( $context_id, $node['ancestor_ids'], true ) || in_array( $node['element_id'], (array) ( $map['nodes'][ $context_id ]['ancestor_ids'] ?? [] ), true ) ) $score += 8;
            if ( (string) ( $map['nodes'][ $context_id ]['parent_id'] ?? '' ) === (string) $node['parent_id'] ) $score += 5;
        }

        $desc = self::normalize( $query );
        if ( preg_match( '/\\b(first|eerste|top|bovenste)\\b/', $desc ) ) {
            if ( 0 === (int) $node['top_level_index'] ) $score += 5;
            if ( 0 === (int) $node['index'] ) $score += 3;
        }
        if ( preg_match( '/\\b(last|laatste|bottom|onderste)\\b/', $desc ) ) {
            if ( (int) $node['index'] === max( 0, (int) $node['sibling_count'] - 1 ) ) $score += 4;
        }
        if ( preg_match( '/\\b(left|links)\\b/', $desc ) && 0 === (int) $node['index'] ) $score += 3;
        if ( preg_match( '/\\b(right|rechts)\\b/', $desc ) && (int) $node['index'] === max( 0, (int) $node['sibling_count'] - 1 ) ) $score += 3;

        if ( $node['editable_field_count'] > 0 ) $score += 1;
        return $score;
    }

    private static function effective_expand( string $expand, string $description, int $expected_count, array $context_ids ): string {
        if ( 'auto' !== $expand ) return $expand;
        $normalized = self::normalize( $description );
        if ( preg_match( '/\\b(section|sectie|blok|whole section|hele sectie)\\b/', $normalized ) ) return 'section';
        if ( $expected_count > 1 ) return 'group';
        if ( preg_match( '/\\b(all|alle|every|elk|rest|overige|others|andere|cards|kaarten|items)\\b/', $normalized ) ) return 'group';
        if ( $context_ids && preg_match( '/\\b(same|zelfde|again|ook|rest|overige|others|andere)\\b/', $normalized ) ) return 'group';
        return 'none';
    }

    private static function expand_selection( string $anchor_id, string $expand, int $expected_count, array $map ): array {
        if ( ! isset( $map['nodes'][ $anchor_id ] ) ) return [ 'root_ids' => [], 'group_id' => null ];
        $anchor = $map['nodes'][ $anchor_id ];
        if ( 'none' === $expand ) return [ 'root_ids' => [ $anchor_id ], 'group_id' => null ];
        if ( 'section' === $expand ) return [ 'root_ids' => [ $anchor['top_level_id'] ], 'group_id' => null ];

        $lineage = array_reverse( array_merge( $anchor['ancestor_ids'], [ $anchor_id ] ) );
        $group_candidates = [];
        foreach ( $lineage as $lineage_id ) {
            foreach ( (array) ( $map['member_groups'][ $lineage_id ] ?? [] ) as $group_id ) {
                $group = $map['groups'][ $group_id ] ?? null;
                if ( is_array( $group ) ) $group_candidates[] = [ 'group_id' => $group_id, 'group' => $group ];
            }
        }
        if ( $expected_count > 0 ) {
            foreach ( $group_candidates as $candidate ) {
                if ( count( $candidate['group']['member_ids'] ) === $expected_count ) return [ 'root_ids' => $candidate['group']['member_ids'], 'group_id' => $candidate['group_id'] ];
            }
        }
        if ( $group_candidates ) {
            foreach ( $lineage as $lineage_id ) {
                $at_level = array_values( array_filter( $group_candidates, static fn( array $candidate ): bool => in_array( $lineage_id, (array) $candidate['group']['member_ids'], true ) ) );
                if ( ! $at_level ) continue;
                usort( $at_level, static fn( array $a, array $b ): int => count( $b['group']['member_ids'] ) <=> count( $a['group']['member_ids'] ) );
                return [ 'root_ids' => $at_level[0]['group']['member_ids'], 'group_id' => $at_level[0]['group_id'] ];
            }
            return [ 'root_ids' => $group_candidates[0]['group']['member_ids'], 'group_id' => $group_candidates[0]['group_id'] ];
        }

        foreach ( $lineage as $lineage_id ) {
            $node = $map['nodes'][ $lineage_id ] ?? null;
            if ( ! is_array( $node ) || '' === $node['parent_id'] ) continue;
            $siblings = [];
            foreach ( $map['nodes'] as $candidate_id => $candidate ) {
                if ( $candidate['parent_id'] !== $node['parent_id'] ) continue;
                if ( $candidate['el_type'] !== $node['el_type'] ) continue;
                if ( $candidate['widget_type'] !== $node['widget_type'] ) continue;
                $siblings[] = $candidate_id;
            }
            if ( count( $siblings ) >= 2 && ( 0 === $expected_count || count( $siblings ) === $expected_count || 'siblings' === $expand ) ) {
                return [ 'root_ids' => $siblings, 'group_id' => null ];
            }
        }

        return [ 'root_ids' => [ $anchor_id ], 'group_id' => null ];
    }

    private static function fields_for_roots( array $root_ids, array $map ): array {
        $fields = [];
        foreach ( $map['order'] as $element_id ) {
            $node = $map['nodes'][ $element_id ];
            $owner = '';
            foreach ( $root_ids as $root_id ) {
                if ( self::node_within( $node, $root_id ) ) { $owner = $root_id; break; }
            }
            if ( '' === $owner ) continue;
            foreach ( (array) ( $map['editable_by_element'][ $element_id ] ?? [] ) as $item ) {
                $field = $item;
                $field['owner_root_id'] = $owner;
                $field['semantic_role'] = $node['semantic_role'];
                $field['section_id'] = $node['top_level_id'];
                $fields[] = $field;
            }
        }
        return $fields;
    }

    private static function public_node( array $node, array $fields ): array {
        $out = [
            'element_id' => $node['element_id'],
            'parent_id' => $node['parent_id'],
            'top_level_id' => $node['top_level_id'],
            'depth' => $node['depth'],
            'index' => $node['index'],
            'sibling_count' => $node['sibling_count'],
            'el_type' => $node['el_type'],
            'widget_type' => $node['widget_type'],
            'semantic_role' => $node['semantic_role'],
            'child_ids' => $node['child_ids'],
            'snippets' => $node['snippets'],
            'editable_field_count' => $node['editable_field_count'],
        ];
        if ( $fields ) $out['editable_fields'] = $fields;
        return $out;
    }

    private static function confidence( array $scored, string $anchor_id, array $map ): string {
        $best = (int) ( $scored[0]['score'] ?? 0 );
        if ( $best < 5 ) return 'low';
        if ( $best >= 16 ) return 'high';
        $best_context = self::context_root( $anchor_id, $map );
        $second_distinct = 0;
        foreach ( array_slice( $scored, 1 ) as $candidate ) {
            if ( self::context_root( $candidate['id'], $map ) === $best_context ) continue;
            $second_distinct = (int) $candidate['score'];
            break;
        }
        if ( 0 === $second_distinct || $best - $second_distinct >= 4 ) return 'high';
        if ( $best - $second_distinct >= 2 ) return 'medium';
        return 'low';
    }

    private static function context_root( string $node_id, array $map ): string {
        $node = $map['nodes'][ $node_id ] ?? null;
        if ( ! is_array( $node ) ) return $node_id;
        $lineage = array_reverse( array_merge( $node['ancestor_ids'], [ $node_id ] ) );
        foreach ( $lineage as $lineage_id ) if ( ! empty( $map['member_groups'][ $lineage_id ] ) ) return $lineage_id;
        return $node['top_level_id'] ?: $node_id;
    }

    private static function node_within( array $node, string $root_id ): bool {
        return $node['element_id'] === $root_id || in_array( $root_id, $node['ancestor_ids'], true );
    }

    private static function string_array_param( $value, int $max_items, int $max_length ) {
        if ( null === $value || '' === $value ) return [];
        if ( ! is_array( $value ) ) return new WP_Error( 'elementize_resolver_invalid_array', 'Resolver list parameters must be arrays.', [ 'status' => 400 ] );
        if ( count( $value ) > $max_items ) return new WP_Error( 'elementize_resolver_too_many_items', 'Too many resolver list items were supplied.', [ 'status' => 400 ] );
        $out = [];
        foreach ( $value as $item ) {
            if ( ! is_string( $item ) ) return new WP_Error( 'elementize_resolver_invalid_item', 'Resolver list items must be strings.', [ 'status' => 400 ] );
            $item = trim( $item );
            if ( strlen( $item ) > $max_length ) return new WP_Error( 'elementize_resolver_item_too_long', 'A resolver list item is too long.', [ 'status' => 400 ] );
            if ( '' !== $item ) $out[] = $item;
        }
        return array_values( array_unique( $out ) );
    }

    private static function normalize( string $value ): string {
        $value = html_entity_decode( wp_strip_all_tags( $value ), ENT_QUOTES | ENT_HTML5, 'UTF-8' );
        $value = strtolower( $value );
        $value = str_replace( [ '-', '_', '/', '\\' ], ' ', $value );
        $value = preg_replace( '/[^\\p{L}\\p{N}]+/u', ' ', $value );
        return trim( preg_replace( '/\\s+/', ' ', (string) $value ) );
    }

    private static function tokens( string $value ): array {
        $stop = [ 'the','a','an','and','or','of','to','in','on','with','for','this','that','these','those','is','are','it','from','at','all','same','other','others','de','het','een','en','of','van','naar','in','op','met','voor','deze','dit','die','dat','is','zijn','alle','zelfde','andere','overige','ook','bij' ];
        $tokens = [];
        foreach ( preg_split( '/\\s+/', self::normalize( $value ) ) ?: [] as $token ) {
            if ( strlen( $token ) < 3 || in_array( $token, $stop, true ) ) continue;
            $tokens[] = $token;
        }
        return array_values( array_unique( $tokens ) );
    }

    private static function last_string_key( array $path ): string {
        for ( $i = count( $path ) - 1; $i >= 0; $i-- ) if ( is_string( $path[ $i ] ) ) return strtolower( $path[ $i ] );
        return '';
    }
}

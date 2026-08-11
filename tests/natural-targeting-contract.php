<?php

define( 'ABSPATH', __DIR__ . '/' );

function wp_strip_all_tags( $value ) { return strip_tags( (string) $value ); }
function is_serialized( $value ) { return false; }
function absint( $value ) { return abs( (int) $value ); }
function get_the_title( $id ) { return ''; }
function get_post_meta( $id, $key, $single = true ) { return ''; }
function wp_basename( $value ) { return basename( (string) $value ); }

require_once __DIR__ . '/../includes/elementize-content.inc';
require_once __DIR__ . '/../includes/elementize-context.inc';

function invoke_private( string $class, string $method, array $args = [] ) {
    $reflection = new ReflectionMethod( $class, $method );
    $reflection->setAccessible( true );
    return $reflection->invokeArgs( null, $args );
}

function assert_same( $expected, $actual, string $message ): void {
    if ( $expected === $actual ) return;
    $details = $message . "\nExpected: " . var_export( $expected, true ) . "\nActual: " . var_export( $actual, true );
    fwrite( STDERR, $details . "\n" );
    if ( 'true' === getenv( 'GITHUB_ACTIONS' ) ) {
        $annotation = str_replace( [ '%', "\r", "\n" ], [ '%25', '%0D', '%0A' ], $details );
        fwrite( STDOUT, '::error title=Natural targeting contract::' . $annotation . "\n" );
    }
    exit( 1 );
}

function assert_true( $actual, string $message ): void {
    assert_same( true, (bool) $actual, $message );
}

function assert_false( $actual, string $message ): void {
    assert_same( false, (bool) $actual, $message );
}

$copy_cases = [
    [ [ 'items', 0, 'title' ], 'Riskmanagement', true ],
    [ [ 'cards', 2, 'description' ], 'Manage security risks from one place.', true ],
    [ [ 'custom_widget', 'my_copy' ], 'A human-readable sentence in an unfamiliar widget field.', true ],
    [ [ 'settings', 'author_name' ], 'Mert', true ],
    [ [ 'settings', 'padding' ], '20px', false ],
    [ [ 'settings', 'custom_css' ], 'color: red;', false ],
    [ [ 'settings', 'background_color' ], '#ffffff', false ],
    [ [ 'settings', 'button_url' ], 'https://example.com', false ],
    [ [ 'settings', 'icon' ], 'Line/pixfort-icon-shield', false ],
    [ [ 'settings', 'layout_mode' ], 'flex', false ],
    [ [ 'settings', 'random_token' ], 'abc123', false ],
    [ [ 'settings', 'button_text_color' ], '#64379f', false ],
    [ [ 'settings', 'subtitle_font_size' ], '18px', false ],
    [ [ 'settings', 'content_width' ], '1200px', false ],
    [ [ 'settings', 'title' ], 'https://example.com/internal-config', false ],
    [ [ 'settings', 'description' ], '{"layout":"grid"}', false ],
    [ [ 'settings', 'heading_tag' ], 'h2', false ],
    [ [ 'settings', 'content_type' ], 'text', false ],
    [ [ 'settings', 'title_style' ], 'compact', false ],
    [ [ 'settings', 'description_source' ], 'manual', false ],
    [ [ 'settings', 'button_text_control' ], 'default', false ],
    [ [ 'settings', 'subtitle_device' ], 'desktop', false ],
];

foreach ( $copy_cases as [ $path, $value, $expected ] ) {
    $actual = invoke_private( Elementize_Content::class, 'copy_field', [ $path, $value ] );
    assert_same( $expected, $actual, 'Safe text discovery regression for ' . json_encode( $path ) );
}

$cards = [];
for ( $i = 1; $i <= 6; $i++ ) {
    $children = [
        [
            'id' => 'h' . $i,
            'elType' => 'widget',
            'widgetType' => 'heading',
            'settings' => [ 'title' => 'Card ' . $i ],
            'elements' => [],
        ],
        [
            'id' => 't' . $i,
            'elType' => 'widget',
            'widgetType' => 'text-editor',
            'settings' => [ 'editor' => 'Description for card ' . $i ],
            'elements' => [],
        ],
    ];

    // One card intentionally differs so the majority-shape structural fallback is tested.
    if ( 4 === $i ) {
        $children[] = [
            'id' => 'icon' . $i,
            'elType' => 'widget',
            'widgetType' => 'icon',
            'settings' => [ 'icon' => 'Line/pixfort-icon-shield' ],
            'elements' => [],
        ];
    }

    $cards[] = [
        'id' => 'card' . $i,
        'elType' => 'container',
        'settings' => [],
        'elements' => $children,
    ];
}

$elements = [
    [
        'id' => 'sectionA',
        'elType' => 'container',
        'settings' => [],
        'elements' => $cards,
    ],
];

$map = invoke_private( Elementize_Context::class, 'build_map', [ $elements ] );
assert_same( 'card', $map['nodes']['card3']['semantic_role'], 'Repeated structural siblings should be recognized as cards.' );
assert_same( 'card', $map['nodes']['card4']['semantic_role'], 'A slightly different card should remain part of the repeated card group.' );

$selection = invoke_private( Elementize_Context::class, 'expand_selection', [ 'h3', 'group', 6, $map ] );
assert_same( [ 'card1', 'card2', 'card3', 'card4', 'card5', 'card6' ], $selection['root_ids'], 'A clue inside one card should expand to all six repeated cards.' );
assert_true( ! empty( $selection['group_id'] ), 'Six-card expansion should expose a reusable group ID.' );

$auto_group = invoke_private( Elementize_Context::class, 'expand_selection', [ 'h3', 'group', 0, $map ] );
assert_same( [ 'card1', 'card2', 'card3', 'card4', 'card5', 'card6' ], $auto_group['root_ids'], 'Group expansion without an explicit count should prefer the complete repeated-card group.' );

$sibling_selection = invoke_private( Elementize_Context::class, 'expand_selection', [ 'h3', 'siblings', 0, $map ] );
assert_same( [ 'card1', 'card2', 'card3', 'card4', 'card5', 'card6' ], $sibling_selection['root_ids'], 'Explicit sibling expansion should return the nearest structural siblings instead of an exact-shape subgroup.' );
assert_same( null, $sibling_selection['group_id'], 'Explicit sibling expansion should not masquerade as a repeated group.' );

$missing_count = invoke_private( Elementize_Context::class, 'expand_selection', [ 'h3', 'group', 7, $map ] );
assert_same( [ 'h3' ], $missing_count['root_ids'], 'A missing expected group size should fail closed so the resolver can ask for clarification.' );

assert_same( 'group', invoke_private( Elementize_Context::class, 'effective_expand', [ 'auto', 'pas alle zes kaarten aan', 6, [] ] ), 'Dutch all-card request should resolve to group expansion.' );
assert_same( 'group', invoke_private( Elementize_Context::class, 'effective_expand', [ 'auto', 'do the rest too', 0, [ 'h3' ] ] ), 'Follow-up context should resolve “the rest” to the repeated group.' );
assert_same( 'section', invoke_private( Elementize_Context::class, 'effective_expand', [ 'auto', 'pas de hele sectie aan', 0, [] ] ), 'Whole-section language should resolve to section expansion.' );

$fields = invoke_private( Elementize_Context::class, 'fields_for_roots', [ $selection['root_ids'], $map ] );
$text_fields = array_values( array_filter( $fields, static fn( array $field ): bool => 'text' === ( $field['kind'] ?? '' ) ) );
assert_same( 12, count( $text_fields ), 'All six cards should expose both heading and body copy as exact writable fields.' );
assert_same( [ 'card1', 'card2', 'card3', 'card4', 'card5', 'card6' ], array_values( array_unique( array_column( $text_fields, 'owner_root_id' ) ) ), 'Writable fields should retain their card ownership.' );

$query = invoke_private( Elementize_Context::class, 'normalize', [ 'Card 3' ] );
$tokens = invoke_private( Elementize_Context::class, 'tokens', [ $query ] );
assert_true( in_array( '3', $tokens, true ), 'Numeric screenshot/location clues should remain usable as exact resolver tokens.' );
$scores = [];
foreach ( $map['order'] as $node_id ) {
    $scores[ $node_id ] = invoke_private( Elementize_Context::class, 'score_node', [ $map['nodes'][ $node_id ], $query, $tokens, [ 'Card 3' ], [], $map ] );
}
arsort( $scores );
assert_same( 'h3', array_key_first( $scores ), 'Visible screenshot/text clue should rank the matching card heading first.' );

$substring_node = [ '_search_text' => 'brisket overview', '_tokens' => [ 'brisket' => true ] ];
assert_false( invoke_private( Elementize_Context::class, 'node_matches_query', [ $substring_node, 'risk', [ 'risk' ] ] ), 'Resolver token matching must not match tokens only because they are substrings of another word.' );

$role_query = invoke_private( Elementize_Context::class, 'normalize', [ 'change the title in this card' ] );
$role_tokens = invoke_private( Elementize_Context::class, 'tokens', [ $role_query ] );
$role_scores = [];
foreach ( $map['order'] as $node_id ) {
    $role_scores[ $node_id ] = invoke_private( Elementize_Context::class, 'score_node', [ $map['nodes'][ $node_id ], $role_query, $role_tokens, [], [ 'card3' ], $map ] );
}
arsort( $role_scores );
assert_same( 'h3', array_key_first( $role_scores ), 'A requested child role inside prior context should outrank the context container itself.' );

$relative_elements = [
    [
        'id' => 'sectionRelative',
        'elType' => 'container',
        'settings' => [],
        'elements' => [
            [
                'id' => 'contextCopy',
                'elType' => 'widget',
                'widgetType' => 'text-editor',
                'settings' => [ 'editor' => 'Current selection' ],
                'elements' => [],
            ],
            [
                'id' => 'buttonBelow',
                'elType' => 'widget',
                'widgetType' => 'button',
                'settings' => [ 'button_text' => 'Learn more', 'button_link' => '#learn-more' ],
                'elements' => [],
            ],
            [
                'id' => 'otherCopy',
                'elType' => 'widget',
                'widgetType' => 'text-editor',
                'settings' => [ 'editor' => 'Other content' ],
                'elements' => [],
            ],
        ],
    ],
];
$relative_map = invoke_private( Elementize_Context::class, 'build_map', [ $relative_elements ] );
$relative_query = invoke_private( Elementize_Context::class, 'normalize', [ 'de knop daaronder' ] );
$relative_tokens = invoke_private( Elementize_Context::class, 'tokens', [ $relative_query ] );
$relative_scores = [];
foreach ( $relative_map['order'] as $node_id ) {
    $relative_scores[ $node_id ] = invoke_private( Elementize_Context::class, 'score_node', [ $relative_map['nodes'][ $node_id ], $relative_query, $relative_tokens, [], [ 'contextCopy' ], $relative_map ] );
}
arsort( $relative_scores );
assert_same( 'buttonBelow', array_key_first( $relative_scores ), '“De knop daaronder” should resolve to the nearby button below prior context, not the context element itself.' );

$ambiguous_map = [
    'nodes' => [
        'a' => [ 'ancestor_ids' => [], 'top_level_id' => 'section1' ],
        'b' => [ 'ancestor_ids' => [], 'top_level_id' => 'section2' ],
    ],
    'member_groups' => [],
    'groups' => [],
];
$ambiguous_scores = [ [ 'id' => 'a', 'score' => 30 ], [ 'id' => 'b', 'score' => 30 ] ];
assert_same( 'low', invoke_private( Elementize_Context::class, 'confidence', [ $ambiguous_scores, 'a', $ambiguous_map ] ), 'Two equally strong targets in different contexts must require clarification even at high absolute scores.' );
$clear_scores = [ [ 'id' => 'a', 'score' => 30 ], [ 'id' => 'b', 'score' => 15 ] ];
assert_same( 'high', invoke_private( Elementize_Context::class, 'confidence', [ $clear_scores, 'a', $ambiguous_map ] ), 'A clearly separated strong target should retain high confidence.' );

$same_group_scores = [ [ 'id' => 'h1', 'score' => 30 ], [ 'id' => 'h2', 'score' => 30 ], [ 'id' => 'h3', 'score' => 30 ] ];
assert_same( 'high', invoke_private( Elementize_Context::class, 'confidence', [ $same_group_scores, 'h1', $map ] ), 'Equally strong candidates inside one repeated-card group must not be treated as cross-section ambiguity.' );

$many_context_roots = [];
for ( $i = 1; $i <= 50; $i++ ) $many_context_roots[] = 'root' . $i;
$capped_context = invoke_private( Elementize_Context::class, 'selection_context_ids', [ 'anchor', $many_context_roots ] );
assert_same( 30, count( $capped_context ), 'Resolver output context must never exceed the 30 IDs accepted by the follow-up resolver input.' );
assert_same( 'anchor', $capped_context[0], 'The resolver anchor must be retained when context IDs are capped.' );

$unrelated = [];
$unrelated_widgets = [ 'heading', 'image', 'button', 'text-editor' ];
foreach ( $unrelated_widgets as $index => $widget_type ) {
    $unrelated[] = [
        'id' => 'unrelated' . ( $index + 1 ),
        'elType' => 'container',
        'settings' => [],
        'elements' => [
            [
                'id' => 'unrelated-widget-' . ( $index + 1 ),
                'elType' => 'widget',
                'widgetType' => $widget_type,
                'settings' => [],
                'elements' => [],
            ],
        ],
    ];
}
$unrelated_elements = [
    [
        'id' => 'sectionB',
        'elType' => 'container',
        'settings' => [],
        'elements' => $unrelated,
    ],
];
$unrelated_map = invoke_private( Elementize_Context::class, 'build_map', [ $unrelated_elements ] );
foreach ( $unrelated as $item ) {
    assert_same( 'container', $unrelated_map['nodes'][ $item['id'] ]['semantic_role'], 'Unrelated same-type containers must not be falsely promoted to repeated cards.' );
}
foreach ( $unrelated_map['groups'] as $group ) {
    assert_false( 'sectionB' === $group['parent_id'] && 4 === count( $group['member_ids'] ), 'Unrelated sibling containers must not be grouped merely because their Elementor element type matches.' );
}

$utf8_context = invoke_private( Elementize_Context::class, 'context_strings', [ [ 'title' => str_repeat( 'é', 230 ) ], [] ] );
assert_true( isset( $utf8_context[0] ), 'Long UTF-8 context should still produce a resolver snippet.' );
assert_same( 1, preg_match( '//u', $utf8_context[0] ), 'Resolver context truncation must preserve valid UTF-8.' );
assert_true( strlen( $utf8_context[0] ) < strlen( str_repeat( 'é', 230 ) ), 'Long resolver context should be truncated.' );

fwrite( STDOUT, "Natural targeting contract OK\n" );

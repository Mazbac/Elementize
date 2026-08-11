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
    fwrite( STDERR, $message . "\nExpected: " . var_export( $expected, true ) . "\nActual: " . var_export( $actual, true ) . "\n" );
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

    // One card intentionally differs so the broad repeated-structural fallback is tested.
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

$selection = invoke_private( Elementize_Context::class, 'expand_selection', [ 'h3', 'group', 6, $map ] );
assert_same( [ 'card1', 'card2', 'card3', 'card4', 'card5', 'card6' ], $selection['root_ids'], 'A clue inside one card should expand to all six repeated cards.' );
assert_true( ! empty( $selection['group_id'] ), 'Six-card expansion should expose a reusable group ID.' );

assert_same( 'group', invoke_private( Elementize_Context::class, 'effective_expand', [ 'auto', 'pas alle zes kaarten aan', 6, [] ] ), 'Dutch all-card request should resolve to group expansion.' );
assert_same( 'group', invoke_private( Elementize_Context::class, 'effective_expand', [ 'auto', 'do the rest too', 0, [ 'h3' ] ] ), 'Follow-up context should resolve “the rest” to the repeated group.' );
assert_same( 'section', invoke_private( Elementize_Context::class, 'effective_expand', [ 'auto', 'pas de hele sectie aan', 0, [] ] ), 'Whole-section language should resolve to section expansion.' );

$fields = invoke_private( Elementize_Context::class, 'fields_for_roots', [ $selection['root_ids'], $map ] );
$text_fields = array_values( array_filter( $fields, static fn( array $field ): bool => 'text' === ( $field['kind'] ?? '' ) ) );
assert_same( 12, count( $text_fields ), 'All six cards should expose both heading and body copy as exact writable fields.' );
assert_same( [ 'card1', 'card2', 'card3', 'card4', 'card5', 'card6' ], array_values( array_unique( array_column( $text_fields, 'owner_root_id' ) ) ), 'Writable fields should retain their card ownership.' );

$query = invoke_private( Elementize_Context::class, 'normalize', [ 'Card 3' ] );
$tokens = invoke_private( Elementize_Context::class, 'tokens', [ $query ] );
$scores = [];
foreach ( $map['order'] as $node_id ) {
    $scores[ $node_id ] = invoke_private( Elementize_Context::class, 'score_node', [ $map['nodes'][ $node_id ], $query, $tokens, [ 'Card 3' ], [], $map ] );
}
arsort( $scores );
assert_same( 'h3', array_key_first( $scores ), 'Visible screenshot/text clue should rank the matching card heading first.' );

fwrite( STDOUT, "Natural targeting contract OK\n" );

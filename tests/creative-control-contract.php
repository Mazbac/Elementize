<?php

define( 'ABSPATH', __DIR__ . '/' );

class WP_Error {
    private string $code;
    private string $message;
    private $data;
    public function __construct( string $code = '', string $message = '', $data = null ) { $this->code = $code; $this->message = $message; $this->data = $data; }
    public function get_error_code(): string { return $this->code; }
    public function get_error_message(): string { return $this->message; }
    public function get_error_data() { return $this->data; }
}
function is_wp_error( $value ): bool { return $value instanceof WP_Error; }
function wp_generate_uuid4(): string { static $i = 0; $i++; return sprintf( '00000000-0000-4000-8000-%012d', $i ); }
function wp_rand(): int { static $i = 100; return ++$i; }
function sanitize_key( $value ): string { return strtolower( preg_replace( '/[^a-z0-9_\-]/i', '', (string) $value ) ); }
function sanitize_text_field( $value ): string { return trim( strip_tags( (string) $value ) ); }

require_once __DIR__ . '/../includes/elementize-tree.inc';
require_once __DIR__ . '/../includes/elementize-creative.inc';

function assert_same_creative( $expected, $actual, string $message ): void {
    if ( $expected === $actual ) return;
    $details = $message . "\nExpected: " . var_export( $expected, true ) . "\nActual: " . var_export( $actual, true );
    fwrite( STDERR, $details . "\n" );
    if ( 'true' === getenv( 'GITHUB_ACTIONS' ) ) {
        $annotation = str_replace( [ '%', "\r", "\n" ], [ '%25', '%0D', '%0A' ], $details );
        fwrite( STDOUT, '::error title=Creative Control contract::' . $annotation . "\n" );
    }
    exit( 1 );
}
function assert_true_creative( $value, string $message ): void { assert_same_creative( true, (bool) $value, $message ); }
function assert_not_same_creative( $unexpected, $actual, string $message ): void {
    if ( $unexpected !== $actual ) return;
    assert_same_creative( 'a different value', $actual, $message );
}
function private_creative( string $method, array $args = [] ) {
    $reflection = new ReflectionMethod( Elementize_Creative::class, $method );
    $reflection->setAccessible( true );
    return $reflection->invokeArgs( null, $args );
}

$fixture = [
    [
        'id' => '12345678',
        'elType' => 'container',
        'version' => '0.4',
        'styles' => [ 'atomic-style-data' => [ 'opaque' => true ] ],
        'editor_settings' => [ 'custom' => 'preserve-me' ],
        'settings' => [
            'image' => [ 'id' => '12345678', 'url' => 'https://example.test/image.jpg' ],
            'items' => [ [ '_id' => 'repeat01', 'title' => 'First' ] ],
            'target_id' => 'childabc',
            'custom_css' => '.elementor-element-childabc [data-id="12345678"] { color: red; }',
        ],
        'elements' => [
            [
                'id' => 'childabc',
                'elType' => 'widget',
                'widgetType' => 'heading',
                'settings' => [ 'title' => 'Hello' ],
                'elements' => [],
            ],
        ],
    ],
];

$used = [ 'reserved' => true ];
$map = [];
$clone = Elementize_Tree::clone_with_fresh_ids( $fixture, $used, $map );
assert_true_creative( isset( $map['12345678'], $map['childabc'], $map['repeat01'] ), 'Element and repeater IDs must receive fresh mappings.' );
assert_not_same_creative( '12345678', $clone[0]['id'], 'Root Elementor ID must change on clone.' );
assert_not_same_creative( 'childabc', $clone[0]['elements'][0]['id'], 'Nested Elementor ID must change on clone.' );
assert_not_same_creative( 'repeat01', $clone[0]['settings']['items'][0]['_id'], 'Repeater _id must change on clone.' );
assert_same_creative( '12345678', $clone[0]['settings']['image']['id'], 'String media attachment IDs must not be treated as Elementor element IDs, even when they equal an old element ID.' );
assert_same_creative( $map['childabc'], $clone[0]['settings']['target_id'], 'Explicit local target ID settings should be remapped.' );
assert_true_creative( false !== strpos( $clone[0]['settings']['custom_css'], 'elementor-element-' . $map['childabc'] ), 'Elementor CSS selector references should follow cloned IDs.' );
assert_true_creative( false !== strpos( $clone[0]['settings']['custom_css'], '[data-id="' . $map['12345678'] . '"]' ), 'data-id selector references should follow cloned IDs.' );
assert_same_creative( '0.4', $clone[0]['version'], 'Unknown/newer Elementor node fields must be preserved.' );
assert_same_creative( [ 'atomic-style-data' => [ 'opaque' => true ] ], $clone[0]['styles'], 'Atomic style payloads must be preserved verbatim.' );
assert_same_creative( [ 'custom' => 'preserve-me' ], $clone[0]['editor_settings'], 'Editor metadata must be preserved verbatim.' );

$page = [
    [ 'id' => 'a111', 'elType' => 'container', 'settings' => [], 'elements' => [] ],
    [ 'id' => 'b222', 'elType' => 'container', 'settings' => [], 'elements' => [
        [ 'id' => 'b1', 'elType' => 'widget', 'widgetType' => 'heading', 'settings' => [ 'title' => 'B1' ], 'elements' => [] ],
        [ 'id' => 'b2', 'elType' => 'widget', 'widgetType' => 'heading', 'settings' => [ 'title' => 'B2' ], 'elements' => [] ],
        [ 'id' => 'b3', 'elType' => 'widget', 'widgetType' => 'heading', 'settings' => [ 'title' => 'B3' ], 'elements' => [] ],
    ] ],
];
$insert = [ [ 'id' => 'new333', 'elType' => 'container', 'settings' => [], 'elements' => [] ] ];
assert_same_creative( true, Elementize_Tree::insert_nodes( $page, 'a111', $insert, 'after' ), 'Insert-after must succeed for a fresh anchor.' );
assert_same_creative( [ 'a111', 'new333', 'b222' ], array_column( $page, 'id' ), 'Insert-after must keep deterministic sibling order.' );
assert_same_creative( false, Elementize_Tree::insert_nodes( $page, 'missing', $insert, 'after' ), 'Missing insertion anchors must fail closed.' );

assert_same_creative( true, Elementize_Tree::reorder_children( $page, 'b222', [ 'b3', 'b1', 'b2' ] ), 'Exact child-set reordering should succeed.' );
assert_same_creative( [ 'b3', 'b1', 'b2' ], array_column( Elementize_Tree::find_node( $page, 'b222' )['elements'], 'id' ), 'Reordered children must match the requested exact set.' );
$mismatch = Elementize_Tree::reorder_children( $page, 'b222', [ 'b1', 'b2' ] );
assert_true_creative( is_wp_error( $mismatch ), 'Partial child lists must be rejected rather than silently dropping siblings.' );

$removed = null; $parent = ''; $index = -1;
assert_same_creative( true, Elementize_Tree::remove_node( $page, 'new333', $removed, $parent, $index ), 'Exact node removal should succeed.' );
assert_same_creative( 'new333', $removed['id'], 'Removed node snapshot must be returned intact.' );
assert_same_creative( false, Elementize_Tree::remove_node( $page, 'missing', $removed, $parent, $index ), 'Missing removal targets must fail closed.' );

$setting_result = Elementize_Tree::mutate_setting( $page, 'b1', [ 'title' ], static function( &$value ) { $value = 'Updated'; return true; } );
assert_same_creative( true, $setting_result, 'Exact setting mutation should succeed.' );
assert_same_creative( 'Updated', Elementize_Tree::find_node( $page, 'b1' )['settings']['title'], 'Setting mutation should only change the requested setting.' );

$invalid_tree = [
    [ 'id' => 'dup', 'elType' => 'container', 'settings' => [], 'elements' => [] ],
    [ 'id' => 'dup', 'elType' => 'container', 'settings' => [], 'elements' => [] ],
];
assert_true_creative( is_wp_error( Elementize_Tree::validate( $invalid_tree ) ), 'Duplicate Elementor element IDs must be rejected before save.' );
assert_same_creative( true, Elementize_Tree::validate( $page ), 'A valid mutated tree should pass pre-save structural validation.' );

$aliases = [
    'inserted' => [ 'direct' => 'freshRoot', 'roots' => [ 'freshRoot' ], 'map' => [ 'oldRoot' => 'freshRoot', 'oldChild' => 'freshChild' ] ],
    'copy' => [ 'direct' => 'copyRoot', 'roots' => [ 'copyRoot' ], 'map' => [ 'oldRoot' => 'copyRoot' ] ],
];
assert_same_creative( 'freshRoot', private_creative( 'resolve_ref', [ 'inserted', $aliases ] ), 'Single-root insertion aliases should resolve directly.' );
assert_same_creative( 'freshChild', private_creative( 'resolve_ref', [ 'inserted:oldChild', $aliases ] ), 'Alias:originalElementId must resolve an inserted descendant.' );
assert_same_creative( 'copyRoot', private_creative( 'resolve_ref', [ 'copy', $aliases ] ), 'Duplicate aliases should resolve their cloned root.' );
assert_true_creative( is_wp_error( private_creative( 'resolve_ref', [ 'inserted:missing', $aliases ] ) ), 'Unknown alias descendants must fail closed.' );

$capabilities_source = file_get_contents( __DIR__ . '/../includes/elementize-capabilities.inc' );
$creative_source = file_get_contents( __DIR__ . '/../includes/elementize-creative.inc' );
$design_source = file_get_contents( __DIR__ . '/../includes/elementize-design.inc' );
$template_source = file_get_contents( __DIR__ . '/../includes/elementize-templates.inc' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
assert_true_creative( is_string( $capabilities_source ) && false !== strpos( $capabilities_source, "PROFILE_STANDARD = 'standard'" ), 'Standard editing must remain the server-side default profile.' );
assert_true_creative( false !== strpos( $capabilities_source, 'require_creative' ) && false !== strpos( $capabilities_source, 'elementize_capabilities_changed' ), 'Creative writes must enforce page scope and stale capability revision.' );
assert_true_creative( false !== strpos( $creative_source, 'expected_capability_revision' ) && false !== strpos( $creative_source, 'confirm_creative_write' ), 'Creative transaction route must require explicit capability and confirmation guards.' );
assert_true_creative( false !== strpos( $creative_source, 'ACTIVITY_SNAPSHOT_META' ) && false !== strpos( $creative_source, 'record_activity' ), 'Creative transactions must persist whole-change Activity/Undo state.' );
assert_true_creative( false !== strpos( $design_source, "'__globals__'" ), 'Global Elementor design references must remain read-only.' );
assert_true_creative( false !== strpos( $template_source, 'embedded_template_references' ), 'Embedded/global template dependencies must be detected before insertion.' );
assert_true_creative( is_string( $instructions ) && false !== strpos( $instructions, 'Never enable, disable, or change Creative Control yourself' ), 'The Custom GPT must never switch Creative Control on its own.' );
assert_true_creative( false !== strpos( $instructions, 'Avoid Frankenstein pages' ), 'The GPT contract must contain explicit design-coherence rules.' );
assert_true_creative( false !== strpos( $instructions, 'visual_render_verified=false' ), 'The GPT must not overclaim rendered visual QA.' );

echo "Creative Control contract passed.\n";

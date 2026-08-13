<?php
define( 'ABSPATH', __DIR__ . '/' );
function sanitize_key( $v ): string { return strtolower( preg_replace( '/[^a-z0-9_\-]/i', '', (string) $v ) ); }
function wp_strip_all_tags( $v ): string { return strip_tags( (string) $v ); }
function wp_json_encode( $v, $flags = 0 ) { return json_encode( $v, $flags ); }
require_once __DIR__ . '/../includes/elementize-design.inc';
function fail_scope( string $m ): void { fwrite( STDERR, $m . "\n" ); exit( 1 ); }
function expect_scope( $ok, string $m ): void { if ( ! $ok ) fail_scope( $m ); }
$method = new ReflectionMethod( Elementize_Design::class, 'control' );
$settings = [ 'items' => [
    [ 'text' => 'From scattered security work to one clear workflow.' ],
    [ 'text' => 'Three simple steps keep risks, responsibilities and proof connected.' ],
] ];
$control = $method->invoke( null, 'size', '27ba85fb', 'pix-highlighted-text', [ 'title_size' ], 'h4', $settings );
expect_scope( 'element' === ( $control['control_scope'] ?? null ), 'title_size must be element-scoped.' );
expect_scope( 'all_highlighted_text_items' === ( $control['control_effect'] ?? null ), 'Highlighted title_size must expose its multi-item effect.' );
expect_scope( false === ( $control['line_specific_safe'] ?? true ), 'Multi-item title_size must be explicitly unsafe for line-specific repair.' );
expect_scope( 2 === count( $control['applies_to_visible_text_targets'] ?? [] ), 'Both visible highlighted-text items must be disclosed.' );
expect_scope( [ 'items', 0, 'text' ] === $control['applies_to_visible_text_targets'][0]['setting_path'], 'First visible text path is wrong.' );
expect_scope( [ 'items', 1, 'text' ] === $control['applies_to_visible_text_targets'][1]['setting_path'], 'Second visible text path is wrong.' );
$item = $method->invoke( null, 'color', '27ba85fb', 'pix-highlighted-text', [ 'items', 1, 'item_color' ], 'dark-opacity-6', $settings );
expect_scope( 'repeater_item' === ( $item['control_scope'] ?? null ), 'Integer setting paths must be repeater-item scoped.' );
$adapter = file_get_contents( __DIR__ . '/../includes/runtime/elementize-pixfort-repeater-colors.inc' );
expect_scope( is_string( $adapter ) && false !== strpos( $adapter, "'control_scope' => 'repeater_item'" ), 'Synthetic Pixfort repeater controls must publish repeater-item scope.' );
expect_scope( false !== strpos( $adapter, "'line_specific_safe' => true" ), 'Synthetic Pixfort repeater colors must advertise line-specific safety.' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
expect_scope( is_string( $instructions ) && false !== strpos( $instructions, 'control_scope' ), 'GPT instructions must inspect control scope.' );
expect_scope( false !== strpos( $instructions, 'line_specific_safe=false' ), 'GPT instructions must reject broader controls for line-specific repairs.' );
echo "Design control scope contract passed.\n";

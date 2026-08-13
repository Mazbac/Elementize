<?php
define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/runtime/elementize-pixfort-repeater-colors.inc';
function fail_repeater_path( string $message ): void { fwrite( STDERR, $message . "\n" ); exit( 1 ); }
$method = new ReflectionMethod( Elementize_Pixfort_Repeater_Colors::class, 'repeater_index' );
$cases = [
    [ 0, 0 ],
    [ 7, 7 ],
    [ '0', 0 ],
    [ '7', 7 ],
    [ '01', null ],
    [ '-1', null ],
    [ '1.0', null ],
    [ '1e0', null ],
    [ 'item', null ],
];
foreach ( $cases as [ $input, $expected ] ) {
    $actual = $method->invoke( null, $input );
    if ( $actual !== $expected ) fail_repeater_path( 'Unexpected repeater index normalization for ' . var_export( $input, true ) );
}
$source = file_get_contents( __DIR__ . '/../includes/runtime/elementize-pixfort-repeater-colors.inc' );
if ( false === strpos( $source, 'self::repeater_index( $path[1] )' ) ) fail_repeater_path( 'apply_style must normalize Action repeater indices.' );
if ( false !== strpos( $source, '! is_int( $path[1] )' ) ) fail_repeater_path( 'Strict integer-only Action repeater path guard returned.' );
echo "Pixfort repeater Action path contract passed.\n";

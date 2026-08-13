<?php

define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-coherence.inc';

function fail_coherence( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}
function has_finding( array $result, string $type ): bool {
    foreach ( (array) ( $result['findings'] ?? [] ) as $finding ) if ( ( $finding['type'] ?? '' ) === $type ) return true;
    return false;
}
function section_with_button( string $id, string $color, int $padding ): array {
    return [
        'id' => 's' . $id, 'elType' => 'container', 'settings' => [ 'background_color' => '#fff' ],
        'elements' => [ [ 'id' => $id, 'elType' => 'widget', 'widgetType' => 'pix-button', 'settings' => [ 'btn_text' => 'Demo', 'btn_color' => $color, 'btn_size' => 'lg', '_padding' => [ 'bottom' => (string) $padding ] ] ] ],
    ];
}
$profile = [ 'controls' => [], 'controls_truncated' => false, 'radius_scale' => [ [ 'value' => '10', 'count' => 8 ] ], 'spacing_scale' => [] ];
$same = [ section_with_button( 'a', 'primary', 0 ), section_with_button( 'b', 'primary', 10 ), section_with_button( 'c', 'primary', 20 ) ];
$r = Elementize_Coherence::analyze( $same, $profile );
if ( 1 !== (int) $r['systems']['buttons']['style_group_count'] ) fail_coherence( 'Wrapper padding incorrectly fragmented the button system.' );
if ( has_finding( $r, 'button_system_fragmentation' ) ) fail_coherence( 'Equivalent buttons produced a fragmentation finding.' );
$split = [ section_with_button( 'a', 'primary', 0 ), section_with_button( 'b', 'primary', 10 ), section_with_button( 'c', 'secondary', 20 ) ];
$r = Elementize_Coherence::analyze( $split, $profile );
if ( ! has_finding( $r, 'button_system_fragmentation' ) ) fail_coherence( 'Real semantic button split was not detected.' );
$radius_profile = [ 'controls' => [], 'controls_truncated' => false, 'radius_scale' => [ [ 'value' => '4', 'count' => 2 ], [ 'value' => '8', 'count' => 2 ], [ 'value' => '12', 'count' => 2 ], [ 'value' => '16', 'count' => 2 ] ], 'spacing_scale' => [] ];
$r = Elementize_Coherence::analyze( [], $radius_profile );
if ( ! has_finding( $r, 'radius_system_fragmentation' ) ) fail_coherence( 'Complete fragmented radius evidence was not detected.' );
$radius_profile['controls_truncated'] = true;
$r = Elementize_Coherence::analyze( [], $radius_profile );
if ( has_finding( $r, 'radius_system_fragmentation' ) ) fail_coherence( 'Truncated evidence produced a radius fragmentation claim.' );
$backgrounds = [];
for ( $i = 0; $i < 6; $i++ ) $backgrounds[] = [ 'id' => 'bg' . $i, 'elType' => 'container', 'settings' => [ 'background_color' => $i < 4 ? '#f8f9fa' : '#fff' ], 'elements' => [] ];
$r = Elementize_Coherence::analyze( $backgrounds, $profile );
if ( ! has_finding( $r, 'background_rhythm_monotony_candidate' ) ) fail_coherence( 'Four-section explicit background run was not surfaced.' );
echo "Coherence engine contract OK\n";

<?php

define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-visual-qa.inc';

function fail_chatgpt_vision_bounds_contract( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$visual = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
$handoff = file_get_contents( __DIR__ . '/../includes/elementize-chatgpt-vision.inc' );
if ( ! is_string( $visual ) || ! is_string( $handoff ) ) fail_chatgpt_vision_bounds_contract( 'Could not read unified native visual bounds sources.' );
$method = new ReflectionMethod( Elementize_Visual_QA::class, 'bound_screenshot' );
if ( ! $method->isPublic() || ! $method->isStatic() ) fail_chatgpt_vision_bounds_contract( 'Unified screenshot bounds helper must be public static for native handoff reuse.' );
foreach ( [ 'bound_screenshot', "'available' => false", "'trimmed' => false", 'imagecrop', 'imagepng' ] as $needle ) {
    if ( false === strpos( $visual, $needle ) ) fail_chatgpt_vision_bounds_contract( 'Missing unified bounds marker: ' . $needle );
}
foreach ( [ 'Elementize_Visual_QA::bound_screenshot', "'capture_bounds_ready'", "'capture_original_height'", "'capture_bounded_height'", "'content_bounded_tall_desktop'" ] as $needle ) {
    if ( false === strpos( $handoff, $needle ) ) fail_chatgpt_vision_bounds_contract( 'Missing native handoff bounds marker: ' . $needle );
}
if ( file_exists( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision-bounds.inc' ) ) {
    fail_chatgpt_vision_bounds_contract( 'Separate native bounds runtime must stay removed after consolidation.' );
}
if ( false !== strpos( $visual, 'update_post_meta' ) || false !== strpos( $handoff, 'wp_update_post' ) ) {
    fail_chatgpt_vision_bounds_contract( 'Native bounds flow must remain read-only.' );
}
echo "ChatGPT native vision bounds contract OK\n";

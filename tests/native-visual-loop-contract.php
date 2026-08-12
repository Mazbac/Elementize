<?php
function fail_native_visual_loop( string $message ): void { fwrite( STDERR, $message . "\n" ); exit( 1 ); }
$schema = file_get_contents( __DIR__ . '/../config/gpt/actions.openapi.yaml' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
$visual = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
$handoff = file_get_contents( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision.inc' );
$bounds = file_get_contents( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision-bounds.inc' );
$plugin = file_get_contents( __DIR__ . '/../elementize.php' );
foreach ( [ 'openaiFileResponse:', 'format: byte', 'native ChatGPT visual QA' ] as $needle ) {
    if ( false === strpos( $schema, $needle ) ) fail_native_visual_loop( "Canonical Action schema missing: {$needle}" );
}
foreach ( [ 'provider=chatgpt_native_vision', 'capture_bounds_ready=true', 'openaiFileResponse', 'screenshot.png', 'visual_analysis_verified=false' ] as $needle ) {
    if ( false === strpos( $instructions, $needle ) ) fail_native_visual_loop( "GPT instructions missing: {$needle}" );
}
foreach ( [ 'settle_preview_motion', 'elementize-visual-settle-motion', 'animation-duration:0s', 'transition-duration:0s' ] as $needle ) {
    if ( false === strpos( $visual, $needle ) ) fail_native_visual_loop( "Motion-settled capture contract missing: {$needle}" );
}
foreach ( [ 'openaiFileResponse', "'mime_type' => 'application/zip'", "'provider' => 'chatgpt_native_vision'" ] as $needle ) {
    if ( false === strpos( $handoff, $needle ) ) fail_native_visual_loop( "Native handoff contract missing: {$needle}" );
}
foreach ( [ 'capture_bounds_ready', 'content_bounded_tall_desktop', 'capture_bounded_height' ] as $needle ) {
    if ( false === strpos( $bounds, $needle ) ) fail_native_visual_loop( "Content-bounds contract missing: {$needle}" );
}
if ( false !== strpos( $plugin, 'elementize-chatgpt-action-schema.inc' ) ) fail_native_visual_loop( 'Runtime schema patch must not shadow canonical OpenAPI.' );
if ( false !== strpos( $schema, 'optionally analyze it locally' ) ) fail_native_visual_loop( 'Legacy local-analysis wording remains in canonical schema.' );
if ( strlen( $instructions ) > 8000 ) fail_native_visual_loop( 'GPT instructions exceed 8000 characters.' );
echo "Native visual repair loop contract passed.\n";

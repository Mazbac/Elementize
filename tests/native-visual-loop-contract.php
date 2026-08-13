<?php
function fail_native_visual_loop( string $message ): void { fwrite( STDERR, $message . "\n" ); exit( 1 ); }
$schema = file_get_contents( __DIR__ . '/../config/gpt/actions.openapi.yaml' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
$visual = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
$handoff = file_get_contents( __DIR__ . '/../includes/elementize-chatgpt-vision.inc' );
$plugin = file_get_contents( __DIR__ . '/../elementize.php' );
$bootstrap = file_get_contents( __DIR__ . '/../includes/elementize-bootstrap.inc' );
foreach ( [ 'openaiFileResponse:', 'format: byte', 'native ChatGPT visual QA' ] as $needle ) {
    if ( false === strpos( $schema, $needle ) ) fail_native_visual_loop( "Canonical Action schema missing: {$needle}" );
}
foreach ( [ 'provider=chatgpt_native_vision', 'capture_bounds_ready=true', 'openaiFileResponse', 'screenshot.png', 'visual_analysis_verified=false' ] as $needle ) {
    if ( false === strpos( $instructions, $needle ) ) fail_native_visual_loop( "GPT instructions missing: {$needle}" );
}
foreach ( [ 'Elementize_ChatGPT_Vision::handle( $request )', 'settle_preview_motion', 'elementize-visual-settle-motion', 'bound_screenshot', 'animation-duration:0s', 'transition-duration:0s' ] as $needle ) {
    if ( false === strpos( $visual, $needle ) ) fail_native_visual_loop( "Native capture contract missing: {$needle}" );
}
foreach ( [ 'openaiFileResponse', "'mime_type' => 'application/zip'", "'provider' => 'chatgpt_native_vision'", 'capture_bounds_ready', 'content_bounded_tall_' ] as $needle ) {
    if ( false === strpos( $handoff, $needle ) ) fail_native_visual_loop( "Native handoff contract missing: {$needle}" );
}
foreach ( [ 'OLLAMA', 'ollama', 'gemma3', 'local_vision_model', 'ELEMENTIZE_LOCAL_VISION_MODEL', 'ELEMENTIZE_OLLAMA_URL', '/api/chat', '/api/tags' ] as $legacy ) {
    if ( false !== strpos( $visual, $legacy ) ) fail_native_visual_loop( "Legacy local vision remains in Visual QA: {$legacy}" );
}
foreach ( [ 'elementize-chatgpt-vision-dispatch.inc', 'elementize-chatgpt-vision-bounds.inc', 'Elementize_ChatGPT_Vision::init();' ] as $legacy ) {
    if ( false !== strpos( $plugin, $legacy ) ) fail_native_visual_loop( "Superseded native vision shim remains loaded: {$legacy}" );
}
if ( file_exists( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision-dispatch.inc' ) ) fail_native_visual_loop( 'Superseded native dispatch shim still exists.' );
if ( file_exists( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision-bounds.inc' ) ) fail_native_visual_loop( 'Superseded native bounds shim still exists.' );
if ( ! is_string( $bootstrap ) || false === strpos( $bootstrap, "require_once __DIR__ . '/elementize-chatgpt-vision.inc';" ) ) fail_native_visual_loop( 'Bootstrap does not load the consolidated native vision handoff.' );
if ( false !== strpos( $plugin, 'elementize-chatgpt-action-schema.inc' ) ) fail_native_visual_loop( 'Runtime schema patch must not shadow canonical OpenAPI.' );
if ( false !== strpos( $schema, 'optionally analyze it locally' ) ) fail_native_visual_loop( 'Legacy local-analysis wording remains in canonical schema.' );
if ( strlen( $instructions ) > 8000 ) fail_native_visual_loop( 'GPT instructions exceed 8000 characters.' );
echo "Native visual repair loop contract passed.\n";

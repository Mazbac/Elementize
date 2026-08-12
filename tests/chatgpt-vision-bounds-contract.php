<?php

define( 'ABSPATH', __DIR__ . '/../' );

function fail_chatgpt_vision_bounds_contract( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$source = file_get_contents( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision-bounds.inc' );
if ( ! is_string( $source ) ) fail_chatgpt_vision_bounds_contract( 'Could not read native ChatGPT vision bounds runtime.' );

foreach ( [
    "final class Elementize_ChatGPT_Vision_Bounds",
    "add_filter( 'rest_dispatch_request'",
    "'chatgpt_native_vision'",
    "'capture_state'",
    "'complete'",
    "'chatgpt_vision_handoff_ready'",
    "trim_trailing_background",
    "Elementize_ChatGPT_Vision::before_callbacks",
    "'capture_mode' = 'content_bounded_tall_desktop'",
    "'capture_original_height'",
    "'capture_bounded_height'",
] as $needle ) {
    if ( false === strpos( $source, $needle ) ) fail_chatgpt_vision_bounds_contract( 'Missing native bounds marker: ' . $needle );
}

if ( false !== strpos( $source, 'update_post_meta' ) || false !== strpos( $source, 'wp_update_post' ) ) {
    fail_chatgpt_vision_bounds_contract( 'Native bounds adapter must remain read-only.' );
}

$plugin = file_get_contents( __DIR__ . '/../elementize.php' );
if ( ! is_string( $plugin )
    || false === strpos( $plugin, "includes/runtime/elementize-chatgpt-vision-bounds.inc" )
    || false === strpos( $plugin, 'Elementize_ChatGPT_Vision_Bounds::init();' ) ) {
    fail_chatgpt_vision_bounds_contract( 'Native ChatGPT vision bounds adapter is not loaded by the plugin.' );
}

echo "ChatGPT native vision bounds contract OK\n";

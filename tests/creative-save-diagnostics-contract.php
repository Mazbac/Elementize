<?php

define( 'ABSPATH', __DIR__ . '/../' );

function fail_creative_save_diagnostics_contract( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$runtime = file_get_contents( __DIR__ . '/../includes/runtime/elementize-creative-save-diagnostics.inc' );
if ( ! is_string( $runtime ) ) fail_creative_save_diagnostics_contract( 'Could not read Creative save diagnostics runtime.' );

foreach ( [
    'final class Elementize_Creative_Save_Diagnostics',
    "rest_request_before_callbacks",
    "elementor/document/save/data",
    "PHP_INT_MAX - 1",
    "rest_request_after_callbacks",
    "elementize_creative_save_input_changed",
    "save_filter_diagnostics",
    "read_only_diagnostic",
    "MAX_DIFFS",
] as $needle ) {
    if ( false === strpos( $runtime, $needle ) ) fail_creative_save_diagnostics_contract( 'Missing Creative save diagnostics marker: ' . $needle );
}

foreach ( [ 'wp_update_post', 'update_post_meta', 'delete_post_meta', 'Document::save', '->save(' ] as $forbidden ) {
    if ( false !== strpos( $runtime, $forbidden ) ) fail_creative_save_diagnostics_contract( 'Diagnostics runtime must remain read-only: ' . $forbidden );
}

$plugin = file_get_contents( __DIR__ . '/../elementize.php' );
if ( ! is_string( $plugin )
    || false === strpos( $plugin, "includes/runtime/elementize-creative-save-diagnostics.inc" )
    || false === strpos( $plugin, 'Elementize_Creative_Save_Diagnostics::init();' ) ) {
    fail_creative_save_diagnostics_contract( 'Creative save diagnostics runtime is not loaded by the plugin.' );
}

echo "Creative save diagnostics contract OK\n";

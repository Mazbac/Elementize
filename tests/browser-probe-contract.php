<?php

define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-visual-qa.inc';

function fail_browser_probe( string $message ): void { fwrite( STDERR, $message . PHP_EOL ); exit( 1 ); }

$source = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
if ( ! preg_match( "/base64_decode\\( '([A-Za-z0-9+\\/=]+)', true \\)/", $source, $encoded ) ) fail_browser_probe( 'Embedded browser probe was not found.' );
$decoded_probe = base64_decode( $encoded[1], true );
if ( ! is_string( $decoded_probe ) ) fail_browser_probe( 'Embedded browser probe was not valid base64.' );
$combined = $source . "\n" . $decoded_probe;
foreach ( [
    "'interaction_probe'",
    "'motion'",
    "'settled', 'live'",
    "'passive', 'safe'",
    'elementize_visual_motion',
    'elementize_visual_interaction',
    'browser_probe_script',
    'receive_browser_probe',
    'browser_probe_for_job',
    'elementize_visual_browser_probe_v2_',
    "'wp_ajax_nopriv_elementize_visual_probe'",
    'elementize_visual_viewport',
    'runner finished but signed browser diagnostics',
    'restorationFailures',
    '!e.closest("form")',
    'button[aria-expanded]',
    'warmMotion',
    'scrollTo(0,y)',
    'docOverflow',
    '.slice(0,4)',
] as $needle ) {
    if ( false === strpos( $combined, $needle ) ) fail_browser_probe( 'Missing bounded browser QA marker: ' . $needle );
}
if ( false !== strpos( $decoded_probe, 'document.querySelectorAll("button,a")' ) ) fail_browser_probe( 'Generic unrestricted click probing must not be introduced.' );

if ( false !== strpos( $source, '--dump-dom' ) ) fail_browser_probe( 'Legacy Chrome stdout DOM probe returned.' );
if ( false === strpos( $decoded_probe, 'fetch(cfg.endpoint' ) ) fail_browser_probe( 'Browser diagnostics are not reported through the signed telemetry endpoint.' );

echo "Browser probe contract OK\n";

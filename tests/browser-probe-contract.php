<?php

define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-visual-qa.inc';

function fail_browser_probe( string $message ): void { fwrite( STDERR, $message . PHP_EOL ); exit( 1 ); }

$fixture = [
    'version' => 2,
    'viewport' => [ 'width' => 390, 'height' => 1200 ],
    'document' => [ 'horizontalOverflow' => false ],
    'counts' => [ 'overflow' => 0, 'tinyInteractive' => 1, 'hiddenMotion' => 0, 'anchorIssues' => 0 ],
    'interactions' => [ 'mode' => 'safe', 'tested' => 2, 'passed' => 2, 'failed' => 0, 'restorationFailures' => 0 ],
];
$tmp = tempnam( sys_get_temp_dir(), 'elementize-probe-' );
if ( false === $tmp ) fail_browser_probe( 'Could not create probe fixture.' );
$html = '<html><body><script type="application/json" id="elementize-browser-qa-result">' . json_encode( $fixture ) . '</script></body></html>';
file_put_contents( $tmp, $html );
$result = Elementize_Visual_QA::browser_probe_result( $tmp );
@unlink( $tmp );
if ( empty( $result['verified'] ) || empty( $result['available'] ) ) fail_browser_probe( 'Browser probe fixture was not verified.' );
if ( 2 !== (int) ( $result['diagnostics']['interactions']['passed'] ?? -1 ) ) fail_browser_probe( 'Interaction diagnostics did not round-trip.' );

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
    'restorationFailures',
    '!e.closest("form")',
    'button[aria-expanded]',
] as $needle ) {
    if ( false === strpos( $combined, $needle ) ) fail_browser_probe( 'Missing bounded browser QA marker: ' . $needle );
}
if ( false !== strpos( $decoded_probe, 'document.querySelectorAll("button,a")' ) ) fail_browser_probe( 'Generic unrestricted click probing must not be introduced.' );

echo "Browser probe contract OK\n";

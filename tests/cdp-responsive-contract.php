<?php

define( 'ABSPATH', __DIR__ . '/../' );
define( 'ELEMENTIZE_DIR', dirname( __DIR__ ) );
if ( ! class_exists( 'WP_Error' ) ) {
    class WP_Error {
        private string $message;
        public function __construct( string $code = '', string $message = '' ) { $this->message = $message; }
        public function get_error_message(): string { return $this->message; }
    }
}
if ( ! function_exists( 'wp_unslash' ) ) { function wp_unslash( $value ) { return $value; } }
if ( ! function_exists( 'esc_url_raw' ) ) { function esc_url_raw( $url, $protocols = null ) { $parts = parse_url( (string) $url ); return is_array( $parts ) && isset( $parts['scheme'], $parts['host'] ) ? (string) $url : ''; } }
if ( ! function_exists( 'wp_parse_url' ) ) { function wp_parse_url( $url ) { return parse_url( $url ); } }
require_once __DIR__ . '/../includes/elementize-visual-qa.inc';
function fail_cdp_contract( string $message ): void { fwrite( STDERR, $message . PHP_EOL ); exit( 1 ); }

$viewport = new ReflectionMethod( Elementize_Visual_QA::class, 'viewport_spec' );
foreach ( [ 'desktop' => [1440,900,12000], 'tablet' => [768,1024,16000], 'mobile' => [390,844,22000] ] as $name => $expected ) {
    $actual = $viewport->invoke( null, $name );
    if ( [ (int) $actual['width'], (int) $actual['height'], (int) $actual['max_capture_height'] ] !== $expected ) fail_cdp_contract( 'Viewport contract failed for ' . $name );
}
$source = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
$runner = file_get_contents( __DIR__ . '/../assets/runtime/elementize-cdp-capture.mjs' );
foreach ( [ "private const VERSION = '10'", 'local_chromium_cdp_async', 'cdp_capture_ready', 'viewport_exact', 'windows_cdp_runner', 'unix_cdp_runner', 'document_capture_complete' ] as $needle ) {
    if ( false === strpos( $source, $needle ) ) fail_cdp_contract( 'Visual QA CDP marker missing: ' . $needle );
}
foreach ( [ 'Emulation.setDeviceMetricsOverride', 'Emulation.setTouchEmulationEnabled', 'Page.captureScreenshot', 'captureBeyondViewport: true', 'scrollHeight', "from 'node:child_process'", '/json/new?about:blank', 'probeReady' ] as $needle ) {
    if ( false === strpos( $runner, $needle ) ) fail_cdp_contract( 'CDP runner marker missing: ' . $needle );
}
foreach ( [ 'puppeteer', 'playwright', 'chrome-remote-interface', 'node_modules' ] as $forbidden ) {
    if ( false !== stripos( $runner, $forbidden ) ) fail_cdp_contract( 'CDP runner gained an external package dependency: ' . $forbidden );
}

$method = new ReflectionMethod( Elementize_Visual_QA::class, 'windows_cdp_runner' );
$paths = [ 'screenshot' => 'C:\\Temp\\x.png', 'profile' => 'C:\\Temp\\profile', 'log' => 'C:\\Temp\\x.log', 'done' => 'C:\\Temp\\x.done', 'runner' => 'C:\\Temp\\x.cmd' ];
$budget_method = new ReflectionMethod( Elementize_Visual_QA::class, 'capture_settle_ms' );
$live_safe_budget = (int) $budget_method->invoke( null, 'live', 'safe' );
$batch = $method->invoke( null, 'C:\\Program Files\\nodejs\\node.exe', 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', 'https://example.com/', $paths, 'mobile', 'live', 'safe' );
if ( $batch instanceof WP_Error || false === strpos( $batch, ' 390 844 22000 ' . $live_safe_budget ) ) fail_cdp_contract( 'Windows CDP runner did not preserve exact mobile metrics and current bounded live-safe completion budget.' );

echo "CDP responsive capture contract OK\n";

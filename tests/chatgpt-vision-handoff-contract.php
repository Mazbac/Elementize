<?php

define( 'ABSPATH', __DIR__ . '/../' );

if ( ! class_exists( 'WP_Error' ) ) {
    class WP_Error {
        private string $code;
        private string $message;
        public function __construct( string $code = '', string $message = '' ) { $this->code = $code; $this->message = $message; }
        public function get_error_message(): string { return $this->message; }
    }
}

require_once __DIR__ . '/../includes/runtime/elementize-chatgpt-vision.inc';

function fail_chatgpt_vision_contract( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$zip_method = new ReflectionMethod( Elementize_ChatGPT_Vision::class, 'zip_single_file' );
$zip_method->setAccessible( true );
$fixture = "ELEMENTIZE_NATIVE_VISION_FIXTURE\x00\x01\x02";
$zip = $zip_method->invoke( null, 'screenshot.png', $fixture );
if ( $zip instanceof WP_Error || ! is_string( $zip ) ) fail_chatgpt_vision_contract( 'Single-file ZIP helper did not return archive bytes.' );
if ( "PK\x03\x04" !== substr( $zip, 0, 4 ) ) fail_chatgpt_vision_contract( 'ZIP local-file header is invalid.' );
if ( false === strpos( $zip, 'screenshot.png' ) ) fail_chatgpt_vision_contract( 'ZIP does not contain the required screenshot.png entry name.' );
if ( false === strpos( $zip, $fixture ) ) fail_chatgpt_vision_contract( 'ZIP does not contain the exact screenshot fixture bytes.' );
if ( false === strpos( $zip, "PK\x01\x02" ) ) fail_chatgpt_vision_contract( 'ZIP central-directory header is missing.' );
if ( false === strpos( $zip, "PK\x05\x06" ) ) fail_chatgpt_vision_contract( 'ZIP end-of-central-directory record is missing.' );

$source = file_get_contents( __DIR__ . '/../includes/runtime/elementize-chatgpt-vision.inc' );
if ( ! is_string( $source ) ) fail_chatgpt_vision_contract( 'Could not read native ChatGPT vision runtime source.' );
foreach ( [
    'openaiFileResponse',
    "'mime_type' => 'application/zip'",
    "private const SCREENSHOT_ENTRY = 'screenshot.png'",
    "set_param( 'analyze', false )",
    "'provider' => 'chatgpt_native_vision'",
    "'visual_analysis_verified' => false",
    "'preview_url_exposed' => false",
] as $needle ) {
    if ( false === strpos( $source, $needle ) ) fail_chatgpt_vision_contract( 'Missing native ChatGPT vision contract marker: ' . $needle );
}

$plugin = file_get_contents( __DIR__ . '/../elementize.php' );
if ( ! is_string( $plugin ) || false === strpos( $plugin, "includes/runtime/elementize-chatgpt-vision.inc" ) || false === strpos( $plugin, 'Elementize_ChatGPT_Vision::init();' ) ) {
    fail_chatgpt_vision_contract( 'Native ChatGPT vision adapter is not loaded by the plugin.' );
}

echo "ChatGPT native vision handoff contract OK\n";

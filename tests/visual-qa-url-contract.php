<?php

define( 'ABSPATH', __DIR__ . '/../' );

if ( ! class_exists( 'WP_Error' ) ) {
    class WP_Error {
        private string $code;
        private string $message;
        public function __construct( string $code = '', string $message = '' ) {
            $this->code = $code;
            $this->message = $message;
        }
        public function get_error_message(): string { return $this->message; }
    }
}

if ( ! function_exists( 'wp_unslash' ) ) {
    function wp_unslash( $value ) { return is_string( $value ) ? stripslashes( $value ) : $value; }
}

if ( ! function_exists( 'esc_url_raw' ) ) {
    function esc_url_raw( $url, $protocols = null ) {
        $url = trim( (string) $url );
        if ( '' === $url || preg_match( '/[\r\n]/', $url ) ) return '';
        $parts = parse_url( $url );
        if ( ! is_array( $parts ) || empty( $parts['scheme'] ) || empty( $parts['host'] ) ) return '';
        $scheme = strtolower( (string) $parts['scheme'] );
        $allowed = is_array( $protocols ) ? $protocols : [ 'http', 'https' ];
        if ( ! in_array( $scheme, $allowed, true ) ) return '';
        return $url;
    }
}

if ( ! function_exists( 'wp_parse_url' ) ) {
    function wp_parse_url( $url ) { return parse_url( $url ); }
}

require_once __DIR__ . '/../includes/elementize-visual-qa.inc';

function fail_visual_qa_url_contract( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$normalize = new ReflectionMethod( Elementize_Visual_QA::class, 'normalize_capture_url' );
$normalize->setAccessible( true );

$cases = [
    'https://example.com/path' => 'https://example.com/path',
    '[label](https://example.com/path)' => 'https://example.com/path',
    '[https://mijn-ibp.local/contact/](https://mijn-ibp.local/contact/)' => 'https://mijn-ibp.local/contact/',
    '"[Contact](https://mijn-ibp.local/contact/)"' => 'https://mijn-ibp.local/contact/',
    '[https://example.com/path]' => 'https://example.com/path',
    '<https://example.com/path>' => 'https://example.com/path',
    'https://example.com/?a=1&amp;b=2' => 'https://example.com/?a=1&b=2',
    'javascript:alert(1)' => '',
    'ftp://example.com/file' => '',
    '[label](javascript:alert(1))' => '',
];

foreach ( $cases as $input => $expected ) {
    $actual = $normalize->invoke( null, $input );
    if ( $actual !== $expected ) {
        fail_visual_qa_url_contract( 'normalize_capture_url failed for ' . var_export( $input, true ) . ': expected ' . var_export( $expected, true ) . ', got ' . var_export( $actual, true ) );
    }
}

$windows_runner = new ReflectionMethod( Elementize_Visual_QA::class, 'windows_runner' );
$windows_runner->setAccessible( true );
$paths = [
    'screenshot' => 'C:\\Temp\\elementize-test.png',
    'profile' => 'C:\\Temp\\elementize-profile',
    'log' => 'C:\\Temp\\elementize-test.log',
    'done' => 'C:\\Temp\\elementize-test.done',
    'runner' => 'C:\\Temp\\elementize-test.cmd',
];
$runner = $windows_runner->invoke(
    null,
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    '[Contact](https://mijn-ibp.local/contact/)',
    $paths
);
if ( $runner instanceof WP_Error ) fail_visual_qa_url_contract( 'windows_runner rejected a normalizable Markdown capture URL.' );
if ( false !== strpos( $runner, '[Contact](' ) ) fail_visual_qa_url_contract( 'Markdown capture URL leaked into the Windows runner.' );
if ( false === strpos( $runner, '"https://mijn-ibp.local/contact/"' ) ) fail_visual_qa_url_contract( 'Normalized raw capture URL was not written to the Windows runner.' );

$source = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
if ( ! is_string( $source ) ) fail_visual_qa_url_contract( 'Could not read Visual QA runtime source.' );
foreach ( [
    '$candidate_url =',
    '$url = self::normalize_capture_url( $candidate_url );',
    'self::start_capture_job( $url,',
    "private const JOB_PREFIX = 'elementize_visual_qa_job_v6_'",
    'Start-Process -FilePath',
    "getenv( 'ComSpec' )",
] as $needle ) {
    if ( false === strpos( $source, $needle ) ) fail_visual_qa_url_contract( 'Missing Visual QA v6 safety/launcher marker: ' . $needle );
}
if ( false !== strpos( $source, 'start "" /b' ) ) fail_visual_qa_url_contract( 'Broken cmd.exe start /b launcher returned.' );

foreach ( [ 'settle_preview_motion', 'elementize-visual-settle-motion', '.animate-in,.group-animate-in,.animating,[data-anim-type]', "[ 'draft', 'publish' ]" ] as $motion_needle ) {
    if ( false === strpos( $source, $motion_needle ) ) fail_visual_qa_url_contract( 'Missing deterministic motion-settling marker: ' . $motion_needle );
}

echo "Visual QA URL contract OK\n";

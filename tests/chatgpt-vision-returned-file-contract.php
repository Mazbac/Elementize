<?php
define( 'ABSPATH', __DIR__ . '/../' );
$GLOBALS['elz_transients'] = [];
$GLOBALS['elz_temp_dir'] = sys_get_temp_dir() . DIRECTORY_SEPARATOR . 'elz-returned-' . getmypid() . DIRECTORY_SEPARATOR;
@mkdir( $GLOBALS['elz_temp_dir'], 0777, true );
class WP_Error { private string $code; public function __construct( string $code = '', string $message = '', array $data = [] ) { $this->code = $code; } public function get_error_code(): string { return $this->code; } }
class Elementize_Core { public const NS = 'elementize/v1'; public static string $hash = ''; public static function elements( int $id ): array { return [ 'elements' => [ [ 'id' => 'fixture' ] ] ]; } public static function hash_elements( array $elements ): string { return self::$hash; } }
class Elementize_Capabilities { public static array $state = [ 'creative_enabled' => true, 'scope_page_id' => 953851, 'revision' => 9 ]; public static function state(): array { return self::$state; } }
class Elementize_Onboarding { public static string $origin = 'https://public.example'; public static function public_api_origin(): string { return self::$origin; } }
function trailingslashit( $v ): string { return rtrim( (string) $v, "/\\" ) . DIRECTORY_SEPARATOR; }
function get_temp_dir(): string { return $GLOBALS['elz_temp_dir']; }
function set_transient( $k, $v, $ttl ): bool { $GLOBALS['elz_transients'][$k] = $v; return true; }
function get_transient( $k ) { return $GLOBALS['elz_transients'][$k] ?? false; }
function delete_transient( $k ): bool { unset( $GLOBALS['elz_transients'][$k] ); return true; }
function wp_normalize_path( $v ): string { return str_replace( '\\', '/', (string) $v ); }
function absint( $v ): int { return abs( (int) $v ); }
function sanitize_textarea_field( $v ): string { return (string) $v; }
function is_wp_error( $v ): bool { return $v instanceof WP_Error; }
require_once __DIR__ . '/../includes/elementize-chatgpt-vision.inc';
function fail_returned_file( string $m ): void { fwrite( STDERR, $m . PHP_EOL ); exit( 1 ); }
$zip_method = new ReflectionMethod( Elementize_ChatGPT_Vision::class, 'zip_single_file' );
$archive = $zip_method->invoke( null, 'screenshot.png', "PNG_FIXTURE\x00\x01" );
if ( $archive instanceof WP_Error || ! is_string( $archive ) ) fail_returned_file( 'Could not create fixture ZIP.' );
$create = new ReflectionMethod( Elementize_ChatGPT_Vision::class, 'create_returned_file' );
Elementize_Core::$hash = str_repeat( 'a', 64 );
$url = $create->invoke( null, $archive, 'job-abc', 953851, Elementize_Core::$hash, 9 );
if ( ! preg_match( '#^https://public\.example/wp-json/elementize/v1/visual-qa-files/([a-f0-9]{64})$#', (string) $url, $m ) ) fail_returned_file( 'Signed returned-file URL is invalid.' );
$token = $m[1];
$meta_method = new ReflectionMethod( Elementize_ChatGPT_Vision::class, 'returned_file_meta' );
$meta = $meta_method->invoke( null, $token );
if ( $meta instanceof WP_Error ) fail_returned_file( 'Fresh returned-file token was rejected.' );
if ( strlen( $archive ) !== ( $meta['size'] ?? null ) || hash( 'sha256', $archive ) !== ( $meta['sha256'] ?? null ) ) fail_returned_file( 'Returned-file metadata integrity changed.' );
if ( ! is_file( $meta['path'] ?? '' ) || file_get_contents( $meta['path'] ) !== $archive ) fail_returned_file( 'Returned ZIP bytes were not persisted exactly.' );
file_put_contents( $meta['path'], 'tampered' );
$tampered = $meta_method->invoke( null, $token );
if ( ! $tampered instanceof WP_Error || 'elementize_visual_file_changed' !== $tampered->get_error_code() ) fail_returned_file( 'Tampered returned file was not rejected.' );
$stale_url = $create->invoke( null, $archive, 'job-stale', 953851, Elementize_Core::$hash, 9 );
preg_match( '#([a-f0-9]{64})$#', (string) $stale_url, $stale_match );
Elementize_Core::$hash = str_repeat( 'd', 64 );
$stale = $meta_method->invoke( null, $stale_match[1] ?? '' );
if ( ! $stale instanceof WP_Error || 'elementize_visual_file_stale' !== $stale->get_error_code() ) fail_returned_file( 'Changed page state did not invalidate the returned file.' );
Elementize_Core::$hash = str_repeat( 'a', 64 );
$expiry_url = $create->invoke( null, $archive, 'job-expiry', 953851, Elementize_Core::$hash, 9 );
preg_match( '#([a-f0-9]{64})$#', (string) $expiry_url, $expiry_match );
$expiry_token = $expiry_match[1] ?? '';
$expiry_key = array_key_first( $GLOBALS['elz_transients'] );
if ( '' === $expiry_token || ! is_string( $expiry_key ) ) fail_returned_file( 'Could not prepare expiry fixture.' );
$GLOBALS['elz_transients'][ $expiry_key ]['expires_at'] = time() - 1;
$expired = $meta_method->invoke( null, $expiry_token );
if ( ! $expired instanceof WP_Error || 'elementize_visual_file_expired' !== $expired->get_error_code() ) fail_returned_file( 'Expired returned file token was not rejected.' );
Elementize_Onboarding::$origin = '';
if ( '' !== $create->invoke( null, $archive, 'job-fallback', 953851, str_repeat( 'b', 64 ), 9 ) ) fail_returned_file( 'Missing public origin did not select inline fallback.' );
$source = file_get_contents( __DIR__ . '/../includes/elementize-chatgpt-vision.inc' );
foreach ( [ "private const VERSION = '2'", 'visual-qa-files/(?P<token>[a-f0-9]{64})', 'rest_pre_serve_request', 'Content-Type: application/zip', 'Content-Disposition: attachment', "'signed_url'", "'inline_fallback'", 'chatgpt_vision_handoff_file_sha256' ] as $needle ) {
    if ( false === strpos( (string) $source, $needle ) ) fail_returned_file( 'Missing returned-file transport marker: ' . $needle );
}
foreach ( glob( $GLOBALS['elz_temp_dir'] . '*' ) as $path ) @unlink( $path );
@rmdir( $GLOBALS['elz_temp_dir'] );
echo "ChatGPT returned-file transport contract OK\n";

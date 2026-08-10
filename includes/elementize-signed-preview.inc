<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Signed_Preview {
    private const VERSION = '0.12.1';
    private const NS = 'elementize/v1';
    private const META_MANAGED = '_elementize_managed';
    private const OPT_PUBLIC = 'elementize_public_api_origin';
    private const TTL = 600;

    private static ?array $validated = null;

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 275, 3 );
        add_action( 'pre_get_posts', [ self::class, 'allow_signed_draft_query' ], 1 );
        add_action( 'template_redirect', [ self::class, 'guard_preview_request' ], 0 );
        add_filter( 'redirect_canonical', [ self::class, 'disable_canonical_for_preview' ], 10, 2 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/state$#', $route, $matches ) ) return $response;
        if ( $response->get_status() >= 400 ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $id = absint( $matches[1] ?? 0 );
        $post = get_post( $id );
        $hash = (string) ( $data['lifecycle']['content_hash'] ?? $data['layout']['content_hash'] ?? '' );
        $managed = $post && 'page' === $post->post_type && 'draft' === $post->post_status && '1' === (string) get_post_meta( $id, self::META_MANAGED, true );
        $origin = self::public_origin();

        $preview = [
            'available' => false,
            'managed_draft_only' => true,
            'short_lived' => true,
            'ttl_seconds' => self::TTL,
            'content_hash_bound' => true,
            'no_store' => true,
            'private_network_hardening' => true,
        ];
        if ( ! $managed ) $preview['reason'] = 'Signed visual previews are available only for Elementize-managed draft pages.';
        elseif ( '' === $hash ) $preview['reason'] = 'A current Elementor content hash is required before a signed preview can be issued.';
        elseif ( '' === $origin ) $preview['reason'] = 'No usable public HTTPS Elementize origin is configured.';
        else {
            $expires = time() + self::TTL;
            $preview['available'] = true;
            $preview['expires_gmt'] = gmdate( 'c', $expires );
            $preview['signed_url'] = self::signed_url( $origin, $id, $hash, $expires );
            $preview['note'] = 'Treat this URL as short-lived private preview material. Do not include it in normal reports.';
        }
        $data['visual_preview'] = $preview;
        $response->set_data( $data );
        return $response;
    }

    private static function public_origin(): string {
        $candidates = [ (string) get_option( self::OPT_PUBLIC, '' ), (string) get_option( 'home', '' ) ];
        foreach ( $candidates as $candidate ) {
            $candidate = trim( $candidate );
            if ( '' === $candidate ) continue;
            $parts = wp_parse_url( $candidate );
            if ( ! is_array( $parts ) || 'https' !== strtolower( (string) ( $parts['scheme'] ?? '' ) ) || empty( $parts['host'] ) || ! empty( $parts['user'] ) || ! empty( $parts['pass'] ) ) continue;
            $origin = 'https://' . strtolower( (string) $parts['host'] );
            if ( isset( $parts['port'] ) ) $origin .= ':' . absint( $parts['port'] );
            return untrailingslashit( $origin );
        }
        return '';
    }

    private static function signed_url( string $origin, int $id, string $hash, int $expires ): string {
        $sig = self::signature( $id, $hash, $expires );
        return add_query_arg( [
            'page_id' => $id,
            'elementize_preview' => 1,
            'elementize_expires' => $expires,
            'elementize_hash' => $hash,
            'elementize_sig' => $sig,
        ], trailingslashit( $origin ) );
    }

    private static function signature( int $id, string $hash, int $expires ): string {
        return hash_hmac( 'sha256', $id . "\n" . $expires . "\n" . $hash, wp_salt( 'auth' ) );
    }

    private static function preview_requested(): bool {
        return isset( $_GET['elementize_preview'] ) && '1' === (string) wp_unslash( $_GET['elementize_preview'] );
    }

    private static function validate_request(): array {
        if ( null !== self::$validated ) return self::$validated;
        $bad = [ 'valid' => false, 'reason' => 'invalid' ];
        if ( ! self::preview_requested() ) return self::$validated = $bad;

        $id = isset( $_GET['page_id'] ) ? absint( wp_unslash( $_GET['page_id'] ) ) : 0;
        $expires = isset( $_GET['elementize_expires'] ) ? absint( wp_unslash( $_GET['elementize_expires'] ) ) : 0;
        $hash = isset( $_GET['elementize_hash'] ) ? sanitize_text_field( wp_unslash( $_GET['elementize_hash'] ) ) : '';
        $sig = isset( $_GET['elementize_sig'] ) ? sanitize_text_field( wp_unslash( $_GET['elementize_sig'] ) ) : '';
        if ( $id < 1 || $expires < time() || $expires > time() + self::TTL + 60 || ! preg_match( '/^[a-f0-9]{64}$/', $hash ) || ! preg_match( '/^[a-f0-9]{64}$/', $sig ) ) return self::$validated = $bad;

        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type || 'draft' !== $post->post_status || '1' !== (string) get_post_meta( $id, self::META_MANAGED, true ) ) return self::$validated = $bad;
        $expected_sig = self::signature( $id, $hash, $expires );
        if ( ! hash_equals( $expected_sig, $sig ) ) return self::$validated = $bad;
        $current_hash = self::content_hash( $id );
        if ( '' === $current_hash || ! hash_equals( $current_hash, $hash ) ) return self::$validated = [ 'valid' => false, 'reason' => 'stale' ];

        return self::$validated = [ 'valid' => true, 'id' => $id, 'hash' => $hash, 'expires' => $expires ];
    }

    private static function content_hash( int $id ): string {
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $doc = \\Elementor\\Plugin::$instance->documents->get( $id );
                $elements = $doc ? $doc->get_elements_data() : null;
                if ( is_array( $elements ) ) {
                    $json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
                    return hash( 'sha256', false === $json ? '' : $json );
                }
            } catch ( Throwable $ignored ) {}
        }
        $raw = get_post_meta( $id, '_elementor_data', true );
        $decoded = is_string( $raw ) ? json_decode( $raw, true ) : null;
        if ( ! is_array( $decoded ) ) return '';
        $json = wp_json_encode( $decoded, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return hash( 'sha256', false === $json ? '' : $json );
    }

    public static function allow_signed_draft_query( WP_Query $query ): void {
        if ( ! $query->is_main_query() || is_admin() || ! self::preview_requested() ) return;
        $valid = self::validate_request();
        if ( empty( $valid['valid'] ) ) return;
        $query->set( 'page_id', (int) $valid['id'] );
        $query->set( 'post_type', 'page' );
        $query->set( 'post_status', 'draft' );
        $query->set( 'preview', true );
    }

    public static function guard_preview_request(): void {
        if ( ! self::preview_requested() ) return;
        $valid = self::validate_request();
        if ( empty( $valid['valid'] ) ) {
            nocache_headers();
            status_header( 404 );
            wp_die( 'This Elementize visual preview is invalid, expired, or stale.', 'Preview unavailable', [ 'response' => 404 ] );
        }

        show_admin_bar( false );
        nocache_headers();
        header( 'Cache-Control: private, no-store, no-cache, must-revalidate, max-age=0', true );
        header( 'Pragma: no-cache', true );
        header( 'X-Robots-Tag: noindex, nofollow, noarchive', true );
        header( 'Referrer-Policy: no-referrer', true );
        ob_start( [ self::class, 'rewrite_public_origin' ] );
    }

    public static function disable_canonical_for_preview( $redirect_url, $requested_url ) {
        return self::preview_requested() ? false : $redirect_url;
    }

    private static function replacements( string $old, string $public ): array {
        $pairs = [
            $old => $public,
            str_replace( '/', '\\/', $old ) => str_replace( '/', '\\/', $public ),
            rawurlencode( $old ) => rawurlencode( $public ),
            str_replace( '/', '\\u002F', $old ) => str_replace( '/', '\\u002F', $public ),
        ];
        $parts = wp_parse_url( $old );
        $host = is_array( $parts ) ? strtolower( (string) ( $parts['host'] ?? '' ) ) : '';
        if ( '' !== $host ) {
            foreach ( [ 'http://' . $host, 'https://' . $host, '//' . $host ] as $variant ) $pairs[$variant] = $public;
            foreach ( [ 'http:\\/\\/' . $host, 'https:\\/\\/' . $host, '\\/\\/' . $host ] as $variant ) $pairs[$variant] = str_replace( '/', '\\/', $public );
            foreach ( [ rawurlencode( 'http://' . $host ), rawurlencode( 'https://' . $host ) ] as $variant ) $pairs[$variant] = rawurlencode( $public );
            foreach ( [ 'http:\\u002F\\u002F' . $host, 'https:\\u002F\\u002F' . $host ] as $variant ) $pairs[$variant] = str_replace( '/', '\\u002F', $public );
        }
        return $pairs;
    }

    public static function rewrite_public_origin( string $html ): string {
        $public = self::public_origin();
        if ( '' === $public || '' === $html ) return $html;
        $old = array_values( array_unique( array_filter( [ untrailingslashit( (string) get_option( 'home', '' ) ), untrailingslashit( (string) get_option( 'siteurl', '' ) ) ] ) ) );
        foreach ( $old as $origin ) {
            foreach ( self::replacements( $origin, $public ) as $needle => $replacement ) $html = str_replace( $needle, $replacement, $html );
        }
        if ( ! $old || false === stripos( $html, '</body>' ) ) return $html;

        $script = '<script id="elementize-preview-origin-rewrite">(function(){var o=' . wp_json_encode( $old ) . ',p=' . wp_json_encode( $public ) . ';function r(v){if(!v)return v;o.forEach(function(x){v=v.split(x).join(p)});return v}function w(rs){if(!rs)return;Array.prototype.forEach.call(rs,function(q){try{if(q.style){for(var i=0;i<q.style.length;i++){var k=q.style[i],v=q.style.getPropertyValue(k),n=r(v);if(n!==v)q.style.setProperty(k,n,q.style.getPropertyPriority(k))}}if(q.cssRules)w(q.cssRules)}catch(e){}})}function f(){Array.prototype.forEach.call(document.styleSheets,function(s){try{w(s.cssRules)}catch(e){}})}if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",f);else f();window.addEventListener("load",f)})();</script>';
        return preg_replace( '/<\\/body>/i', $script . '</body>', $html, 1 ) ?: $html;
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['signed_visual_preview'] = true;
        $data['signed_visual_preview_ttl_seconds'] = self::TTL;
        $data['signed_visual_preview_managed_drafts_only'] = true;
        $data['signed_visual_preview_content_hash_bound'] = true;
        $data['signed_visual_preview_no_store'] = true;
        $data['signed_visual_preview_public_origin_rewrite'] = true;
        $data['signed_visual_preview_private_network_hardening'] = true;
        $data['signed_visual_preview_action_slot_cost'] = 0;
        $data['automatic_visual_screenshot_capture'] = false;
        $response->set_data( $data );
        return $response;
    }
}
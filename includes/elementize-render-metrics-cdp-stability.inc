<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Render_Metrics_CDP', false ) ) return;

final class Elementize_Render_Metrics_CDP_Stability {
    private const VERSION = '0.18.1';
    private const NS = 'elementize/v1';
    private const VIEWPORT_WIDTH = 1280;
    private const VIEWPORT_HEIGHT = 9000;
    private const STABILITY_TIMEOUT_SECONDS = 8;
    private const STABLE_POLLS_REQUIRED = 3;
    private const POST_STABLE_WAIT_US = 700000;

    public static function init(): void {
        remove_filter( 'rest_request_after_callbacks', [ 'Elementize_Render_Metrics_CDP', 'after_rest' ], 400 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 410, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual']['available'] ) ) return $response;
        $data['visual']['render_metrics'] = self::collect( absint( $m[1] ?? 0 ) );
        $response->set_data( $data );
        return $response;
    }

    private static function collect( int $id ): array {
        $base = [
            'render_metrics_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'provider' => 'local_chromium_cdp',
            'transport' => 'chrome_devtools_protocol_websocket_loopback',
            'context_stability_hardening' => true,
            'navigation_transient_retry' => true,
            'signed_preview_url_exposed' => false,
            'cdp_endpoint_exposed' => false,
            'raw_dom_exposed' => false,
            'raw_dom_persisted' => false,
            'action_slot_cost' => 0,
        ];
        if ( $id < 1 ) { $base['reason'] = 'A valid page ID is required.'; return $base; }

        $process_available = self::private_call( 'process_available' );
        if ( is_wp_error( $process_available ) || ! $process_available || ! function_exists( 'stream_socket_client' ) ) {
            $base['reason'] = 'Local CDP process/socket support is unavailable.';
            return $base;
        }
        $browser = self::private_call( 'browser_path' );
        if ( is_wp_error( $browser ) || ! is_string( $browser ) || '' === $browser ) { $base['reason'] = 'A supported local Chrome/Chromium or Edge executable was not found.'; return $base; }
        $preview = self::private_call( 'fresh_signed_preview', $id );
        if ( is_wp_error( $preview ) ) { $base['reason'] = self::error_message( $preview, 'Could not issue a fresh signed preview.' ); return $base; }
        if ( ! is_array( $preview ) || empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) { $base['reason'] = 'A fresh signed preview is unavailable.'; return $base; }
        $map = self::private_call( 'top_level_map', $id );
        if ( is_wp_error( $map ) || ! is_array( $map ) || ! $map ) { $base['reason'] = 'No top-level Elementor sections were available for rendered metrics.'; return $base; }

        $profile = self::private_call( 'temp_profile_dir' );
        if ( is_wp_error( $profile ) || ! is_string( $profile ) || '' === $profile ) { $base['reason'] = 'Could not create an isolated temporary browser profile.'; return $base; }
        $stdout = self::private_call( 'temp_path', '.stdout.log' );
        $stderr = self::private_call( 'temp_path', '.stderr.log' );
        if ( ! is_string( $stdout ) || ! is_string( $stderr ) ) { $base['reason'] = 'Could not allocate temporary local browser logs.'; return $base; }

        $proc = null;
        $socket = null;
        $started = microtime( true );
        try {
            $null = DIRECTORY_SEPARATOR === '\\' ? 'NUL' : '/dev/null';
            $command = [
                $browser,
                '--headless',
                '--user-data-dir=' . $profile,
                '--remote-debugging-port=0',
                '--no-first-run',
                '--no-default-browser-check',
                '--disable-gpu',
                '--hide-scrollbars',
                '--mute-audio',
                '--disable-background-networking',
                '--disable-component-update',
                '--window-size=' . self::VIEWPORT_WIDTH . ',' . self::VIEWPORT_HEIGHT,
                (string) $preview['signed_url'],
            ];
            $spec = [ 0 => [ 'file', $null, 'r' ], 1 => [ 'file', $stdout, 'w' ], 2 => [ 'file', $stderr, 'w' ] ];
            $options = DIRECTORY_SEPARATOR === '\\' ? [ 'bypass_shell' => true, 'suppress_errors' => true ] : [ 'bypass_shell' => true ];
            $proc = @proc_open( $command, $spec, $pipes, null, null, $options );
            if ( ! is_resource( $proc ) ) { $base['reason'] = 'Could not start the isolated local browser with DevTools enabled.'; return $base; }

            $port = self::private_call( 'wait_for_debug_port', $profile, $stdout, $stderr, $proc );
            if ( is_wp_error( $port ) ) { $base['reason'] = self::error_message( $port, 'Chrome did not expose a local DevTools endpoint.' ); return $base; }
            $target = self::private_call( 'wait_for_target', (int) $port, $id );
            if ( is_wp_error( $target ) ) { $base['reason'] = self::error_message( $target, 'Could not discover the signed preview page target.' ); return $base; }
            $socket = self::private_call( 'ws_connect', (string) ( $target['webSocketDebuggerUrl'] ?? '' ) );
            if ( is_wp_error( $socket ) ) { $base['reason'] = self::error_message( $socket, 'Could not connect to the local DevTools WebSocket.' ); return $base; }

            $seq = 0;
            $stable = self::wait_for_stable_context( $socket, $seq, $id, $map );
            if ( is_wp_error( $stable ) ) { $base['reason'] = self::error_message( $stable, 'The rendered page did not reach a stable execution context.' ); return $base; }

            $expression = self::private_call( 'metrics_expression', $map );
            if ( is_wp_error( $expression ) || ! is_string( $expression ) ) { $base['reason'] = 'Could not construct the rendered metrics expression.'; return $base; }
            $metrics = self::evaluate_with_navigation_retry( $socket, $seq, $expression, $id, $map );
            if ( is_wp_error( $metrics ) ) { $base['reason'] = self::error_message( $metrics, 'CDP could not evaluate rendered page metrics.' ); return $base; }
            if ( ! is_array( $metrics ) || ! is_array( $metrics['sections'] ?? null ) ) { $base['reason'] = 'CDP returned an invalid rendered metrics object.'; return $base; }

            $base['available'] = true;
            $base['capture_wait_seconds'] = round( microtime( true ) - $started, 2 );
            $base['page_ready_state'] = (string) ( $stable['ready_state'] ?? '' );
            $base['stable_poll_count'] = (int) ( $stable['stable_polls'] ?? 0 );
            $base['navigation_transient_count'] = (int) ( $stable['transient_count'] ?? 0 );
            $base['viewport'] = is_array( $metrics['viewport'] ?? null ) ? $metrics['viewport'] : [];
            $base['document'] = is_array( $metrics['document'] ?? null ) ? $metrics['document'] : [];
            $base['section_count'] = count( $metrics['sections'] );
            $base['sections'] = array_slice( $metrics['sections'], 0, 24 );
            $base['note'] = 'Rendered metrics were evaluated only after the signed-preview execution context stayed stable across repeated synchronous CDP probes. Navigation-destroyed contexts are treated as transient and retried. Metrics remain read-only evidence and do not authorize a write.';
            return $base;
        } finally {
            if ( is_resource( $socket ) ) @fclose( $socket );
            self::private_call_silent( 'stop_process', $proc );
            @unlink( $stdout );
            @unlink( $stderr );
            self::private_call_silent( 'remove_tree', $profile );
        }
    }

    private static function wait_for_stable_context( $socket, int &$seq, int $id, array $map ) {
        $first = trim( (string) ( $map[0]['id'] ?? '' ) );
        $first_json = wp_json_encode( $first );
        if ( ! is_string( $first_json ) ) $first_json = '""';
        $expression = '(()=>({ready_state:String(document.readyState||""),href:String(location.href||""),has_body:!!document.body,has_anchor:'
            . '(function(){var id=' . $first_json . ';return !id||!!document.querySelector("[data-id=\\\""+id+"\\\"]")})()}))()';
        $deadline = microtime( true ) + self::STABILITY_TIMEOUT_SECONDS;
        $stable_polls = 0;
        $transient_count = 0;
        $last_href = '';
        $latest = null;

        while ( microtime( true ) < $deadline ) {
            $probe = self::evaluate_private( $socket, $seq, $expression, false );
            if ( is_wp_error( $probe ) ) {
                if ( self::is_navigation_transient( $probe ) ) {
                    $transient_count++;
                    $stable_polls = 0;
                    $last_href = '';
                    usleep( 120000 );
                    continue;
                }
                return $probe;
            }
            if ( ! is_array( $probe ) ) { $stable_polls = 0; usleep( 120000 ); continue; }
            $href = (string) ( $probe['href'] ?? '' );
            $ready = (string) ( $probe['ready_state'] ?? '' );
            $signed = false !== strpos( $href, 'elementize_preview=1' ) && false !== strpos( $href, 'page_id=' . $id );
            $usable = $signed && ! empty( $probe['has_body'] ) && ! empty( $probe['has_anchor'] ) && in_array( $ready, [ 'interactive', 'complete' ], true );
            if ( $usable && '' !== $href && $href === $last_href ) $stable_polls++; else $stable_polls = $usable ? 1 : 0;
            $last_href = $usable ? $href : '';
            $latest = $probe;
            if ( $stable_polls >= self::STABLE_POLLS_REQUIRED ) {
                usleep( self::POST_STABLE_WAIT_US );
                $confirm = self::evaluate_private( $socket, $seq, $expression, false );
                if ( is_wp_error( $confirm ) ) {
                    if ( self::is_navigation_transient( $confirm ) ) { $transient_count++; $stable_polls = 0; $last_href = ''; continue; }
                    return $confirm;
                }
                if ( is_array( $confirm ) && (string) ( $confirm['href'] ?? '' ) === $last_href && ! empty( $confirm['has_body'] ) && ! empty( $confirm['has_anchor'] ) && in_array( (string) ( $confirm['ready_state'] ?? '' ), [ 'interactive', 'complete' ], true ) ) {
                    $confirm['stable_polls'] = $stable_polls + 1;
                    $confirm['transient_count'] = $transient_count;
                    return $confirm;
                }
                $stable_polls = 0;
                $last_href = '';
            }
            usleep( 120000 );
        }
        if ( is_array( $latest ) ) return new WP_Error( 'elementize_cdp_context_not_stable', 'The signed preview execution context did not remain stable long enough for deterministic rendered metrics.' );
        return new WP_Error( 'elementize_cdp_context_unavailable', 'The signed preview execution context was unavailable during the local stability window.' );
    }

    private static function evaluate_with_navigation_retry( $socket, int &$seq, string $expression, int $id, array $map ) {
        for ( $attempt = 1; $attempt <= 4; $attempt++ ) {
            $result = self::evaluate_private( $socket, $seq, $expression, false );
            if ( ! is_wp_error( $result ) ) return $result;
            if ( ! self::is_navigation_transient( $result ) ) return $result;
            $stable = self::wait_for_stable_context( $socket, $seq, $id, $map );
            if ( is_wp_error( $stable ) ) return $stable;
        }
        return new WP_Error( 'elementize_cdp_navigation_retry_exhausted', 'The page kept replacing its execution context while rendered metrics were being evaluated.' );
    }

    private static function is_navigation_transient( WP_Error $error ): bool {
        $message = strtolower( (string) $error->get_error_message() );
        foreach ( [ 'execution context was destroyed', 'cannot find context', 'context with specified id', 'inspected target navigated', 'target closed' ] as $needle ) {
            if ( false !== strpos( $message, $needle ) ) return true;
        }
        return false;
    }

    private static function evaluate_private( $socket, int &$seq, string $expression, bool $await_promise ) {
        try {
            $method = new ReflectionMethod( 'Elementize_Render_Metrics_CDP', 'evaluate' );
            $method->setAccessible( true );
            $args = [ $socket, &$seq, $expression, $await_promise ];
            return $method->invokeArgs( null, $args );
        } catch ( Throwable $e ) {
            return new WP_Error( 'elementize_cdp_reflection_evaluate_failed', 'Could not invoke the internal CDP evaluator.' );
        }
    }

    private static function private_call( string $method_name, ...$args ) {
        try {
            $method = new ReflectionMethod( 'Elementize_Render_Metrics_CDP', $method_name );
            $method->setAccessible( true );
            return $method->invokeArgs( null, $args );
        } catch ( Throwable $e ) {
            return new WP_Error( 'elementize_cdp_reflection_failed', 'Could not invoke the internal CDP helper: ' . $method_name . '.' );
        }
    }

    private static function private_call_silent( string $method_name, ...$args ): void {
        $result = self::private_call( $method_name, ...$args );
        unset( $result );
    }

    private static function error_message( WP_Error $error, string $fallback ): string {
        $message = trim( (string) $error->get_error_message() );
        if ( '' === $message ) return $fallback;
        return function_exists( 'mb_substr' ) ? mb_substr( $message, 0, 500 ) : substr( $message, 0, 500 );
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_repair_metrics_context_stability_hardening'] = true;
        $data['rendered_repair_metrics_async_settle_removed'] = true;
        $data['rendered_repair_metrics_sync_ready_polling'] = true;
        $data['rendered_repair_metrics_navigation_transient_retry'] = true;
        $data['rendered_repair_metrics_stable_polls_required'] = self::STABLE_POLLS_REQUIRED;
        $data['rendered_repair_metrics_post_stable_wait_ms'] = (int) ( self::POST_STABLE_WAIT_US / 1000 );
        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;
if ( ! class_exists( 'Elementize_Render_Metrics_CDP', false ) ) return;

final class Elementize_Render_Metrics_CDP_Stability {
    private const VERSION = '0.18.2';
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
            'safe_dom_lookup' => true,
            'selector_escape_hardening' => true,
            'exception_details_enabled' => true,
            'failure_stage_reporting' => true,
            'signed_preview_url_exposed' => false,
            'cdp_endpoint_exposed' => false,
            'raw_dom_exposed' => false,
            'raw_dom_persisted' => false,
            'action_slot_cost' => 0,
        ];
        if ( $id < 1 ) { $base['failure_stage'] = 'input'; $base['reason'] = 'A valid page ID is required.'; return $base; }

        $process_available = self::private_call( 'process_available' );
        if ( is_wp_error( $process_available ) || ! $process_available || ! function_exists( 'stream_socket_client' ) ) {
            $base['failure_stage'] = 'capability';
            $base['reason'] = 'Local CDP process/socket support is unavailable.';
            return $base;
        }
        $browser = self::private_call( 'browser_path' );
        if ( is_wp_error( $browser ) || ! is_string( $browser ) || '' === $browser ) { $base['failure_stage'] = 'browser'; $base['reason'] = 'A supported local Chrome/Chromium or Edge executable was not found.'; return $base; }
        $preview = self::private_call( 'fresh_signed_preview', $id );
        if ( is_wp_error( $preview ) ) { $base['failure_stage'] = 'preview'; $base['reason'] = self::error_message( $preview, 'Could not issue a fresh signed preview.' ); return $base; }
        if ( ! is_array( $preview ) || empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) { $base['failure_stage'] = 'preview'; $base['reason'] = 'A fresh signed preview is unavailable.'; return $base; }
        $map = self::private_call( 'top_level_map', $id );
        if ( is_wp_error( $map ) || ! is_array( $map ) || ! $map ) { $base['failure_stage'] = 'element_map'; $base['reason'] = 'No top-level Elementor sections were available for rendered metrics.'; return $base; }

        $profile = self::private_call( 'temp_profile_dir' );
        if ( is_wp_error( $profile ) || ! is_string( $profile ) || '' === $profile ) { $base['failure_stage'] = 'profile'; $base['reason'] = 'Could not create an isolated temporary browser profile.'; return $base; }
        $stdout = self::private_call( 'temp_path', '.stdout.log' );
        $stderr = self::private_call( 'temp_path', '.stderr.log' );
        if ( ! is_string( $stdout ) || ! is_string( $stderr ) ) { $base['failure_stage'] = 'temp_files'; $base['reason'] = 'Could not allocate temporary local browser logs.'; return $base; }

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
            if ( ! is_resource( $proc ) ) { $base['failure_stage'] = 'browser_start'; $base['reason'] = 'Could not start the isolated local browser with DevTools enabled.'; return $base; }

            $port = self::private_call( 'wait_for_debug_port', $profile, $stdout, $stderr, $proc );
            if ( is_wp_error( $port ) ) { $base['failure_stage'] = 'devtools_port'; $base['reason'] = self::error_message( $port, 'Chrome did not expose a local DevTools endpoint.' ); return $base; }
            $target = self::private_call( 'wait_for_target', (int) $port, $id );
            if ( is_wp_error( $target ) ) { $base['failure_stage'] = 'target_discovery'; $base['reason'] = self::error_message( $target, 'Could not discover the signed preview page target.' ); return $base; }
            $socket = self::private_call( 'ws_connect', (string) ( $target['webSocketDebuggerUrl'] ?? '' ) );
            if ( is_wp_error( $socket ) ) { $base['failure_stage'] = 'websocket'; $base['reason'] = self::error_message( $socket, 'Could not connect to the local DevTools WebSocket.' ); return $base; }

            $seq = 0;
            $stable = self::wait_for_stable_context( $socket, $seq, $id, $map );
            if ( is_wp_error( $stable ) ) { $base['failure_stage'] = 'context_stability'; $base['reason'] = self::error_message( $stable, 'The rendered page did not reach a stable execution context.' ); return $base; }

            $expression = self::metrics_expression( $map );
            $metrics = self::evaluate_with_navigation_retry( $socket, $seq, $expression, $id, $map );
            if ( is_wp_error( $metrics ) ) { $base['failure_stage'] = 'metrics_evaluate'; $base['reason'] = self::error_message( $metrics, 'CDP could not evaluate rendered page metrics.' ); return $base; }
            if ( is_array( $metrics ) && isset( $metrics['__elementize_error'] ) ) {
                $err = is_array( $metrics['__elementize_error'] ) ? $metrics['__elementize_error'] : [];
                $name = trim( (string) ( $err['name'] ?? 'Error' ) );
                $message = trim( (string) ( $err['message'] ?? 'Unknown metrics error.' ) );
                $base['failure_stage'] = 'metrics_javascript';
                $base['reason'] = self::clip( 'Rendered metrics JavaScript failed: ' . $name . ': ' . $message, 500 );
                return $base;
            }
            if ( ! is_array( $metrics ) || ! is_array( $metrics['sections'] ?? null ) ) { $base['failure_stage'] = 'metrics_shape'; $base['reason'] = 'CDP returned an invalid rendered metrics object.'; return $base; }

            $base['available'] = true;
            $base['capture_wait_seconds'] = round( microtime( true ) - $started, 2 );
            $base['page_ready_state'] = (string) ( $stable['ready_state'] ?? '' );
            $base['stable_poll_count'] = (int) ( $stable['stable_polls'] ?? 0 );
            $base['navigation_transient_count'] = (int) ( $stable['transient_count'] ?? 0 );
            $base['viewport'] = is_array( $metrics['viewport'] ?? null ) ? $metrics['viewport'] : [];
            $base['document'] = is_array( $metrics['document'] ?? null ) ? $metrics['document'] : [];
            $base['section_count'] = count( $metrics['sections'] );
            $base['sections'] = array_slice( $metrics['sections'], 0, 24 );
            $base['note'] = 'Rendered metrics were evaluated only after the signed-preview execution context stayed stable across repeated synchronous CDP probes. DOM lookup avoids dynamically quoted selectors, and browser evaluation exceptions are surfaced as bounded diagnostics. Metrics remain read-only evidence and do not authorize a write.';
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
        $template = <<<'JS'
(()=>{try{var id=__FIRST__,has_anchor=!id;if(id){var nodes=document.querySelectorAll('[data-id]');for(var i=0;i<nodes.length;i++){if(String(nodes[i].getAttribute('data-id')||'')===String(id)){has_anchor=true;break}}}return {ready_state:String(document.readyState||''),href:String(location.href||''),has_body:!!document.body,has_anchor:has_anchor};}catch(e){return {__elementize_probe_error:String(e&&e.name?e.name:'Error')+': '+String(e&&e.message?e.message:e)};}})()
JS;
        $expression = str_replace( '__FIRST__', $first_json, $template );
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
            if ( is_array( $probe ) && isset( $probe['__elementize_probe_error'] ) ) return new WP_Error( 'elementize_cdp_probe_javascript', self::clip( 'Context probe JavaScript failed: ' . (string) $probe['__elementize_probe_error'], 500 ) );
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
                if ( is_array( $confirm ) && isset( $confirm['__elementize_probe_error'] ) ) return new WP_Error( 'elementize_cdp_probe_javascript', self::clip( 'Context probe JavaScript failed: ' . (string) $confirm['__elementize_probe_error'], 500 ) );
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

    private static function metrics_expression( array $map ): string {
        $json = wp_json_encode( $map, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $json ) ) $json = '[]';
        $template = <<<'JS'
(()=>{try{var map=__MAP__;function n(v){var x=parseFloat(v);return isFinite(x)?Math.round(x*100)/100:null}function vis(el){if(!el)return false;var r=el.getBoundingClientRect(),s=getComputedStyle(el);return r.width>1&&r.height>1&&s.display!=='none'&&s.visibility!=='hidden'&&parseFloat(s.opacity||'1')>0}function eid(el){var x=el&&el.closest?el.closest('[data-id]'):null;return x?String(x.getAttribute('data-id')||''):''}function tx(el){return String((el.innerText||el.textContent||'').replace(/\s+/g,' ').trim()).slice(0,180)}function st(el){var s=getComputedStyle(el);return {font_size:n(s.fontSize),line_height:s.lineHeight==='normal'?'normal':n(s.lineHeight),font_weight:String(s.fontWeight||''),color:String(s.color||''),background_color:String(s.backgroundColor||''),border_radius:String(s.borderRadius||''),padding_top:n(s.paddingTop),padding_right:n(s.paddingRight),padding_bottom:n(s.paddingBottom),padding_left:n(s.paddingLeft),margin_top:n(s.marginTop),margin_right:n(s.marginRight),margin_bottom:n(s.marginBottom),margin_left:n(s.marginLeft)}}function sm(root,sel,max,kind){var out=[];Array.prototype.forEach.call(root.querySelectorAll(sel),function(el){if(out.length>=max||!vis(el))return;var r=el.getBoundingClientRect(),o={element_id:eid(el),tag:String(el.tagName||'').toLowerCase(),text:tx(el),width:n(r.width),height:n(r.height),style:st(el)};if(kind==='media'){var u=String(el.currentSrc||el.src||'');o.src_file=u?u.split('?')[0].split('/').pop():'';o.object_fit=String(getComputedStyle(el).objectFit||'')}out.push(o)});return out}function findById(id){var nodes=document.querySelectorAll('[data-id]');for(var i=0;i<nodes.length;i++){if(String(nodes[i].getAttribute('data-id')||'')===String(id))return nodes[i]}return null}var rows=[];map.forEach(function(x){var el=findById(x.id);if(!el||!vis(el))return;var r=el.getBoundingClientRect(),s=getComputedStyle(el);rows.push({marker:x.marker,top_level_element_id:x.id,top:n(r.top+window.scrollY),bottom:n(r.bottom+window.scrollY),width:n(r.width),height:n(r.height),padding:{top:n(s.paddingTop),right:n(s.paddingRight),bottom:n(s.paddingBottom),left:n(s.paddingLeft)},margin:{top:n(s.marginTop),right:n(s.marginRight),bottom:n(s.marginBottom),left:n(s.marginLeft)},background_color:String(s.backgroundColor||''),text_samples:sm(el,'h1,h2,h3,h4,h5,h6,p,.elementor-heading-title',10,'text'),cta_samples:sm(el,'a,button,.btn,.pix-btn',8,'cta'),media_samples:sm(el,'img,video',8,'media')})});rows.sort(function(a,b){return a.top-b.top});for(var i=0;i<rows.length;i++){rows[i].gap_before=i?Math.max(0,n(rows[i].top-rows[i-1].bottom)):0;rows[i].gap_after=i<rows.length-1?Math.max(0,n(rows[i+1].top-rows[i].bottom)):0}return {version:'0.18.2',capture_phase:'cdp_runtime_evaluate_safe_dom_lookup',viewport:{width:window.innerWidth,height:window.innerHeight,device_pixel_ratio:window.devicePixelRatio||1},document:{scroll_height:Math.max(document.documentElement.scrollHeight,document.body?document.body.scrollHeight:0)},sections:rows};}catch(e){return {__elementize_error:{name:String(e&&e.name?e.name:'Error'),message:String(e&&e.message?e.message:e)}};}})()
JS;
        return str_replace( '__MAP__', $json, $template );
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
        $id = ++$seq;
        $payload = [
            'id' => $id,
            'method' => 'Runtime.evaluate',
            'params' => [
                'expression' => $expression,
                'returnByValue' => true,
                'awaitPromise' => $await_promise,
                'silent' => true,
            ],
        ];
        $sent = self::private_call( 'ws_send_json', $socket, $payload );
        if ( is_wp_error( $sent ) || ! $sent ) return new WP_Error( 'elementize_cdp_write_failed', 'Could not write a CDP command.' );
        $reply = self::private_call( 'ws_wait_for_id', $socket, $id );
        if ( is_wp_error( $reply ) ) return $reply;
        if ( ! is_array( $reply ) ) return new WP_Error( 'elementize_cdp_invalid_reply', 'Chrome returned an invalid CDP reply.' );
        if ( isset( $reply['error'] ) ) {
            $message = trim( (string) ( $reply['error']['message'] ?? 'Chrome rejected a CDP command.' ) );
            return new WP_Error( 'elementize_cdp_command_error', self::clip( $message, 500 ) );
        }
        $result = is_array( $reply['result'] ?? null ) ? $reply['result'] : [];
        if ( ! empty( $result['exceptionDetails'] ) ) {
            $details = is_array( $result['exceptionDetails'] ) ? $result['exceptionDetails'] : [];
            $exception = is_array( $details['exception'] ?? null ) ? $details['exception'] : [];
            $description = trim( (string) ( $exception['description'] ?? '' ) );
            $text = trim( (string) ( $details['text'] ?? '' ) );
            $message = '' !== $description ? $description : ( '' !== $text ? $text : 'Chrome reported an exception while evaluating rendered metrics.' );
            return new WP_Error( 'elementize_cdp_evaluation_exception', self::clip( $message, 500 ) );
        }
        $remote = is_array( $result['result'] ?? null ) ? $result['result'] : [];
        return $remote['value'] ?? null;
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
        return self::clip( $message, 500 );
    }

    private static function clip( string $value, int $max ): string {
        return function_exists( 'mb_substr' ) ? mb_substr( $value, 0, $max ) : substr( $value, 0, $max );
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
        $data['rendered_repair_metrics_safe_dom_lookup'] = true;
        $data['rendered_repair_metrics_selector_escape_hardening'] = true;
        $data['rendered_repair_metrics_exception_details'] = true;
        $data['rendered_repair_metrics_failure_stage_reporting'] = true;
        $response->set_data( $data );
        return $response;
    }
}

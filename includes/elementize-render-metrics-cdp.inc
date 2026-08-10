<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Render_Metrics_CDP {
    private const VERSION = '0.18.0';
    private const NS = 'elementize/v1';
    private const CHROME_PATH_CONST = 'ELEMENTIZE_CHROME_PATH';
    private const VIEWPORT_WIDTH = 1280;
    private const VIEWPORT_HEIGHT = 9000;
    private const MAX_SECTIONS = 24;
    private const MAX_TEXT_SAMPLES = 10;
    private const MAX_CTA_SAMPLES = 8;
    private const MAX_MEDIA_SAMPLES = 8;
    private const DISCOVERY_TIMEOUT_SECONDS = 8;
    private const COMMAND_TIMEOUT_SECONDS = 8;
    private const SETTLE_MS = 1200;

    public static function init(): void {
        remove_filter( 'rest_request_after_callbacks', [ 'Elementize_Render_Metrics', 'after_rest' ], 380 );
        remove_action( 'template_redirect', [ 'Elementize_Render_Metrics', 'start_metrics_buffer' ], 2 );
        remove_action( 'template_redirect', [ 'Elementize_Render_Metrics_Hardening', 'start_bootstrap_buffer' ], 3 );
        remove_action( 'wp_footer', [ 'Elementize_Render_Metrics_Hardening', 'emit_footer_agent' ], 9999 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 400, 3 );
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
            'signed_preview_url_exposed' => false,
            'cdp_endpoint_exposed' => false,
            'raw_dom_exposed' => false,
            'raw_dom_persisted' => false,
            'action_slot_cost' => 0,
        ];
        if ( $id < 1 ) { $base['reason'] = 'A valid page ID is required.'; return $base; }
        if ( ! self::process_available() || ! function_exists( 'stream_socket_client' ) ) { $base['reason'] = 'Local CDP process/socket support is unavailable.'; return $base; }
        $browser = self::browser_path();
        if ( '' === $browser ) { $base['reason'] = 'A supported local Chrome/Chromium or Edge executable was not found.'; return $base; }
        $preview = self::fresh_signed_preview( $id );
        if ( is_wp_error( $preview ) ) { $base['reason'] = self::safe_error( $preview, 'Could not issue a fresh signed preview.' ); return $base; }
        if ( empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) { $base['reason'] = 'A fresh signed preview is unavailable.'; return $base; }
        $map = self::top_level_map( $id );
        if ( ! $map ) { $base['reason'] = 'No top-level Elementor sections were available for rendered metrics.'; return $base; }

        $profile = self::temp_profile_dir();
        if ( '' === $profile ) { $base['reason'] = 'Could not create an isolated temporary browser profile.'; return $base; }
        $stdout = self::temp_path( '.stdout.log' );
        $stderr = self::temp_path( '.stderr.log' );
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

            $port = self::wait_for_debug_port( $profile, $stdout, $stderr, $proc );
            if ( is_wp_error( $port ) ) { $base['reason'] = self::safe_error( $port, 'Chrome did not expose a local DevTools endpoint.' ); return $base; }
            $target = self::wait_for_target( (int) $port, $id );
            if ( is_wp_error( $target ) ) { $base['reason'] = self::safe_error( $target, 'Could not discover the signed preview page target.' ); return $base; }
            $socket = self::ws_connect( (string) ( $target['webSocketDebuggerUrl'] ?? '' ) );
            if ( is_wp_error( $socket ) ) { $base['reason'] = self::safe_error( $socket, 'Could not connect to the local DevTools WebSocket.' ); return $base; }

            $seq = 0;
            $ready = self::evaluate( $socket, $seq, self::settle_expression(), true );
            if ( is_wp_error( $ready ) ) { $base['reason'] = self::safe_error( $ready, 'The rendered page could not be settled through CDP.' ); return $base; }
            $metrics = self::evaluate( $socket, $seq, self::metrics_expression( $map ), false );
            if ( is_wp_error( $metrics ) ) { $base['reason'] = self::safe_error( $metrics, 'CDP could not evaluate rendered page metrics.' ); return $base; }
            if ( ! is_array( $metrics ) || ! is_array( $metrics['sections'] ?? null ) ) { $base['reason'] = 'CDP returned an invalid rendered metrics object.'; return $base; }

            $base['available'] = true;
            $base['capture_wait_seconds'] = round( microtime( true ) - $started, 2 );
            $base['page_ready_state'] = is_array( $ready ) ? (string) ( $ready['ready_state'] ?? '' ) : '';
            $base['viewport'] = is_array( $metrics['viewport'] ?? null ) ? $metrics['viewport'] : [];
            $base['document'] = is_array( $metrics['document'] ?? null ) ? $metrics['document'] : [];
            $base['section_count'] = count( $metrics['sections'] );
            $base['sections'] = array_slice( $metrics['sections'], 0, self::MAX_SECTIONS );
            $base['note'] = 'Rendered metrics were evaluated directly in the live signed preview through a loopback-only Chrome DevTools Protocol connection. They are read-only evidence and do not authorize a write.';
            return $base;
        } finally {
            if ( is_resource( $socket ) ) @fclose( $socket );
            self::stop_process( $proc );
            @unlink( $stdout );
            @unlink( $stderr );
            self::remove_tree( $profile );
        }
    }

    private static function settle_expression(): string {
        return '(async()=>{try{if(document.fonts&&document.fonts.ready)await Promise.race([document.fonts.ready,new Promise(r=>setTimeout(r,2000))]);}catch(e){}await new Promise(r=>setTimeout(r,' . self::SETTLE_MS . '));return {ready_state:String(document.readyState||""),href:String(location.href||"")};})()';
    }

    private static function metrics_expression( array $map ): string {
        $json = wp_json_encode( $map, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $json ) ) $json = '[]';
        return '(()=>{var map=' . $json . ';'
            . 'function n(v){var x=parseFloat(v);return isFinite(x)?Math.round(x*100)/100:null}'
            . 'function vis(el){if(!el)return false;var r=el.getBoundingClientRect(),s=getComputedStyle(el);return r.width>1&&r.height>1&&s.display!=="none"&&s.visibility!=="hidden"&&parseFloat(s.opacity||"1")>0}'
            . 'function eid(el){var x=el&&el.closest?el.closest("[data-id]"):null;return x?String(x.getAttribute("data-id")||""):""}'
            . 'function tx(el){return String((el.innerText||el.textContent||"").replace(/\\s+/g," ").trim()).slice(0,180)}'
            . 'function st(el){var s=getComputedStyle(el);return {font_size:n(s.fontSize),line_height:"normal"===s.lineHeight?"normal":n(s.lineHeight),font_weight:String(s.fontWeight||""),color:String(s.color||""),background_color:String(s.backgroundColor||""),border_radius:String(s.borderRadius||""),padding_top:n(s.paddingTop),padding_right:n(s.paddingRight),padding_bottom:n(s.paddingBottom),padding_left:n(s.paddingLeft),margin_top:n(s.marginTop),margin_right:n(s.marginRight),margin_bottom:n(s.marginBottom),margin_left:n(s.marginLeft)}}'
            . 'function sm(root,sel,max,kind){var out=[];Array.prototype.forEach.call(root.querySelectorAll(sel),function(el){if(out.length>=max||!vis(el))return;var r=el.getBoundingClientRect(),o={element_id:eid(el),tag:String(el.tagName||"").toLowerCase(),text:tx(el),width:n(r.width),height:n(r.height),style:st(el)};if(kind==="media"){var u=String(el.currentSrc||el.src||"");o.src_file=u?u.split("?")[0].split("/").pop():"";o.object_fit=String(getComputedStyle(el).objectFit||"")}out.push(o)});return out}'
            . 'var rows=[];map.forEach(function(x){var el=document.querySelector(".elementor-element[data-id=\\\""+x.id+"\\\"]")||document.querySelector("[data-id=\\\""+x.id+"\\\"]");if(!el||!vis(el))return;var r=el.getBoundingClientRect(),s=getComputedStyle(el);rows.push({marker:x.marker,top_level_element_id:x.id,top:n(r.top+window.scrollY),bottom:n(r.bottom+window.scrollY),width:n(r.width),height:n(r.height),padding:{top:n(s.paddingTop),right:n(s.paddingRight),bottom:n(s.paddingBottom),left:n(s.paddingLeft)},margin:{top:n(s.marginTop),right:n(s.marginRight),bottom:n(s.marginBottom),left:n(s.marginLeft)},background_color:String(s.backgroundColor||""),text_samples:sm(el,"h1,h2,h3,h4,h5,h6,p,.elementor-heading-title",' . self::MAX_TEXT_SAMPLES . ',"text"),cta_samples:sm(el,"a,button,.btn,.pix-btn",' . self::MAX_CTA_SAMPLES . ',"cta"),media_samples:sm(el,"img,video",' . self::MAX_MEDIA_SAMPLES . ',"media")})});'
            . 'rows.sort(function(a,b){return a.top-b.top});for(var i=0;i<rows.length;i++){rows[i].gap_before=i?Math.max(0,n(rows[i].top-rows[i-1].bottom)):0;rows[i].gap_after=i<rows.length-1?Math.max(0,n(rows[i+1].top-rows[i].bottom)):0}'
            . 'return {version:"' . self::VERSION . '",capture_phase:"cdp_runtime_evaluate",viewport:{width:window.innerWidth,height:window.innerHeight,device_pixel_ratio:window.devicePixelRatio||1},document:{scroll_height:Math.max(document.documentElement.scrollHeight,document.body?document.body.scrollHeight:0)},sections:rows};})()';
    }

    private static function evaluate( $socket, int &$seq, string $expression, bool $await_promise ) {
        $id = ++$seq;
        $payload = [ 'id' => $id, 'method' => 'Runtime.evaluate', 'params' => [ 'expression' => $expression, 'returnByValue' => true, 'awaitPromise' => $await_promise, 'silent' => true ] ];
        if ( ! self::ws_send_json( $socket, $payload ) ) return new WP_Error( 'elementize_cdp_write_failed', 'Could not write a CDP command.' );
        $reply = self::ws_wait_for_id( $socket, $id );
        if ( is_wp_error( $reply ) ) return $reply;
        if ( isset( $reply['error'] ) ) return new WP_Error( 'elementize_cdp_command_error', self::clip( (string) ( $reply['error']['message'] ?? 'Chrome rejected a CDP command.' ), 400 ) );
        $result = is_array( $reply['result'] ?? null ) ? $reply['result'] : [];
        if ( ! empty( $result['exceptionDetails'] ) ) return new WP_Error( 'elementize_cdp_evaluation_exception', 'Chrome reported an exception while evaluating rendered metrics.' );
        $remote = is_array( $result['result'] ?? null ) ? $result['result'] : [];
        return $remote['value'] ?? null;
    }

    private static function wait_for_debug_port( string $profile, string $stdout, string $stderr, $proc ) {
        $deadline = microtime( true ) + self::DISCOVERY_TIMEOUT_SECONDS;
        do {
            $active = trailingslashit( $profile ) . 'DevToolsActivePort';
            if ( is_file( $active ) ) {
                $lines = @file( $active, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES );
                $port = isset( $lines[0] ) ? absint( trim( (string) $lines[0] ) ) : 0;
                if ( $port > 0 && $port < 65536 ) return $port;
            }
            foreach ( [ $stderr, $stdout ] as $log ) {
                $text = is_file( $log ) ? (string) @file_get_contents( $log ) : '';
                if ( preg_match( '#DevTools listening on ws://(?:127\.0\.0\.1|localhost):(\d+)/#i', $text, $m ) ) {
                    $port = absint( $m[1] ?? 0 );
                    if ( $port > 0 && $port < 65536 ) return $port;
                }
            }
            $status = is_resource( $proc ) ? @proc_get_status( $proc ) : null;
            if ( is_array( $status ) && empty( $status['running'] ) ) break;
            usleep( 100000 );
        } while ( microtime( true ) < $deadline );
        return new WP_Error( 'elementize_cdp_debug_port_missing', 'Chrome did not expose a loopback DevTools port within the local discovery timeout.' );
    }

    private static function wait_for_target( int $port, int $id ) {
        $deadline = microtime( true ) + self::DISCOVERY_TIMEOUT_SECONDS;
        do {
            $response = wp_remote_get( 'http://127.0.0.1:' . $port . '/json/list', [ 'timeout' => 2, 'redirection' => 0, 'sslverify' => false, 'limit_response_size' => 1048576 ] );
            if ( ! is_wp_error( $response ) && 200 === (int) wp_remote_retrieve_response_code( $response ) ) {
                $list = json_decode( (string) wp_remote_retrieve_body( $response ), true );
                if ( is_array( $list ) ) {
                    $fallback = null;
                    foreach ( $list as $entry ) {
                        if ( ! is_array( $entry ) || 'page' !== strtolower( (string) ( $entry['type'] ?? '' ) ) || empty( $entry['webSocketDebuggerUrl'] ) ) continue;
                        if ( null === $fallback ) $fallback = $entry;
                        $url = (string) ( $entry['url'] ?? '' );
                        if ( false !== strpos( $url, 'elementize_preview=1' ) && false !== strpos( $url, 'page_id=' . $id ) ) return $entry;
                    }
                    if ( null !== $fallback && microtime( true ) + 0.5 >= $deadline ) return $fallback;
                }
            }
            usleep( 100000 );
        } while ( microtime( true ) < $deadline );
        return new WP_Error( 'elementize_cdp_target_missing', 'Chrome DevTools did not expose the signed preview page target within the local discovery timeout.' );
    }

    private static function ws_connect( string $url ) {
        $parts = wp_parse_url( $url );
        if ( ! is_array( $parts ) || 'ws' !== strtolower( (string) ( $parts['scheme'] ?? '' ) ) ) return new WP_Error( 'elementize_cdp_ws_scheme', 'Chrome returned an unsupported DevTools WebSocket scheme.' );
        $host = strtolower( (string) ( $parts['host'] ?? '' ) );
        if ( ! in_array( $host, [ '127.0.0.1', 'localhost' ], true ) ) return new WP_Error( 'elementize_cdp_ws_host', 'Chrome returned a non-loopback DevTools WebSocket host.' );
        $port = absint( $parts['port'] ?? 0 );
        if ( $port < 1 || $port > 65535 ) return new WP_Error( 'elementize_cdp_ws_port', 'Chrome returned an invalid DevTools WebSocket port.' );
        $path = (string) ( $parts['path'] ?? '/' );
        if ( isset( $parts['query'] ) ) $path .= '?' . $parts['query'];
        $errno = 0; $errstr = '';
        $fp = @stream_socket_client( 'tcp://127.0.0.1:' . $port, $errno, $errstr, 2, STREAM_CLIENT_CONNECT );
        if ( ! is_resource( $fp ) ) return new WP_Error( 'elementize_cdp_ws_connect', 'Could not open the loopback DevTools WebSocket transport.' );
        stream_set_timeout( $fp, 1 );
        $key = base64_encode( random_bytes( 16 ) );
        $request = "GET {$path} HTTP/1.1\r\nHost: 127.0.0.1:{$port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {$key}\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if ( ! self::write_all( $fp, $request ) ) { @fclose( $fp ); return new WP_Error( 'elementize_cdp_ws_handshake_write', 'Could not write the DevTools WebSocket handshake.' ); }
        $headers = '';
        while ( ! feof( $fp ) && strlen( $headers ) < 16384 ) {
            $line = @fgets( $fp, 4096 );
            if ( false === $line ) break;
            $headers .= $line;
            if ( "\r\n" === $line || "\n" === $line ) break;
        }
        $expected = base64_encode( sha1( $key . '258EAFA5-E914-47DA-95CA-C5AB0DC85B11', true ) );
        if ( ! preg_match( '#^HTTP/\d(?:\.\d)?\s+101\b#i', $headers ) || false === stripos( $headers, 'Sec-WebSocket-Accept: ' . $expected ) ) {
            @fclose( $fp );
            return new WP_Error( 'elementize_cdp_ws_handshake_rejected', 'Chrome rejected the DevTools WebSocket handshake.' );
        }
        return $fp;
    }

    private static function ws_send_json( $socket, array $payload ): bool {
        $json = wp_json_encode( $payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        return is_string( $json ) && self::ws_send_frame( $socket, $json, 0x1 );
    }

    private static function ws_send_frame( $socket, string $payload, int $opcode ): bool {
        $len = strlen( $payload );
        $first = 0x80 | ( $opcode & 0x0f );
        if ( $len <= 125 ) $header = pack( 'CC', $first, 0x80 | $len );
        elseif ( $len <= 65535 ) $header = pack( 'CCn', $first, 0x80 | 126, $len );
        else $header = pack( 'CCNN', $first, 0x80 | 127, 0, $len );
        $mask = random_bytes( 4 );
        $stream = substr( str_repeat( $mask, (int) ceil( max( 1, $len ) / 4 ) ), 0, $len );
        $masked = $len ? ( $payload ^ $stream ) : '';
        return self::write_all( $socket, $header . $mask . $masked );
    }

    private static function ws_wait_for_id( $socket, int $id ) {
        $deadline = microtime( true ) + self::COMMAND_TIMEOUT_SECONDS;
        do {
            $message = self::ws_read_message( $socket, $deadline );
            if ( is_wp_error( $message ) ) return $message;
            if ( ! is_string( $message ) || '' === $message ) continue;
            $data = json_decode( $message, true );
            if ( is_array( $data ) && isset( $data['id'] ) && (int) $data['id'] === $id ) return $data;
        } while ( microtime( true ) < $deadline );
        return new WP_Error( 'elementize_cdp_response_timeout', 'Chrome did not answer the CDP command within the local timeout.' );
    }

    private static function ws_read_message( $socket, float $deadline ) {
        $message = ''; $started = false;
        while ( microtime( true ) < $deadline ) {
            $head = self::read_exact( $socket, 2, $deadline );
            if ( is_wp_error( $head ) ) return $head;
            if ( ! is_string( $head ) ) continue;
            $a = ord( $head[0] ); $b = ord( $head[1] );
            $fin = 0 !== ( $a & 0x80 ); $opcode = $a & 0x0f; $masked = 0 !== ( $b & 0x80 ); $len = $b & 0x7f;
            if ( 126 === $len ) {
                $ext = self::read_exact( $socket, 2, $deadline ); if ( ! is_string( $ext ) ) return $ext;
                $u = unpack( 'nlen', $ext ); $len = (int) ( $u['len'] ?? 0 );
            } elseif ( 127 === $len ) {
                $ext = self::read_exact( $socket, 8, $deadline ); if ( ! is_string( $ext ) ) return $ext;
                $u = unpack( 'Nhi/Nlo', $ext ); if ( ! empty( $u['hi'] ) ) return new WP_Error( 'elementize_cdp_ws_frame_large', 'Chrome returned an unexpectedly large DevTools frame.' );
                $len = (int) ( $u['lo'] ?? 0 );
            }
            $mask = $masked ? self::read_exact( $socket, 4, $deadline ) : '';
            if ( is_wp_error( $mask ) || ( $masked && ! is_string( $mask ) ) ) return $mask;
            $payload = $len ? self::read_exact( $socket, $len, $deadline ) : '';
            if ( is_wp_error( $payload ) || ! is_string( $payload ) ) return $payload;
            if ( $masked && $len ) $payload = $payload ^ substr( str_repeat( $mask, (int) ceil( $len / 4 ) ), 0, $len );
            if ( 0x8 === $opcode ) return new WP_Error( 'elementize_cdp_ws_closed', 'Chrome closed the DevTools WebSocket unexpectedly.' );
            if ( 0x9 === $opcode ) { self::ws_send_frame( $socket, $payload, 0xA ); continue; }
            if ( 0xA === $opcode ) continue;
            if ( 0x1 === $opcode || 0x2 === $opcode ) { $message = $payload; $started = true; }
            elseif ( 0x0 === $opcode && $started ) $message .= $payload;
            else continue;
            if ( $fin && $started ) return $message;
        }
        return null;
    }

    private static function read_exact( $socket, int $length, float $deadline ) {
        $data = '';
        while ( strlen( $data ) < $length && microtime( true ) < $deadline ) {
            $chunk = @fread( $socket, $length - strlen( $data ) );
            if ( false === $chunk ) return new WP_Error( 'elementize_cdp_ws_read', 'Could not read from the DevTools WebSocket.' );
            if ( '' === $chunk ) {
                if ( feof( $socket ) ) return new WP_Error( 'elementize_cdp_ws_eof', 'The DevTools WebSocket closed unexpectedly.' );
                usleep( 20000 );
                continue;
            }
            $data .= $chunk;
        }
        return strlen( $data ) === $length ? $data : null;
    }

    private static function write_all( $socket, string $data ): bool {
        $offset = 0; $length = strlen( $data );
        while ( $offset < $length ) {
            $written = @fwrite( $socket, substr( $data, $offset ) );
            if ( false === $written || 0 === $written ) return false;
            $offset += $written;
        }
        return true;
    }

    private static function fresh_signed_preview( int $id ) {
        $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/state' );
        $request->set_param( 'id', $id );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_cdp_state_failed', 'Fresh page state could not be read.' );
        $data = $response->get_data();
        return is_array( $data ) && is_array( $data['visual_preview'] ?? null ) ? $data['visual_preview'] : new WP_Error( 'elementize_cdp_preview_missing', 'Fresh page state did not provide a signed visual preview.' );
    }

    private static function top_level_map( int $id ): array {
        $elements = null;
        if ( class_exists( '\\Elementor\\Plugin' ) ) {
            try {
                $doc = \Elementor\Plugin::$instance->documents->get( $id );
                $elements = $doc ? $doc->get_elements_data() : null;
            } catch ( Throwable $ignored ) {}
        }
        if ( ! is_array( $elements ) ) return [];
        $out = []; $n = 0;
        foreach ( $elements as $element ) {
            if ( ! is_array( $element ) ) continue;
            $eid = trim( (string) ( $element['id'] ?? '' ) );
            if ( '' === $eid || ! preg_match( '/^[A-Za-z0-9_-]+$/', $eid ) ) continue;
            $n++; if ( $n > self::MAX_SECTIONS ) break;
            $out[] = [ 'marker' => 'S' . $n, 'id' => $eid ];
        }
        return $out;
    }

    private static function process_available(): bool {
        return function_exists( 'proc_open' ) && function_exists( 'proc_get_status' ) && function_exists( 'proc_terminate' );
    }

    private static function browser_path(): string {
        if ( defined( self::CHROME_PATH_CONST ) ) {
            $path = trim( (string) constant( self::CHROME_PATH_CONST ) );
            if ( '' !== $path && is_file( $path ) ) return $path;
        }
        $candidates = [];
        foreach ( [ getenv( 'LOCALAPPDATA' ), getenv( 'ProgramFiles' ), getenv( 'ProgramFiles(x86)' ) ] as $base ) {
            if ( ! is_string( $base ) || '' === $base ) continue;
            $candidates[] = $base . '\\Google\\Chrome\\Application\\chrome.exe';
            $candidates[] = $base . '\\Microsoft\\Edge\\Application\\msedge.exe';
        }
        foreach ( [ '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium', '/usr/bin/chromium-browser' ] as $path ) $candidates[] = $path;
        foreach ( array_unique( $candidates ) as $candidate ) if ( is_file( $candidate ) ) return $candidate;
        return '';
    }

    private static function temp_path( string $suffix ): string {
        $dir = function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir();
        return trailingslashit( $dir ) . 'elementize-render-cdp-' . wp_generate_uuid4() . $suffix;
    }

    private static function temp_profile_dir(): string {
        $dir = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() ) . 'elementize-render-cdp-profile-' . wp_generate_uuid4();
        return wp_mkdir_p( $dir ) && is_dir( $dir ) ? $dir : '';
    }

    private static function stop_process( $proc ): void {
        if ( ! is_resource( $proc ) ) return;
        $status = @proc_get_status( $proc );
        if ( is_array( $status ) && ! empty( $status['running'] ) ) @proc_terminate( $proc );
        usleep( 200000 );
        $status = @proc_get_status( $proc );
        if ( is_array( $status ) && ! empty( $status['running'] ) ) @proc_terminate( $proc, 9 );
        @proc_close( $proc );
    }

    private static function remove_tree( string $path ): void {
        if ( '' === $path || ! is_dir( $path ) ) return;
        foreach ( @scandir( $path ) ?: [] as $item ) {
            if ( '.' === $item || '..' === $item ) continue;
            $child = $path . DIRECTORY_SEPARATOR . $item;
            if ( is_dir( $child ) && ! is_link( $child ) ) self::remove_tree( $child ); else @unlink( $child );
        }
        @rmdir( $path );
    }

    private static function clip( string $value, int $max ): string {
        return function_exists( 'mb_substr' ) ? mb_substr( $value, 0, $max ) : substr( $value, 0, $max );
    }

    private static function safe_error( WP_Error $error, string $fallback ): string {
        $message = trim( (string) $error->get_error_message() );
        return '' !== $message ? self::clip( $message, 500 ) : $fallback;
    }

    private static function truthy( $value ): bool {
        if ( true === $value || 1 === $value ) return true;
        return is_string( $value ) && in_array( strtolower( trim( $value ) ), [ '1', 'true', 'yes', 'on' ], true );
    }

    private static function status( WP_REST_Response $response ): WP_REST_Response {
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_repair_metrics'] = true;
        $data['rendered_repair_metrics_read_only'] = true;
        $data['rendered_repair_metrics_provider'] = 'local_chromium_cdp';
        $data['rendered_repair_metrics_dump_dom_superseded'] = true;
        $data['rendered_repair_metrics_cdp_loopback_only'] = true;
        $data['rendered_repair_metrics_cdp_transport_available'] = function_exists( 'stream_socket_client' );
        $data['rendered_repair_metrics_cdp_runtime_evaluate'] = true;
        $data['rendered_repair_metrics_exact_top_level_ids'] = true;
        $data['rendered_repair_metrics_computed_typography'] = true;
        $data['rendered_repair_metrics_computed_spacing'] = true;
        $data['rendered_repair_metrics_cta_samples'] = true;
        $data['rendered_repair_metrics_media_samples'] = true;
        $data['rendered_repair_metrics_raw_dom_exposed'] = false;
        $data['rendered_repair_metrics_cdp_endpoint_exposed'] = false;
        $data['rendered_repair_metrics_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

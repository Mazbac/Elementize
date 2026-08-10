<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Render_Metrics {
    private const VERSION = '0.17.0';
    private const NS = 'elementize/v1';
    private const CHROME_PATH_CONST = 'ELEMENTIZE_CHROME_PATH';
    private const PAGE_TIMEOUT_MS = 6500;
    private const PROCESS_TIMEOUT_SECONDS = 20;
    private const VIEWPORT_WIDTH = 1280;
    private const VIEWPORT_HEIGHT = 9000;
    private const MAX_SECTIONS = 24;
    private const MAX_TEXT_SAMPLES = 10;
    private const MAX_CTA_SAMPLES = 8;
    private const MAX_MEDIA_SAMPLES = 8;

    public static function init(): void {
        add_action( 'template_redirect', [ self::class, 'start_metrics_buffer' ], 2 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 380, 3 );
    }

    public static function start_metrics_buffer(): void {
        if ( ! self::metrics_preview_requested() ) return;
        // Elementize_Signed_Preview validates the signed preview at template_redirect priority 0.
        // If validation fails it terminates the request before this callback can run.
        ob_start( [ self::class, 'inject_metrics_script' ] );
    }

    private static function metrics_preview_requested(): bool {
        return isset( $_GET['elementize_preview'], $_GET['elementize_metrics'] )
            && '1' === (string) wp_unslash( $_GET['elementize_preview'] )
            && '1' === (string) wp_unslash( $_GET['elementize_metrics'] );
    }

    public static function inject_metrics_script( string $html ): string {
        if ( '' === $html || false === stripos( $html, '</body>' ) ) return $html;
        $id = isset( $_GET['page_id'] ) ? absint( wp_unslash( $_GET['page_id'] ) ) : 0;
        if ( $id < 1 ) return $html;
        $map = self::top_level_map( $id );
        if ( ! $map ) return $html;
        $map_json = wp_json_encode( $map, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $map_json ) || '' === $map_json ) return $html;

        $script = '<script id="elementize-render-metrics-agent">(function(){'
            . 'var map=' . $map_json . ';var done=false;'
            . 'function n(v){var x=parseFloat(v);return isFinite(x)?Math.round(x*100)/100:null}'
            . 'function vis(el){if(!el)return false;var r=el.getBoundingClientRect(),s=getComputedStyle(el);return r.width>1&&r.height>1&&s.display!=="none"&&s.visibility!=="hidden"&&parseFloat(s.opacity||"1")>0}'
            . 'function eid(el){var x=el&&el.closest?el.closest("[data-id]"):null;return x?String(x.getAttribute("data-id")||""):""}'
            . 'function txt(el){return String((el.innerText||el.textContent||"").replace(/\\s+/g," ").trim()).slice(0,180)}'
            . 'function style(el){var s=getComputedStyle(el);return {font_size:n(s.fontSize),line_height:"normal"===s.lineHeight?"normal":n(s.lineHeight),font_weight:String(s.fontWeight||""),color:String(s.color||""),background_color:String(s.backgroundColor||""),border_radius:String(s.borderRadius||""),padding_top:n(s.paddingTop),padding_right:n(s.paddingRight),padding_bottom:n(s.paddingBottom),padding_left:n(s.paddingLeft),margin_top:n(s.marginTop),margin_right:n(s.marginRight),margin_bottom:n(s.marginBottom),margin_left:n(s.marginLeft)}}'
            . 'function samples(root,sel,max,kind){var out=[];Array.prototype.forEach.call(root.querySelectorAll(sel),function(el){if(out.length>=max||!vis(el))return;var r=el.getBoundingClientRect(),o={element_id:eid(el),tag:String(el.tagName||"").toLowerCase(),text:txt(el),width:n(r.width),height:n(r.height),style:style(el)};if(kind==="media"){var u=String(el.currentSrc||el.src||"");o.src_file=u?u.split("?")[0].split("/").pop():"";o.object_fit=String(getComputedStyle(el).objectFit||"")}out.push(o)});return out}'
            . 'function run(){if(done)return;done=true;var rows=[];map.forEach(function(x){var el=document.querySelector(".elementor-element[data-id=\""+x.id+"\"]")||document.querySelector("[data-id=\""+x.id+"\"]");if(!el||!vis(el))return;var r=el.getBoundingClientRect(),s=getComputedStyle(el);rows.push({marker:x.marker,top_level_element_id:x.id,top:n(r.top+window.scrollY),bottom:n(r.bottom+window.scrollY),width:n(r.width),height:n(r.height),padding:{top:n(s.paddingTop),right:n(s.paddingRight),bottom:n(s.paddingBottom),left:n(s.paddingLeft)},margin:{top:n(s.marginTop),right:n(s.marginRight),bottom:n(s.marginBottom),left:n(s.marginLeft)},background_color:String(s.backgroundColor||""),text_samples:samples(el,"h1,h2,h3,h4,h5,h6,p,.elementor-heading-title",' . self::MAX_TEXT_SAMPLES . ',"text"),cta_samples:samples(el,"a,button,.btn,.pix-btn",' . self::MAX_CTA_SAMPLES . ',"cta"),media_samples:samples(el,"img,video",' . self::MAX_MEDIA_SAMPLES . ',"media")})});rows.sort(function(a,b){return a.top-b.top});for(var i=0;i<rows.length;i++){rows[i].gap_before=i?Math.max(0,n(rows[i].top-rows[i-1].bottom)):0;rows[i].gap_after=i<rows.length-1?Math.max(0,n(rows[i+1].top-rows[i].bottom)):0}var payload={version:"' . self::VERSION . '",viewport:{width:window.innerWidth,height:window.innerHeight,device_pixel_ratio:window.devicePixelRatio||1},document:{scroll_height:Math.max(document.documentElement.scrollHeight,document.body?document.body.scrollHeight:0)},sections:rows};var node=document.getElementById("elementize-render-metrics-data");if(!node){node=document.createElement("script");node.id="elementize-render-metrics-data";node.type="application/json";document.body.appendChild(node)}node.textContent=JSON.stringify(payload)}'
            . 'if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",function(){setTimeout(run,1200)});else setTimeout(run,1200);window.addEventListener("load",function(){setTimeout(run,800)});setTimeout(run,3200)})();</script>';

        return str_ireplace( '</body>', $script . '</body>', $html );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( 'GET' !== strtoupper( $request->get_method() ) || ! preg_match( '#^/' . preg_quote( self::NS, '#' ) . '/pages/(\d+)/completion-audit$#', $route, $m ) ) return $response;
        if ( $response->get_status() >= 400 || ! self::truthy( $request->get_param( 'include_visual' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || empty( $data['visual']['available'] ) ) return $response;
        $metrics = self::collect( absint( $m[1] ?? 0 ) );
        $data['visual']['render_metrics'] = $metrics;
        $response->set_data( $data );
        return $response;
    }

    private static function collect( int $id ): array {
        $base = [
            'render_metrics_version' => self::VERSION,
            'available' => false,
            'read_only' => true,
            'writes_performed' => false,
            'provider' => 'local_chromium_dom',
            'signed_preview_url_exposed' => false,
            'raw_dom_exposed' => false,
            'raw_dom_persisted' => false,
            'action_slot_cost' => 0,
        ];
        if ( $id < 1 || ! self::process_available() ) { $base['reason'] = 'Local process execution is unavailable.'; return $base; }
        $browser = self::browser_path();
        if ( '' === $browser ) { $base['reason'] = 'A supported local Chrome/Chromium or Edge executable was not found.'; return $base; }
        $preview = self::fresh_signed_preview( $id );
        if ( is_wp_error( $preview ) ) { $base['reason'] = self::safe_error( $preview, 'Could not issue a fresh signed preview for render metrics.' ); return $base; }
        if ( empty( $preview['available'] ) || empty( $preview['signed_url'] ) ) { $base['reason'] = 'A fresh signed preview is unavailable.'; return $base; }

        $url = add_query_arg( 'elementize_metrics', '1', (string) $preview['signed_url'] );
        $dump = self::dump_dom( $url, $browser );
        if ( is_wp_error( $dump ) ) { $base['reason'] = self::safe_error( $dump, 'Local DOM metric capture failed.' ); return $base; }
        $parsed = self::extract_metrics( $dump['html'] );
        @unlink( $dump['stdout'] );
        @unlink( $dump['stderr'] );
        unset( $dump['html'] );
        if ( is_wp_error( $parsed ) ) { $base['reason'] = self::safe_error( $parsed, 'Rendered page did not expose valid internal metrics.' ); return $base; }

        $sections = is_array( $parsed['sections'] ?? null ) ? array_slice( $parsed['sections'], 0, self::MAX_SECTIONS ) : [];
        $base['available'] = true;
        $base['capture_wait_seconds'] = $dump['wait_seconds'];
        $base['viewport'] = is_array( $parsed['viewport'] ?? null ) ? $parsed['viewport'] : [];
        $base['document'] = is_array( $parsed['document'] ?? null ) ? $parsed['document'] : [];
        $base['section_count'] = count( $sections );
        $base['sections'] = $sections;
        $base['note'] = 'Computed rendered metrics are read-only evidence for locating spacing, typography, CTA, and media causes. They do not authorize a write.';
        return $base;
    }

    private static function extract_metrics( string $html ) {
        if ( '' === $html ) return new WP_Error( 'elementize_render_metrics_empty_dom', 'Chrome returned an empty DOM dump.' );
        if ( ! preg_match( '#<script[^>]+id=["\']elementize-render-metrics-data["\'][^>]*>(.*?)</script>#is', $html, $m ) ) {
            return new WP_Error( 'elementize_render_metrics_missing_payload', 'The rendered page did not produce the internal metrics payload.' );
        }
        $raw = html_entity_decode( trim( (string) ( $m[1] ?? '' ) ), ENT_QUOTES | ENT_HTML5, 'UTF-8' );
        $data = json_decode( $raw, true );
        if ( ! is_array( $data ) || ! is_array( $data['sections'] ?? null ) ) return new WP_Error( 'elementize_render_metrics_invalid_payload', 'The internal metrics payload was invalid JSON.' );
        return $data;
    }

    private static function dump_dom( string $url, string $browser ) {
        $stdout = self::temp_path( '.dom.html' );
        $stderr = self::temp_path( '.dom.stderr.log' );
        $profile = self::temp_profile_dir();
        if ( '' === $profile ) return new WP_Error( 'elementize_render_metrics_profile_failed', 'Could not create an isolated temporary browser profile.' );
        $command = [
            $browser,
            '--headless',
            '--user-data-dir=' . $profile,
            '--no-first-run',
            '--no-default-browser-check',
            '--disable-gpu',
            '--hide-scrollbars',
            '--mute-audio',
            '--disable-background-networking',
            '--disable-component-update',
            '--timeout=' . self::PAGE_TIMEOUT_MS,
            '--window-size=' . self::VIEWPORT_WIDTH . ',' . self::VIEWPORT_HEIGHT,
            '--dump-dom',
            $url,
        ];
        $null = DIRECTORY_SEPARATOR === '\\' ? 'NUL' : '/dev/null';
        $spec = [ 0 => [ 'file', $null, 'r' ], 1 => [ 'file', $stdout, 'w' ], 2 => [ 'file', $stderr, 'w' ] ];
        $options = DIRECTORY_SEPARATOR === '\\' ? [ 'bypass_shell' => true, 'suppress_errors' => true ] : [ 'bypass_shell' => true ];
        $start = microtime( true );
        $proc = @proc_open( $command, $spec, $pipes, null, null, $options );
        if ( ! is_resource( $proc ) ) { self::remove_tree( $profile ); return new WP_Error( 'elementize_render_metrics_browser_start_failed', 'Could not start the isolated local browser.' ); }

        $timed_out = false;
        while ( true ) {
            $status = @proc_get_status( $proc );
            if ( ! is_array( $status ) || empty( $status['running'] ) ) break;
            if ( microtime( true ) - $start >= self::PROCESS_TIMEOUT_SECONDS ) { $timed_out = true; @proc_terminate( $proc ); usleep( 200000 ); @proc_terminate( $proc, 9 ); break; }
            usleep( 100000 );
        }
        @proc_close( $proc );
        self::remove_tree( $profile );
        $wait = round( microtime( true ) - $start, 2 );
        $html = is_file( $stdout ) ? (string) @file_get_contents( $stdout ) : '';
        $err = is_file( $stderr ) ? (string) @file_get_contents( $stderr ) : '';
        if ( $timed_out && '' === $html ) { @unlink( $stdout ); @unlink( $stderr ); return new WP_Error( 'elementize_render_metrics_timeout', 'Local browser DOM capture exceeded ' . self::PROCESS_TIMEOUT_SECONDS . ' seconds.' ); }
        if ( '' === $html ) { @unlink( $stdout ); @unlink( $stderr ); return new WP_Error( 'elementize_render_metrics_empty', self::browser_error_summary( $err, 'Chrome did not return a DOM dump.' ) ); }
        return [ 'html' => $html, 'stdout' => $stdout, 'stderr' => $stderr, 'wait_seconds' => $wait ];
    }

    private static function fresh_signed_preview( int $id ) {
        $request = new WP_REST_Request( 'GET', '/' . self::NS . '/pages/' . $id . '/state' );
        $request->set_param( 'id', $id );
        $response = rest_do_request( $request );
        if ( is_wp_error( $response ) ) return $response;
        if ( ! $response instanceof WP_REST_Response || $response->get_status() >= 400 ) return new WP_Error( 'elementize_render_metrics_state_failed', 'Fresh page state could not be read.' );
        $data = $response->get_data();
        return is_array( $data ) && is_array( $data['visual_preview'] ?? null ) ? $data['visual_preview'] : new WP_Error( 'elementize_render_metrics_preview_missing', 'Fresh page state did not provide a signed visual preview.' );
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
        $candidates = array_merge( $candidates, [ '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge', '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/microsoft-edge', '/usr/bin/microsoft-edge-stable' ] );
        foreach ( array_unique( $candidates ) as $candidate ) if ( is_file( $candidate ) ) return $candidate;
        return '';
    }

    private static function temp_path( string $suffix ): string {
        $dir = function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir();
        return trailingslashit( $dir ) . 'elementize-render-metrics-' . wp_generate_uuid4() . $suffix;
    }

    private static function temp_profile_dir(): string {
        $dir = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() ) . 'elementize-render-metrics-profile-' . wp_generate_uuid4();
        return wp_mkdir_p( $dir ) && is_dir( $dir ) ? $dir : '';
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

    private static function browser_error_summary( string $stderr, string $fallback ): string {
        $ignored = [ 'device_event_log', 'usb_service_win.cc', 'registration_request.cc', 'PHONE_REGISTRATION_ERROR', 'DEPRECATED_ENDPOINT', 'gcm\\engine' ];
        $useful = [];
        foreach ( preg_split( '/\\R+/', $stderr ) ?: [] as $line ) {
            $line = trim( $line ); if ( '' === $line ) continue; $skip = false;
            foreach ( $ignored as $needle ) if ( false !== stripos( $line, $needle ) ) { $skip = true; break; }
            if ( ! $skip ) $useful[] = $line;
        }
        return $useful ? self::clip( implode( ' ', array_slice( $useful, -3 ) ), 500 ) : $fallback;
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
        $data['rendered_repair_metrics_local_chromium_dom'] = true;
        $data['rendered_repair_metrics_exact_top_level_ids'] = true;
        $data['rendered_repair_metrics_computed_typography'] = true;
        $data['rendered_repair_metrics_computed_spacing'] = true;
        $data['rendered_repair_metrics_cta_samples'] = true;
        $data['rendered_repair_metrics_media_samples'] = true;
        $data['rendered_repair_metrics_raw_dom_exposed'] = false;
        $data['rendered_repair_metrics_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

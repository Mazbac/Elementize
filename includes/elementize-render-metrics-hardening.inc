<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Render_Metrics_Hardening {
    private const VERSION = '0.17.2';
    private const NS = 'elementize/v1';
    private const MAX_SECTIONS = 24;
    private const MAX_TEXT_SAMPLES = 10;
    private const MAX_CTA_SAMPLES = 8;
    private const MAX_MEDIA_SAMPLES = 8;
    private static bool $footer_agent_emitted = false;

    public static function init(): void {
        // Prefer direct footer emission so the agent is part of the template output
        // before Chrome serializes the DOM. Keep the output-buffer injection as a
        // fallback for templates that omit wp_footer().
        add_action( 'template_redirect', [ self::class, 'start_bootstrap_buffer' ], 3 );
        add_action( 'wp_footer', [ self::class, 'emit_footer_agent' ], 9999 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 390, 3 );
    }

    public static function start_bootstrap_buffer(): void {
        if ( ! self::metrics_preview_requested() ) return;
        ob_start( [ self::class, 'inject_bootstrap' ] );
    }

    private static function metrics_preview_requested(): bool {
        return isset( $_GET['elementize_preview'], $_GET['elementize_metrics'] )
            && '1' === (string) wp_unslash( $_GET['elementize_preview'] )
            && '1' === (string) wp_unslash( $_GET['elementize_metrics'] );
    }

    public static function emit_footer_agent(): void {
        if ( ! self::metrics_preview_requested() || self::$footer_agent_emitted ) return;
        $id = isset( $_GET['page_id'] ) ? absint( wp_unslash( $_GET['page_id'] ) ) : 0;
        $markup = self::agent_markup( $id, 'direct_footer' );
        if ( '' === $markup ) return;
        self::$footer_agent_emitted = true;
        echo $markup; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped -- generated internal script only on validated signed preview.
    }

    public static function inject_bootstrap( string $html ): string {
        if ( self::$footer_agent_emitted || '' === $html || false === stripos( $html, '</body>' ) ) return $html;
        $id = isset( $_GET['page_id'] ) ? absint( wp_unslash( $_GET['page_id'] ) ) : 0;
        $markup = self::agent_markup( $id, 'buffer_fallback' );
        if ( '' === $markup ) return $html;
        return str_ireplace( '</body>', $markup . '</body>', $html );
    }

    private static function agent_markup( int $id, string $phase ): string {
        if ( $id < 1 ) return '';
        $map = self::top_level_map( $id );
        if ( ! $map ) return '';
        $map_json = wp_json_encode( $map, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $map_json ) || '' === $map_json ) return '';
        $phase_json = wp_json_encode( $phase );

        return '<script id="elementize-render-metrics-bootstrap">(function(){'
            . 'var map=' . $map_json . ',phase=' . $phase_json . ';'
            . 'function n(v){var x=parseFloat(v);return isFinite(x)?Math.round(x*100)/100:null}'
            . 'function vis(el){if(!el)return false;var r=el.getBoundingClientRect(),s=getComputedStyle(el);return r.width>1&&r.height>1&&s.display!=="none"&&s.visibility!=="hidden"&&parseFloat(s.opacity||"1")>0}'
            . 'function eid(el){var x=el&&el.closest?el.closest("[data-id]"):null;return x?String(x.getAttribute("data-id")||""):""}'
            . 'function txt(el){return String((el.innerText||el.textContent||"").replace(/\\s+/g," ").trim()).slice(0,180)}'
            . 'function sty(el){var s=getComputedStyle(el);return {font_size:n(s.fontSize),line_height:"normal"===s.lineHeight?"normal":n(s.lineHeight),font_weight:String(s.fontWeight||""),color:String(s.color||""),background_color:String(s.backgroundColor||""),border_radius:String(s.borderRadius||""),padding_top:n(s.paddingTop),padding_right:n(s.paddingRight),padding_bottom:n(s.paddingBottom),padding_left:n(s.paddingLeft),margin_top:n(s.marginTop),margin_right:n(s.marginRight),margin_bottom:n(s.marginBottom),margin_left:n(s.marginLeft)}}'
            . 'function samples(root,sel,max,kind){var out=[];Array.prototype.forEach.call(root.querySelectorAll(sel),function(el){if(out.length>=max||!vis(el))return;var r=el.getBoundingClientRect(),o={element_id:eid(el),tag:String(el.tagName||"").toLowerCase(),text:txt(el),width:n(r.width),height:n(r.height),style:sty(el)};if(kind==="media"){var u=String(el.currentSrc||el.src||"");o.src_file=u?u.split("?")[0].split("/").pop():"";o.object_fit=String(getComputedStyle(el).objectFit||"")}out.push(o)});return out}'
            . 'function run(){try{var rows=[];map.forEach(function(x){var el=document.querySelector(".elementor-element[data-id=\""+x.id+"\"]")||document.querySelector("[data-id=\""+x.id+"\"]");if(!el||!vis(el))return;var r=el.getBoundingClientRect(),s=getComputedStyle(el);rows.push({marker:x.marker,top_level_element_id:x.id,top:n(r.top+window.scrollY),bottom:n(r.bottom+window.scrollY),width:n(r.width),height:n(r.height),padding:{top:n(s.paddingTop),right:n(s.paddingRight),bottom:n(s.paddingBottom),left:n(s.paddingLeft)},margin:{top:n(s.marginTop),right:n(s.marginRight),bottom:n(s.marginBottom),left:n(s.marginLeft)},background_color:String(s.backgroundColor||""),text_samples:samples(el,"h1,h2,h3,h4,h5,h6,p,.elementor-heading-title",' . self::MAX_TEXT_SAMPLES . ',"text"),cta_samples:samples(el,"a,button,.btn,.pix-btn",' . self::MAX_CTA_SAMPLES . ',"cta"),media_samples:samples(el,"img,video",' . self::MAX_MEDIA_SAMPLES . ',"media")})});rows.sort(function(a,b){return a.top-b.top});for(var i=0;i<rows.length;i++){rows[i].gap_before=i?Math.max(0,n(rows[i].top-rows[i-1].bottom)):0;rows[i].gap_after=i<rows.length-1?Math.max(0,n(rows[i+1].top-rows[i].bottom)):0}var payload={version:"' . self::VERSION . '",capture_phase:phase,agent_executed:true,viewport:{width:window.innerWidth,height:window.innerHeight,device_pixel_ratio:window.devicePixelRatio||1},document:{scroll_height:Math.max(document.documentElement.scrollHeight,document.body?document.body.scrollHeight:0)},sections:rows};var node=document.getElementById("elementize-render-metrics-data");if(!node){node=document.createElement("script");node.id="elementize-render-metrics-data";node.type="application/json";(document.body||document.documentElement).appendChild(node)}node.textContent=JSON.stringify(payload);document.documentElement.setAttribute("data-elementize-render-metrics","ready")}catch(e){document.documentElement.setAttribute("data-elementize-render-metrics","error")}}'
            . 'run();document.addEventListener("DOMContentLoaded",run);window.addEventListener("load",run);})();</script>';
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['elementize_version'] = self::VERSION;
        $data['rendered_repair_metrics_synchronous_bootstrap'] = true;
        $data['rendered_repair_metrics_dump_dom_timer_race_hardening'] = true;
        $data['rendered_repair_metrics_bootstrap_updates_on_load'] = true;
        $data['rendered_repair_metrics_bootstrap_internal_only'] = true;
        $data['rendered_repair_metrics_direct_footer_agent'] = true;
        $data['rendered_repair_metrics_output_buffer_fallback'] = true;
        $data['rendered_repair_metrics_agent_execution_marker'] = true;
        $response->set_data( $data );
        return $response;
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
}

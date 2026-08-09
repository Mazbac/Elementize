<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Onboarding {
    private const VERSION = '0.6.1';
    private const SLUG = 'elementize-setup';
    private const OPT_PUBLIC = 'elementize_public_api_origin';
    private const OPT_GPT = 'elementize_wp_builder_gpt_url';

    public static function init(): void {
        add_action( 'admin_menu', [ self::class, 'menu' ], 30 );
        add_action( 'admin_post_elementize_save_onboarding', [ self::class, 'save' ] );
        add_action( 'wp_ajax_elementize_generate_connection_key', [ self::class, 'generate_key' ] );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 101, 3 );
    }

    public static function menu(): void {
        add_submenu_page( 'elementize', 'Elementize Setup', 'Setup WP Builder', 'manage_options', self::SLUG, [ self::class, 'render' ] );
    }

    public static function save(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_die( 'Administrator permission is required.' );
        check_admin_referer( 'elementize_save_onboarding' );
        $public = self::origin( isset( $_POST['elementize_public_api_origin'] ) ? wp_unslash( $_POST['elementize_public_api_origin'] ) : '' );
        if ( is_wp_error( $public ) ) self::redirect( 'public_url_invalid' );
        $gpt = self::gpt_url( isset( $_POST['elementize_wp_builder_gpt_url'] ) ? wp_unslash( $_POST['elementize_wp_builder_gpt_url'] ) : '' );
        if ( is_wp_error( $gpt ) ) self::redirect( 'gpt_url_invalid' );
        update_option( self::OPT_PUBLIC, $public, false );
        update_option( self::OPT_GPT, $gpt, false );
        self::redirect( 'saved' );
    }

    private static function redirect( string $notice ): void {
        wp_safe_redirect( add_query_arg( [ 'page' => self::SLUG, 'elementize_notice' => $notice ], admin_url( 'admin.php' ) ) );
        exit;
    }

    public static function generate_key(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_send_json_error( [ 'message' => 'Administrator permission is required.' ], 403 );
        check_ajax_referer( 'elementize_onboarding', 'nonce' );
        $user = wp_get_current_user();
        if ( ! $user || ! $user->exists() ) wp_send_json_error( [ 'message' => 'Current user could not be resolved.' ], 400 );
        if ( ! function_exists( 'wp_is_application_passwords_available_for_user' ) || ! wp_is_application_passwords_available_for_user( $user ) ) {
            wp_send_json_error( [ 'message' => 'WordPress Application Passwords are unavailable for this user/site.' ], 400 );
        }
        if ( ! class_exists( 'WP_Application_Passwords' ) ) wp_send_json_error( [ 'message' => 'Application Password support is unavailable.' ], 500 );
        $name = 'Elementize WP Builder - ' . gmdate( 'Y-m-d H:i:s' ) . ' UTC';
        $created = WP_Application_Passwords::create_new_application_password( $user->ID, [ 'name' => $name, 'app_id' => wp_generate_uuid4() ] );
        if ( is_wp_error( $created ) ) wp_send_json_error( [ 'message' => $created->get_error_message() ], 400 );
        $plain = (string) ( $created[0] ?? '' );
        if ( '' === $plain ) wp_send_json_error( [ 'message' => 'WordPress did not return the generated Application Password.' ], 500 );
        nocache_headers();
        wp_send_json_success( [
            'connection_key' => base64_encode( $user->user_login . ':' . $plain ),
            'name' => $name,
            'warning' => 'Treat this connection key like a password. Elementize does not store it and cannot show it again.',
        ] );
    }

    public static function render(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;
        $public = (string) get_option( self::OPT_PUBLIC, '' );
        if ( '' === $public ) $public = self::default_origin();
        $gpt = (string) get_option( self::OPT_GPT, '' );
        $instructions = self::file( 'WP_BUILDER_INSTRUCTIONS.md' );
        $schema = self::schema( $public );
        $nonce = wp_create_nonce( 'elementize_onboarding' );
        $notice = isset( $_GET['elementize_notice'] ) ? sanitize_key( wp_unslash( $_GET['elementize_notice'] ) ) : '';
        ?>
        <div class="wrap" style="max-width:1100px">
            <h1>Elementize → WP Builder Setup</h1>
            <p>Configure a guarded Custom GPT without manually editing schema server URLs or building a Basic-auth key in PowerShell.</p>
            <?php self::notice( $notice ); ?>
            <style>.elz-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.elz-card{background:#fff;border:1px solid #dcdcde;border-radius:8px;padding:20px}.elz-full{grid-column:1/-1}.elz-pass{color:#008a20;font-weight:600}.elz-warn{color:#b26200;font-weight:600}.elz-secret{font-family:monospace;word-break:break-all;background:#f6f7f7;border:1px solid #c3c4c7;padding:10px}.elz-text{width:100%;min-height:260px;font-family:monospace;font-size:12px}.elz-schema{min-height:380px}.elz-note{background:#fff8e5;border-left:4px solid #dba617;padding:10px;margin:10px 0}@media(max-width:900px){.elz-grid{grid-template-columns:1fr}.elz-full{grid-column:auto}}</style>
            <div class="elz-grid">
                <section class="elz-card"><h2>1. Prerequisites</h2><?php self::checks( $public ); ?></section>
                <section class="elz-card"><h2>2. Setup URLs</h2>
                    <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>"><input type="hidden" name="action" value="elementize_save_onboarding"><?php wp_nonce_field( 'elementize_save_onboarding' ); ?>
                    <p><label><strong>Public HTTPS origin</strong></label><br><input style="width:100%" class="regular-text code" name="elementize_public_api_origin" type="url" required value="<?php echo esc_attr( $public ); ?>"></p>
                    <p class="description">Origin only, for example <code>https://your-tunnel.example</code>. Do not include <code>/wp-json</code>.</p>
                    <p><label><strong>WP Builder GPT URL</strong> (optional until created)</label><br><input style="width:100%" class="regular-text code" name="elementize_wp_builder_gpt_url" type="url" value="<?php echo esc_attr( $gpt ); ?>"></p>
                    <p><button class="button button-primary">Save setup URLs</button><?php if ( $gpt ) : ?> <a class="button" target="_blank" rel="noopener" href="<?php echo esc_url( $gpt ); ?>">Open WP Builder</a><?php endif; ?></p></form>
                </section>
                <section class="elz-card"><h2>3. Connection Key</h2><p>Creates a dedicated WordPress Application Password and outputs the Base64 key expected by GPT Actions Basic authentication.</p><div class="elz-note">The generated key is a secret. It is shown once and is not stored by Elementize.</div><p><button type="button" class="button button-primary" id="elz-key-btn">Generate Connection Key</button></p><div id="elz-key-box" style="display:none"><div id="elz-key" class="elz-secret"></div><p><button type="button" class="button elz-copy" data-target="elz-key">Copy Connection Key</button></p><p id="elz-key-meta" class="description"></p></div><p id="elz-key-error" style="display:none;color:#b32d2e"></p></section>
                <section class="elz-card"><h2>4. GPT Builder</h2><p><a class="button button-primary" target="_blank" rel="noopener" href="https://chatgpt.com/gpts/editor">Open GPT Builder</a></p><ol><li>Paste the Instructions below.</li><li>Create a new Action and paste the generated schema.</li><li>Authentication: <strong>API Key → Basic</strong>.</li><li>Paste the Connection Key as the API key value.</li><li>Save, then test in Preview with <code>Call getElementizeStatus and show me the full raw result.</code></li></ol></section>
                <section class="elz-card elz-full"><h2>5. WP Builder Instructions</h2><textarea id="elz-instructions" class="elz-text" readonly><?php echo esc_textarea( $instructions ); ?></textarea><p><button type="button" class="button elz-copy" data-target="elz-instructions">Copy GPT Instructions</button></p></section>
                <section class="elz-card elz-full"><h2>6. Generated Actions Schema</h2><p>Generated for <code><?php echo esc_html( $public ); ?></code>.</p><textarea id="elz-schema" class="elz-text elz-schema" readonly><?php echo esc_textarea( $schema ); ?></textarea><p><button type="button" class="button elz-copy" data-target="elz-schema">Copy Actions Schema</button></p></section>
                <section class="elz-card elz-full"><h2>7. Finish</h2><p>After the GPT is saved, its status test should report <code>elementize_version: 0.6.1</code> and <code>onboarding_wizard: true</code>. Save the GPT link above for a one-click <strong>Open WP Builder</strong> shortcut.</p></section>
            </div>
        </div>
        <script>(function(){const ajax=<?php echo wp_json_encode( admin_url( 'admin-ajax.php' ) ); ?>,nonce=<?php echo wp_json_encode( $nonce ); ?>;function val(id){const e=document.getElementById(id);return e?(('value'in e)?e.value:e.textContent):''}async function copy(id,b){const t=val(id);if(!t)return;try{await navigator.clipboard.writeText(t);const o=b.textContent;b.textContent='Copied';setTimeout(()=>b.textContent=o,1000)}catch(e){const n=document.getElementById(id);if(n&&n.select){n.select();document.execCommand('copy')}}}document.querySelectorAll('.elz-copy').forEach(b=>b.onclick=()=>copy(b.dataset.target,b));const btn=document.getElementById('elz-key-btn');btn.onclick=async()=>{btn.disabled=true;btn.textContent='Generating…';const er=document.getElementById('elz-key-error');er.style.display='none';try{const body=new URLSearchParams({action:'elementize_generate_connection_key',nonce});const r=await fetch(ajax,{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString(),cache:'no-store'});const j=await r.json();if(!j.success)throw new Error(j?.data?.message||'Generation failed');document.getElementById('elz-key').textContent=j.data.connection_key;document.getElementById('elz-key-meta').textContent='Created: '+j.data.name+'. Copy it now; it will not be shown again.';document.getElementById('elz-key-box').style.display='block'}catch(e){er.textContent=e.message;er.style.display='block'}finally{btn.disabled=false;btn.textContent='Generate New Connection Key'}}})();</script>
        <?php
    }

    private static function checks( string $public ): void {
        global $wp_version;
        $u = wp_get_current_user();
        $app = function_exists( 'wp_is_application_passwords_available_for_user' ) && $u && $u->exists() && wp_is_application_passwords_available_for_user( $u );
        $elementor = class_exists( '\\Elementor\\Plugin' );
        $theme = wp_get_theme();
        $rows = [
            [ 'WordPress', (string) $wp_version, version_compare( (string) $wp_version, '6.5', '>=' ) ],
            [ 'Elementor', $elementor ? ( defined( 'ELEMENTOR_VERSION' ) ? ELEMENTOR_VERSION : 'Loaded' ) : 'Not loaded', $elementor ],
            [ 'Essentials theme', (string) $theme->get( 'Name' ), 'Essentials' === $theme->get( 'Name' ) ],
            [ 'Application Passwords', $app ? 'Available' : 'Unavailable', $app ],
            [ 'Elementize REST status', rest_url( 'elementize/v1/status' ), true ],
        ];
        foreach ( $rows as $r ) echo '<p><strong>' . esc_html( $r[0] ) . ':</strong> <span class="' . ( $r[2] ? 'elz-pass' : 'elz-warn' ) . '">' . esc_html( $r[1] ) . '</span></p>';
        if ( self::localish( $public ) ) echo '<div class="elz-note"><strong>Public URL required:</strong> this address looks local/private. ChatGPT Actions needs a public HTTPS hostname or tunnel.</div>';
    }

    private static function notice( string $n ): void {
        if ( 'saved' === $n ) echo '<div class="notice notice-success"><p>Elementize setup URLs saved.</p></div>';
        if ( 'public_url_invalid' === $n ) echo '<div class="notice notice-error"><p>The public URL must be an HTTPS origin with no path/query.</p></div>';
        if ( 'gpt_url_invalid' === $n ) echo '<div class="notice notice-error"><p>The GPT URL must be an HTTPS chatgpt.com URL.</p></div>';
    }

    private static function default_origin(): string {
        $p = wp_parse_url( home_url( '/' ) );
        if ( ! is_array( $p ) || empty( $p['host'] ) ) return 'https://example.com';
        return ( ! empty( $p['scheme'] ) ? strtolower( (string) $p['scheme'] ) : 'https' ) . '://' . $p['host'] . ( isset( $p['port'] ) ? ':' . absint( $p['port'] ) : '' );
    }

    private static function origin( $raw ) {
        $p = wp_parse_url( trim( (string) $raw ) );
        if ( ! is_array( $p ) || 'https' !== strtolower( (string) ( $p['scheme'] ?? '' ) ) || empty( $p['host'] ) ) return new WP_Error( 'invalid_origin' );
        if ( ! empty( $p['user'] ) || ! empty( $p['pass'] ) || ! empty( $p['query'] ) || ! empty( $p['fragment'] ) ) return new WP_Error( 'invalid_origin' );
        $path = trim( (string) ( $p['path'] ?? '' ) );
        if ( '' !== $path && '/' !== $path ) return new WP_Error( 'invalid_origin' );
        return 'https://' . strtolower( (string) $p['host'] ) . ( isset( $p['port'] ) ? ':' . absint( $p['port'] ) : '' );
    }

    private static function gpt_url( $raw ) {
        $raw = trim( (string) $raw ); if ( '' === $raw ) return '';
        $p = wp_parse_url( $raw ); $host = strtolower( (string) ( $p['host'] ?? '' ) );
        if ( ! is_array( $p ) || 'https' !== strtolower( (string) ( $p['scheme'] ?? '' ) ) || ! in_array( $host, [ 'chatgpt.com', 'www.chatgpt.com', 'chat.openai.com' ], true ) ) return new WP_Error( 'invalid_gpt_url' );
        return esc_url_raw( $raw, [ 'https' ] );
    }

    private static function localish( string $url ): bool {
        $h = strtolower( (string) wp_parse_url( $url, PHP_URL_HOST ) );
        return '' === $h || in_array( $h, [ 'localhost', '127.0.0.1', '::1' ], true ) || (bool) preg_match( '/\\.(local|test|localhost|invalid)$/', $h ) || (bool) preg_match( '/^(10\\.|192\\.168\\.|172\\.(1[6-9]|2[0-9]|3[01])\\.)/', $h );
    }

    private static function file( string $name ): string {
        $p = dirname( __DIR__ ) . '/' . $name; if ( ! is_readable( $p ) ) return '';
        $v = file_get_contents( $p ); return is_string( $v ) ? $v : '';
    }

    private static function schema( string $public ): string {
        $s = self::file( 'CUSTOM_GPT_ACTIONS.openapi.yaml' );
        $o = self::origin( $public ); if ( is_wp_error( $o ) ) $o = self::default_origin();
        $s = (string) preg_replace( '/(?m)^(\\s*)- url: .*$/', '${1}- url: ' . $o, $s, 1 );
        $s = str_replace( 'version: 0.5.14.0', 'version: 0.6.1.0', $s );
        $s = str_replace( 'Controlled WordPress, Elementor, Pixfort, Media Library, conversation-image import, visual, layout, lifecycle, and page-owned embedded Pixfort copy actions for Elementize.', 'Controlled WordPress, Elementor, Pixfort, media, links, quality audit, layout, lifecycle, and embedded-copy actions for Elementize.', $s );
        $s = str_replace( 'Current Cloudflare quick tunnel. Replace if the quick tunnel changes.', 'Elementize public WordPress API origin.', $s );
        $old_desc = 'Use when the user supplied an image in the conversation and wants that exact image used on the WordPress page. Send exactly one conversation image through openaiFileIdRefs. The action imports it as a normal WordPress image attachment; use the returned attachment_id in a later guarded media write. JPG, PNG, WEBP, AVIF, and GIF are supported. Do not use for unrelated files.';
        $new_desc = 'Import one image attached in this ChatGPT conversation into WordPress and return its attachment_id for a later guarded media write.';
        $s = str_replace( $old_desc, $new_desc, $s );
        if ( false === strpos( $s, 'operationId: getElementizePageQualityAudit' ) ) $s .= "\n" . self::file( 'CUSTOM_GPT_ACTIONS_0.6_APPEND.yaml' );
        return $s;
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $d = $response->get_data(); if ( ! is_array( $d ) ) return $response;
        $d['elementize_version'] = self::VERSION;
        $d['onboarding_wizard'] = true;
        $d['onboarding_schema_generator'] = true;
        $d['onboarding_connection_key_generator'] = true;
        $d['onboarding_connection_key_stored'] = false;
        $d['onboarding_gpt_url_storage'] = true;
        $d['onboarding_schema_server_indent_fix'] = true;
        $response->set_data( $d ); return $response;
    }
}

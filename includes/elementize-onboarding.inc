<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Onboarding {
    private const VERSION = '0.6.2';
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

        $saved = false;
        if ( isset( $_POST['elementize_public_api_origin'] ) ) {
            $public = self::origin( wp_unslash( $_POST['elementize_public_api_origin'] ) );
            if ( is_wp_error( $public ) ) self::redirect( 'public_url_invalid' );
            if ( self::localish( $public ) ) self::redirect( 'public_url_private' );
            update_option( self::OPT_PUBLIC, $public, false );
            $saved = true;
        }

        if ( isset( $_POST['elementize_wp_builder_gpt_url'] ) ) {
            $gpt = self::gpt_url( wp_unslash( $_POST['elementize_wp_builder_gpt_url'] ) );
            if ( is_wp_error( $gpt ) ) self::redirect( 'gpt_url_invalid' );
            update_option( self::OPT_GPT, $gpt, false );
            $saved = true;
        }

        self::redirect( $saved ? 'saved' : 'nothing_to_save' );
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
            wp_send_json_error( [ 'message' => 'Secure WordPress connection keys are unavailable for this user/site.' ], 400 );
        }
        if ( ! class_exists( 'WP_Application_Passwords' ) ) wp_send_json_error( [ 'message' => 'WordPress secure connection support is unavailable.' ], 500 );

        $name = 'Elementize WP Builder - ' . gmdate( 'Y-m-d H:i:s' ) . ' UTC';
        $created = WP_Application_Passwords::create_new_application_password( $user->ID, [ 'name' => $name, 'app_id' => wp_generate_uuid4() ] );
        if ( is_wp_error( $created ) ) wp_send_json_error( [ 'message' => $created->get_error_message() ], 400 );
        $plain = (string) ( $created[0] ?? '' );
        if ( '' === $plain ) wp_send_json_error( [ 'message' => 'WordPress did not return the generated connection key.' ], 500 );

        nocache_headers();
        wp_send_json_success( [
            'connection_key' => base64_encode( $user->user_login . ':' . $plain ),
            'name' => $name,
            'warning' => 'Treat this connection key like a password. Elementize does not store it and cannot show it again.',
        ] );
    }

    public static function render(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;

        $env = self::environment();
        $public = $env['effective_origin'];
        $gpt = (string) get_option( self::OPT_GPT, '' );
        $instructions = self::file( 'WP_BUILDER_INSTRUCTIONS.md' );
        $schema = $env['connection_ready'] ? self::schema( $public ) : '';
        $nonce = wp_create_nonce( 'elementize_onboarding' );
        $notice = isset( $_GET['elementize_notice'] ) ? sanitize_key( wp_unslash( $_GET['elementize_notice'] ) ) : '';
        ?>
        <div class="wrap elz-wrap">
            <h1>Connect Elementize to WP Builder</h1>
            <p class="elz-lead">Set up the WordPress connection once, then open your WP Builder directly from this dashboard.</p>
            <?php self::notice( $notice ); ?>
            <style>
                .elz-wrap{max-width:1080px}.elz-lead{font-size:15px;color:#50575e;margin-bottom:20px}
                .elz-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.elz-card{background:#fff;border:1px solid #dcdcde;border-radius:10px;padding:22px}.elz-full{grid-column:1/-1}
                .elz-card h2{margin-top:0}.elz-pass{color:#008a20;font-weight:600}.elz-warn{color:#9a6700;font-weight:600}.elz-muted{color:#646970}
                .elz-badge{display:inline-block;border-radius:999px;padding:4px 9px;font-size:12px;font-weight:700;margin-bottom:8px}.elz-badge-pass{background:#edfaef;color:#06752e}.elz-badge-warn{background:#fff8e5;color:#8a5b00}
                .elz-secret{font-family:monospace;word-break:break-all;background:#f6f7f7;border:1px solid #c3c4c7;padding:10px;border-radius:4px}.elz-note{background:#fff8e5;border-left:4px solid #dba617;padding:10px;margin:12px 0}
                .elz-actions{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.elz-text{width:100%;min-height:220px;font-family:monospace;font-size:12px}.elz-schema{min-height:320px}
                .elz-check{margin:7px 0}.elz-url{font-family:monospace;word-break:break-all}.elz-details{margin-top:14px}.elz-details summary{cursor:pointer;font-weight:600}.elz-step{margin:8px 0}
                @media(max-width:900px){.elz-grid{grid-template-columns:1fr}.elz-full{grid-column:auto}}
            </style>

            <div class="elz-grid">
                <section class="elz-card elz-full">
                    <h2>1. Website connection</h2>
                    <?php self::environment_summary( $env ); ?>
                    <?php self::checks(); ?>

                    <?php if ( $env['site_local'] ) : ?>
                        <div class="elz-note"><strong>Local development site detected.</strong> ChatGPT cannot reach <span class="elz-url"><?php echo esc_html( $env['site_origin'] ); ?></span> directly, so this installation needs a public HTTPS tunnel or staging address.</div>
                        <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>">
                            <input type="hidden" name="action" value="elementize_save_onboarding"><?php wp_nonce_field( 'elementize_save_onboarding' ); ?>
                            <p><label for="elz-public"><strong>Public connection URL</strong></label><br>
                            <input id="elz-public" style="width:100%;max-width:720px" class="regular-text code" name="elementize_public_api_origin" type="url" required value="<?php echo esc_attr( $env['stored_public'] ); ?>" placeholder="https://your-public-tunnel.example"></p>
                            <p class="description">Use only the HTTPS origin. Do not add <code>/wp-json</code>.</p>
                            <p><button class="button button-primary">Save connection URL</button></p>
                        </form>
                    <?php else : ?>
                        <p><strong>Connection address:</strong> <span class="elz-url"><?php echo esc_html( $public ); ?></span></p>
                        <p class="elz-muted">Elementize detected a public HTTPS WordPress site, so no tunnel setup is required.</p>
                        <details class="elz-details"><summary>Advanced: use a different public API address</summary>
                            <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>" style="margin-top:10px">
                                <input type="hidden" name="action" value="elementize_save_onboarding"><?php wp_nonce_field( 'elementize_save_onboarding' ); ?>
                                <input style="width:100%;max-width:720px" class="regular-text code" name="elementize_public_api_origin" type="url" value="<?php echo esc_attr( $public ); ?>">
                                <p><button class="button">Save custom address</button></p>
                            </form>
                        </details>
                    <?php endif; ?>
                </section>

                <section class="elz-card">
                    <h2>2. Create connection key</h2>
                    <p>This key securely connects your WP Builder to this WordPress site.</p>
                    <div class="elz-note">The key is shown once and is never stored by Elementize. Treat it like a password.</div>
                    <p><button type="button" class="button button-primary" id="elz-key-btn" <?php disabled( ! $env['connection_ready'] ); ?>>Generate Connection Key</button></p>
                    <?php if ( ! $env['connection_ready'] ) : ?><p class="elz-muted">Finish the website connection above first.</p><?php endif; ?>
                    <div id="elz-key-box" style="display:none"><div id="elz-key" class="elz-secret"></div><p><button type="button" class="button elz-copy" data-target="elz-key">Copy Connection Key</button></p><p id="elz-key-meta" class="description"></p></div>
                    <p id="elz-key-error" style="display:none;color:#b32d2e"></p>
                </section>

                <section class="elz-card">
                    <h2>3. Create your WP Builder</h2>
                    <?php if ( $env['connection_ready'] ) : ?>
                        <div class="elz-actions">
                            <a class="button button-primary" target="_blank" rel="noopener" href="https://chatgpt.com/gpts/editor">Open GPT Builder</a>
                            <button type="button" class="button elz-copy" data-target="elz-instructions">Copy Instructions</button>
                            <button type="button" class="button elz-copy" data-target="elz-schema">Copy Actions Schema</button>
                        </div>
                        <p class="elz-step"><strong>Then:</strong> paste the Instructions, create an Action with the copied schema, choose <strong>API Key → Basic</strong> authentication, and paste your Connection Key.</p>
                        <p class="elz-step">Test in Preview with <code>Call getElementizeStatus and show me the full raw result.</code></p>
                    <?php else : ?>
                        <p class="elz-muted">The GPT setup materials will be available after the website connection is ready.</p>
                    <?php endif; ?>
                </section>

                <?php if ( $env['connection_ready'] ) : ?>
                <section class="elz-card elz-full">
                    <details class="elz-details"><summary>View setup materials</summary>
                        <h3>WP Builder Instructions</h3>
                        <textarea id="elz-instructions" class="elz-text" readonly><?php echo esc_textarea( $instructions ); ?></textarea>
                        <p><button type="button" class="button elz-copy" data-target="elz-instructions">Copy Instructions</button></p>
                        <h3>Actions Schema</h3>
                        <p>Generated for <code><?php echo esc_html( $public ); ?></code>.</p>
                        <textarea id="elz-schema" class="elz-text elz-schema" readonly><?php echo esc_textarea( $schema ); ?></textarea>
                        <p><button type="button" class="button elz-copy" data-target="elz-schema">Copy Actions Schema</button></p>
                    </details>
                </section>
                <?php else : ?>
                    <textarea id="elz-instructions" style="display:none" readonly></textarea><textarea id="elz-schema" style="display:none" readonly></textarea>
                <?php endif; ?>

                <section class="elz-card elz-full">
                    <h2>4. Finish</h2>
                    <p>Save the link to your finished Custom GPT so you can launch it directly from WordPress.</p>
                    <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>">
                        <input type="hidden" name="action" value="elementize_save_onboarding"><?php wp_nonce_field( 'elementize_save_onboarding' ); ?>
                        <p><label for="elz-gpt"><strong>WP Builder GPT URL</strong></label><br>
                        <input id="elz-gpt" style="width:100%;max-width:720px" class="regular-text code" name="elementize_wp_builder_gpt_url" type="url" value="<?php echo esc_attr( $gpt ); ?>" placeholder="https://chatgpt.com/g/..."></p>
                        <div class="elz-actions"><button class="button button-primary">Save WP Builder link</button><?php if ( $gpt ) : ?><a class="button" target="_blank" rel="noopener" href="<?php echo esc_url( $gpt ); ?>">Open WP Builder</a><?php endif; ?></div>
                    </form>
                </section>
            </div>
        </div>

        <script>(function(){
            const ajax=<?php echo wp_json_encode( admin_url( 'admin-ajax.php' ) ); ?>,nonce=<?php echo wp_json_encode( $nonce ); ?>;
            function val(id){const e=document.getElementById(id);return e?(('value'in e)?e.value:e.textContent):''}
            async function copy(id,b){const t=val(id);if(!t)return;try{await navigator.clipboard.writeText(t);const o=b.textContent;b.textContent='Copied';setTimeout(()=>b.textContent=o,1000)}catch(e){const n=document.getElementById(id);if(n&&n.select){n.select();document.execCommand('copy')}}}
            document.querySelectorAll('.elz-copy').forEach(b=>b.onclick=()=>copy(b.dataset.target,b));
            const btn=document.getElementById('elz-key-btn');
            if(btn) btn.onclick=async()=>{btn.disabled=true;btn.textContent='Generating…';const er=document.getElementById('elz-key-error');er.style.display='none';try{const body=new URLSearchParams({action:'elementize_generate_connection_key',nonce});const r=await fetch(ajax,{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString(),cache:'no-store'});const j=await r.json();if(!j.success)throw new Error(j?.data?.message||'Generation failed');document.getElementById('elz-key').textContent=j.data.connection_key;document.getElementById('elz-key-meta').textContent='Created: '+j.data.name+'. Copy it now; it will not be shown again.';document.getElementById('elz-key-box').style.display='block'}catch(e){er.textContent=e.message;er.style.display='block'}finally{btn.disabled=false;btn.textContent='Generate New Connection Key'}};
        })();</script>
        <?php
    }

    private static function environment(): array {
        $site = self::default_origin();
        $site_parts = wp_parse_url( $site );
        $site_https = is_array( $site_parts ) && 'https' === strtolower( (string) ( $site_parts['scheme'] ?? '' ) );
        $site_local = self::localish( $site );

        $stored = trim( (string) get_option( self::OPT_PUBLIC, '' ) );
        $stored_origin = '';
        if ( '' !== $stored ) {
            $candidate = self::origin( $stored );
            if ( ! is_wp_error( $candidate ) && ! self::localish( $candidate ) ) $stored_origin = $candidate;
        }

        if ( ! $site_local && $site_https ) {
            $effective = '' !== $stored_origin ? $stored_origin : $site;
            return [
                'mode' => 'hosted',
                'site_origin' => $site,
                'site_local' => false,
                'site_https' => true,
                'stored_public' => $stored_origin,
                'effective_origin' => $effective,
                'connection_ready' => true,
            ];
        }

        return [
            'mode' => 'local',
            'site_origin' => $site,
            'site_local' => $site_local,
            'site_https' => $site_https,
            'stored_public' => $stored_origin,
            'effective_origin' => $stored_origin,
            'connection_ready' => '' !== $stored_origin,
        ];
    }

    private static function environment_summary( array $env ): void {
        if ( 'hosted' === $env['mode'] ) {
            echo '<span class="elz-badge elz-badge-pass">Hosted website detected</span>';
            echo '<p><strong>Ready:</strong> Elementize can use this website\'s public HTTPS address automatically.</p>';
            return;
        }
        if ( $env['connection_ready'] ) {
            echo '<span class="elz-badge elz-badge-pass">Local site connected</span>';
            echo '<p><strong>Ready:</strong> the local WordPress site has a public connection URL for ChatGPT.</p>';
            return;
        }
        echo '<span class="elz-badge elz-badge-warn">Local development site</span>';
        echo '<p><strong>Action needed:</strong> add a public HTTPS tunnel or staging address below.</p>';
    }

    private static function checks(): void {
        global $wp_version;
        $u = wp_get_current_user();
        $app = function_exists( 'wp_is_application_passwords_available_for_user' ) && $u && $u->exists() && wp_is_application_passwords_available_for_user( $u );
        $elementor = class_exists( '\\Elementor\\Plugin' );
        $theme = wp_get_theme();
        $rows = [
            [ 'WordPress', (string) $wp_version, version_compare( (string) $wp_version, '6.5', '>=' ) ],
            [ 'Elementor', $elementor ? ( defined( 'ELEMENTOR_VERSION' ) ? ELEMENTOR_VERSION : 'Loaded' ) : 'Not loaded', $elementor ],
            [ 'Essentials', (string) $theme->get( 'Name' ), 'Essentials' === $theme->get( 'Name' ) ],
            [ 'Secure connection support', $app ? 'Available' : 'Unavailable', $app ],
        ];
        foreach ( $rows as $r ) echo '<p class="elz-check"><strong>' . esc_html( $r[0] ) . ':</strong> <span class="' . ( $r[2] ? 'elz-pass' : 'elz-warn' ) . '">' . esc_html( $r[1] ) . '</span></p>';
    }

    private static function notice( string $n ): void {
        if ( 'saved' === $n ) echo '<div class="notice notice-success"><p>Elementize setup saved.</p></div>';
        if ( 'public_url_invalid' === $n ) echo '<div class="notice notice-error"><p>The public connection URL must be an HTTPS origin with no path or query.</p></div>';
        if ( 'public_url_private' === $n ) echo '<div class="notice notice-error"><p>That address is still local/private. Enter the public HTTPS tunnel or staging address.</p></div>';
        if ( 'gpt_url_invalid' === $n ) echo '<div class="notice notice-error"><p>The WP Builder link must be an HTTPS ChatGPT URL.</p></div>';
        if ( 'nothing_to_save' === $n ) echo '<div class="notice notice-warning"><p>Nothing was changed.</p></div>';
    }

    private static function default_origin(): string {
        $p = wp_parse_url( home_url( '/' ) );
        if ( ! is_array( $p ) || empty( $p['host'] ) ) return '';
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
        $raw = trim( (string) $raw );
        if ( '' === $raw ) return '';
        $p = wp_parse_url( $raw );
        $host = strtolower( (string) ( $p['host'] ?? '' ) );
        if ( ! is_array( $p ) || 'https' !== strtolower( (string) ( $p['scheme'] ?? '' ) ) || ! in_array( $host, [ 'chatgpt.com', 'www.chatgpt.com', 'chat.openai.com' ], true ) ) return new WP_Error( 'invalid_gpt_url' );
        return esc_url_raw( $raw, [ 'https' ] );
    }

    private static function localish( string $url ): bool {
        $h = strtolower( (string) wp_parse_url( $url, PHP_URL_HOST ) );
        return '' === $h || in_array( $h, [ 'localhost', '127.0.0.1', '::1' ], true ) || (bool) preg_match( '/\\.(local|test|localhost|invalid)$/', $h ) || (bool) preg_match( '/^(10\\.|192\\.168\\.|172\\.(1[6-9]|2[0-9]|3[01])\\.)/', $h );
    }

    private static function file( string $name ): string {
        $p = dirname( __DIR__ ) . '/' . $name;
        if ( ! is_readable( $p ) ) return '';
        $v = file_get_contents( $p );
        return is_string( $v ) ? $v : '';
    }

    private static function schema( string $public ): string {
        $s = self::file( 'CUSTOM_GPT_ACTIONS.openapi.yaml' );
        $o = self::origin( $public );
        if ( is_wp_error( $o ) ) return '';
        $s = (string) preg_replace( '/(?m)^(\\s*)- url: .*$/', '${1}- url: ' . $o, $s, 1 );
        $s = str_replace( 'version: 0.5.14.0', 'version: 0.6.2.0', $s );
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
        $d = $response->get_data();
        if ( ! is_array( $d ) ) return $response;
        $d['elementize_version'] = self::VERSION;
        $d['onboarding_wizard'] = true;
        $d['onboarding_schema_generator'] = true;
        $d['onboarding_connection_key_generator'] = true;
        $d['onboarding_connection_key_stored'] = false;
        $d['onboarding_gpt_url_storage'] = true;
        $d['onboarding_schema_server_indent_fix'] = true;
        $d['onboarding_environment_detection'] = true;
        $d['onboarding_hosted_origin_auto_detect'] = true;
        $d['onboarding_local_origin_conditional'] = true;
        $d['onboarding_simplified_setup_ui'] = true;
        $response->set_data( $d );
        return $response;
    }
}

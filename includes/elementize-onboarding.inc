<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Onboarding {
    private const SLUG = 'elementize';
    private const OPT_PUBLIC_ORIGIN = 'elementize_public_api_origin';
    private const REDIRECT_TRANSIENT = 'elementize_activation_redirect';

    public static function init(): void {
        add_action( 'admin_menu', [ self::class, 'menu' ] );
        add_action( 'admin_init', [ self::class, 'maybe_redirect_after_activation' ] );
        add_action( 'admin_enqueue_scripts', [ self::class, 'enqueue_assets' ] );
        add_action( 'admin_post_elementize_save_connection', [ self::class, 'save_connection' ] );
        add_action( 'wp_ajax_elementize_generate_connection_key', [ self::class, 'generate_key' ] );
    }

    public static function activate(): void {
        set_transient( self::REDIRECT_TRANSIENT, '1', 60 );
    }

    public static function maybe_redirect_after_activation(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;
        if ( wp_doing_ajax() || wp_doing_cron() ) return;
        if ( defined( 'WP_CLI' ) && WP_CLI ) return;
        if ( isset( $_GET['activate-multi'] ) ) return;
        if ( '1' !== get_transient( self::REDIRECT_TRANSIENT ) ) return;
        delete_transient( self::REDIRECT_TRANSIENT );
        wp_safe_redirect( admin_url( 'admin.php?page=' . self::SLUG ) );
        exit;
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', self::SLUG, [ self::class, 'render' ], 'dashicons-edit-page', 58 );
    }

    public static function enqueue_assets( string $hook ): void {
        if ( 'toplevel_page_' . self::SLUG !== $hook || ! current_user_can( 'manage_options' ) ) return;
        if ( ! self::frontend_assets_ready() ) return;

        $css_path = trailingslashit( ELEMENTIZE_DIR ) . 'assets/admin/elementize-admin.css';
        $js_path = trailingslashit( ELEMENTIZE_DIR ) . 'assets/admin/elementize-admin.js';
        $base_url = trailingslashit( plugin_dir_url( ELEMENTIZE_FILE ) );

        wp_enqueue_style(
            'elementize-admin',
            $base_url . 'assets/admin/elementize-admin.css',
            [],
            (string) filemtime( $css_path )
        );
        wp_enqueue_script(
            'elementize-admin',
            $base_url . 'assets/admin/elementize-admin.js',
            [],
            (string) filemtime( $js_path ),
            true
        );
        wp_add_inline_script(
            'elementize-admin',
            'window.ElementizeAdminConfig = ' . wp_json_encode( self::frontend_config() ) . ';',
            'before'
        );
    }

    public static function save_connection(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_die( 'Administrator permission is required.' );
        check_admin_referer( 'elementize_save_connection' );
        if ( isset( $_POST['elementize_clear_public_origin'] ) && '1' === (string) wp_unslash( $_POST['elementize_clear_public_origin'] ) ) {
            delete_option( self::OPT_PUBLIC_ORIGIN );
            self::redirect( 'connection_cleared' );
        }
        $raw = isset( $_POST['elementize_public_api_origin'] ) ? trim( (string) wp_unslash( $_POST['elementize_public_api_origin'] ) ) : '';
        if ( '' === $raw ) self::redirect( 'connection_invalid' );
        $origin = self::normalize_public_origin( $raw );
        if ( is_wp_error( $origin ) ) self::redirect( 'connection_invalid' );
        update_option( self::OPT_PUBLIC_ORIGIN, $origin, false );
        self::redirect( 'connection_saved' );
    }

    private static function redirect( string $notice ): void {
        wp_safe_redirect( add_query_arg( [ 'page' => self::SLUG, 'elementize_notice' => $notice ], admin_url( 'admin.php' ) ) );
        exit;
    }

    public static function generate_key(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_send_json_error( [ 'message' => 'Administrator permission is required.' ], 403 );
        check_ajax_referer( 'elementize_onboarding', 'nonce' );
        $user = wp_get_current_user();
        if ( ! $user || ! $user->exists() ) wp_send_json_error( [ 'message' => 'Current WordPress user could not be resolved.' ], 400 );
        if ( ! self::application_passwords_available( $user ) ) wp_send_json_error( [ 'message' => 'WordPress Application Passwords are unavailable for this user/site.' ], 400 );
        if ( ! class_exists( 'WP_Application_Passwords' ) ) wp_send_json_error( [ 'message' => 'WordPress Application Password support is unavailable.' ], 500 );

        $name = 'Elementize - ' . gmdate( 'Y-m-d H:i:s' ) . ' UTC';
        $created = WP_Application_Passwords::create_new_application_password( $user->ID, [ 'name' => $name, 'app_id' => wp_generate_uuid4() ] );
        if ( is_wp_error( $created ) ) wp_send_json_error( [ 'message' => $created->get_error_message() ], 400 );
        $plain = (string) ( $created[0] ?? '' );
        if ( '' === $plain ) wp_send_json_error( [ 'message' => 'WordPress did not return the generated Application Password.' ], 500 );

        nocache_headers();
        wp_send_json_success( [
            'connection_key' => base64_encode( $user->user_login . ':' . $plain ),
            'warning' => 'Shown once. Treat this connection key like a password. Elementize does not store it.',
        ] );
    }

    public static function render(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;
        if ( self::frontend_assets_ready() ) {
            echo '<div id="elementize-admin-root"></div>';
            return;
        }

        $env = self::environment();
        $requirements = self::requirements();
        $instructions = self::read_config_file( 'gpt/wp-builder-instructions.md' );
        $schema = $env['connection_ready'] ? self::schema( $env['effective_origin'] ) : '';
        $materials_ready = '' !== $instructions && '' !== $schema;
        $notice = isset( $_GET['elementize_notice'] ) ? sanitize_key( wp_unslash( $_GET['elementize_notice'] ) ) : '';
        $nonce = wp_create_nonce( 'elementize_onboarding' );
        $all_ready = $requirements['elementor'] && $requirements['pixfort'] && $requirements['revisions'] && $requirements['application_passwords'] && $env['connection_ready'] && $materials_ready;
        ?>
        <div class="wrap elz-wrap">
            <h1>Elementize Setup</h1>
            <p class="elz-lead">Connect a Custom GPT to guarded editing for existing Elementor + Pixfort pages. Standard mode handles recognized content, media, and icons; optional Creative Control unlocks guarded page-scoped design and structure.</p>
            <?php self::render_notice( $notice ); ?>
            <style>
                .elz-wrap{max-width:1040px}.elz-lead{font-size:15px;color:#50575e;max-width:850px}.elz-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px;margin:20px 0}.elz-card{background:#fff;border:1px solid #dcdcde;border-radius:10px;padding:20px;margin:16px 0}.elz-card h2{margin:0 0 10px}.elz-check{background:#fff;border:1px solid #dcdcde;border-radius:8px;padding:14px}.elz-ok{color:#087b2d;font-weight:700}.elz-bad{color:#b32d2e;font-weight:700}.elz-muted{color:#646970}.elz-code{font-family:monospace;word-break:break-all;background:#f6f7f7;border:1px solid #dcdcde;padding:10px;border-radius:6px}.elz-step{display:grid;grid-template-columns:34px 1fr;gap:12px;margin:18px 0}.elz-num{width:30px;height:30px;border-radius:50%;display:flex;align-items:center;justify-content:center;background:#f0f0f1;font-weight:700}.elz-hidden{position:absolute!important;left:-10000px!important;width:1px!important;height:1px!important;overflow:hidden!important}.elz-note{border-left:4px solid #dba617;background:#fff8e5;padding:10px 12px;margin:12px 0}.elz-ready{border-left:4px solid #00a32a;background:#edfaef;padding:12px;margin:16px 0}.elz-error{display:none;color:#b32d2e;margin-top:8px}
            </style>

            <div class="elz-grid">
                <?php self::requirement_card( 'Elementor', $requirements['elementor'], $requirements['elementor_detail'] ); ?>
                <?php self::requirement_card( 'Pixfort Core', $requirements['pixfort'], $requirements['pixfort_detail'] ); ?>
                <?php self::requirement_card( 'WordPress revisions', $requirements['revisions'], $requirements['revisions'] ? 'Enabled' : 'Required for safe writes' ); ?>
                <?php self::requirement_card( 'Application Passwords', $requirements['application_passwords'], $requirements['application_passwords'] ? 'Available' : 'Unavailable' ); ?>
                <?php self::requirement_card( 'Public API address', $env['connection_ready'], $env['connection_ready'] ? $env['effective_origin'] : 'Needs setup' ); ?>
            </div>

            <section class="elz-card">
                <h2>1. Connection address</h2>
                <?php if ( $env['site_public_https'] && '' === $env['stored_public'] ) : ?>
                    <p>Elementize detected this public HTTPS address automatically:</p><div class="elz-code"><?php echo esc_html( $env['effective_origin'] ); ?></div>
                <?php elseif ( $env['connection_ready'] ) : ?>
                    <p>Elementize is using this public address:</p><div class="elz-code"><?php echo esc_html( $env['effective_origin'] ); ?></div>
                <?php else : ?>
                    <div class="elz-note"><strong>A public HTTPS address is required.</strong> For a local WordPress site, start an HTTPS tunnel first and paste only its public origin below.</div>
                <?php endif; ?>
                <details<?php echo $env['connection_ready'] ? '' : ' open'; ?>>
                    <summary><strong><?php echo $env['connection_ready'] ? 'Use a different address' : 'Set public HTTPS address'; ?></strong></summary>
                    <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>" style="margin-top:12px">
                        <input type="hidden" name="action" value="elementize_save_connection"><?php wp_nonce_field( 'elementize_save_connection' ); ?>
                        <p><input name="elementize_public_api_origin" type="url" class="regular-text code" style="width:100%;max-width:720px" value="<?php echo esc_attr( $env['stored_public'] ); ?>" placeholder="https://your-tunnel.example"></p>
                        <p class="description">Origin only; do not add <code>/wp-json</code>.</p><p><button class="button button-primary">Save address</button></p>
                    </form>
                    <?php if ( '' !== $env['stored_public'] ) : ?>
                        <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>"><input type="hidden" name="action" value="elementize_save_connection"><input type="hidden" name="elementize_clear_public_origin" value="1"><?php wp_nonce_field( 'elementize_save_connection' ); ?><p><button class="button">Clear override</button></p></form>
                    <?php endif; ?>
                </details>
            </section>

            <section class="elz-card">
                <h2>2. Connect your Custom GPT</h2>
                <?php if ( ! $env['connection_ready'] ) : ?>
                    <p class="elz-muted">Set the public HTTPS address above first.</p>
                <?php elseif ( ! $materials_ready ) : ?>
                    <div class="notice notice-error inline"><p>Elementize setup materials could not be loaded. Reinstall the plugin files before connecting the GPT.</p></div>
                <?php else : ?>
                    <div class="elz-step"><div class="elz-num">A</div><div><strong>Open GPT Builder</strong><p><a class="button" target="_blank" rel="noopener" href="https://chatgpt.com/gpts/editor">Open GPT Builder</a></p></div></div>
                    <div class="elz-step"><div class="elz-num">B</div><div><strong>Paste Instructions</strong><p><button type="button" class="button button-primary elz-copy" data-target="elz-instructions">Copy Instructions</button></p></div></div>
                    <div class="elz-step"><div class="elz-num">C</div><div><strong>Create one Action</strong><p>Choose <strong>Actions → Create new action</strong> and paste the schema.</p><p><button type="button" class="button button-primary elz-copy" data-target="elz-schema">Copy Action Schema</button></p></div></div>
                    <div class="elz-step"><div class="elz-num">D</div><div><strong>Add authentication</strong><p>Choose <strong>API Key → Basic</strong>. Generate the one-time connection key below and paste it as the API key.</p><div class="elz-note">The key is an encoded WordPress username + Application Password. It is shown once and is not stored by Elementize.</div><p><button type="button" class="button button-primary" id="elz-key-btn">Generate Connection Key</button></p><div id="elz-key-wrap" style="display:none"><div id="elz-key" class="elz-code"></div><p><button type="button" class="button elz-copy" data-target="elz-key">Copy Connection Key</button></p></div><p id="elz-key-error" class="elz-error"></p></div></div>
                    <div class="elz-step"><div class="elz-num">E</div><div><strong>Test</strong><div class="elz-code">Call getElementizeStatus and show me the full raw result.</div></div></div>
                    <textarea id="elz-instructions" class="elz-hidden" readonly aria-hidden="true"><?php echo esc_textarea( $instructions ); ?></textarea>
                    <textarea id="elz-schema" class="elz-hidden" readonly aria-hidden="true"><?php echo esc_textarea( $schema ); ?></textarea>
                <?php endif; ?>
            </section>

            <section class="elz-card"><h2>3. Ready</h2>
                <?php if ( $all_ready ) : ?><div class="elz-ready"><strong>WordPress is ready.</strong><br>Elementize can safely edit recognized copy, links, WordPress images, and Pixfort icons in Standard mode. When an administrator enables Creative Control for one page, guarded page-scoped design and structural controls become available too.</div>
                <?php else : ?><p class="elz-muted">Resolve the requirements marked in red above.</p><?php endif; ?>
            </section>
        </div>
        <script>
        (function(){
            function fallbackCopy(text){var area=document.createElement('textarea');area.value=text;area.setAttribute('readonly','');area.style.position='fixed';area.style.opacity='0';document.body.appendChild(area);area.select();var ok=false;try{ok=document.execCommand('copy');}catch(e){ok=false;}document.body.removeChild(area);return ok;}
            function copyText(text){if(navigator.clipboard&&window.isSecureContext){return navigator.clipboard.writeText(text);}return fallbackCopy(text)?Promise.resolve():Promise.reject(new Error('Clipboard access is unavailable in this browser.'));}
            function copyTarget(id,button){var element=document.getElementById(id);if(!element)return;var text=('value' in element)?element.value:element.textContent;copyText(text).then(function(){var original=button.textContent;button.textContent='Copied';setTimeout(function(){button.textContent=original;},1400);}).catch(function(error){window.alert(error.message);});}
            document.querySelectorAll('.elz-copy').forEach(function(button){button.addEventListener('click',function(){copyTarget(button.getAttribute('data-target'),button);});});
            var keyButton=document.getElementById('elz-key-btn');if(!keyButton)return;
            keyButton.addEventListener('click',function(){keyButton.disabled=true;var errorBox=document.getElementById('elz-key-error');errorBox.style.display='none';var body=new URLSearchParams();body.set('action','elementize_generate_connection_key');body.set('nonce',<?php echo wp_json_encode( $nonce ); ?>);fetch(ajaxurl,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded; charset=UTF-8'},body:body.toString(),credentials:'same-origin'}).then(function(response){return response.json();}).then(function(result){if(!result||!result.success)throw new Error(result&&result.data&&result.data.message?result.data.message:'Could not generate key.');document.getElementById('elz-key').textContent=result.data.connection_key;document.getElementById('elz-key-wrap').style.display='block';}).catch(function(error){errorBox.textContent=error.message;errorBox.style.display='block';}).finally(function(){keyButton.disabled=false;});});
        })();
        </script>
        <?php
    }

    private static function frontend_assets_ready(): bool {
        return is_readable( trailingslashit( ELEMENTIZE_DIR ) . 'assets/admin/elementize-admin.css' )
            && is_readable( trailingslashit( ELEMENTIZE_DIR ) . 'assets/admin/elementize-admin.js' );
    }

    private static function frontend_config(): array {
        $env = self::environment();
        $requirements = self::requirements();
        $instructions = self::read_config_file( 'gpt/wp-builder-instructions.md' );
        $schema = $env['connection_ready'] ? self::schema( $env['effective_origin'] ) : '';
        $materials_ready = '' !== $instructions && '' !== $schema;
        $notice = isset( $_GET['elementize_notice'] ) ? sanitize_key( wp_unslash( $_GET['elementize_notice'] ) ) : '';
        $all_ready = $requirements['elementor'] && $requirements['pixfort'] && $requirements['revisions'] && $requirements['application_passwords'] && $env['connection_ready'] && $materials_ready;

        return [
            'version' => defined( 'ELEMENTIZE_VERSION' ) ? ELEMENTIZE_VERSION : '',
            'requirements' => [
                'elementor' => (bool) $requirements['elementor'],
                'elementorDetail' => (string) $requirements['elementor_detail'],
                'pixfort' => (bool) $requirements['pixfort'],
                'pixfortDetail' => (string) $requirements['pixfort_detail'],
                'revisions' => (bool) $requirements['revisions'],
                'applicationPasswords' => (bool) $requirements['application_passwords'],
            ],
            'environment' => [
                'siteOrigin' => (string) $env['site_origin'],
                'sitePublicHttps' => (bool) $env['site_public_https'],
                'storedPublic' => (string) $env['stored_public'],
                'effectiveOrigin' => (string) $env['effective_origin'],
                'connectionReady' => (bool) $env['connection_ready'],
            ],
            'materialsReady' => $materials_ready,
            'allReady' => $all_ready,
            'instructions' => $instructions,
            'schema' => $schema,
            'notice' => $notice,
            'testPrompt' => 'Call getElementizeStatus and show me the full raw result.',
            'urls' => [
                'gptBuilder' => 'https://chatgpt.com/gpts/editor',
                'ajax' => admin_url( 'admin-ajax.php' ),
                'adminPost' => admin_url( 'admin-post.php' ),
                'setup' => admin_url( 'admin.php?page=' . self::SLUG ),
            ],
            'nonces' => [
                'generateKey' => wp_create_nonce( 'elementize_onboarding' ),
                'saveConnection' => wp_create_nonce( 'elementize_save_connection' ),
            ],
        ];
    }

    private static function requirement_card( string $label, bool $ok, string $detail ): void {
        echo '<div class="elz-check"><strong>' . esc_html( $label ) . '</strong><br><span class="' . ( $ok ? 'elz-ok' : 'elz-bad' ) . '">' . esc_html( $ok ? 'Ready' : 'Attention' ) . '</span><br><span class="elz-muted">' . esc_html( $detail ) . '</span></div>';
    }

    private static function render_notice( string $notice ): void {
        if ( 'connection_saved' === $notice ) echo '<div class="notice notice-success"><p>Public Elementize address saved.</p></div>';
        if ( 'connection_cleared' === $notice ) echo '<div class="notice notice-success"><p>Public-address override cleared.</p></div>';
        if ( 'connection_invalid' === $notice ) echo '<div class="notice notice-error"><p>Enter a public HTTPS origin such as https://example.com, with no path, query, credentials, or local/private host.</p></div>';
    }

    private static function requirements(): array {
        $elementor = class_exists( '\\Elementor\\Plugin' );
        $pixfort = Elementize_Core::pixfort_info();
        $user = wp_get_current_user();
        $app = $user && $user->exists() && self::application_passwords_available( $user );
        return [
            'elementor' => $elementor,
            'elementor_detail' => $elementor ? ( defined( 'ELEMENTOR_VERSION' ) ? 'Version ' . ELEMENTOR_VERSION : 'Loaded' ) : 'Not detected',
            'pixfort' => ! empty( $pixfort['active'] ),
            'pixfort_detail' => ! empty( $pixfort['active'] ) ? trim( (string) $pixfort['name'] . ' ' . (string) $pixfort['version'] ) : 'Not detected',
            'revisions' => Elementize_Core::page_revisions_available(),
            'application_passwords' => $app,
        ];
    }

    private static function application_passwords_available( WP_User $user ): bool {
        if ( function_exists( 'wp_is_application_passwords_available_for_user' ) ) return (bool) wp_is_application_passwords_available_for_user( $user );
        return function_exists( 'wp_is_application_passwords_available' ) && (bool) wp_is_application_passwords_available();
    }

    private static function environment(): array {
        $site = self::site_origin();
        $site_public_https = 'https' === strtolower( (string) wp_parse_url( $site, PHP_URL_SCHEME ) ) && ! self::localish( $site );
        $stored = trim( (string) get_option( self::OPT_PUBLIC_ORIGIN, '' ) );
        $stored_public = '';
        if ( '' !== $stored ) {
            $normalized = self::normalize_public_origin( $stored );
            if ( ! is_wp_error( $normalized ) ) $stored_public = $normalized;
        }
        $effective = '' !== $stored_public ? $stored_public : ( $site_public_https ? $site : '' );
        return [ 'site_origin' => $site, 'site_public_https' => $site_public_https, 'stored_public' => $stored_public, 'effective_origin' => $effective, 'connection_ready' => '' !== $effective ];
    }

    private static function site_origin(): string {
        $parts = wp_parse_url( home_url( '/' ) );
        if ( ! is_array( $parts ) || empty( $parts['host'] ) ) return '';
        return strtolower( (string) ( $parts['scheme'] ?? 'https' ) ) . '://' . strtolower( (string) $parts['host'] ) . ( isset( $parts['port'] ) ? ':' . absint( $parts['port'] ) : '' );
    }

    private static function normalize_public_origin( string $raw ) {
        $parts = wp_parse_url( trim( $raw ) );
        if ( ! is_array( $parts ) || 'https' !== strtolower( (string) ( $parts['scheme'] ?? '' ) ) || empty( $parts['host'] ) ) return new WP_Error( 'elementize_invalid_origin' );
        if ( ! empty( $parts['user'] ) || ! empty( $parts['pass'] ) || ! empty( $parts['query'] ) || ! empty( $parts['fragment'] ) ) return new WP_Error( 'elementize_invalid_origin' );
        $path = trim( (string) ( $parts['path'] ?? '' ) );
        if ( '' !== $path && '/' !== $path ) return new WP_Error( 'elementize_invalid_origin' );
        $origin = 'https://' . strtolower( (string) $parts['host'] ) . ( isset( $parts['port'] ) ? ':' . absint( $parts['port'] ) : '' );
        return self::localish( $origin ) ? new WP_Error( 'elementize_private_origin' ) : $origin;
    }

    private static function localish( string $url ): bool {
        $host = strtolower( rtrim( (string) wp_parse_url( $url, PHP_URL_HOST ), '.' ) );
        if ( '' === $host || in_array( $host, [ 'localhost', '0.0.0.0', '::', '::1' ], true ) ) return true;
        if ( preg_match( '/\\.(local|test|localhost|invalid|internal|lan|home)$/', $host ) ) return true;
        if ( filter_var( $host, FILTER_VALIDATE_IP ) ) return false === filter_var( $host, FILTER_VALIDATE_IP, FILTER_FLAG_NO_PRIV_RANGE | FILTER_FLAG_NO_RES_RANGE );
        return false;
    }

    private static function read_config_file( string $relative ): string {
        $path = trailingslashit( ELEMENTIZE_DIR ) . 'config/' . ltrim( $relative, '/\\' );
        if ( ! is_readable( $path ) ) return '';
        $content = file_get_contents( $path );
        return is_string( $content ) ? $content : '';
    }

    private static function schema( string $origin ): string {
        $schema = self::read_config_file( 'gpt/actions.openapi.yaml' );
        $normalized = self::normalize_public_origin( $origin );
        if ( is_wp_error( $normalized ) || '' === $schema ) return '';
        $count = 0;
        $schema = (string) preg_replace( '/(?m)^  - url: .*$/', '  - url: ' . $normalized, $schema, 1, $count );
        return 1 === $count ? $schema : '';
    }
}

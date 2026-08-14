<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Onboarding {
    private const SLUG = 'elementize';
    private const OPT_PUBLIC_ORIGIN = 'elementize_public_api_origin';
    private const REDIRECT_TRANSIENT = 'elementize_activation_redirect';
    private const AUTO_ORIGIN_FILE = 'elementize-public-origin.txt';

    public static function init(): void {
        add_action( 'admin_menu', [ self::class, 'menu' ] );
        add_action( 'admin_init', [ self::class, 'maybe_redirect_after_activation' ] );
        add_action( 'admin_post_elementize_save_connection', [ self::class, 'save_connection' ] );
        add_action( 'admin_post_elementize_relay_control', [ self::class, 'relay_control' ] );
        add_action( 'wp_ajax_elementize_generate_connection_key', [ self::class, 'generate_key' ] );
    }

    public static function activate(): void {
        set_transient( self::REDIRECT_TRANSIENT, '1', 60 );
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', self::SLUG, [ self::class, 'render' ], 'dashicons-edit-page', 58 );
    }

    public static function maybe_redirect_after_activation(): void {
        if ( ! current_user_can( 'manage_options' ) || wp_doing_ajax() || wp_doing_cron() ) return;
        if ( defined( 'WP_CLI' ) && WP_CLI ) return;
        if ( isset( $_GET['activate-multi'] ) ) return;
        if ( '1' !== get_transient( self::REDIRECT_TRANSIENT ) ) return;
        delete_transient( self::REDIRECT_TRANSIENT );
        wp_safe_redirect( admin_url( 'admin.php?page=' . self::SLUG ) );
        exit;
    }

    public static function save_connection(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_die( 'Administrator permission is required.' );
        check_admin_referer( 'elementize_save_connection' );
        if ( isset( $_POST['elementize_clear_public_origin'] ) ) {
            delete_option( self::OPT_PUBLIC_ORIGIN );
            self::redirect( 'connection_cleared' );
        }
        $raw = trim( (string) wp_unslash( $_POST['elementize_public_api_origin'] ?? '' ) );
        $origin = self::normalize_public_origin( $raw );
        if ( is_wp_error( $origin ) ) self::redirect( 'connection_invalid' );
        update_option( self::OPT_PUBLIC_ORIGIN, $origin, false );
        self::redirect( 'connection_saved' );
    }

    public static function relay_control(): void {
        if ( ! current_user_can( 'manage_options' ) ) wp_die( 'Administrator permission is required.' );
        check_admin_referer( 'elementize_relay_control' );
        $action = sanitize_key( (string) wp_unslash( $_POST['relay_action'] ?? '' ) );
        $allowed = [ 'start', 'stop', 'enable', 'disable', 'autostart-on', 'autostart-off' ];
        if ( ! in_array( $action, $allowed, true ) ) {
            self::set_relay_notice( 'error', 'Unknown relay control action.' );
            self::redirect( 'relay_controlled' );
        }
        $result = self::run_relay_controller( $action );
        if ( is_wp_error( $result ) ) {
            self::set_relay_notice( 'error', $result->get_error_message() );
            self::redirect( 'relay_controlled' );
        }
        $messages = [
            'start' => 'Relay started.',
            'stop' => ! empty( $result['autostart'] ) ? 'Relay stopped for now. It will start again at the next Windows sign-in.' : 'Relay stopped.',
            'enable' => 'Relay turned on and Windows autostart enabled.',
            'disable' => 'Relay turned off and Windows autostart disabled.',
            'autostart-on' => 'Windows autostart enabled.',
            'autostart-off' => 'Windows autostart disabled.',
        ];
        self::set_relay_notice( 'success', $messages[ $action ] ?? 'Relay settings updated.' );
        self::redirect( 'relay_controlled' );
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
        if ( ! self::application_passwords_available( $user ) || ! class_exists( 'WP_Application_Passwords' ) ) {
            wp_send_json_error( [ 'message' => 'WordPress Application Passwords are unavailable.' ], 400 );
        }
        $name = 'Elementize - ' . gmdate( 'Y-m-d H:i:s' ) . ' UTC';
        $created = WP_Application_Passwords::create_new_application_password( $user->ID, [
            'name' => $name,
            'app_id' => wp_generate_uuid4(),
        ] );
        if ( is_wp_error( $created ) ) wp_send_json_error( [ 'message' => $created->get_error_message() ], 400 );
        $plain = (string) ( $created[0] ?? '' );
        if ( '' === $plain ) wp_send_json_error( [ 'message' => 'WordPress did not return the generated Application Password.' ], 500 );
        nocache_headers();
        wp_send_json_success( [
            'connection_key' => base64_encode( $user->user_login . ':' . $plain ),
            'warning' => 'Shown once. Treat this like a password.',
        ] );
    }

    private static function set_relay_notice( string $type, string $message ): void {
        set_transient( 'elementize_relay_notice_' . get_current_user_id(), [ 'type' => $type, 'message' => $message ], 60 );
    }

    private static function relay_state(): array {
        $fallback = [ 'supported' => 'Windows' === PHP_OS_FAMILY, 'installed' => false, 'running' => false, 'tunnel_running' => false, 'autostart' => false, 'stable_origin' => '', 'error' => '' ];
        if ( ! $fallback['supported'] ) return $fallback;
        $state = self::run_relay_controller( 'status' );
        if ( is_wp_error( $state ) ) {
            $fallback['error'] = $state->get_error_message();
            return $fallback;
        }
        return array_merge( $fallback, $state );
    }

    private static function relay_runtime_pointer(): array {
        $path = trailingslashit( ELEMENTIZE_DIR ) . 'elementize-relay-runtime.json';
        if ( ! is_readable( $path ) ) return [];
        $data = json_decode( (string) file_get_contents( $path ), true );
        if ( ! is_array( $data ) ) return [];
        $site_host = strtolower( (string) wp_parse_url( self::site_origin(), PHP_URL_HOST ) );
        $pointer_host = strtolower( (string) wp_parse_url( (string) ( $data['localOrigin'] ?? '' ), PHP_URL_HOST ) );
        if ( '' === $site_host || $site_host !== $pointer_host ) return [];
        $runtime = trim( (string) ( $data['runtimeRoot'] ?? '' ) );
        $startup = trim( (string) ( $data['startupCmd'] ?? '' ) );
        return '' !== $runtime ? [ 'runtimeRoot' => $runtime, 'startupCmd' => $startup ] : [];
    }

    private static function run_relay_controller( string $action ) {
        if ( ! in_array( $action, [ 'status', 'start', 'stop', 'enable', 'disable', 'autostart-on', 'autostart-off' ], true ) ) return new WP_Error( 'elementize_relay_action_invalid', 'Invalid relay control action.' );
        if ( 'Windows' !== PHP_OS_FAMILY ) return new WP_Error( 'elementize_relay_unsupported', 'Relay controls are available on Windows only.' );
        if ( ! function_exists( 'proc_open' ) ) return new WP_Error( 'elementize_relay_process_unavailable', 'PHP process control is unavailable on this WordPress installation.' );
        $script = trailingslashit( ELEMENTIZE_DIR ) . 'tools/windows/control-free-cloudflare-relay.ps1';
        if ( ! is_readable( $script ) ) return new WP_Error( 'elementize_relay_controller_missing', 'Elementize relay controller is missing.' );
        $system_root = rtrim( (string) getenv( 'SystemRoot' ), '/\\' );
        $powershell = '' !== $system_root ? $system_root . '\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' : 'powershell.exe';
        $command = [ $powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Action', $action, '-LocalOrigin', self::site_origin() ];
        $runtime = self::relay_runtime_pointer();
        if ( ! empty( $runtime['runtimeRoot'] ) ) { $command[] = '-RuntimeRoot'; $command[] = $runtime['runtimeRoot']; }
        if ( ! empty( $runtime['startupCmd'] ) ) { $command[] = '-StartupCmd'; $command[] = $runtime['startupCmd']; }
        $descriptors = [ 1 => [ 'pipe', 'w' ], 2 => [ 'pipe', 'w' ] ];
        $process = @proc_open( $command, $descriptors, $pipes, ELEMENTIZE_DIR, null, [ 'bypass_shell' => true ] );
        if ( ! is_resource( $process ) ) return new WP_Error( 'elementize_relay_process_failed', 'Elementize could not launch the relay controller.' );
        $stdout = stream_get_contents( $pipes[1] ); fclose( $pipes[1] );
        $stderr = stream_get_contents( $pipes[2] ); fclose( $pipes[2] );
        $exit = proc_close( $process );
        if ( 0 !== $exit ) {
            $detail = sanitize_text_field( substr( trim( (string) ( $stderr ?: $stdout ) ), 0, 500 ) );
            return new WP_Error( 'elementize_relay_control_failed', '' !== $detail ? $detail : 'Relay control command failed.' );
        }
        $data = json_decode( trim( (string) $stdout ), true );
        return is_array( $data ) ? $data : new WP_Error( 'elementize_relay_state_invalid', 'Relay controller returned an invalid status response.' );
    }

    public static function render(): void {
        if ( ! current_user_can( 'manage_options' ) ) return;
        $env = self::environment();
        $req = self::requirements();
        $relay = self::relay_state();
        $relay_managed = ! empty( $relay['installed'] ) && str_ends_with( strtolower( (string) wp_parse_url( $env['effective_origin'], PHP_URL_HOST ) ), '.workers.dev' );
        $relay_live = ! $relay_managed || ( ! empty( $relay['running'] ) && ! empty( $relay['tunnel_running'] ) );
        $connection_live = $env['connection_ready'] && $env['restart_safe'] && $relay_live;
        $connection_detail = $env['origin_label'];
        if ( $relay_managed && empty( $relay['running'] ) ) $connection_detail = 'Relay stopped';
        elseif ( $relay_managed && empty( $relay['tunnel_running'] ) ) $connection_detail = 'Relay starting';
        $instructions = self::read_config_file( 'gpt/wp-builder-instructions.md' );
        $schema = $env['connection_ready'] ? self::schema( $env['effective_origin'] ) : '';
        $materials_ready = '' !== $instructions && '' !== $schema;
        $all_ready = $req['elementor'] && $req['pixfort'] && $req['revisions'] && $req['application_passwords'] && $connection_live && $materials_ready;
        $notice = sanitize_key( (string) wp_unslash( $_GET['elementize_notice'] ?? '' ) );
        $nonce = wp_create_nonce( 'elementize_onboarding' );
        $setup_command = self::windows_setup_command();
        ?>
        <div class="wrap elz-wrap">
            <h1>Elementize</h1>
            <p class="description elz-lead">Direct edits to existing Pixfort/Elementor content: text, colours, images and icons. Nothing else.</p>
            <?php self::render_notice( $notice ); self::render_relay_notice(); ?>
            <style>
                .elz-wrap{max-width:900px}.elz-lead{font-size:15px;margin-bottom:20px}.elz-card{background:#fff;border:1px solid #dcdcde;border-radius:8px;padding:20px;margin:16px 0}.elz-card h2{margin:0 0 14px}.elz-status{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px}.elz-pill{border:1px solid #dcdcde;border-radius:7px;padding:12px}.elz-ok{color:#087b2d;font-weight:700}.elz-bad{color:#b32d2e;font-weight:700}.elz-code{font-family:monospace;word-break:break-all;background:#f6f7f7;border:1px solid #dcdcde;padding:10px;border-radius:6px}.elz-actions{display:flex;gap:8px;flex-wrap:wrap}.elz-hidden{position:absolute!important;left:-10000px!important;width:1px!important;height:1px!important;overflow:hidden!important}.elz-ready{border-left:4px solid #00a32a;background:#edfaef;padding:12px}.elz-note{border-left:4px solid #dba617;background:#fff8e5;padding:12px}.elz-error{display:none;color:#b32d2e}.elz-muted{color:#646970}
            </style>

            <section class="elz-card">
                <h2>Status</h2>
                <div class="elz-status">
                    <?php self::status_pill( 'Elementor', $req['elementor'], $req['elementor_detail'] ); ?>
                    <?php self::status_pill( 'Pixfort', $req['pixfort'], $req['pixfort_detail'] ); ?>
                    <?php self::status_pill( 'Safe revisions', $req['revisions'], $req['revisions'] ? 'Enabled' : 'Required' ); ?>
                    <?php self::status_pill( 'Connection', $connection_live, $connection_detail ); ?>
                </div>
            </section>

            <section class="elz-card">
                <h2>Persistent connection</h2>
                <?php if ( $env['connection_ready'] && $env['restart_safe'] ) : ?>
                    <?php if ( $relay_managed ) :
                        $relay_label = ! empty( $relay['running'] ) ? ( ! empty( $relay['tunnel_running'] ) ? 'Running' : 'Starting' ) : 'Stopped'; ?>
                        <div class="<?php echo $relay_live ? 'elz-ready' : 'elz-note'; ?>">
                            <strong>Relay: <?php echo esc_html( $relay_label ); ?></strong><br>
                            Windows autostart: <strong><?php echo ! empty( $relay['autostart'] ) ? 'On' : 'Off'; ?></strong><br>
                            <?php echo esc_html( $env['effective_origin'] ); ?>
                        </div>
                        <div class="elz-actions" style="margin-top:12px">
                            <?php
                            if ( ! empty( $relay['running'] ) || ! empty( $relay['autostart'] ) ) self::relay_button( 'disable', 'Turn off completely' );
                            else self::relay_button( 'enable', 'Turn on', true );
                            if ( ! empty( $relay['running'] ) && ! empty( $relay['autostart'] ) ) self::relay_button( 'stop', 'Pause until next Windows sign-in' );
                            elseif ( empty( $relay['running'] ) && ! empty( $relay['autostart'] ) ) self::relay_button( 'start', 'Start now', true );
                            elseif ( ! empty( $relay['running'] ) && empty( $relay['autostart'] ) ) self::relay_button( 'enable', 'Enable Windows autostart' );
                            ?>
                        </div>
                    <?php else : ?>
                        <div class="elz-ready"><strong>Stable connection configured.</strong><br><?php echo esc_html( $env['effective_origin'] ); ?></div>
                    <?php endif; ?>
                <?php else : ?>
                    <div class="elz-note"><strong>No stable public URL yet.</strong> A free Quick Tunnel is deliberately not treated as finished because its URL changes after every restart.</div>
                    <p>Run this one-time setup for a free stable <code>workers.dev</code> relay. It keeps one permanent CustomGPT URL while the hidden Quick Tunnel can rotate behind it:</p>
                    <div id="elz-setup-command" class="elz-code"><?php echo esc_html( $setup_command ); ?></div>
                    <p><button type="button" class="button elz-copy" data-target="elz-setup-command">Copy setup command</button></p>
                    <p class="description">This opens Cloudflare authorization once and deploys on the Workers Free plan. No custom domain and no paid Cloudflare plan are required.</p>
                <?php endif; ?>
                <details style="margin-top:14px">
                    <summary>Advanced: set public URL manually</summary>
                    <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>" style="margin-top:10px">
                        <input type="hidden" name="action" value="elementize_save_connection"><?php wp_nonce_field( 'elementize_save_connection' ); ?>
                        <input name="elementize_public_api_origin" type="url" class="regular-text code" value="<?php echo esc_attr( $env['stored_public'] ); ?>" placeholder="https://elementize.example.com">
                        <button class="button button-primary">Save</button>
                    </form>
                    <?php if ( '' !== $env['stored_public'] ) : ?>
                        <form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>" style="margin-top:8px"><input type="hidden" name="action" value="elementize_save_connection"><input type="hidden" name="elementize_clear_public_origin" value="1"><?php wp_nonce_field( 'elementize_save_connection' ); ?><button class="button">Clear manual URL</button></form>
                    <?php endif; ?>
                </details>
            </section>

            <section class="elz-card">
                <h2>CustomGPT setup</h2>
                <?php if ( ! $env['connection_ready'] ) : ?>
                    <p class="elz-muted">Set a public HTTPS URL first.</p>
                <?php elseif ( ! $materials_ready ) : ?>
                    <p class="elz-bad">Setup materials are missing from the plugin.</p>
                <?php else : ?>
                    <div class="elz-actions">
                        <button type="button" class="button button-primary elz-copy" data-target="elz-instructions">Copy GPT instructions</button>
                        <button type="button" class="button button-primary elz-copy" data-target="elz-schema">Copy Action schema</button>
                        <button type="button" class="button" id="elz-key-btn">Generate connection key</button>
                    </div>
                    <div id="elz-key-wrap" style="display:none;margin-top:12px"><div id="elz-key" class="elz-code"></div><p><button type="button" class="button elz-copy" data-target="elz-key">Copy key</button></p></div>
                    <p id="elz-key-error" class="elz-error"></p>
                    <p class="description">In GPT Builder: paste the instructions, add one Action with the schema, then choose API Key → Basic and paste the generated key.</p>
                    <textarea id="elz-instructions" class="elz-hidden" readonly><?php echo esc_textarea( $instructions ); ?></textarea>
                    <textarea id="elz-schema" class="elz-hidden" readonly><?php echo esc_textarea( $schema ); ?></textarea>
                <?php endif; ?>
            </section>

            <?php if ( $all_ready ) : ?>
                <div class="elz-ready"><strong>Elementize is ready.</strong> CustomGPT can edit existing text, local colours, images and Pixfort icons.</div>
            <?php endif; ?>
        </div>
        <script>
        (function(){
            function copy(text){if(navigator.clipboard&&window.isSecureContext)return navigator.clipboard.writeText(text);var a=document.createElement('textarea');a.value=text;document.body.appendChild(a);a.select();var ok=document.execCommand('copy');a.remove();return ok?Promise.resolve():Promise.reject(new Error('Clipboard unavailable'));}
            document.querySelectorAll('.elz-copy').forEach(function(button){button.addEventListener('click',function(){var el=document.getElementById(button.dataset.target);if(!el)return;var text=('value'in el)?el.value:el.textContent;copy(text).then(function(){var old=button.textContent;button.textContent='Copied';setTimeout(function(){button.textContent=old;},1200);});});});
            var keyButton=document.getElementById('elz-key-btn');
            if(!keyButton)return;
            keyButton.addEventListener('click',function(){
                keyButton.disabled=true;var error=document.getElementById('elz-key-error');error.style.display='none';
                var body=new URLSearchParams();body.set('action','elementize_generate_connection_key');body.set('nonce',<?php echo wp_json_encode( $nonce ); ?>);
                fetch(ajaxurl,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded; charset=UTF-8'},body:body.toString(),credentials:'same-origin'})
                .then(function(r){return r.json();}).then(function(result){if(!result||!result.success)throw new Error(result&&result.data&&result.data.message?result.data.message:'Could not generate key.');document.getElementById('elz-key').textContent=result.data.connection_key;document.getElementById('elz-key-wrap').style.display='block';})
                .catch(function(e){error.textContent=e.message;error.style.display='block';}).finally(function(){keyButton.disabled=false;});
            });
        })();
        </script>
        <?php
    }

    private static function status_pill( string $label, bool $ok, string $detail ): void {
        echo '<div class="elz-pill"><strong>' . esc_html( $label ) . '</strong><br><span class="' . ( $ok ? 'elz-ok' : 'elz-bad' ) . '">' . esc_html( $ok ? 'Ready' : 'Attention' ) . '</span><br><span class="elz-muted">' . esc_html( $detail ) . '</span></div>';
    }

    private static function relay_button( string $action, string $label, bool $primary = false ): void {
        echo '<form method="post" action="' . esc_url( admin_url( 'admin-post.php' ) ) . '" style="display:inline-block">';
        echo '<input type="hidden" name="action" value="elementize_relay_control"><input type="hidden" name="relay_action" value="' . esc_attr( $action ) . '">';
        wp_nonce_field( 'elementize_relay_control' );
        echo '<button type="submit" class="button ' . ( $primary ? 'button-primary' : '' ) . '">' . esc_html( $label ) . '</button></form>';
    }

    private static function render_relay_notice(): void {
        $key = 'elementize_relay_notice_' . get_current_user_id();
        $notice = get_transient( $key );
        if ( ! is_array( $notice ) || empty( $notice['message'] ) ) return;
        delete_transient( $key );
        $class = 'success' === ( $notice['type'] ?? '' ) ? 'notice-success' : 'notice-error';
        echo '<div class="notice ' . esc_attr( $class ) . '"><p>' . esc_html( (string) $notice['message'] ) . '</p></div>';
    }

    private static function render_notice( string $notice ): void {
        if ( 'connection_saved' === $notice ) echo '<div class="notice notice-success"><p>Public Elementize URL saved.</p></div>';
        if ( 'connection_cleared' === $notice ) echo '<div class="notice notice-success"><p>Manual public URL cleared.</p></div>';
        if ( 'connection_invalid' === $notice ) echo '<div class="notice notice-error"><p>Enter a public HTTPS origin with no path, query or credentials.</p></div>';
    }

    private static function requirements(): array {
        $elementor = class_exists( '\\Elementor\\Plugin' );
        $pixfort = Elementize_Core::pixfort_info();
        $user = wp_get_current_user();
        $app = $user && $user->exists() && self::application_passwords_available( $user );
        return [
            'elementor' => $elementor,
            'elementor_detail' => $elementor ? ( defined( 'ELEMENTOR_VERSION' ) ? 'v' . ELEMENTOR_VERSION : 'Loaded' ) : 'Not detected',
            'pixfort' => ! empty( $pixfort['active'] ),
            'pixfort_detail' => ! empty( $pixfort['active'] ) ? trim( (string) $pixfort['name'] . ' ' . (string) $pixfort['version'] ) : 'Not detected',
            'revisions' => Elementize_Core::page_revisions_available(),
            'application_passwords' => (bool) $app,
        ];
    }

    private static function application_passwords_available( WP_User $user ): bool {
        if ( function_exists( 'wp_is_application_passwords_available_for_user' ) ) return (bool) wp_is_application_passwords_available_for_user( $user );
        return function_exists( 'wp_is_application_passwords_available' ) && (bool) wp_is_application_passwords_available();
    }

    private static function environment(): array {
        $site = self::site_origin();
        $site_public = 'https' === strtolower( (string) wp_parse_url( $site, PHP_URL_SCHEME ) ) && ! self::localish( $site );
        $stored = self::validated_origin( (string) get_option( self::OPT_PUBLIC_ORIGIN, '' ) );
        $auto = self::auto_origin();
        $stored_temporary = '' !== $stored && self::is_quick_tunnel( $stored );
        if ( '' !== $stored && ! $stored_temporary ) $effective = $stored;
        elseif ( '' !== $auto ) $effective = $auto;
        elseif ( $site_public ) $effective = $site;
        else $effective = $stored;
        $meta = self::origin_meta( $effective, $site_public && $effective === $site );
        return [
            'site_origin' => $site,
            'stored_public' => $stored,
            'auto_public' => $auto,
            'effective_origin' => $effective,
            'connection_ready' => '' !== $effective,
            'origin_kind' => $meta['kind'],
            'origin_label' => $meta['label'],
            'restart_safe' => $meta['restart_safe'],
        ];
    }

    private static function auto_origin(): string {
        $path = trailingslashit( ELEMENTIZE_DIR ) . self::AUTO_ORIGIN_FILE;
        if ( ! is_readable( $path ) ) return '';
        $value = trim( (string) file_get_contents( $path ) );
        return self::validated_origin( $value );
    }

    private static function validated_origin( string $raw ): string {
        if ( '' === trim( $raw ) ) return '';
        $origin = self::normalize_public_origin( $raw );
        return is_wp_error( $origin ) ? '' : $origin;
    }

    private static function origin_meta( string $origin, bool $is_public_site ): array {
        if ( '' === $origin ) return [ 'kind' => 'none', 'label' => 'Not configured', 'restart_safe' => false ];
        if ( self::is_quick_tunnel( $origin ) ) {
            return [ 'kind' => 'quick_tunnel', 'label' => 'Temporary URL', 'restart_safe' => false ];
        }
        if ( $is_public_site ) return [ 'kind' => 'public_site', 'label' => 'Stable public site', 'restart_safe' => true ];
        return [ 'kind' => 'stable_address', 'label' => 'Stable public URL', 'restart_safe' => true ];
    }

    private static function is_quick_tunnel( string $origin ): bool {
        $host = strtolower( rtrim( (string) wp_parse_url( $origin, PHP_URL_HOST ), '.' ) );
        return '' !== $host && str_ends_with( $host, '.trycloudflare.com' );
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
        if ( preg_match( '/\.(local|test|localhost|invalid|internal|lan|home)$/', $host ) ) return true;
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

    private static function windows_setup_command(): string {
        $script = trailingslashit( ELEMENTIZE_DIR ) . 'tools/windows/install-free-cloudflare-relay.ps1';
        $local = self::site_origin();
        return 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' . $script . '" -LocalOrigin "' . $local . '"';
    }
}

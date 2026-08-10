<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Plugin', false ) ) return;

final class Elementize_Onboarding_Visual_AI {
    private const VERSION = '0.24.0';
    private const NS = 'elementize/v1';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const CHROME_PATH_CONST = 'ELEMENTIZE_CHROME_PATH';

    public static function init(): void {
        add_action( 'admin_footer', [ self::class, 'render_setup_step' ], 40 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'status' ], 514, 3 );
    }

    public static function render_setup_step(): void {
        if ( ! current_user_can( 'manage_options' ) || ! self::is_setup_screen() ) return;

        $health = self::health();
        $plugin_version = self::plugin_version();
        $model = (string) $health['model'];
        $ready = ! empty( $health['ready'] );
        $ollama_running = ! empty( $health['ollama_running'] );
        $model_available = ! empty( $health['model_available'] );
        $browser_available = ! empty( $health['browser_available'] );
        ?>
        <template id="elz-visual-ai-step">
            <section class="elz-card" id="elz-local-visual-ai">
                <h2>2. Local visual AI</h2>
                <?php if ( $ready ) : ?>
                    <span class="elz-badge elz-badge-pass">Visual QA ready</span>
                    <p><strong>Ready:</strong> Elementize can render pages locally and use <code><?php echo esc_html( $model ); ?></code> for screenshot-based visual review.</p>
                <?php else : ?>
                    <span class="elz-badge elz-badge-warn">Visual QA setup needed</span>
                    <p>Complete the missing items below to enable screenshot-based visual critique and self-correction.</p>
                <?php endif; ?>

                <p class="elz-muted">This is optional for normal page building, copy, links, audits, and guarded Elementor writes. It is required for Elementize's advanced local visual QA and self-correction loop.</p>

                <div style="margin-top:16px">
                    <p class="elz-check"><strong>Browser renderer:</strong>
                        <span class="<?php echo $browser_available ? 'elz-pass' : 'elz-warn'; ?>">
                            <?php echo esc_html( $browser_available ? (string) $health['browser_name'] : 'Chrome or Edge not found' ); ?>
                        </span>
                    </p>
                    <?php if ( ! $browser_available ) : ?>
                        <p class="description">Install Google Chrome or Microsoft Edge, or define <code>ELEMENTIZE_CHROME_PATH</code> in <code>wp-config.php</code>.</p>
                    <?php endif; ?>

                    <p class="elz-check"><strong>Ollama:</strong>
                        <span class="<?php echo $ollama_running ? 'elz-pass' : 'elz-warn'; ?>">
                            <?php echo esc_html( $ollama_running ? 'Running locally' : 'Not reachable locally' ); ?>
                        </span>
                    </p>
                    <?php if ( ! $ollama_running ) : ?>
                        <p class="description">Install and launch Ollama on the same computer/server that runs WordPress. Elementize only connects to a localhost Ollama address.</p>
                        <p><a class="button" target="_blank" rel="noopener" href="https://ollama.com/download">Open Ollama download</a></p>
                    <?php endif; ?>

                    <p class="elz-check"><strong>Vision model:</strong>
                        <span class="<?php echo $model_available ? 'elz-pass' : 'elz-warn'; ?>">
                            <?php echo esc_html( $model_available ? $model . ' available' : $model . ' not installed' ); ?>
                        </span>
                    </p>
                    <?php if ( $ollama_running && ! $model_available ) : ?>
                        <p class="description">Run this once in Terminal / PowerShell on the WordPress machine:</p>
                        <p><code>ollama pull <?php echo esc_html( $model ); ?></code></p>
                    <?php endif; ?>
                </div>

                <div class="elz-note">
                    <strong>Local by default.</strong> Screenshots used by this visual QA path are sent to the local Ollama service, not to a paid external vision API.
                </div>

                <div class="elz-actions">
                    <button type="button" class="button<?php echo $ready ? '' : ' button-primary'; ?>" onclick="window.location.reload()">Check again</button>
                </div>
            </section>
        </template>
        <script>(function(){
            const wrap=document.querySelector('.elz-wrap');
            const tpl=document.getElementById('elz-visual-ai-step');
            if(!wrap||!tpl)return;
            const cards=wrap.querySelectorAll(':scope > .elz-card');
            if(cards.length>=2){cards[1].before(tpl.content.cloneNode(true));}
            else{wrap.appendChild(tpl.content.cloneNode(true));}
            const updated=wrap.querySelectorAll(':scope > .elz-card');
            if(updated[2]){const h=updated[2].querySelector('h2');if(h)h.textContent='3. Set up your WP Builder';}
            if(updated[3]){const h=updated[3].querySelector('h2');if(h)h.textContent='4. Finish';}
            wrap.querySelectorAll('code').forEach(function(c){
                if(c.textContent.trim()==='elementize_version: 0.6.3')c.textContent='elementize_version: <?php echo esc_js( $plugin_version ); ?>';
            });
        })();</script>
        <?php
    }

    private static function health(): array {
        $model = self::model();
        $ollama_url = self::ollama_url();
        $browser = self::browser();
        $ollama_running = false;
        $model_available = false;
        $models = [];
        $reason = '';

        $response = wp_remote_get( $ollama_url . '/api/tags', [
            'timeout' => 2.5,
            'redirection' => 0,
            'sslverify' => false,
            'limit_response_size' => 262144,
            'headers' => [ 'Accept' => 'application/json' ],
        ] );

        if ( is_wp_error( $response ) ) {
            $reason = self::clip( $response->get_error_message(), 220 );
        } elseif ( 200 !== (int) wp_remote_retrieve_response_code( $response ) ) {
            $reason = 'Ollama returned HTTP ' . (int) wp_remote_retrieve_response_code( $response ) . '.';
        } else {
            $decoded = json_decode( (string) wp_remote_retrieve_body( $response ), true );
            if ( is_array( $decoded ) ) {
                $ollama_running = true;
                foreach ( is_array( $decoded['models'] ?? null ) ? $decoded['models'] : [] as $entry ) {
                    if ( ! is_array( $entry ) ) continue;
                    $name = trim( (string) ( $entry['name'] ?? ( $entry['model'] ?? '' ) ) );
                    if ( '' !== $name ) $models[] = $name;
                }
                $models = array_values( array_unique( $models ) );
                foreach ( $models as $name ) {
                    if ( self::same_model( $name, $model ) ) { $model_available = true; break; }
                }
            } else {
                $reason = 'Ollama returned an invalid model list.';
            }
        }

        return [
            'ready' => $ollama_running && $model_available && ! empty( $browser['path'] ),
            'ollama_running' => $ollama_running,
            'ollama_url' => $ollama_url,
            'ollama_reason' => $reason,
            'model' => $model,
            'model_available' => $model_available,
            'available_models' => $models,
            'browser_available' => ! empty( $browser['path'] ),
            'browser_name' => (string) $browser['name'],
            'browser_path' => (string) $browser['path'],
        ];
    }

    private static function same_model( string $available, string $wanted ): bool {
        $available = strtolower( trim( $available ) );
        $wanted = strtolower( trim( $wanted ) );
        if ( $available === $wanted ) return true;
        if ( ! str_contains( $wanted, ':' ) && str_starts_with( $available, $wanted . ':' ) ) return true;
        return false;
    }

    private static function browser(): array {
        if ( defined( self::CHROME_PATH_CONST ) ) {
            $path = trim( (string) constant( self::CHROME_PATH_CONST ) );
            if ( '' !== $path && is_file( $path ) ) return [ 'name' => self::browser_name( $path ), 'path' => $path ];
        }

        $candidates = [];
        foreach ( [ getenv( 'LOCALAPPDATA' ), getenv( 'ProgramFiles' ), getenv( 'ProgramFiles(x86)' ) ] as $base ) {
            if ( ! is_string( $base ) || '' === $base ) continue;
            $candidates[] = $base . '\\Google\\Chrome\\Application\\chrome.exe';
            $candidates[] = $base . '\\Microsoft\\Edge\\Application\\msedge.exe';
        }
        $candidates = array_merge( $candidates, [
            '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
            '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
            '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable',
            '/usr/bin/chromium', '/usr/bin/chromium-browser',
            '/usr/bin/microsoft-edge', '/usr/bin/microsoft-edge-stable',
        ] );
        foreach ( array_unique( $candidates ) as $path ) {
            if ( is_file( $path ) ) return [ 'name' => self::browser_name( $path ), 'path' => $path ];
        }
        return [ 'name' => '', 'path' => '' ];
    }

    private static function browser_name( string $path ): string {
        $lower = strtolower( $path );
        if ( str_contains( $lower, 'edge' ) ) return 'Microsoft Edge';
        if ( str_contains( $lower, 'chrom' ) ) return 'Google Chrome / Chromium';
        return basename( $path );
    }

    private static function model(): string {
        $model = defined( self::MODEL_CONST ) ? trim( (string) constant( self::MODEL_CONST ) ) : self::DEFAULT_MODEL;
        return '' !== $model ? $model : self::DEFAULT_MODEL;
    }

    private static function ollama_url(): string {
        $url = defined( self::OLLAMA_URL_CONST ) ? trim( (string) constant( self::OLLAMA_URL_CONST ) ) : self::DEFAULT_OLLAMA_URL;
        $url = untrailingslashit( $url );
        return preg_match( '#^https?://(?:127\\.0\\.0\\.1|localhost)(?::\\d+)?$#i', $url ) ? $url : self::DEFAULT_OLLAMA_URL;
    }

    private static function is_setup_screen(): bool {
        return 'elementize-setup' === sanitize_key( (string) ( $_GET['page'] ?? '' ) );
    }

    private static function plugin_version(): string {
        $file = dirname( __DIR__ ) . '/elementize.php';
        if ( function_exists( 'get_file_data' ) && is_readable( $file ) ) {
            $data = get_file_data( $file, [ 'Version' => 'Version' ], 'plugin' );
            $version = trim( (string) ( $data['Version'] ?? '' ) );
            if ( '' !== $version ) return $version;
        }
        return self::VERSION;
    }

    private static function clip( string $value, int $max ): string {
        $value = trim( preg_replace( '/\\s+/', ' ', $value ) ?? $value );
        return function_exists( 'mb_substr' ) ? mb_substr( $value, 0, $max ) : substr( $value, 0, $max );
    }

    public static function status( $response, $handler, WP_REST_Request $request ) {
        if ( '/elementize/v1/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $data['onboarding_local_visual_ai_step'] = true;
        $data['onboarding_local_visual_ai_optional_for_core_builder'] = true;
        $data['onboarding_local_visual_ai_required_for_advanced_visual_qa'] = true;
        $data['onboarding_local_visual_ai_default_model'] = self::DEFAULT_MODEL;
        $data['onboarding_local_visual_ai_ollama_localhost_only'] = true;
        $data['onboarding_local_visual_ai_paid_external_api_required'] = false;
        $data['onboarding_local_visual_ai_action_slot_cost'] = 0;
        $response->set_data( $data );
        return $response;
    }
}

<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Visual_QA {
    private const VERSION = '8';
    private const TTL = 180;
    private const JOB_TTL = 180;
    private const JOB_TIMEOUT_SECONDS = 75;
    private const JOB_PREFIX = 'elementize_visual_qa_job_v8_';
    private const CHROME_PATH_CONST = 'ELEMENTIZE_CHROME_PATH';
    private const VIEWPORT_WIDTH = 1440;
    private const CAPTURE_HEIGHT = 9000;
    private const MAX_SCREENSHOT_BYTES = 10485760;
    private static ?array $validated_preview = null;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'routes' ], 29 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'annotate_status' ], 95, 3 );
        add_action( 'pre_get_posts', [ self::class, 'allow_signed_draft_query' ], 1 );
        add_action( 'template_redirect', [ self::class, 'guard_preview_request' ], 0 );
        add_action( 'wp_head', [ self::class, 'settle_preview_motion' ], PHP_INT_MAX );
        add_action( 'wp_footer', [ self::class, 'browser_probe_script' ], PHP_INT_MAX );
        add_filter( 'redirect_canonical', [ self::class, 'disable_canonical_for_preview' ], 10, 2 );
    }

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pages/(?P<id>\\d+)/visual-qa', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'read' ],
            'permission_callback' => [ Elementize_Core::class, 'can_edit_page' ],
            'args' => [
                'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
                'analyze' => [ 'type' => 'boolean', 'default' => true ],
                'viewport' => [ 'type' => 'string', 'default' => 'desktop', 'enum' => [ 'desktop', 'tablet', 'mobile' ], 'sanitize_callback' => 'sanitize_key' ],
                'motion' => [ 'type' => 'string', 'default' => 'settled', 'enum' => [ 'settled', 'live' ], 'sanitize_callback' => 'sanitize_key' ],
                'interaction_probe' => [ 'type' => 'string', 'default' => 'passive', 'enum' => [ 'passive', 'safe' ], 'sanitize_callback' => 'sanitize_key' ],
            ],
        ] );
    }

    public static function read( WP_REST_Request $request ) {
        $id = absint( $request['id'] );
        $raw_analyze = $request->get_param( 'analyze' );
        $analyze = null === $raw_analyze ? true : (bool) filter_var( $raw_analyze, FILTER_VALIDATE_BOOLEAN );
        $viewport = self::normalize_viewport( (string) $request->get_param( 'viewport' ) );
        $viewport_spec = self::viewport_spec( $viewport );
        $motion = self::normalize_motion( (string) $request->get_param( 'motion' ) );
        $interaction = self::normalize_interaction( (string) $request->get_param( 'interaction_probe' ) );
        if ( $analyze ) {
            if ( ! class_exists( 'Elementize_ChatGPT_Vision' ) ) {
                return new WP_Error( 'elementize_chatgpt_vision_unavailable', 'Native ChatGPT visual analysis is unavailable.', [ 'status' => 503 ] );
            }
            return Elementize_ChatGPT_Vision::handle( $request );
        }

        $state = Elementize_Capabilities::state();
        if ( empty( $state['creative_enabled'] ) || absint( $state['scope_page_id'] ) !== $id ) {
            return new WP_Error( 'elementize_visual_qa_scope_mismatch', 'Rendered Visual QA is currently limited to the active Creative Control page.', [ 'status' => 403, 'capability_state' => $state ] );
        }

        $loaded = Elementize_Core::elements( $id );
        if ( is_wp_error( $loaded ) ) return $loaded;
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type ) return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );

        $hash = Elementize_Core::hash_elements( $loaded['elements'] );
        $revision = absint( $state['revision'] );
        $cap = self::capability();
        $base = [
            'visual_qa_version' => self::VERSION,
            'page' => Elementize_Core::page_identity( $id, $loaded['document'] ),
            'content_hash' => $hash,
            'capability_revision' => $revision,
            'scope' => 'active_creative_control_page',
            'read_only' => true,
            'advisory_only' => true,
            'provider' => 'local_chromium_capture',
            'paid_services_required' => false,
            'external_account_required' => false,
            'capture_backend' => 'local_chromium_async',
            'viewport' => $viewport,
            'viewport_width' => $viewport_spec['width'],
            'motion' => $motion,
            'interaction_probe' => $interaction,
            'analysis_backend' => 'chatgpt_native_vision',
            'browser_available' => $cap['browser_available'],
            'browser_name' => $cap['browser_name'],
            'capture_ready' => $cap['capture_ready'],
            'analysis_requested' => false,
            'analysis_ready' => false,
            'native_vision_handoff_available' => class_exists( 'Elementize_ChatGPT_Vision' ),
            'visual_render_verified' => false,
            'visual_analysis_verified' => false,
            'screenshot_persisted' => false,
            'preview_url_exposed' => false,
        ];
        if ( ! $cap['capture_ready'] ) {
            $base['capture_state'] = 'unavailable';
            $base['reason'] = 'Local screenshot capture is unavailable. A supported Chrome/Chromium/Edge executable and PHP proc_open are required.';
            return new WP_REST_Response( $base, 200 );
        }

        $candidate_url = self::signed_preview_url( $id, $hash, $revision, $motion, $interaction );
        $url = self::normalize_capture_url( $candidate_url );
        if ( '' === $url ) {
            $base['capture_state'] = 'failed';
            $base['stage'] = 'capture_url';
            $base['reason'] = 'Elementize could not derive a safe HTTP(S) rendered-preview URL for this page.';
            return new WP_REST_Response( $base, 200 );
        }

        $context = self::capture_context( $id, $hash, $revision, $viewport, $motion, $interaction );
        $job_id = $context['job_id'];
        $job_key = $context['job_key'];
        $job = get_transient( $job_key );
        if ( is_array( $job ) && ! self::job_matches( $job, $id, $hash, $revision ) ) {
            self::cleanup_job( $job );
            delete_transient( $job_key );
            $job = false;
        }

        if ( ! is_array( $job ) ) {
            self::cleanup_stale_temp_files();
            $job = self::start_capture_job( $url, (string) $cap['browser_path'], $job_id, $id, $hash, $revision, $viewport );
            if ( is_wp_error( $job ) ) {
                $base['capture_state'] = 'failed';
                $base['reason'] = self::safe_error( $job, 'Could not start the local rendered capture job.' );
                $base['stage'] = 'capture_launch';
                return new WP_REST_Response( $base, 200 );
            }
            set_transient( $job_key, $job, self::JOB_TTL );
            return new WP_REST_Response( self::pending_payload( $base, $job_id, $job ), 200 );
        }

        $paths = is_array( $job['paths'] ?? null ) ? $job['paths'] : [];
        $capture_complete = self::capture_job_complete( $paths );
        if ( ! $capture_complete ) {
            $elapsed = max( 0, time() - absint( $job['started_at'] ?? 0 ) );
            if ( $elapsed <= self::JOB_TIMEOUT_SECONDS ) return new WP_REST_Response( self::pending_payload( $base, $job_id, $job ), 200 );

            $diagnostic = self::read_capture_log( (string) ( $paths['log'] ?? '' ) );
            $artifact_state = self::capture_artifact_state( $paths );
            self::cleanup_job( $job );
            delete_transient( $job_key );
            $base['capture_state'] = 'failed';
            $base['stage'] = 'capture';
            $base['capture_artifact_state'] = $artifact_state;
            $base['reason'] = self::clip( 'Asynchronous local browser capture exceeded ' . self::JOB_TIMEOUT_SECONDS . ' seconds without a complete PNG.' . ( '' !== $diagnostic ? ' Browser detail: ' . $diagnostic : '' ), 500 );
            return new WP_REST_Response( $base, 200 );
        }

        $capture = self::finalize_capture_job( $job );
        delete_transient( $job_key );
        if ( is_wp_error( $capture ) ) {
            self::cleanup_job( $job );
            $base['capture_state'] = 'failed';
            $base['reason'] = self::safe_error( $capture, 'Local rendered capture failed.' );
            $base['stage'] = 'capture';
            return new WP_REST_Response( $base, 200 );
        }

        $base['capture_state'] = 'complete';
        $base['capture_job_id'] = $job_id;
        $base['visual_render_verified'] = true;
        $base['screenshot'] = [
            'captured' => true,
            'mime_type' => 'image/png',
            'byte_count' => $capture['byte_count'],
            'sha256' => $capture['sha256'],
            'width' => $capture['width'],
            'height' => $capture['height'],
            'viewport_width' => $viewport_spec['width'],
            'capture_height_limit' => self::CAPTURE_HEIGHT,
            'capture_mode' => 'bounded_tall_' . $viewport,
            'persisted' => false,
        ];

        $base['browser_probe_verified'] = (bool) ( $capture['browser_probe']['verified'] ?? false );
        $base['browser_diagnostics'] = $capture['browser_probe']['diagnostics'] ?? null;
        $base['note'] = 'Rendered screenshot capture and passive browser diagnostics completed. Native ChatGPT visual analysis was not requested for this capture-only call.';
        self::cleanup_job( $job );
        return new WP_REST_Response( $base, 200 );
    }

    public static function annotate_status( $response, $handler, WP_REST_Request $request ) {
        if ( '/' . Elementize_Core::NS . '/status' !== $request->get_route() || is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $cap = self::capability();
        $native_ready = class_exists( 'Elementize_ChatGPT_Vision' ) && $cap['capture_ready'];
        $data['rendered_visual_qa'] = true;
        $data['rendered_visual_qa_version'] = self::VERSION;
        $data['rendered_visual_qa_scope'] = 'active_creative_control_page';
        $data['rendered_visual_capture_backend'] = 'local_chromium_async';
        $data['rendered_visual_capture_ready'] = $cap['capture_ready'];
        $data['rendered_visual_browser'] = $cap['browser_name'];
        $data['rendered_visual_analysis_backend'] = 'chatgpt_native_vision';
        $data['rendered_visual_analysis_ready'] = $native_ready;
        $data['rendered_visual_native_handoff'] = class_exists( 'Elementize_ChatGPT_Vision' );
        $data['rendered_visual_server_side_analysis'] = false;
        $data['rendered_visual_paid_services_required'] = false;
        $data['rendered_visual_preview_exposed'] = false;
        $response->set_data( $data );
        return $response;
    }


    private static function normalize_viewport( string $viewport ): string {
        $viewport = strtolower( trim( $viewport ) );
        return in_array( $viewport, [ 'desktop', 'tablet', 'mobile' ], true ) ? $viewport : 'desktop';
    }

    private static function viewport_spec( string $viewport ): array {
        return match ( self::normalize_viewport( $viewport ) ) {
            'mobile' => [ 'width' => 390, 'label' => 'mobile' ],
            'tablet' => [ 'width' => 768, 'label' => 'tablet' ],
            default => [ 'width' => 1440, 'label' => 'desktop' ],
        };
    }

    private static function normalize_motion( string $motion ): string {
        return 'live' === strtolower( trim( $motion ) ) ? 'live' : 'settled';
    }

    private static function normalize_interaction( string $interaction ): string {
        return 'safe' === strtolower( trim( $interaction ) ) ? 'safe' : 'passive';
    }

    private static function process_available(): bool {
        return function_exists( 'proc_open' ) && function_exists( 'proc_get_status' ) && function_exists( 'proc_close' );
    }

    private static function browser_path(): string {
        if ( defined( self::CHROME_PATH_CONST ) ) {
            $custom = trim( (string) constant( self::CHROME_PATH_CONST ) );
            if ( '' !== $custom && is_file( $custom ) ) return $custom;
        }
        $candidates = [];
        foreach ( [ (string) getenv( 'LOCALAPPDATA' ), (string) getenv( 'ProgramFiles' ), (string) getenv( 'ProgramFiles(x86)' ) ] as $base ) {
            if ( '' === $base ) continue;
            $candidates[] = $base . '\\Google\\Chrome\\Application\\chrome.exe';
            $candidates[] = $base . '\\Microsoft\\Edge\\Application\\msedge.exe';
        }
        $candidates = array_merge( $candidates, [
            'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
            'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
            'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
            'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
            '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
            '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
            '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/microsoft-edge', '/usr/bin/microsoft-edge-stable',
        ] );
        foreach ( array_unique( $candidates ) as $candidate ) if ( is_file( $candidate ) ) return $candidate;
        return '';
    }

    private static function browser_name( string $path ): string {
        $base = strtolower( basename( $path ) );
        if ( false !== strpos( $base, 'edge' ) ) return 'Microsoft Edge';
        return '' !== $path ? 'Google Chrome/Chromium' : '';
    }

    private static function capability(): array {
        $browser = self::browser_path();
        $process = self::process_available();
        return [
            'browser_available' => '' !== $browser,
            'browser_name' => self::browser_name( $browser ),
            'browser_path' => $browser,
            'capture_ready' => $process && '' !== $browser,
        ];
    }

    private static function signed_preview_url( int $id, string $hash, int $revision, string $motion = 'settled', string $interaction = 'passive' ): string {
        $expires = time() + self::TTL;
        $motion = self::normalize_motion( $motion );
        $interaction = self::normalize_interaction( $interaction );
        $sig = self::preview_signature( $id, $hash, $revision, $expires, $motion, $interaction );
        return add_query_arg( [
            'page_id' => $id,
            'elementize_visual_preview' => 1,
            'elementize_visual_expires' => $expires,
            'elementize_visual_hash' => $hash,
            'elementize_visual_revision' => $revision,
            'elementize_visual_motion' => $motion,
            'elementize_visual_interaction' => $interaction,
            'elementize_visual_sig' => $sig,
        ], trailingslashit( home_url( '/' ) ) );
    }

    private static function normalize_capture_url( $candidate ): string {
        if ( ! is_scalar( $candidate ) ) return '';
        $value = trim( (string) $candidate );
        if ( '' === $value ) return '';

        $value = wp_unslash( $value );
        $value = html_entity_decode( $value, ENT_QUOTES | ENT_HTML5, 'UTF-8' );
        $value = trim( $value );
        $value = trim( $value, " \t\n\r\0\x0B\"'" );

        if ( preg_match( '/^\[[^\]\r\n]*\]\(\s*(https?:\/\/.+)\s*\)$/i', $value, $match ) ) {
            $value = trim( (string) $match[1] );
        }

        if ( strlen( $value ) >= 2 ) {
            $first = $value[0];
            $last = $value[ strlen( $value ) - 1 ];
            if ( ( '[' === $first && ']' === $last ) || ( '<' === $first && '>' === $last ) ) {
                $inner = trim( substr( $value, 1, -1 ) );
                if ( preg_match( '#^https?://#i', $inner ) ) $value = $inner;
            }
        }

        $value = trim( $value, " \t\n\r\0\x0B\"'" );
        if ( '' === $value || preg_match( '/[\r\n]/', $value ) ) return '';

        $safe = esc_url_raw( $value, [ 'http', 'https' ] );
        if ( ! is_string( $safe ) || '' === $safe ) return '';
        $parts = wp_parse_url( $safe );
        if ( ! is_array( $parts ) ) return '';
        $scheme = strtolower( (string) ( $parts['scheme'] ?? '' ) );
        $host = trim( (string) ( $parts['host'] ?? '' ) );
        if ( ! in_array( $scheme, [ 'http', 'https' ], true ) || '' === $host ) return '';
        return $safe;
    }

    private static function preview_signature( int $id, string $hash, int $revision, int $expires, string $motion = 'settled', string $interaction = 'passive' ): string {
        return hash_hmac( 'sha256', $id . "\n" . $revision . "\n" . $expires . "\n" . $hash . "\n" . self::normalize_motion( $motion ) . "\n" . self::normalize_interaction( $interaction ), wp_salt( 'auth' ) );
    }

    private static function preview_requested(): bool {
        return isset( $_GET['elementize_visual_preview'] ) && '1' === (string) wp_unslash( $_GET['elementize_visual_preview'] );
    }

    private static function validate_preview_request(): array {
        if ( null !== self::$validated_preview ) return self::$validated_preview;
        $bad = [ 'valid' => false, 'reason' => 'invalid' ];
        if ( ! self::preview_requested() ) return self::$validated_preview = $bad;
        $id = isset( $_GET['page_id'] ) ? absint( wp_unslash( $_GET['page_id'] ) ) : 0;
        $expires = isset( $_GET['elementize_visual_expires'] ) ? absint( wp_unslash( $_GET['elementize_visual_expires'] ) ) : 0;
        $revision = isset( $_GET['elementize_visual_revision'] ) ? absint( wp_unslash( $_GET['elementize_visual_revision'] ) ) : 0;
        $hash = isset( $_GET['elementize_visual_hash'] ) ? sanitize_text_field( wp_unslash( $_GET['elementize_visual_hash'] ) ) : '';
        $motion = self::normalize_motion( isset( $_GET['elementize_visual_motion'] ) ? (string) wp_unslash( $_GET['elementize_visual_motion'] ) : 'settled' );
        $interaction = self::normalize_interaction( isset( $_GET['elementize_visual_interaction'] ) ? (string) wp_unslash( $_GET['elementize_visual_interaction'] ) : 'passive' );
        $sig = isset( $_GET['elementize_visual_sig'] ) ? sanitize_text_field( wp_unslash( $_GET['elementize_visual_sig'] ) ) : '';
        if ( $id < 1 || $revision < 1 || $expires < time() || $expires > time() + self::TTL + 30 || ! preg_match( '/^[a-f0-9]{64}$/', $hash ) || ! preg_match( '/^[a-f0-9]{64}$/', $sig ) ) return self::$validated_preview = $bad;
        $post = get_post( $id );
        if ( ! $post || 'page' !== $post->post_type || ! in_array( (string) $post->post_status, [ 'draft', 'publish' ], true ) ) return self::$validated_preview = $bad;
        $state = Elementize_Capabilities::state();
        if ( empty( $state['creative_enabled'] ) || absint( $state['scope_page_id'] ) !== $id || absint( $state['revision'] ) !== $revision ) return self::$validated_preview = $bad;
        if ( ! hash_equals( self::preview_signature( $id, $hash, $revision, $expires, $motion, $interaction ), $sig ) ) return self::$validated_preview = $bad;
        $loaded = Elementize_Core::elements( $id );
        $current = is_wp_error( $loaded ) ? '' : Elementize_Core::hash_elements( $loaded['elements'] );
        if ( '' === $current || ! hash_equals( $current, $hash ) ) return self::$validated_preview = [ 'valid' => false, 'reason' => 'stale' ];
        return self::$validated_preview = [ 'valid' => true, 'id' => $id, 'hash' => $hash, 'revision' => $revision, 'expires' => $expires, 'status' => (string) $post->post_status, 'motion' => $motion, 'interaction' => $interaction ];
    }

    public static function allow_signed_draft_query( WP_Query $query ): void {
        if ( ! $query->is_main_query() || is_admin() || ! self::preview_requested() ) return;
        $valid = self::validate_preview_request();
        if ( empty( $valid['valid'] ) ) return;
        $query->set( 'page_id', (int) $valid['id'] );
        $query->set( 'post_type', 'page' );
        $status = (string) ( $valid['status'] ?? 'draft' );
        $query->set( 'post_status', $status );
        if ( 'draft' === $status ) $query->set( 'preview', true );
    }

    public static function guard_preview_request(): void {
        if ( ! self::preview_requested() ) return;
        $valid = self::validate_preview_request();
        if ( empty( $valid['valid'] ) ) {
            nocache_headers();
            status_header( 404 );
            wp_die( 'This Elementize rendered preview is invalid, expired, or stale.', 'Preview unavailable', [ 'response' => 404 ] );
        }
        show_admin_bar( false );
        nocache_headers();
        header( 'Cache-Control: private, no-store, no-cache, must-revalidate, max-age=0', true );
        header( 'Pragma: no-cache', true );
        header( 'X-Robots-Tag: noindex, nofollow, noarchive', true );
        header( 'Referrer-Policy: no-referrer', true );
    }

    public static function settle_preview_motion(): void {
        if ( ! self::preview_requested() ) return;
        $valid = self::validate_preview_request();
        if ( empty( $valid['valid'] ) ) return;
        if ( 'live' === (string) ( $valid['motion'] ?? 'settled' ) ) return;
        echo '<style id="elementize-visual-settle-motion">'
            . '*,*::before,*::after{animation-delay:0s!important;animation-duration:0s!important;transition-delay:0s!important;transition-duration:0s!important;scroll-behavior:auto!important;caret-color:transparent!important}'
            . '.animate-in,.group-animate-in,.animating,[data-anim-type]{visibility:visible!important;opacity:1!important;transform:none!important;animation:none!important;transition:none!important}'
            . '</style>';
    }
    public static function browser_probe_script(): void {
        if ( ! self::preview_requested() ) return;
        $valid = self::validate_preview_request();
        if ( empty( $valid['valid'] ) ) return;
        $config = [
            'safe' => 'safe' === (string) ( $valid['interaction'] ?? 'passive' ),
            'motion' => (string) ( $valid['motion'] ?? 'settled' ),
            'delay' => 'live' === (string) ( $valid['motion'] ?? 'settled' ) ? 1200 : 300,
        ];
        $script = base64_decode( 'KGZ1bmN0aW9uKGNmZyl7Y29uc3Qgd2FpdD1tcz0+bmV3IFByb21pc2Uocj0+c2V0VGltZW91dChyLG1zKSk7Y29uc3QgdmlzPWVsPT57aWYoIWVsKXJldHVybiBudWxsO2NvbnN0IHI9ZWwuZ2V0Qm91bmRpbmdDbGllbnRSZWN0KCksYz1nZXRDb21wdXRlZFN0eWxlKGVsKTtyZXR1cm4gci53aWR0aD4wJiZyLmhlaWdodD4wJiZjLmRpc3BsYXkhPT0ibm9uZSImJmMudmlzaWJpbGl0eSE9PSJoaWRkZW4iJiZwYXJzZUZsb2F0KGMub3BhY2l0eXx8IjEiKT4uMDJ9O2NvbnN0IHRhcmdldD1lbD0+e2xldCBpZD0oZWwuZ2V0QXR0cmlidXRlKCJhcmlhLWNvbnRyb2xzIil8fCIiKS50cmltKCk7aWYoIWlkKXtsZXQgdj0oZWwuZ2V0QXR0cmlidXRlKCJkYXRhLWJzLXRhcmdldCIpfHwiIikudHJpbSgpO2lmKHYuY2hhckF0KDApPT09IiMiKWlkPXYuc2xpY2UoMSl9cmV0dXJuIGlkP2RvY3VtZW50LmdldEVsZW1lbnRCeUlkKGlkKTpudWxsfTtjb25zdCBzbmFwPShlbCx0KT0+KHtleHBhbmRlZDplbC5nZXRBdHRyaWJ1dGUoImFyaWEtZXhwYW5kZWQiKSxzZWxlY3RlZDplbC5nZXRBdHRyaWJ1dGUoImFyaWEtc2VsZWN0ZWQiKSxjbGFzc05hbWU6KHR5cGVvZiBlbC5jbGFzc05hbWU9PT0ic3RyaW5nIj9lbC5jbGFzc05hbWU6IiIpLnNsaWNlKDAsMTgwKSx0YXJnZXRWaXNpYmxlOnZpcyh0KX0pO2NvbnN0IHNhbWU9KGEsYik9PmEuZXhwYW5kZWQ9PT1iLmV4cGFuZGVkJiZhLnNlbGVjdGVkPT09Yi5zZWxlY3RlZCYmYS50YXJnZXRWaXNpYmxlPT09Yi50YXJnZXRWaXNpYmxlO2FzeW5jIGZ1bmN0aW9uIGludGVyYWN0aW9ucygpe2lmKCFjZmcuc2FmZSlyZXR1cm57bW9kZToicGFzc2l2ZSIsdGVzdGVkOjAscGFzc2VkOjAsZmFpbGVkOjAscmVzdG9yYXRpb25GYWlsdXJlczowLGl0ZW1zOltdfTtsZXQgZWxzPVsuLi5kb2N1bWVudC5xdWVyeVNlbGVjdG9yQWxsKCdidXR0b25bYXJpYS1leHBhbmRlZF0sW3JvbGU9ImJ1dHRvbiJdW2FyaWEtZXhwYW5kZWRdLFtyb2xlPSJ0YWIiXVthcmlhLWNvbnRyb2xzXScpXS5maWx0ZXIoZT0+IWUuZGlzYWJsZWQmJiFlLmNsb3Nlc3QoImZvcm0iKSkuc2xpY2UoMCwxMiksaXRlbXM9W107Zm9yKGNvbnN0IGVsIG9mIGVscyl7bGV0IHQ9dGFyZ2V0KGVsKSxiZWZvcmU9c25hcChlbCx0KSxyb2xlPShlbC5nZXRBdHRyaWJ1dGUoInJvbGUiKXx8IiIpLnRvTG93ZXJDYXNlKCksb3JpZ2luYWw9bnVsbDtpZihyb2xlPT09InRhYiIpe2xldCBncm91cD1lbC5jbG9zZXN0KCdbcm9sZT0idGFibGlzdCJdJyl8fGVsLnBhcmVudEVsZW1lbnQ7b3JpZ2luYWw9Z3JvdXAmJmdyb3VwLnF1ZXJ5U2VsZWN0b3IoJ1tyb2xlPSJ0YWIiXVthcmlhLXNlbGVjdGVkPSJ0cnVlIl0nKX10cnl7ZWwuY2xpY2soKTthd2FpdCB3YWl0KDEyMCk7bGV0IGFmdGVyPXNuYXAoZWwsdCksY2hhbmdlZD0hc2FtZShiZWZvcmUsYWZ0ZXIpO2lmKHJvbGU9PT0idGFiIiYmb3JpZ2luYWwmJm9yaWdpbmFsIT09ZWwpb3JpZ2luYWwuY2xpY2soKTtlbHNlIGlmKGNoYW5nZWQpZWwuY2xpY2soKTthd2FpdCB3YWl0KDEwMCk7bGV0IHJlc3RvcmVkPXNhbWUoYmVmb3JlLHNuYXAoZWwsdCkpO2l0ZW1zLnB1c2goe3RhZzplbC50YWdOYW1lLnRvTG93ZXJDYXNlKCkscm9sZTpyb2xlLHRleHQ6KGVsLmlubmVyVGV4dHx8ZWwuZ2V0QXR0cmlidXRlKCJhcmlhLWxhYmVsIil8fCIiKS50cmltKCkuc2xpY2UoMCwxMDApLGNoYW5nZWQ6Y2hhbmdlZCxyZXN0b3JlZDpyZXN0b3JlZCxwYXNzZWQ6Y2hhbmdlZCYmcmVzdG9yZWR9KTt9Y2F0Y2goZSl7aXRlbXMucHVzaCh7dGFnOmVsLnRhZ05hbWUudG9Mb3dlckNhc2UoKSxyb2xlOnJvbGUsZXJyb3I6U3RyaW5nKGUmJmUubWVzc2FnZXx8ZSkscGFzc2VkOmZhbHNlLHJlc3RvcmVkOmZhbHNlfSl9fXJldHVybnttb2RlOiJzYWZlIix0ZXN0ZWQ6aXRlbXMubGVuZ3RoLHBhc3NlZDppdGVtcy5maWx0ZXIoeD0+eC5wYXNzZWQpLmxlbmd0aCxmYWlsZWQ6aXRlbXMuZmlsdGVyKHg9PiF4LnBhc3NlZCkubGVuZ3RoLHJlc3RvcmF0aW9uRmFpbHVyZXM6aXRlbXMuZmlsdGVyKHg9PnguY2hhbmdlZCYmIXgucmVzdG9yZWQpLmxlbmd0aCxpdGVtczppdGVtc319ZnVuY3Rpb24gc3RhdGljQ2hlY2tzKCl7bGV0IGQ9ZG9jdW1lbnQuZG9jdW1lbnRFbGVtZW50LGI9ZG9jdW1lbnQuYm9keSxhbGw9Wy4uLmRvY3VtZW50LnF1ZXJ5U2VsZWN0b3JBbGwoImJvZHkgKiIpXSx2dz1pbm5lcldpZHRoLG92ZXJmbG93PVtdLHRpbnk9W10saGlkZGVuTW90aW9uPVtdLGFuY2hvcnM9W107YWxsLmZvckVhY2goZWw9PntsZXQgcj1lbC5nZXRCb3VuZGluZ0NsaWVudFJlY3QoKTtpZihyLndpZHRoPjAmJnIuaGVpZ2h0PjAmJihyLnJpZ2h0PnZ3KzJ8fHIubGVmdDwtMikmJm92ZXJmbG93Lmxlbmd0aDwzMClvdmVyZmxvdy5wdXNoKHt0YWc6ZWwudGFnTmFtZS50b0xvd2VyQ2FzZSgpLGlkOihlbC5pZHx8IiIpLnNsaWNlKDAsODApLGNsYXNzTmFtZToodHlwZW9mIGVsLmNsYXNzTmFtZT09PSJzdHJpbmciP2VsLmNsYXNzTmFtZToiIikuc2xpY2UoMCwxMjApLGxlZnQ6TWF0aC5yb3VuZChyLmxlZnQpLHJpZ2h0Ok1hdGgucm91bmQoci5yaWdodCksd2lkdGg6TWF0aC5yb3VuZChyLndpZHRoKX0pO2xldCBpbnRlcj1lbC5tYXRjaGVzJiZlbC5tYXRjaGVzKCJhW2hyZWZdLGJ1dHRvbixpbnB1dCxzZWxlY3QsdGV4dGFyZWEsW3JvbGU9YnV0dG9uXSxbcm9sZT10YWJdLFt0YWJpbmRleF0iKTtpZihpbnRlciYmci53aWR0aD4wJiZyLmhlaWdodD4wJiYoci53aWR0aDw0MHx8ci5oZWlnaHQ8NDApJiZ0aW55Lmxlbmd0aDwzMCl0aW55LnB1c2goe3RhZzplbC50YWdOYW1lLnRvTG93ZXJDYXNlKCksdGV4dDooZWwuaW5uZXJUZXh0fHxlbC5nZXRBdHRyaWJ1dGUoImFyaWEtbGFiZWwiKXx8IiIpLnRyaW0oKS5zbGljZSgwLDEwMCksd2lkdGg6TWF0aC5yb3VuZChyLndpZHRoKSxoZWlnaHQ6TWF0aC5yb3VuZChyLmhlaWdodCl9KTtsZXQgY2xzPXR5cGVvZiBlbC5jbGFzc05hbWU9PT0ic3RyaW5nIj9lbC5jbGFzc05hbWU6IiI7aWYoKC9hbmltYXRlfGFuaW1hdGluZy8udGVzdChjbHMpfHxlbC5oYXNBdHRyaWJ1dGUoImRhdGEtYW5pbS10eXBlIikpJiZyLndpZHRoPjAmJnIuaGVpZ2h0PjApe2xldCBjcz1nZXRDb21wdXRlZFN0eWxlKGVsKTtpZigocGFyc2VGbG9hdChjcy5vcGFjaXR5KXx8MCk8LjA1fHxjcy52aXNpYmlsaXR5PT09ImhpZGRlbiIpaWYoaGlkZGVuTW90aW9uLmxlbmd0aDwzMCloaWRkZW5Nb3Rpb24ucHVzaCh7dGFnOmVsLnRhZ05hbWUudG9Mb3dlckNhc2UoKSxjbGFzc05hbWU6Y2xzLnNsaWNlKDAsMTIwKX0pfX0pO2RvY3VtZW50LnF1ZXJ5U2VsZWN0b3JBbGwoImFbaHJlZl0iKS5mb3JFYWNoKGE9PntpZihhbmNob3JzLmxlbmd0aD49NDApcmV0dXJuO2xldCBoPShhLmdldEF0dHJpYnV0ZSgiaHJlZiIpfHwiIikudHJpbSgpO2lmKCFofHxoPT09IiMifHwvXmphdmFzY3JpcHQ6L2kudGVzdChoKSl7YW5jaG9ycy5wdXNoKHtocmVmOmgsdGV4dDooYS5pbm5lclRleHR8fCIiKS50cmltKCkuc2xpY2UoMCwxMDApLGlzc3VlOiJlbXB0eV9vcl9ub25fbmF2aWdhdGlvbiJ9KTtyZXR1cm59aWYoaFswXT09PSIjIiYmaC5sZW5ndGg+MSYmIWRvY3VtZW50LmdldEVsZW1lbnRCeUlkKGguc2xpY2UoMSkpKWFuY2hvcnMucHVzaCh7aHJlZjpoLHRleHQ6KGEuaW5uZXJUZXh0fHwiIikudHJpbSgpLnNsaWNlKDAsMTAwKSxpc3N1ZToibWlzc2luZ19pbnRlcm5hbF90YXJnZXQifSl9KTtsZXQgYW5pbT1bXTt0cnl7YW5pbT1kb2N1bWVudC5nZXRBbmltYXRpb25zP2RvY3VtZW50LmdldEFuaW1hdGlvbnMoKTpbXX1jYXRjaChlKXt9bGV0IHJ1bm5pbmc9YW5pbS5maWx0ZXIoYT0+YS5wbGF5U3RhdGU9PT0icnVubmluZyIpLmxlbmd0aCxpbmZpbml0ZT1hbmltLmZpbHRlcihhPT57dHJ5e3JldHVybiBhLmVmZmVjdCYmYS5lZmZlY3QuZ2V0VGltaW5nKCkuaXRlcmF0aW9ucz09PUluZmluaXR5fWNhdGNoKGUpe3JldHVybiBmYWxzZX19KS5sZW5ndGg7cmV0dXJue3ZpZXdwb3J0Ont3aWR0aDp2dyxoZWlnaHQ6aW5uZXJIZWlnaHR9LGRvY3VtZW50OntzY3JvbGxXaWR0aDpNYXRoLm1heChkLnNjcm9sbFdpZHRoLGI/Yi5zY3JvbGxXaWR0aDowKSxjbGllbnRXaWR0aDpkLmNsaWVudFdpZHRoLGhvcml6b250YWxPdmVyZmxvdzpNYXRoLm1heChkLnNjcm9sbFdpZHRoLGI/Yi5zY3JvbGxXaWR0aDowKT5kLmNsaWVudFdpZHRoKzJ9LGNvdW50czp7ZWxlbWVudHM6YWxsLmxlbmd0aCxvdmVyZmxvdzpvdmVyZmxvdy5sZW5ndGgsdGlueUludGVyYWN0aXZlOnRpbnkubGVuZ3RoLGhpZGRlbk1vdGlvbjpoaWRkZW5Nb3Rpb24ubGVuZ3RoLGFuY2hvcklzc3VlczphbmNob3JzLmxlbmd0aH0sb3ZlcmZsb3c6b3ZlcmZsb3csdGlueUludGVyYWN0aXZlOnRpbnksaGlkZGVuTW90aW9uOmhpZGRlbk1vdGlvbixhbmNob3JJc3N1ZXM6YW5jaG9ycyxtb3Rpb246e21vZGU6Y2ZnLm1vdGlvbixydW5uaW5nQW5pbWF0aW9uczpydW5uaW5nLGluZmluaXRlQW5pbWF0aW9uczppbmZpbml0ZX19fWFzeW5jIGZ1bmN0aW9uIHJ1bigpe3RyeXthd2FpdCB3YWl0KGNmZy5kZWxheSk7bGV0IGludGVyPWF3YWl0IGludGVyYWN0aW9ucygpLHJlc3VsdD1zdGF0aWNDaGVja3MoKTtyZXN1bHQudmVyc2lvbj0yO3Jlc3VsdC5pbnRlcmFjdGlvbnM9aW50ZXI7bGV0IG9sZD1kb2N1bWVudC5nZXRFbGVtZW50QnlJZCgiZWxlbWVudGl6ZS1icm93c2VyLXFhLXJlc3VsdCIpO2lmKG9sZClvbGQucmVtb3ZlKCk7bGV0IG91dD1kb2N1bWVudC5jcmVhdGVFbGVtZW50KCJzY3JpcHQiKTtvdXQudHlwZT0iYXBwbGljYXRpb24vanNvbiI7b3V0LmlkPSJlbGVtZW50aXplLWJyb3dzZXItcWEtcmVzdWx0IjtvdXQudGV4dENvbnRlbnQ9SlNPTi5zdHJpbmdpZnkocmVzdWx0KS5yZXBsYWNlKC88L2csIlxcdTAwM2MiKTtkb2N1bWVudC5ib2R5LmFwcGVuZENoaWxkKG91dCl9Y2F0Y2goZSl7bGV0IG91dD1kb2N1bWVudC5jcmVhdGVFbGVtZW50KCJzY3JpcHQiKTtvdXQudHlwZT0iYXBwbGljYXRpb24vanNvbiI7b3V0LmlkPSJlbGVtZW50aXplLWJyb3dzZXItcWEtcmVzdWx0IjtvdXQudGV4dENvbnRlbnQ9SlNPTi5zdHJpbmdpZnkoe3ZlcnNpb246MixlcnJvcjpTdHJpbmcoZSYmZS5tZXNzYWdlfHxlKX0pO2RvY3VtZW50LmJvZHkuYXBwZW5kQ2hpbGQob3V0KX19aWYoZG9jdW1lbnQucmVhZHlTdGF0ZT09PSJjb21wbGV0ZSIpcnVuKCk7ZWxzZSBhZGRFdmVudExpc3RlbmVyKCJsb2FkIixydW4se29uY2U6dHJ1ZX0pO30p', true );
        if ( ! is_string( $script ) ) return;
        echo '<script id="elementize-browser-probe">' . $script . '(' . wp_json_encode( $config ) . ');</script>';
    }

    public static function disable_canonical_for_preview( $redirect_url, $requested_url ) {
        return self::preview_requested() ? false : $redirect_url;
    }

    public static function capture_context( int $id, string $hash, int $revision, string $viewport = 'desktop', string $motion = 'settled', string $interaction = 'passive' ): array {
        $viewport = self::normalize_viewport( $viewport );
        $motion = self::normalize_motion( $motion );
        $interaction = self::normalize_interaction( $interaction );
        $job_id = self::job_id( $id, $hash, $revision, $viewport, $motion, $interaction );
        return [
            'version' => self::VERSION,
            'viewport' => $viewport,
            'motion' => $motion,
            'interaction' => $interaction,
            'job_id' => $job_id,
            'job_key' => self::job_key( $job_id ),
            'paths' => self::temp_paths( $job_id ),
        ];
    }

    private static function job_id( int $id, string $hash, int $revision, string $viewport = 'desktop', string $motion = 'settled', string $interaction = 'passive' ): string {
        return substr( hash( 'sha256', $id . "\n" . $revision . "\n" . $hash . "\n" . self::normalize_viewport( $viewport ) . "\n" . self::normalize_motion( $motion ) . "\n" . self::normalize_interaction( $interaction ) ), 0, 24 );
    }

    private static function job_key( string $job_id ): string {
        return self::JOB_PREFIX . $job_id;
    }

    private static function job_matches( array $job, int $id, string $hash, int $revision ): bool {
        return absint( $job['page_id'] ?? 0 ) === $id
            && absint( $job['revision'] ?? 0 ) === $revision
            && is_string( $job['content_hash'] ?? null ) && hash_equals( (string) $job['content_hash'], $hash );
    }

    private static function pending_payload( array $base, string $job_id, array $job ): array {
        $paths = is_array( $job['paths'] ?? null ) ? $job['paths'] : [];
        $base['capture_state'] = 'pending';
        $base['capture_job_id'] = $job_id;
        $base['retry_after_ms'] = 1500;
        $base['capture_elapsed_ms'] = max( 0, (int) round( ( microtime( true ) - (float) ( $job['started_microtime'] ?? microtime( true ) ) ) * 1000 ) );
        $base['capture_artifact_state'] = self::capture_artifact_state( $paths );
        $base['note'] = 'The local browser capture is running outside the current WordPress request so the page can render without blocking on the REST worker. Call this same read-only Visual QA action again to collect the result.';
        return $base;
    }

    private static function temp_paths( string $job_id ): array {
        $base = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() );
        $prefix = $base . 'elementize-visual-' . $job_id;
        return [
            'screenshot' => $prefix . '.png',
            'profile' => $base . 'elementize-chrome-' . $job_id,
            'log' => $prefix . '.log',
            'done' => $prefix . '.done',
            'dom' => $prefix . '.html',
            'runner' => $prefix . ( 'Windows' === PHP_OS_FAMILY ? '.cmd' : '.sh' ),
        ];
    }

    private static function start_capture_job( string $url, string $browser, string $job_id, int $id, string $hash, int $revision, string $viewport = 'desktop' ) {
        $url = self::normalize_capture_url( $url );
        if ( '' === $url ) return new WP_Error( 'elementize_visual_capture_url_invalid', 'The local rendered capture target must be a valid HTTP(S) URL.' );
        if ( '' === $browser || ! is_file( $browser ) ) return new WP_Error( 'elementize_visual_browser_missing', 'A supported local browser executable was not found.' );
        $paths = self::temp_paths( $job_id );
        self::cleanup_paths( $paths );
        if ( ! wp_mkdir_p( $paths['profile'] ) ) return new WP_Error( 'elementize_visual_profile_failed', 'Could not create an isolated local browser profile.' );

        $runner = 'Windows' === PHP_OS_FAMILY
            ? self::windows_runner( $browser, $url, $paths, $viewport )
            : self::unix_runner( $browser, $url, $paths, $viewport );
        if ( is_wp_error( $runner ) ) { self::cleanup_paths( $paths ); return $runner; }
        if ( false === @file_put_contents( $paths['runner'], $runner ) ) { self::cleanup_paths( $paths ); return new WP_Error( 'elementize_visual_runner_write_failed', 'Could not create the temporary local capture runner.' ); }
        if ( 'Windows' !== PHP_OS_FAMILY ) @chmod( $paths['runner'], 0700 );

        $launched = self::launch_runner( $paths['runner'] );
        if ( is_wp_error( $launched ) ) { self::cleanup_paths( $paths ); return $launched; }
        return [
            'job_id' => $job_id,
            'page_id' => $id,
            'content_hash' => $hash,
            'revision' => $revision,
            'viewport' => self::normalize_viewport( $viewport ),
            'started_at' => time(),
            'started_microtime' => microtime( true ),
            'paths' => $paths,
        ];
    }

    private static function windows_runner( string $browser, string $url, array $paths, string $viewport = 'desktop' ) {
        $url = self::normalize_capture_url( $url );
        if ( '' === $url ) return new WP_Error( 'elementize_visual_capture_url_invalid', 'The Windows local capture runner refused an invalid HTTP(S) target URL.' );
        foreach ( [ $browser, $url, $paths['screenshot'], $paths['profile'], $paths['log'], $paths['done'] ] as $value ) {
            if ( preg_match( '/[\r\n"]/', (string) $value ) ) return new WP_Error( 'elementize_visual_runner_value_invalid', 'A local capture runner value could not be safely encoded.' );
        }
        $dom_path = (string) ( $paths['dom'] ?? ( (string) ( $paths['screenshot'] ?? '' ) . '.html' ) );
        $q = static function( string $value ): string { return '"' . str_replace( '%', '%%', $value ) . '"'; };
        return "@echo off\r\n"
            . "ping 127.0.0.1 -n 2 >nul\r\n"
            . $q( $browser )
            . ' --headless=new --disable-gpu --disable-dev-shm-usage --hide-scrollbars --mute-audio --no-first-run --no-default-browser-check --disable-extensions --disable-background-networking --disable-sync --metrics-recording-only --disable-logging --log-level=3 --ignore-certificate-errors --allow-insecure-localhost --timeout=12000'
            . ' --user-data-dir=' . $q( $paths['profile'] )
            . ' --window-size=' . self::viewport_spec( $viewport )['width'] . ',' . self::CAPTURE_HEIGHT
            . ' --virtual-time-budget=8000 --screenshot=' . $q( $paths['screenshot'] )
            . ' ' . $q( $url ) . ' >' . $q( $paths['log'] ) . " 2>&1\r\n"
            . $q( $browser ) . ' --headless=new --disable-gpu --disable-dev-shm-usage --mute-audio --no-first-run --no-default-browser-check --disable-extensions --disable-background-networking --disable-sync --metrics-recording-only --disable-logging --log-level=3 --ignore-certificate-errors --allow-insecure-localhost --timeout=12000 --user-data-dir=' . $q( $paths['profile'] ) . ' --window-size=' . self::viewport_spec( $viewport )['width'] . ',1200 --virtual-time-budget=3500 --dump-dom ' . $q( $url ) . ' >' . $q( $dom_path ) . ' 2>>' . $q( $paths['log'] ) . "\r\n"
            . '> ' . $q( $paths['done'] ) . " echo done\r\n"
            . "del \"%~f0\" >nul 2>&1\r\n";
    }

    private static function unix_runner( string $browser, string $url, array $paths, string $viewport = 'desktop' ): string {
        $url = self::normalize_capture_url( $url );
        if ( '' === $url ) return '';
        $dom_path = (string) ( $paths['dom'] ?? ( (string) ( $paths['screenshot'] ?? '' ) . '.html' ) );
        $q = static fn( string $value ): string => escapeshellarg( $value );
        return "#!/bin/sh\n"
            . "sleep 1\n"
            . $q( $browser )
            . ' --headless=new --disable-gpu --disable-dev-shm-usage --hide-scrollbars --mute-audio --no-first-run --no-default-browser-check --disable-extensions --disable-background-networking --disable-sync --metrics-recording-only --disable-logging --log-level=3 --ignore-certificate-errors --allow-insecure-localhost --timeout=12000'
            . ' --user-data-dir=' . $q( $paths['profile'] )
            . ' --window-size=' . self::viewport_spec( $viewport )['width'] . ',' . self::CAPTURE_HEIGHT
            . ' --virtual-time-budget=8000 --screenshot=' . $q( $paths['screenshot'] )
            . ' ' . $q( $url ) . ' >' . $q( $paths['log'] ) . " 2>&1\n"
            . $q( $browser ) . ' --headless=new --disable-gpu --disable-dev-shm-usage --mute-audio --no-first-run --no-default-browser-check --disable-extensions --disable-background-networking --disable-sync --metrics-recording-only --disable-logging --log-level=3 --ignore-certificate-errors --allow-insecure-localhost --timeout=12000 --user-data-dir=' . $q( $paths['profile'] ) . ' --window-size=' . self::viewport_spec( $viewport )['width'] . ',1200 --virtual-time-budget=3500 --dump-dom ' . $q( $url ) . ' >' . $q( $dom_path ) . ' 2>>' . $q( $paths['log'] ) . "\n"
            . 'printf done >' . $q( $paths['done'] ) . "\n"
            . 'rm -f ' . $q( $paths['runner'] ) . "\n";
    }

    private static function launch_runner( string $runner ) {
        if ( ! self::process_available() ) return new WP_Error( 'elementize_visual_process_unavailable', 'PHP proc_open is unavailable.' );
        if ( preg_match( '/[\r\n"]/', $runner ) ) return new WP_Error( 'elementize_visual_runner_path_invalid', 'The temporary local capture runner path could not be safely encoded.' );
        $null = 'Windows' === PHP_OS_FAMILY ? 'NUL' : '/dev/null';
        $spec = [ 0 => [ 'file', $null, 'r' ], 1 => [ 'file', $null, 'a' ], 2 => [ 'file', $null, 'a' ] ];
        $pipes = [];
        if ( 'Windows' === PHP_OS_FAMILY ) {
            $system_root = rtrim( trim( (string) getenv( 'SystemRoot' ) ), "\\/" );
            $powershell = '' !== $system_root ? $system_root . '\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' : 'powershell.exe';
            if ( '' !== $system_root && ! is_file( $powershell ) ) $powershell = 'powershell.exe';
            $cmd = trim( (string) getenv( 'ComSpec' ) );
            if ( '' === $cmd ) $cmd = '' !== $system_root ? $system_root . '\\System32\\cmd.exe' : 'cmd.exe';
            $psq = static function( string $value ): string { return "'" . str_replace( "'", "''", $value ) . "'"; };
            $script = 'Start-Process -FilePath ' . $psq( $cmd ) . ' -ArgumentList ' . $psq( '/d /c call "' . $runner . '"' ) . ' -WindowStyle Hidden';
            $script = str_replace( '\\"', '"', $script );
            $command = [ $powershell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-Command', $script ];
        } else {
            $command = [ '/bin/sh', '-c', 'nohup /bin/sh ' . escapeshellarg( $runner ) . ' >/dev/null 2>&1 &' ];
        }
        $proc = @proc_open( $command, $spec, $pipes, null, null, [ 'bypass_shell' => true ] );
        if ( ! is_resource( $proc ) ) return new WP_Error( 'elementize_visual_runner_start_failed', 'Could not start the asynchronous local capture runner.' );
        $started = microtime( true );
        do {
            $status = proc_get_status( $proc );
            if ( empty( $status['running'] ) ) break;
            if ( microtime( true ) - $started > 2 ) break;
            usleep( 50000 );
        } while ( true );
        @proc_close( $proc );
        return true;
    }

    public static function capture_job_complete( array $paths ): bool {
        $screenshot = (string) ( $paths['screenshot'] ?? '' );
        $done = (string) ( $paths['done'] ?? '' );
        $dom = (string) ( $paths['dom'] ?? '' );
        return self::png_file_complete( $screenshot ) && '' !== $done && is_file( $done ) && '' !== $dom && is_file( $dom );
    }

    public static function png_file_complete( string $path ): bool {
        if ( '' === $path || ! is_file( $path ) ) return false;
        clearstatcache( true, $path );
        $size = (int) @filesize( $path );
        if ( $size < 1000 ) return false;
        $handle = @fopen( $path, 'rb' );
        if ( ! is_resource( $handle ) ) return false;
        $tail = '';
        if ( 0 === @fseek( $handle, -12, SEEK_END ) ) $tail = (string) @fread( $handle, 12 );
        @fclose( $handle );
        return "\x00\x00\x00\x00IEND\xAE\x42\x60\x82" === $tail;
    }

    public static function capture_artifact_state( array $paths ): array {
        $screenshot = (string) ( $paths['screenshot'] ?? '' );
        clearstatcache( true, $screenshot );
        return [
            'runner_exists' => isset( $paths['runner'] ) && is_string( $paths['runner'] ) && is_file( $paths['runner'] ),
            'log_exists' => isset( $paths['log'] ) && is_string( $paths['log'] ) && is_file( $paths['log'] ),
            'done_exists' => isset( $paths['done'] ) && is_string( $paths['done'] ) && is_file( $paths['done'] ),
            'screenshot_exists' => '' !== $screenshot && is_file( $screenshot ),
            'screenshot_bytes' => '' !== $screenshot && is_file( $screenshot ) ? max( 0, (int) @filesize( $screenshot ) ) : 0,
            'screenshot_complete' => self::png_file_complete( $screenshot ),
            'browser_probe_exists' => isset( $paths['dom'] ) && is_string( $paths['dom'] ) && is_file( $paths['dom'] ),
            'browser_probe_complete' => self::capture_job_complete( $paths ),
        ];
    }

    private static function finalize_capture_job( array $job ) {
        $paths = is_array( $job['paths'] ?? null ) ? $job['paths'] : [];
        $screenshot = (string) ( $paths['screenshot'] ?? '' );
        if ( ! self::png_file_complete( $screenshot ) ) {
            $diagnostic = self::read_capture_log( (string) ( $paths['log'] ?? '' ) );
            return new WP_Error( 'elementize_visual_no_screenshot', self::clip( 'The asynchronous local browser did not produce a complete PNG.' . ( '' !== $diagnostic ? ' Browser detail: ' . $diagnostic : '' ), 500 ) );
        }
        self::bound_screenshot( $screenshot );
        $bytes = @file_get_contents( $screenshot );
        if ( false === $bytes || '' === $bytes ) return new WP_Error( 'elementize_visual_screenshot_read_failed', 'The captured screenshot could not be read.' );
        if ( strlen( $bytes ) > self::MAX_SCREENSHOT_BYTES ) return new WP_Error( 'elementize_visual_screenshot_too_large', 'The local screenshot exceeded the 10 MB safety limit.' );
        $width = null;
        $height = null;
        if ( function_exists( 'getimagesizefromstring' ) ) {
            $size = @getimagesizefromstring( $bytes );
            if ( is_array( $size ) ) {
                $width = absint( $size[0] ?? 0 ) ?: null;
                $height = absint( $size[1] ?? 0 ) ?: null;
            }
        }
        $probe = self::browser_probe_result( (string) ( $paths['dom'] ?? '' ) );
        return [ 'bytes' => $bytes, 'byte_count' => strlen( $bytes ), 'sha256' => hash( 'sha256', $bytes ), 'width' => $width, 'height' => $height, 'browser_probe' => $probe ];
    }

    public static function browser_probe_result( string $path ): array {
        $result = [ 'verified' => false, 'available' => false, 'diagnostics' => null ];
        if ( '' === $path || ! is_file( $path ) ) return $result;
        $html = @file_get_contents( $path );
        if ( ! is_string( $html ) || '' === $html ) return $result;
        if ( ! preg_match( '#<script[^>]+id=["\']elementize-browser-qa-result["\'][^>]*>(.*?)</script>#si', $html, $match ) ) return $result;
        $decoded = json_decode( html_entity_decode( (string) $match[1], ENT_QUOTES | ENT_HTML5, 'UTF-8' ), true );
        if ( ! is_array( $decoded ) ) return $result;
        $result['available'] = true;
        $result['verified'] = empty( $decoded['error'] );
        $result['diagnostics'] = $decoded;
        return $result;
    }

    private static function read_capture_log( string $path ): string {
        if ( '' === $path || ! is_file( $path ) ) return '';
        $raw = @file_get_contents( $path );
        if ( ! is_string( $raw ) || '' === trim( $raw ) ) return '';
        $raw = preg_replace( '/\\s+/', ' ', trim( $raw ) ) ?: trim( $raw );
        return self::clip( $raw, 280 );
    }

    private static function cleanup_paths( array $paths ): void {
        foreach ( [ 'screenshot', 'log', 'done', 'dom', 'runner' ] as $key ) if ( isset( $paths[ $key ] ) && is_string( $paths[ $key ] ) ) @unlink( $paths[ $key ] );
        if ( isset( $paths['profile'] ) && is_string( $paths['profile'] ) ) self::remove_dir( $paths['profile'] );
    }

    private static function cleanup_job( array $job ): void {
        self::cleanup_paths( is_array( $job['paths'] ?? null ) ? $job['paths'] : [] );
    }

    private static function cleanup_stale_temp_files(): void {
        $base = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() );
        $items = @scandir( $base );
        if ( ! is_array( $items ) ) return;
        $cutoff = time() - 900;
        foreach ( $items as $item ) {
            if ( ! str_starts_with( $item, 'elementize-visual-' ) && ! str_starts_with( $item, 'elementize-chrome-' ) ) continue;
            $path = $base . $item;
            $mtime = @filemtime( $path );
            if ( false === $mtime || $mtime > $cutoff ) continue;
            if ( is_dir( $path ) ) self::remove_dir( $path ); else @unlink( $path );
        }
    }

    public static function bound_screenshot( string $path ): array {
        $result = [
            'available' => false,
            'trimmed' => false,
            'original_height' => null,
            'bounded_height' => null,
        ];
        if ( '' === $path || ! is_file( $path ) ) return $result;
        if ( ! function_exists( 'imagecreatefrompng' ) || ! function_exists( 'imagecrop' ) || ! function_exists( 'imagepng' ) ) return $result;
        $im = @imagecreatefrompng( $path );
        if ( ! $im ) return $result;
        $w = imagesx( $im );
        $h = imagesy( $im );
        $result['available'] = true;
        $result['original_height'] = $h;
        $result['bounded_height'] = $h;
        if ( $w < 100 || $h < 1200 ) {
            imagedestroy( $im );
            return $result;
        }
        $sample_x = [ (int) ( $w * .08 ), (int) ( $w * .25 ), (int) ( $w * .5 ), (int) ( $w * .75 ), (int) ( $w * .92 ) ];
        $bg = [ 0, 0, 0 ];
        foreach ( $sample_x as $x ) {
            $rgb = imagecolorat( $im, $x, $h - 6 );
            $bg[0] += ( $rgb >> 16 ) & 255;
            $bg[1] += ( $rgb >> 8 ) & 255;
            $bg[2] += $rgb & 255;
        }
        $bg = [
            (int) round( $bg[0] / count( $sample_x ) ),
            (int) round( $bg[1] / count( $sample_x ) ),
            (int) round( $bg[2] / count( $sample_x ) ),
        ];
        $last = $h - 1;
        for ( $y = $h - 20; $y > 400; $y -= 4 ) {
            $different = 0;
            for ( $i = 1; $i <= 20; $i++ ) {
                $x = (int) round( $w * $i / 21 );
                $rgb = imagecolorat( $im, $x, $y );
                $r = ( $rgb >> 16 ) & 255;
                $g = ( $rgb >> 8 ) & 255;
                $b = $rgb & 255;
                if ( abs( $r - $bg[0] ) + abs( $g - $bg[1] ) + abs( $b - $bg[2] ) > 54 ) $different++;
            }
            if ( $different >= 2 ) {
                $last = min( $h, $y + 180 );
                break;
            }
        }
        if ( $last < $h - 300 ) {
            $bounded_height = max( 600, $last );
            $crop = @imagecrop( $im, [ 'x' => 0, 'y' => 0, 'width' => $w, 'height' => $bounded_height ] );
            if ( $crop ) {
                if ( @imagepng( $crop, $path, 6 ) ) {
                    $result['trimmed'] = true;
                    $result['bounded_height'] = $bounded_height;
                }
                imagedestroy( $crop );
            }
        }
        imagedestroy( $im );
        return $result;
    }

    private static function safe_error( WP_Error $error, string $fallback ): string {
        $message = trim( (string) $error->get_error_message() );
        return self::clip( '' !== $message ? $message : $fallback, 500 );
    }

    private static function clip( string $value, int $limit ): string {
        $value = sanitize_textarea_field( $value );
        if ( function_exists( 'mb_strlen' ) && function_exists( 'mb_substr' ) ) return mb_strlen( $value ) > $limit ? mb_substr( $value, 0, $limit ) . '…' : $value;
        return strlen( $value ) > $limit ? substr( $value, 0, $limit ) . '…' : $value;
    }

    private static function remove_dir( string $dir ): void {
        if ( '' === $dir || ! is_dir( $dir ) ) return;
        $items = @scandir( $dir );
        foreach ( is_array( $items ) ? $items : [] as $item ) {
            if ( '.' === $item || '..' === $item ) continue;
            $path = $dir . DIRECTORY_SEPARATOR . $item;
            if ( is_dir( $path ) ) self::remove_dir( $path ); else @unlink( $path );
        }
        @rmdir( $dir );
    }
}

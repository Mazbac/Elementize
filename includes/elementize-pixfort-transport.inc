<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Pixfort_Ajax_Stop extends RuntimeException {}

final class Elementize_Pixfort_Transport {
    private const ESSENTIALS_PURCHASE_OPTION = 'envato_purchase_code_27889640';
    private const TEMPLATE_AJAX_ACTION = 'pix_core_getElementorTemplate';
    private const MAX_CALLBACK_SOURCE_BYTES = 2097152;
    private const MAX_AJAX_RESPONSE_BYTES = 33554432;

    public static function init(): void {
        add_filter( 'pixfort_el_remote_get_args', [ self::class, 'authenticated_remote_args' ], 100 );
    }

    public static function authenticated_remote_args( $args ) {
        if ( ! is_array( $args ) ) $args = [];

        // Pixfort Core's native Elementor source expects the licensed site identity on template-download requests.
        // This mirrors the transport used by Elementize's previously runtime-proven Pixfort inspection path.
        $purchase_key = trim( (string) get_option( self::ESSENTIALS_PURCHASE_OPTION, '' ) );
        if ( '' === $purchase_key ) return $args;

        $headers = is_array( $args['headers'] ?? null ) ? $args['headers'] : [];
        $headers['pix_domain'] = site_url();
        $headers['purchase_key'] = $purchase_key;
        $args['headers'] = $headers;
        return $args;
    }

    /**
     * Execute the same authenticated local Pixfort AJAX handler observed in the real Elementor editor HAR.
     *
     * This deliberately calls the already-registered WordPress callback in-process instead of performing a
     * loopback HTTP request. The current REST/Application-Password user therefore remains the authenticated
     * user and no cookies, purchase keys, or nonces leave the PHP process.
     */
    public static function load_template_via_registered_ajax( string $template_id, int $page_id ) {
        $template_id = sanitize_text_field( $template_id );
        $page_id = absint( $page_id );
        if ( '' === $template_id ) {
            return new WP_Error( 'elementize_pixfort_ajax_template_required', 'A Pixfort template ID is required.', [ 'status' => 400, 'adapter' => 'pixfort_registered_ajax' ] );
        }
        if ( $page_id < 1 ) {
            return new WP_Error(
                'elementize_pixfort_ajax_page_context_required',
                'Pixfort local template loading requires the active Creative Control page context.',
                [ 'status' => 503, 'adapter' => 'pixfort_registered_ajax' ]
            );
        }

        $hook = 'wp_ajax_' . self::TEMPLATE_AJAX_ACTION;
        $callbacks = self::hook_callbacks( $hook );
        if ( ! $callbacks ) {
            return new WP_Error(
                'elementize_pixfort_ajax_handler_unavailable',
                'The Pixfort template AJAX handler observed in the Elementor editor is not registered in this request.',
                [ 'status' => 503, 'adapter' => 'pixfort_registered_ajax', 'ajax_action' => self::TEMPLATE_AJAX_ACTION ]
            );
        }

        $inspection = self::inspect_callbacks( $callbacks );
        if ( is_wp_error( $inspection ) ) return $inspection;

        $nonce_action = (string) ( $inspection['nonce_action'] ?? '' );
        $nonce_detection = (string) ( $inspection['nonce_detection'] ?? 'fallback_action_name' );
        if ( '' === $nonce_action ) {
            // The real HAR contains a `nonce` request field. When the callback does not contain a literal nonce
            // action, the AJAX action name is the safest conventional fallback; a failed check remains contained.
            $nonce_action = self::TEMPLATE_AJAX_ACTION;
            $nonce_detection = 'fallback_action_name';
        }

        $request_fields = [
            'action' => self::TEMPLATE_AJAX_ACTION,
            'nonce' => wp_create_nonce( $nonce_action ),
            'editor_post_id' => $page_id,
            'initial_document_id' => $page_id,
            'template_id' => $template_id,
        ];

        $previous_post = $_POST;
        $previous_get = $_GET;
        $previous_request = $_REQUEST;
        $had_request_method = array_key_exists( 'REQUEST_METHOD', $_SERVER );
        $previous_request_method = $had_request_method ? $_SERVER['REQUEST_METHOD'] : null;
        $had_requested_with = array_key_exists( 'HTTP_X_REQUESTED_WITH', $_SERVER );
        $previous_requested_with = $had_requested_with ? $_SERVER['HTTP_X_REQUESTED_WITH'] : null;

        $_POST = $request_fields;
        $_GET = [];
        $_REQUEST = $request_fields;
        $_SERVER['REQUEST_METHOD'] = 'POST';
        $_SERVER['HTTP_X_REQUESTED_WITH'] = 'XMLHttpRequest';

        add_filter( 'wp_doing_ajax', [ self::class, 'force_doing_ajax' ], PHP_INT_MAX );
        add_filter( 'wp_die_ajax_handler', [ self::class, 'capture_wp_die_handler' ], PHP_INT_MAX );

        $output = '';
        $die_message = '';
        $thrown = null;
        $buffer_level = ob_get_level();
        ob_start();
        try {
            do_action( $hook );
        } catch ( Elementize_Pixfort_Ajax_Stop $e ) {
            $die_message = $e->getMessage();
        } catch ( Throwable $e ) {
            $thrown = $e;
        } finally {
            if ( ob_get_level() > $buffer_level ) $output = (string) ob_get_clean();
            while ( ob_get_level() > $buffer_level ) ob_end_clean();

            remove_filter( 'wp_doing_ajax', [ self::class, 'force_doing_ajax' ], PHP_INT_MAX );
            remove_filter( 'wp_die_ajax_handler', [ self::class, 'capture_wp_die_handler' ], PHP_INT_MAX );

            $_POST = $previous_post;
            $_GET = $previous_get;
            $_REQUEST = $previous_request;
            if ( $had_request_method ) $_SERVER['REQUEST_METHOD'] = $previous_request_method;
            else unset( $_SERVER['REQUEST_METHOD'] );
            if ( $had_requested_with ) $_SERVER['HTTP_X_REQUESTED_WITH'] = $previous_requested_with;
            else unset( $_SERVER['HTTP_X_REQUESTED_WITH'] );
        }

        $base_data = [
            'status' => 503,
            'adapter' => 'pixfort_registered_ajax',
            'ajax_action' => self::TEMPLATE_AJAX_ACTION,
            'scope_page_id' => $page_id,
            'nonce_detection' => $nonce_detection,
        ];

        if ( $thrown instanceof Throwable ) {
            return new WP_Error(
                'elementize_pixfort_ajax_handler_failed',
                'Pixfort local template loading failed inside its registered AJAX handler: ' . sanitize_text_field( $thrown->getMessage() ),
                $base_data
            );
        }

        $bytes = strlen( $output );
        if ( $bytes > self::MAX_AJAX_RESPONSE_BYTES ) {
            $base_data['response_bytes'] = $bytes;
            return new WP_Error( 'elementize_pixfort_ajax_response_too_large', 'Pixfort local template loading returned an oversized response.', $base_data );
        }

        $decoded = self::decode_ajax_output( $output );
        if ( ! is_array( $decoded ) ) {
            $base_data['response_bytes'] = $bytes;
            $base_data['response_kind'] = preg_match( '/^\s*</', $output ) ? 'html_or_xml' : ( '' === trim( $output ) ? 'empty' : 'non_json' );
            if ( '' !== trim( $die_message ) ) $base_data['die_message'] = substr( sanitize_text_field( $die_message ), 0, 160 );
            return new WP_Error( 'elementize_pixfort_ajax_response_invalid', 'Pixfort local template loading did not return usable JSON.', $base_data );
        }

        if ( array_key_exists( 'success', $decoded ) && false === $decoded['success'] ) {
            $message = self::response_message( $decoded['data'] ?? null );
            return new WP_Error(
                'elementize_pixfort_ajax_remote_error',
                '' !== $message ? 'Pixfort local template loading failed: ' . $message : 'Pixfort local template loading returned an error.',
                $base_data
            );
        }

        $payload = isset( $decoded['data'] ) && is_array( $decoded['data'] ) ? $decoded['data'] : $decoded;
        if ( ! self::contains_elementor_content( $payload ) ) {
            $base_data['response_bytes'] = $bytes;
            return new WP_Error( 'elementize_pixfort_ajax_content_missing', 'Pixfort local template loading returned JSON without Elementor content.', $base_data );
        }
        return $payload;
    }

    public static function force_doing_ajax( $doing_ajax ): bool {
        return true;
    }

    public static function capture_wp_die_handler( $handler ) {
        return [ self::class, 'throw_wp_die' ];
    }

    public static function throw_wp_die( $message = '', $title = '', $args = [] ): void {
        $text = is_scalar( $message ) ? (string) $message : '';
        throw new Elementize_Pixfort_Ajax_Stop( $text );
    }

    private static function hook_callbacks( string $hook ): array {
        $registered = $GLOBALS['wp_filter'][ $hook ] ?? null;
        if ( ! $registered instanceof WP_Hook || ! is_array( $registered->callbacks ) ) return [];

        $out = [];
        foreach ( $registered->callbacks as $priority => $callbacks ) {
            if ( ! is_array( $callbacks ) ) continue;
            foreach ( $callbacks as $entry ) {
                $callback = is_array( $entry ) ? ( $entry['function'] ?? null ) : null;
                if ( is_callable( $callback ) ) $out[] = $callback;
            }
        }
        return $out;
    }

    private static function inspect_callbacks( array $callbacks ) {
        $body_actions = [];
        $file_actions = [];
        $direct_exit = false;
        $inspected = 0;

        foreach ( $callbacks as $callback ) {
            $reflection = self::reflect_callable( $callback );
            if ( ! $reflection instanceof ReflectionFunctionAbstract ) continue;
            $file = $reflection->getFileName();
            if ( ! is_string( $file ) || ! is_file( $file ) || ! self::trusted_source_file( $file ) ) continue;
            $size = @filesize( $file );
            if ( ! is_int( $size ) || $size < 1 || $size > self::MAX_CALLBACK_SOURCE_BYTES ) continue;

            $contents = @file_get_contents( $file );
            if ( ! is_string( $contents ) || '' === $contents ) continue;
            $inspected++;

            $lines = preg_split( '/\R/', $contents );
            if ( is_array( $lines ) ) {
                $start = max( 1, (int) $reflection->getStartLine() );
                $end = max( $start, (int) $reflection->getEndLine() );
                $body = implode( "\n", array_slice( $lines, $start - 1, $end - $start + 1 ) );
                $body_actions = array_merge( $body_actions, self::nonce_actions( $body ) );
                if ( self::contains_direct_exit( $body ) ) $direct_exit = true;
            }
            $file_actions = array_merge( $file_actions, self::nonce_actions( $contents ) );
        }

        if ( $direct_exit ) {
            return new WP_Error(
                'elementize_pixfort_ajax_handler_unsafe_exit',
                'The registered Pixfort AJAX handler contains a direct die/exit statement, so Elementize will not execute it in-process.',
                [ 'status' => 503, 'adapter' => 'pixfort_registered_ajax', 'ajax_action' => self::TEMPLATE_AJAX_ACTION ]
            );
        }

        $body_actions = array_values( array_unique( array_filter( $body_actions ) ) );
        if ( 1 === count( $body_actions ) ) {
            return [ 'nonce_action' => $body_actions[0], 'nonce_detection' => 'callback_literal', 'callbacks_inspected' => $inspected ];
        }
        if ( count( $body_actions ) > 1 ) {
            return new WP_Error(
                'elementize_pixfort_ajax_nonce_ambiguous',
                'The registered Pixfort AJAX callback contains multiple nonce actions; Elementize will not guess which one protects template loading.',
                [ 'status' => 503, 'adapter' => 'pixfort_registered_ajax', 'ajax_action' => self::TEMPLATE_AJAX_ACTION, 'nonce_candidates' => count( $body_actions ) ]
            );
        }

        $file_actions = array_values( array_unique( array_filter( $file_actions ) ) );
        if ( 1 === count( $file_actions ) ) {
            return [ 'nonce_action' => $file_actions[0], 'nonce_detection' => 'callback_file_literal', 'callbacks_inspected' => $inspected ];
        }

        return [ 'nonce_action' => self::TEMPLATE_AJAX_ACTION, 'nonce_detection' => 'fallback_action_name', 'callbacks_inspected' => $inspected ];
    }

    private static function reflect_callable( $callback ): ?ReflectionFunctionAbstract {
        try {
            if ( $callback instanceof Closure || is_string( $callback ) ) return new ReflectionFunction( $callback );
            if ( is_array( $callback ) && 2 === count( $callback ) ) return new ReflectionMethod( $callback[0], (string) $callback[1] );
            if ( is_object( $callback ) && method_exists( $callback, '__invoke' ) ) return new ReflectionMethod( $callback, '__invoke' );
        } catch ( Throwable $ignored ) {}
        return null;
    }

    private static function trusted_source_file( string $file ): bool {
        $real = realpath( $file );
        $root = defined( 'ABSPATH' ) ? realpath( ABSPATH ) : false;
        if ( ! is_string( $real ) || ! is_string( $root ) ) return false;
        $real = wp_normalize_path( $real );
        $root = rtrim( wp_normalize_path( $root ), '/' ) . '/';
        return str_starts_with( $real, $root );
    }

    private static function nonce_actions( string $source ): array {
        $out = [];
        foreach ( [
            '/check_ajax_referer\s*\(\s*[\'\"]([^\'\"]+)[\'\"]/i',
            '/wp_verify_nonce\s*\([^;]{0,700},\s*[\'\"]([^\'\"]+)[\'\"]\s*\)/i',
        ] as $pattern ) {
            if ( preg_match_all( $pattern, $source, $matches ) ) {
                foreach ( (array) ( $matches[1] ?? [] ) as $value ) {
                    $value = trim( (string) $value );
                    if ( '' !== $value ) $out[] = $value;
                }
            }
        }
        return $out;
    }

    private static function contains_direct_exit( string $source ): bool {
        $tokens = token_get_all( "<?php\n" . $source );
        foreach ( $tokens as $token ) {
            if ( is_array( $token ) && T_EXIT === $token[0] ) return true;
        }
        return false;
    }

    private static function decode_ajax_output( string $output ): ?array {
        $output = trim( $output );
        if ( str_starts_with( $output, "\xEF\xBB\xBF" ) ) $output = substr( $output, 3 );
        if ( '' === $output ) return null;

        $decoded = json_decode( $output, true, 512, JSON_BIGINT_AS_STRING );
        if ( is_array( $decoded ) ) return $decoded;

        // Contain harmless warnings/notices emitted ahead of an otherwise valid JSON response.
        $first = strpos( $output, '{' );
        $last = strrpos( $output, '}' );
        if ( false !== $first && false !== $last && $last > $first ) {
            $candidate = substr( $output, $first, $last - $first + 1 );
            $decoded = json_decode( $candidate, true, 512, JSON_BIGINT_AS_STRING );
            if ( is_array( $decoded ) ) return $decoded;
        }
        return null;
    }

    private static function response_message( $data ): string {
        if ( is_string( $data ) ) return substr( sanitize_text_field( $data ), 0, 240 );
        if ( is_array( $data ) ) {
            foreach ( [ 'message', 'error' ] as $key ) {
                if ( is_scalar( $data[ $key ] ?? null ) ) return substr( sanitize_text_field( (string) $data[ $key ] ), 0, 240 );
            }
        }
        return '';
    }

    private static function contains_elementor_content( array $payload ): bool {
        foreach ( [ 'content', 'elements' ] as $key ) {
            if ( isset( $payload[ $key ] ) && is_array( $payload[ $key ] ) && $payload[ $key ] ) return true;
        }
        if ( isset( $payload['data'] ) && is_array( $payload['data'] ) ) return self::contains_elementor_content( $payload['data'] );
        return false;
    }
}

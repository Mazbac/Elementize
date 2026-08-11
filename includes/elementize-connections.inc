<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Connections {
    private const NAME_PREFIX = 'Elementize - ';
    private const OPT_USAGE = 'elementize_connection_usage';

    public static function init(): void {
        add_filter( 'rest_request_after_callbacks', [ self::class, 'track_successful_request' ], 90, 3 );
        add_action( 'wp_ajax_elementize_get_connections', [ self::class, 'ajax_get_connections' ] );
        add_action( 'wp_ajax_elementize_revoke_connection', [ self::class, 'ajax_revoke_connection' ] );
        add_action( 'wp_ajax_elementize_revoke_all_connections', [ self::class, 'ajax_revoke_all_connections' ] );
    }

    public static function track_successful_request( $response, array $handler, WP_REST_Request $request ) {
        $route = (string) $request->get_route();
        if ( ! str_starts_with( $route, '/' . Elementize_Core::NS . '/' ) ) return $response;
        if ( is_wp_error( $response ) ) return $response;

        $rest_response = rest_ensure_response( $response );
        $status = (int) $rest_response->get_status();
        if ( $status < 200 || $status >= 400 ) return $response;
        if ( ! function_exists( 'rest_get_authenticated_app_password' ) || ! class_exists( 'WP_Application_Passwords' ) ) return $response;

        $uuid = (string) rest_get_authenticated_app_password();
        $user_id = get_current_user_id();
        if ( '' === $uuid || ! $user_id ) return $response;

        $password = WP_Application_Passwords::get_user_application_password( $user_id, $uuid );
        if ( ! is_array( $password ) || ! self::is_elementize_password( $password ) ) return $response;

        $usage = self::usage();
        $usage[ $uuid ] = [
            'user_id' => $user_id,
            'last_successful_call' => time(),
            'route' => sanitize_text_field( $route ),
        ];
        self::save_usage( $usage );
        return $response;
    }

    public static function ajax_get_connections(): void {
        $user = self::require_admin_user();
        nocache_headers();
        wp_send_json_success( self::summary( $user ) );
    }

    public static function ajax_revoke_connection(): void {
        $user = self::require_admin_user();
        if ( ! class_exists( 'WP_Application_Passwords' ) ) wp_send_json_error( [ 'message' => 'WordPress Application Password support is unavailable.' ], 500 );

        $uuid = sanitize_text_field( (string) wp_unslash( $_POST['uuid'] ?? '' ) );
        if ( '' === $uuid || ! self::owns_uuid( $user->ID, $uuid ) ) wp_send_json_error( [ 'message' => 'Elementize connection not found.' ], 404 );

        $deleted = WP_Application_Passwords::delete_application_password( $user->ID, $uuid );
        if ( is_wp_error( $deleted ) ) wp_send_json_error( [ 'message' => $deleted->get_error_message() ], 400 );

        self::remove_usage( [ $uuid ] );
        nocache_headers();
        wp_send_json_success( self::summary( $user ) );
    }

    public static function ajax_revoke_all_connections(): void {
        $user = self::require_admin_user();
        if ( ! class_exists( 'WP_Application_Passwords' ) ) wp_send_json_error( [ 'message' => 'WordPress Application Password support is unavailable.' ], 500 );

        $passwords = self::elementize_passwords( $user->ID );
        $removed = [];
        $errors = [];
        foreach ( $passwords as $password ) {
            $uuid = (string) ( $password['uuid'] ?? '' );
            if ( '' === $uuid ) continue;
            $deleted = WP_Application_Passwords::delete_application_password( $user->ID, $uuid );
            if ( is_wp_error( $deleted ) ) {
                $errors[] = $deleted->get_error_message();
                continue;
            }
            $removed[] = $uuid;
        }

        self::remove_usage( $removed );
        if ( $errors ) wp_send_json_error( [ 'message' => 'Some Elementize connections could not be revoked.', 'details' => $errors ], 500 );

        nocache_headers();
        wp_send_json_success( self::summary( $user ) );
    }

    private static function require_admin_user(): WP_User {
        if ( ! current_user_can( 'manage_options' ) ) wp_send_json_error( [ 'message' => 'Administrator permission is required.' ], 403 );
        check_ajax_referer( 'elementize_onboarding', 'nonce' );
        $user = wp_get_current_user();
        if ( ! $user || ! $user->exists() ) wp_send_json_error( [ 'message' => 'Current WordPress user could not be resolved.' ], 400 );
        return $user;
    }

    private static function summary( WP_User $user ): array {
        $passwords = self::elementize_passwords( $user->ID );
        $usage = self::usage();
        $connections = [];
        $latest = 0;

        foreach ( $passwords as $password ) {
            $uuid = (string) ( $password['uuid'] ?? '' );
            if ( '' === $uuid ) continue;
            $entry = isset( $usage[ $uuid ] ) && is_array( $usage[ $uuid ] ) ? $usage[ $uuid ] : [];
            $last_successful_call = absint( $entry['last_successful_call'] ?? 0 );
            $latest = max( $latest, $last_successful_call );
            $connections[] = [
                'uuid' => $uuid,
                'name' => sanitize_text_field( (string) ( $password['name'] ?? 'Elementize connection' ) ),
                'created' => absint( $password['created'] ?? 0 ),
                'lastSuccessfulCall' => $last_successful_call,
            ];
        }

        usort( $connections, static fn( array $a, array $b ): int => (int) $b['created'] <=> (int) $a['created'] );

        return [
            'connected' => $latest > 0,
            'activeCount' => count( $connections ),
            'lastSuccessfulCall' => $latest,
            'connections' => $connections,
        ];
    }

    private static function elementize_passwords( int $user_id ): array {
        if ( ! class_exists( 'WP_Application_Passwords' ) ) return [];
        $passwords = WP_Application_Passwords::get_user_application_passwords( $user_id );
        if ( ! is_array( $passwords ) ) return [];
        return array_values( array_filter( $passwords, [ self::class, 'is_elementize_password' ] ) );
    }

    private static function is_elementize_password( $password ): bool {
        if ( ! is_array( $password ) ) return false;
        $name = (string) ( $password['name'] ?? '' );
        return '' !== $name && str_starts_with( $name, self::NAME_PREFIX );
    }

    private static function owns_uuid( int $user_id, string $uuid ): bool {
        foreach ( self::elementize_passwords( $user_id ) as $password ) {
            $candidate = (string) ( $password['uuid'] ?? '' );
            if ( '' !== $candidate && hash_equals( $candidate, $uuid ) ) return true;
        }
        return false;
    }

    private static function usage(): array {
        $usage = get_option( self::OPT_USAGE, [] );
        return is_array( $usage ) ? $usage : [];
    }

    private static function save_usage( array $usage ): void {
        update_option( self::OPT_USAGE, $usage, false );
    }

    private static function remove_usage( array $uuids ): void {
        if ( ! $uuids ) return;
        $usage = self::usage();
        $changed = false;
        foreach ( $uuids as $uuid ) {
            if ( array_key_exists( $uuid, $usage ) ) {
                unset( $usage[ $uuid ] );
                $changed = true;
            }
        }
        if ( $changed ) self::save_usage( $usage );
    }
}

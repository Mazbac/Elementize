<?php
if ( ! defined( 'ABSPATH' ) ) exit;

require_once __DIR__ . '/elementize-aesthetic-ab-transport-hardening.inc';
require_once __DIR__ . '/elementize-aesthetic-ab-coverage-hardening.inc';
require_once __DIR__ . '/elementize-aesthetic-ab-context-hardening.inc';
require_once __DIR__ . '/elementize-aesthetic-ab-judgment-calibration.inc';

final class Elementize_Status_Version {
    private const NS = 'elementize/v1';

    public static function init(): void {
        if ( class_exists( 'Elementize_Aesthetic_AB_Transport_Hardening', false ) ) Elementize_Aesthetic_AB_Transport_Hardening::init();
        if ( class_exists( 'Elementize_Aesthetic_AB_Coverage_Hardening', false ) ) Elementize_Aesthetic_AB_Coverage_Hardening::init();
        if ( class_exists( 'Elementize_Aesthetic_AB_Context_Hardening', false ) ) Elementize_Aesthetic_AB_Context_Hardening::init();
        if ( class_exists( 'Elementize_Aesthetic_AB_Judgment_Calibration', false ) ) Elementize_Aesthetic_AB_Judgment_Calibration::init();
        // Run after all feature status decorators so older module version constants
        // cannot overwrite the actual installed plugin version.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 1000, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        if ( '/' . self::NS . '/status' !== $request->get_route() ) return $response;
        $data = $response->get_data();
        if ( ! is_array( $data ) ) return $response;
        $version = self::plugin_version();
        if ( '' !== $version ) $data['elementize_version'] = $version;
        $data['elementize_status_version_source'] = 'plugin_header';
        $data['elementize_status_version_finalized_last'] = true;
        $response->set_data( $data );
        return $response;
    }

    private static function plugin_version(): string {
        $file = dirname( __DIR__ ) . '/elementize.php';
        if ( ! is_file( $file ) ) return '';
        if ( ! function_exists( 'get_file_data' ) ) require_once ABSPATH . 'wp-admin/includes/plugin.php';
        if ( ! function_exists( 'get_file_data' ) ) return '';
        $headers = get_file_data( $file, [ 'Version' => 'Version' ], 'plugin' );
        return trim( (string) ( $headers['Version'] ?? '' ) );
    }
}

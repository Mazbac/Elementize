<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Pixfort_Transport {
    private const ESSENTIALS_PURCHASE_OPTION = 'envato_purchase_code_27889640';

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
}

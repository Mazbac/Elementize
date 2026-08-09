<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Lifecycle', false ) ) return;

final class Elementize_Admin_Display {
    private const VERSION = '0.4.2';

    public static function init(): void {
        remove_action( 'admin_menu', [ Elementize_Lifecycle::class, 'menu' ] );
        add_action( 'admin_menu', [ self::class, 'menu' ] );
    }

    public static function menu(): void {
        add_menu_page( 'Elementize', 'Elementize', 'manage_options', 'elementize', [ self::class, 'admin' ], 'dashicons-admin-tools', 58 );
    }

    public static function admin(): void {
        ob_start();
        Elementize_Lifecycle::admin();
        $html = (string) ob_get_clean();
        $html = preg_replace( '/Version 0\.4\.0/', 'Version ' . self::VERSION, $html, 1 );
        $html = str_replace( '0.4.0 marks newly created Elementize drafts as managed', '0.4.2 keeps newly created Elementize drafts managed', $html );
        echo $html; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped
    }
}

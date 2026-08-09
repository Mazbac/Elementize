<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Lifecycle', false ) ) return;

final class Elementize_Admin_Display {
    private const VERSION = '0.5.3';

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
        $html = str_replace( '0.4.0 marks newly created Elementize drafts as managed', '0.5.3 keeps newly created Elementize drafts managed', $html );
        $layout = '<div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Page layout control</h2><p>0.5.0 adds guarded draft-only page layout control for Elementize-managed pages. Site mode uses the active theme layout; standalone mode uses Elementor Canvas so the page renders without the global theme header and footer.</p></div>';
        $media = '<div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Media Library lookup</h2><p>0.5.2 adds authenticated image-only Media Library search and makes filename lookup tolerant of WordPress image-format conversion, so an uploaded JPG can still be found when WordPress stores it as AVIF or another image extension.</p></div>';
        $audit = '<div class="card" style="max-width:1000px;margin-top:20px;padding:20px;"><h2 style="margin-top:0">Read-only text-source audit</h2><p>0.5.3 adds a diagnostic endpoint that searches raw page Elementor data and referenced Elementor templates for rendered copy that is missing from the guarded editable-text surface. It does not add any new write path.</p></div>';
        $last = strrpos( $html, '</div>' );
        if ( false !== $last ) $html = substr( $html, 0, $last ) . $layout . $media . $audit . substr( $html, $last );
        echo $html; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped
    }
}

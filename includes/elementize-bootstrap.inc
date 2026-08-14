<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Bootstrap {
    private static bool $admin_loaded = false;
    private static bool $rest_loaded = false;

    public static function init(): void {
        add_action( 'rest_api_init', [ self::class, 'register_rest_routes' ], 1 );
        if ( is_admin() ) self::load_admin();
    }

    public static function activate(): void {
        self::load_admin();
        Elementize_Onboarding::activate();
    }

    public static function register_rest_routes(): void {
        self::load_rest();
        Elementize_Core::routes();
        Elementize_Pixfort_Icons::routes();
        Elementize_Content::routes();
        Elementize_Media_Library::routes();
        Elementize_Media_Import::routes();
    }

    private static function load_core(): void {
        if ( class_exists( Elementize_Core::class, false ) ) return;
        require_once __DIR__ . '/elementize-core.inc';
    }

    private static function load_admin(): void {
        if ( self::$admin_loaded ) return;
        self::load_core();
        require_once __DIR__ . '/elementize-onboarding.inc';
        Elementize_Onboarding::init();
        self::$admin_loaded = true;
    }

    private static function load_rest(): void {
        if ( self::$rest_loaded ) return;
        self::load_core();
        require_once __DIR__ . '/elementize-pixfort-theme-tokens.inc';
        require_once __DIR__ . '/elementize-pixfort-icons.inc';
        require_once __DIR__ . '/elementize-content.inc';
        require_once __DIR__ . '/elementize-media-library.inc';
        require_once __DIR__ . '/elementize-media-import.inc';
        self::$rest_loaded = true;
    }
}

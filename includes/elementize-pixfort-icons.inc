<?php
if ( ! defined( 'ABSPATH' ) ) exit;

final class Elementize_Pixfort_Icons {
    private const STYLES = [ 'Line', 'Duotone', 'Solid' ];
    private const MAX_PER_PAGE = 100;
    private const MAX_SCAN_FILES = 1500;
    private const MAX_SCAN_BYTES = 41943040;
    private const MAX_FILE_BYTES = 4194304;
    private const CACHE_TTL = 43200;
    private const CACHE_VERSION = '1';
    private static ?array $memory_index = null;
    private static ?array $memory_lookup = null;

    public static function routes(): void {
        register_rest_route( Elementize_Core::NS, '/pixfort/icons', [
            'methods' => WP_REST_Server::READABLE,
            'callback' => [ self::class, 'search' ],
            'permission_callback' => [ Elementize_Core::class, 'can_access' ],
            'args' => [
                'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
                'style' => [ 'type' => 'string', 'default' => 'all', 'sanitize_callback' => 'sanitize_key' ],
                'page' => [ 'type' => 'integer', 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
                'per_page' => [ 'type' => 'integer', 'default' => 30, 'minimum' => 1, 'maximum' => self::MAX_PER_PAGE, 'sanitize_callback' => 'absint' ],
            ],
        ] );
    }

    public static function search( WP_REST_Request $request ) {
        $index = self::index();
        if ( is_wp_error( $index ) ) return $index;

        $style_param = strtolower( trim( (string) $request->get_param( 'style' ) ) );
        $styles = self::styles_for_filter( $style_param );
        if ( is_wp_error( $styles ) ) return $styles;
        $query = strtolower( trim( (string) $request->get_param( 'q' ) ) );
        $page = max( 1, absint( $request->get_param( 'page' ) ) );
        $per_page = min( self::MAX_PER_PAGE, max( 1, absint( $request->get_param( 'per_page' ) ) ) );
        $offset = ( $page - 1 ) * $per_page;

        $items = [];
        $matched_bases = 0;
        $flat_index = 0;
        foreach ( $index['bases'] as $base ) {
            $name = substr( $base, strlen( 'pixfort-icon-' ) );
            $label = ucwords( str_replace( [ '-', '_' ], ' ', $name ) );
            if ( '' !== $query && false === strpos( strtolower( $base . ' ' . $label ), $query ) ) continue;
            $matched_bases++;
            foreach ( $styles as $style ) {
                if ( $flat_index >= $offset && count( $items ) < $per_page ) {
                    $items[] = [
                        'value' => $style . '/' . $base,
                        'style' => $style,
                        'icon_id' => $base,
                        'name' => $name,
                        'label' => $label,
                    ];
                }
                $flat_index++;
            }
        }

        $total = $matched_bases * count( $styles );
        return new WP_REST_Response( [
            'q' => $query,
            'style' => $style_param,
            'styles' => self::STYLES,
            'page' => $page,
            'per_page' => $per_page,
            'total' => $total,
            'total_pages' => $total > 0 ? (int) ceil( $total / $per_page ) : 0,
            'library_indexed' => ! empty( $index['bases'] ),
            'base_icon_count' => count( $index['bases'] ),
            'discovery_source' => 'installed_pixfort_and_theme_assets',
            'scanned_files' => $index['scanned_files'],
            'scan_truncated' => $index['scan_truncated'],
            'items' => $items,
        ], 200 );
    }

    public static function validate_value( string $value ) {
        $value = trim( $value );
        if ( ! preg_match( '#^(Line|Duotone|Solid)/(pixfort-icon-[A-Za-z0-9_-]+)$#', $value, $matches ) ) return false;
        $index = self::index();
        if ( is_wp_error( $index ) ) return $index;
        if ( empty( $index['bases'] ) ) {
            return new WP_Error( 'elementize_pixfort_icon_library_unavailable', 'Elementize could not index the installed Pixfort icon library. Choose the icon in Elementor instead.', [ 'status' => 503 ] );
        }
        if ( null === self::$memory_lookup ) self::$memory_lookup = array_fill_keys( $index['bases'], true );
        return isset( self::$memory_lookup[ $matches[2] ] );
    }

    public static function status_capabilities(): array {
        return [
            'pixfort_icon_library_search' => true,
            'pixfort_icon_styles' => self::STYLES,
            'pixfort_icon_values_verified_against_installed_assets' => true,
        ];
    }

    private static function styles_for_filter( string $style ) {
        if ( '' === $style || 'all' === $style ) return self::STYLES;
        foreach ( self::STYLES as $candidate ) if ( strtolower( $candidate ) === $style ) return [ $candidate ];
        return new WP_Error( 'elementize_pixfort_icon_style_invalid', 'style must be all, line, duotone, or solid.', [ 'status' => 400 ] );
    }

    private static function index() {
        if ( null !== self::$memory_index ) return self::$memory_index;
        $key = 'elementize_pixfort_icons_' . md5( self::fingerprint() );
        $cached = get_transient( $key );
        if ( is_array( $cached ) && isset( $cached['bases'], $cached['scanned_files'], $cached['scan_truncated'] ) ) {
            self::$memory_index = $cached;
            return $cached;
        }

        $roots = self::roots();
        if ( ! $roots ) return new WP_Error( 'elementize_pixfort_icon_library_unavailable', 'No Pixfort/theme asset roots were available for icon discovery.', [ 'status' => 503 ] );

        $bases = [];
        $scanned_files = 0;
        $scanned_bytes = 0;
        $truncated = false;
        foreach ( $roots as $root ) {
            $result = self::scan_root( $root, $bases, $scanned_files, $scanned_bytes );
            if ( $result ) { $truncated = true; break; }
        }
        $bases = array_keys( $bases );
        sort( $bases, SORT_STRING );
        $index = [
            'bases' => $bases,
            'scanned_files' => $scanned_files,
            'scan_truncated' => $truncated,
        ];
        set_transient( $key, $index, self::CACHE_TTL );
        self::$memory_index = $index;
        return $index;
    }

    private static function fingerprint(): string {
        $pixfort = Elementize_Core::pixfort_info();
        $theme = wp_get_theme();
        return implode( '|', [
            self::CACHE_VERSION,
            defined( 'ELEMENTIZE_VERSION' ) ? ELEMENTIZE_VERSION : '',
            (string) ( $pixfort['version'] ?? '' ),
            (string) $theme->get_stylesheet(),
            (string) $theme->get( 'Version' ),
        ] );
    }

    private static function roots(): array {
        $roots = [];
        if ( ! function_exists( 'get_plugins' ) ) require_once ABSPATH . 'wp-admin/includes/plugin.php';
        $active = (array) get_option( 'active_plugins', [] );
        if ( is_multisite() ) $active = array_merge( $active, array_keys( (array) get_site_option( 'active_sitewide_plugins', [] ) ) );
        $plugins = get_plugins();
        foreach ( array_unique( $active ) as $file ) {
            $data = $plugins[ $file ] ?? [];
            $name = (string) ( $data['Name'] ?? '' );
            if ( false === stripos( $file, 'pixfort-core' ) && false === stripos( $name, 'pixfort core' ) ) continue;
            $root = realpath( WP_PLUGIN_DIR . '/' . dirname( $file ) );
            if ( is_string( $root ) && is_dir( $root ) ) $roots[] = $root;
            break;
        }
        foreach ( [ get_stylesheet_directory(), get_template_directory() ] as $theme_root ) {
            $root = realpath( $theme_root );
            if ( is_string( $root ) && is_dir( $root ) ) $roots[] = $root;
        }
        return array_values( array_unique( $roots ) );
    }

    private static function scan_root( string $root, array &$bases, int &$scanned_files, int &$scanned_bytes ): bool {
        try {
            $iterator = new RecursiveIteratorIterator(
                new RecursiveDirectoryIterator( $root, FilesystemIterator::SKIP_DOTS ),
                RecursiveIteratorIterator::LEAVES_ONLY
            );
            foreach ( $iterator as $file ) {
                if ( ! $file instanceof SplFileInfo || ! $file->isFile() || $file->isLink() ) continue;
                if ( $scanned_files >= self::MAX_SCAN_FILES || $scanned_bytes >= self::MAX_SCAN_BYTES ) return true;
                $extension = strtolower( $file->getExtension() );
                if ( ! in_array( $extension, [ 'css', 'js', 'json', 'php' ], true ) ) continue;
                $path = $file->getPathname();
                $relative_hint = strtolower( str_replace( '\\', '/', $path ) );
                if ( false === strpos( strtolower( $root ), 'pixfort' ) && false === strpos( $relative_hint, 'icon' ) && false === strpos( $relative_hint, 'pixfort' ) ) continue;
                $size = $file->getSize();
                if ( $size < 1 || $size > self::MAX_FILE_BYTES || ( $scanned_bytes + $size ) > self::MAX_SCAN_BYTES ) continue;
                $content = @file_get_contents( $path );
                if ( ! is_string( $content ) || '' === $content ) continue;
                $scanned_files++;
                $scanned_bytes += strlen( $content );
                if ( preg_match_all( '#pixfort-icon-[A-Za-z0-9_-]+#', $content, $matches ) ) {
                    foreach ( $matches[0] as $base ) $bases[ $base ] = true;
                }
            }
        } catch ( Throwable $e ) {
            return false;
        }
        return false;
    }
}

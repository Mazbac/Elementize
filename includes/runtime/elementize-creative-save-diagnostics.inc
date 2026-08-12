<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Read-only diagnostics for Creative transactions rejected because Elementor's
 * document/save/data filter changed the already validated transaction.
 *
 * The Creative guard remains authoritative and still rolls back. This adapter
 * only adds a bounded path-level diff to that specific error response so a
 * legitimate Elementor/Pixfort normalization can be distinguished from an
 * unsafe third-party mutation before any guard is relaxed.
 */
final class Elementize_Creative_Save_Diagnostics {
    private const MAX_DIFFS = 48;
    private const MAX_DEPTH = 48;
    private const MAX_VALUE_CHARS = 220;

    private static int $page_id = 0;
    private static ?array $baseline = null;
    private static ?array $report = null;

    public static function init(): void {
        add_filter( 'rest_request_before_callbacks', [ self::class, 'prepare' ], 5, 3 );
        add_filter( 'elementor/document/save/data', [ self::class, 'capture_filtered_data' ], PHP_INT_MAX - 1, 2 );
        add_filter( 'rest_request_after_callbacks', [ self::class, 'augment_error' ], 5, 3 );
    }

    public static function prepare( $response, array $handler, WP_REST_Request $request ) {
        self::reset();
        if ( 'POST' !== strtoupper( $request->get_method() ) ) return $response;
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/(\\d+)/creative$#', (string) $request->get_route(), $matches ) ) return $response;

        $page_id = absint( $matches[1] );
        if ( ! $page_id ) return $response;
        $loaded = Elementize_Core::elements( $page_id );
        if ( is_wp_error( $loaded ) || ! is_array( $loaded['elements'] ?? null ) ) return $response;

        self::$page_id = $page_id;
        self::$baseline = $loaded['elements'];
        return $response;
    }

    public static function capture_filtered_data( $data, $document ) {
        if ( ! self::$page_id || ! is_array( self::$baseline ) || ! is_array( $data ) || ! is_array( $data['elements'] ?? null ) ) return $data;
        if ( ! is_object( $document ) || ! method_exists( $document, 'get_main_id' ) ) return $data;
        try { $saving_id = absint( $document->get_main_id() ); } catch ( Throwable $ignored ) { $saving_id = 0; }
        if ( self::$page_id !== $saving_id ) return $data;

        $diffs = [];
        $truncated = false;
        self::diff( self::$baseline, $data['elements'], [ 'elements' ], $diffs, $truncated, 0 );
        self::$report = [
            'diagnostic_version' => '1',
            'source' => 'elementor_document_save_data_after_other_filters',
            'page_id' => self::$page_id,
            'diff_count_returned' => count( $diffs ),
            'diffs_truncated' => $truncated,
            'diffs' => $diffs,
            'read_only_diagnostic' => true,
        ];
        return $data;
    }

    public static function augment_error( $response, array $handler, WP_REST_Request $request ) {
        if ( ! self::$page_id || ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/\\d+/creative$#', (string) $request->get_route() ) ) {
            self::reset();
            return $response;
        }

        if ( is_wp_error( $response ) && 'elementize_creative_save_input_changed' === $response->get_error_code() && is_array( self::$report ) ) {
            $data = $response->get_error_data();
            if ( ! is_array( $data ) ) $data = [];
            $data['save_filter_diagnostics'] = self::$report;
            $response->add_data( $data, $response->get_error_code() );
        }
        self::reset();
        return $response;
    }

    private static function diff( $before, $after, array $path, array &$diffs, bool &$truncated, int $depth ): void {
        if ( count( $diffs ) >= self::MAX_DIFFS ) { $truncated = true; return; }
        if ( $depth > self::MAX_DEPTH ) { $truncated = true; return; }
        if ( gettype( $before ) !== gettype( $after ) ) {
            self::add_diff( $diffs, $truncated, $path, 'type_changed', $before, $after );
            return;
        }
        if ( ! is_array( $before ) ) {
            if ( $before !== $after ) self::add_diff( $diffs, $truncated, $path, 'value_changed', $before, $after );
            return;
        }

        $keys = array_values( array_unique( array_merge( array_keys( $before ), array_keys( $after ) ), SORT_REGULAR ) );
        foreach ( $keys as $key ) {
            if ( count( $diffs ) >= self::MAX_DIFFS ) { $truncated = true; return; }
            $child_path = array_merge( $path, [ is_int( $key ) ? $key : (string) $key ] );
            $has_before = array_key_exists( $key, $before );
            $has_after = array_key_exists( $key, $after );
            if ( ! $has_before ) {
                self::add_diff( $diffs, $truncated, $child_path, 'added', null, $after[ $key ] );
                continue;
            }
            if ( ! $has_after ) {
                self::add_diff( $diffs, $truncated, $child_path, 'removed', $before[ $key ], null );
                continue;
            }
            self::diff( $before[ $key ], $after[ $key ], $child_path, $diffs, $truncated, $depth + 1 );
        }
    }

    private static function add_diff( array &$diffs, bool &$truncated, array $path, string $kind, $before, $after ): void {
        if ( count( $diffs ) >= self::MAX_DIFFS ) { $truncated = true; return; }
        $diffs[] = [
            'path' => array_values( $path ),
            'kind' => $kind,
            'before' => self::summarize( $before ),
            'after' => self::summarize( $after ),
        ];
    }

    private static function summarize( $value ) {
        if ( is_array( $value ) ) return [ 'type' => 'array', 'count' => count( $value ) ];
        if ( is_object( $value ) ) return [ 'type' => 'object', 'class' => get_class( $value ) ];
        if ( is_string( $value ) ) {
            if ( strlen( $value ) > self::MAX_VALUE_CHARS ) return substr( $value, 0, self::MAX_VALUE_CHARS ) . '…';
            return $value;
        }
        if ( is_int( $value ) || is_float( $value ) || is_bool( $value ) || null === $value ) return $value;
        return gettype( $value );
    }

    private static function reset(): void {
        self::$page_id = 0;
        self::$baseline = null;
        self::$report = null;
    }
}

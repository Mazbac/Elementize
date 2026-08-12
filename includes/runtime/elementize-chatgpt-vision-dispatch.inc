<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Dispatch bridge for native ChatGPT visual QA.
 *
 * rest_request_before_callbacks is a validation hook: a normal response returned
 * there is still overwritten when WordPress executes the matched endpoint callback.
 * rest_dispatch_request is the supported callback-override hook, so analyze=true
 * Visual QA requests are routed through the native ChatGPT handoff here.
 */
final class Elementize_ChatGPT_Vision_Dispatch {
    public static function init(): void {
        if ( class_exists( 'Elementize_ChatGPT_Vision' ) ) {
            remove_filter( 'rest_request_before_callbacks', [ Elementize_ChatGPT_Vision::class, 'before_callbacks' ], 10 );
        }
        add_filter( 'rest_dispatch_request', [ self::class, 'dispatch' ], 10, 4 );
    }

    public static function dispatch( $dispatch_result, WP_REST_Request $request, string $route, array $handler ) {
        if ( null !== $dispatch_result ) return $dispatch_result;
        if ( ! class_exists( 'Elementize_ChatGPT_Vision' ) ) return $dispatch_result;

        $request_route = (string) $request->get_route();
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/\\d+/visual-qa$#', $request_route ) ) {
            return $dispatch_result;
        }

        $raw_analyze = $request->get_param( 'analyze' );
        $analyze = null === $raw_analyze ? true : (bool) filter_var( $raw_analyze, FILTER_VALIDATE_BOOLEAN );
        if ( ! $analyze ) return $dispatch_result;

        return Elementize_ChatGPT_Vision::before_callbacks( null, $handler, $request );
    }
}

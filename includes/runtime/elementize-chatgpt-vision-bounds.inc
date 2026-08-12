<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Content-bounds adapter for native ChatGPT visual QA.
 *
 * Chromium intentionally captures into a tall desktop viewport so long pages are
 * not truncated. The base Visual QA collector already removes trailing uniform
 * background before returning screenshot bytes, but the native ChatGPT handoff
 * reads the PNG earlier and therefore needs the same bounded crop before it
 * builds openaiFileResponse.
 *
 * This adapter is read-only. It only rewrites the temporary PNG artifact and
 * rebuilds the native handoff response from that bounded artifact.
 */
final class Elementize_ChatGPT_Vision_Bounds {
    private const VERSION = '1';
    private const MIN_CROP_SAVING = 300;

    public static function init(): void {
        add_filter( 'rest_dispatch_request', [ self::class, 'bound_native_handoff' ], 20, 4 );
    }

    public static function bound_native_handoff( $dispatch_result, WP_REST_Request $request, string $route, array $handler ) {
        if ( ! $dispatch_result instanceof WP_REST_Response ) return $dispatch_result;
        if ( ! class_exists( 'Elementize_ChatGPT_Vision' ) ) return $dispatch_result;

        $request_route = (string) $request->get_route();
        if ( ! preg_match( '#^/' . preg_quote( Elementize_Core::NS, '#' ) . '/pages/\\d+/visual-qa$#', $request_route ) ) return $dispatch_result;

        $raw_analyze = $request->get_param( 'analyze' );
        $analyze = null === $raw_analyze ? true : (bool) filter_var( $raw_analyze, FILTER_VALIDATE_BOOLEAN );
        if ( ! $analyze ) return $dispatch_result;

        $data = $dispatch_result->get_data();
        if ( ! is_array( $data ) ) return $dispatch_result;
        if ( 'chatgpt_native_vision' !== (string) ( $data['provider'] ?? '' ) ) return $dispatch_result;
        if ( 'complete' !== (string) ( $data['capture_state'] ?? '' ) || empty( $data['chatgpt_vision_handoff_ready'] ) ) return $dispatch_result;

        $job_id = (string) ( $data['capture_job_id'] ?? '' );
        if ( ! preg_match( '/^[a-f0-9]{24}$/', $job_id ) ) return $dispatch_result;

        $base = trailingslashit( function_exists( 'get_temp_dir' ) ? get_temp_dir() : sys_get_temp_dir() );
        $screenshot = $base . 'elementize-visual-' . $job_id . '.png';
        $bounds = self::trim_trailing_background( $screenshot );

        if ( empty( $bounds['available'] ) ) {
            $data['chatgpt_vision_bounds_version'] = self::VERSION;
            $data['capture_bounds_ready'] = false;
            $data['capture_bounds_reason'] = 'Temporary rendered screenshot was unavailable for content-bounds normalization.';
            $dispatch_result->set_data( $data );
            return $dispatch_result;
        }

        if ( ! empty( $bounds['trimmed'] ) ) {
            // Rebuild openaiFileResponse from the now-bounded PNG. Calling the
            // adapter directly avoids a second REST dispatch or browser capture.
            $rebuilt = Elementize_ChatGPT_Vision::before_callbacks( null, $handler, $request );
            if ( $rebuilt instanceof WP_REST_Response ) {
                $dispatch_result = $rebuilt;
                $data = $dispatch_result->get_data();
                if ( ! is_array( $data ) ) return $dispatch_result;
            }
        }

        $data['chatgpt_vision_bounds_version'] = self::VERSION;
        $data['capture_bounds_ready'] = true;
        $data['capture_bounds_trimmed'] = (bool) ( $bounds['trimmed'] ?? false );
        $data['capture_original_height'] = $bounds['original_height'] ?? null;
        $data['capture_bounded_height'] = $bounds['bounded_height'] ?? null;
        $data['capture_mode'] = 'content_bounded_tall_desktop';
        $dispatch_result->set_data( $data );
        return $dispatch_result;
    }

    private static function trim_trailing_background( string $path ): array {
        $result = [
            'available' => false,
            'trimmed' => false,
            'original_height' => null,
            'bounded_height' => null,
        ];

        if ( '' === $path || ! is_file( $path ) ) return $result;
        if ( ! function_exists( 'imagecreatefrompng' ) || ! function_exists( 'imagecrop' ) || ! function_exists( 'imagepng' ) ) return $result;

        $im = @imagecreatefrompng( $path );
        if ( ! $im ) return $result;

        $w = imagesx( $im );
        $h = imagesy( $im );
        $result['available'] = true;
        $result['original_height'] = $h;
        $result['bounded_height'] = $h;

        if ( $w < 100 || $h < 1200 ) {
            imagedestroy( $im );
            return $result;
        }

        $sample_x = [ (int) ( $w * .08 ), (int) ( $w * .25 ), (int) ( $w * .5 ), (int) ( $w * .75 ), (int) ( $w * .92 ) ];
        $bg = [ 0, 0, 0 ];
        foreach ( $sample_x as $x ) {
            $rgb = imagecolorat( $im, $x, $h - 6 );
            $bg[0] += ( $rgb >> 16 ) & 255;
            $bg[1] += ( $rgb >> 8 ) & 255;
            $bg[2] += $rgb & 255;
        }
        $bg = [
            (int) round( $bg[0] / count( $sample_x ) ),
            (int) round( $bg[1] / count( $sample_x ) ),
            (int) round( $bg[2] / count( $sample_x ) ),
        ];

        $last = $h - 1;
        for ( $y = $h - 20; $y > 400; $y -= 4 ) {
            $different = 0;
            for ( $i = 1; $i <= 20; $i++ ) {
                $x = (int) round( $w * $i / 21 );
                $rgb = imagecolorat( $im, $x, $y );
                $r = ( $rgb >> 16 ) & 255;
                $g = ( $rgb >> 8 ) & 255;
                $b = $rgb & 255;
                if ( abs( $r - $bg[0] ) + abs( $g - $bg[1] ) + abs( $b - $bg[2] ) > 54 ) $different++;
            }
            if ( $different >= 2 ) {
                $last = min( $h, $y + 180 );
                break;
            }
        }

        if ( $last < $h - self::MIN_CROP_SAVING ) {
            $bounded_height = max( 600, $last );
            $crop = @imagecrop( $im, [ 'x' => 0, 'y' => 0, 'width' => $w, 'height' => $bounded_height ] );
            if ( $crop ) {
                if ( @imagepng( $crop, $path, 6 ) ) {
                    $result['trimmed'] = true;
                    $result['bounded_height'] = $bounded_height;
                }
                imagedestroy( $crop );
            }
        }

        imagedestroy( $im );
        return $result;
    }
}

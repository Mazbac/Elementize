<?php
if ( ! defined( 'ABSPATH' ) ) exit;
if ( ! class_exists( 'Elementize_Aesthetic_AB_Independent_Discrimination', false ) ) return;

/**
 * Multi-sample consensus for ambiguous anchored visual discrimination.
 *
 * One local vision sample can move a near-floor comparison by a point even when the
 * exact page state and candidate set are unchanged. This layer runs only after the
 * anchored discriminator has applied and remains ambiguous: top score below 6/10,
 * or a score gap of at most one point. The original anchored result is sample #1;
 * two extra anonymous one-image samples are collected per candidate. PHP takes the
 * median of every 1-5 fit axis and the median mismatch severity, then derives the
 * final score deterministically. A deterministic thumbnail-intensity proxy is used
 * instead of trusting one model intensity estimate. No pairwise comparison occurs.
 *
 * Read-only. No Elementor/page mutation capability exists in this class.
 */
final class Elementize_Aesthetic_AB_Multisample_Consensus {
    private const VERSION = '0.27.9-m1';
    private const NS = 'elementize/v1';
    private const MODEL_CONST = 'ELEMENTIZE_LOCAL_VISION_MODEL';
    private const OLLAMA_URL_CONST = 'ELEMENTIZE_OLLAMA_URL';
    private const DEFAULT_MODEL = 'gemma3:4b';
    private const DEFAULT_OLLAMA_URL = 'http://127.0.0.1:11434';
    private const VISION_TIMEOUT_SECONDS = 150;
    private const MAX_PROMPT_CONTEXT_BYTES = 18000;
    private const MAX_IMAGE_BYTES = 2097152;
    private const SELECTION_FLOOR = 6;
    private const AMBIGUOUS_MAX_GAP = 1;
    private const GRID = 32;

    public static function init(): void {
        // Discriminator=605, deterministic plateau=607, calibration=610.
        add_filter( 'rest_request_after_callbacks', [ self::class, 'after_rest' ], 609, 3 );
    }

    public static function after_rest( $response, $handler, WP_REST_Request $request ) {
        if ( is_wp_error( $response ) || ! $response instanceof WP_REST_Response ) return $response;
        $route = $request->get_route();
        if ( '/' . self::NS . '/status' === $route ) return self::status( $response );
        if ( '/' . self::NS . '/pixfort/visual-probe' !== $route || 'POST' !== strtoupper( $request->get_method() ) || $response->get_status() >= 400 ) return $response;
        if ( ! self::truthy( $request->get_param( 'aesthetic_compare' ) ) ) return $response;

        $data = $response->get_data();
        if ( ! is_array( $data ) || ! is_array( $data['aesthetic_comparison'] ?? null ) ) return $response;
        $comparison = $data['aesthetic_comparison'];
        $disc = is_array( $comparison['independent_discrimination'] ?? null ) ? $comparison['independent_discrimination'] : [];
        if ( empty( $disc['applied'] ) ) return $response;

        $top = absint( $disc['top_score'] ?? 0 );
        $runner = absint( $disc['runner_up_score'] ?? 0 );
        $gap = abs( $top - $runner );
        if ( $top >= self::SELECTION_FLOOR && $gap > self::AMBIGUOUS_MAX_GAP ) return $response;

        $diag = [
            'version' => self::VERSION,
            'attempted' => true,
            'applied' => false,
            'trigger' => $top < self::SELECTION_FLOOR ? 'anchored_top_below_selection_floor' : 'anchored_score_gap_at_most_' . self::AMBIGUOUS_MAX_GAP,
            'initial_top_score' => $top,
            'initial_runner_up_score' => $runner,
            'initial_score_gap' => $gap,
            'samples_per_candidate' => 3,
            'additional_model_samples_per_candidate' => 2,
            'median_axis_aggregation' => true,
            'deterministic_thumbnail_intensity' => true,
            'slot_order_blind' => true,
            'template_identity_hidden_from_model' => true,
            'one_image_per_model_request' => true,
            'pairwise_comparison' => false,
            'read_only' => true,
            'writes_performed' => false,
            'automatic_write_allowed' => false,
            'action_slot_cost' => 0,
        ];

        $result = self::consensus( $data, $request, $disc );
        if ( is_wp_error( $result ) ) {
            $diag['reason'] = self::clip( $result->get_error_message(), 500 );
            $comparison['multisample_consensus'] = $diag;
            $data['aesthetic_comparison'] = $comparison;
            $response->set_data( $data );
            return $response;
        }

        $diag = array_merge( $diag, $result['diagnostics'], [ 'applied' => true ] );
        $comparison['candidate_judgments'] = $result['candidate_judgments'];
        $comparison['winner_slot'] = $result['winner_slot'];
        $comparison['winner_template_id'] = $result['winner_template_id'];
        $comparison['confidence'] = $result['confidence'];
        $comparison['winner_margin'] = $result['winner_margin'];
        $comparison['professional_rationale'] = $result['professional_rationale'];
        $comparison['comparison_method'] = 'slot_blind_independent_candidate_scoring_with_anchored_multisample_consensus';
        $comparison['usable_for_selection'] = 'none' !== $result['winner_slot'] && in_array( $result['confidence'], [ 'medium', 'high' ], true );
        $comparison['selection_policy'] = $comparison['usable_for_selection']
            ? 'The median anchored consensus winner may be used only as a read-only recommendation; deterministic calibration still runs afterward and may block it.'
            : 'Multi-sample anchored consensus remains advisory. Do not select automatically unless later deterministic calibration considers it usable.';
        $comparison['multisample_consensus'] = $diag;
        $data['aesthetic_comparison'] = $comparison;
        $response->set_data( $data );
        return $response;
    }

    private static function consensus( array $data, WP_REST_Request $request, array $disc ) {
        $page_id = absint( $request->get_param( 'context_page_id' ) );
        $marker = strtoupper( trim( (string) $request->get_param( 'target_marker' ) ) );
        if ( $page_id < 1 || ! preg_match( '/^S\d{1,2}$/', $marker ) ) return new WP_Error( 'elementize_multisample_context_invalid', 'Multi-sample consensus requires a valid context_page_id and target_marker.' );

        $state_hash = self::page_state_hash( $page_id );
        if ( '' === $state_hash ) return new WP_Error( 'elementize_multisample_hash_unavailable', 'The current Elementor page state could not be fingerprinted.' );
        $snapshot = get_transient( self::cache_key( $page_id, $state_hash ) );
        if ( ! is_array( $snapshot ) || ! hash_equals( $state_hash, (string) ( $snapshot['page_state_hash'] ?? '' ) ) ) return new WP_Error( 'elementize_multisample_context_stale', 'The exact-state grounded context is unavailable or stale.' );
        $context = self::target_context( $snapshot, $marker );
        if ( is_wp_error( $context ) ) return $context;

        $proof = self::extract_candidates( $data );
        if ( is_wp_error( $proof ) ) return $proof;
        $normalized = self::normalize_images( $proof );
        if ( is_wp_error( $normalized ) ) return $normalized;

        $sample1 = [];
        foreach ( array_values( array_filter( (array) ( $disc['candidate_results'] ?? [] ), 'is_array' ) ) as $row ) {
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) );
            if ( '' !== $slot ) $sample1[ $slot ] = $row;
        }
        if ( count( $sample1 ) !== count( $normalized['candidates'] ) ) return new WP_Error( 'elementize_multisample_sample1_incomplete', 'Anchored sample #1 does not exactly cover the candidate set.' );

        $target_intensity = self::clamp_score( $context['target']['visual_intensity'] ?? 5 );
        $rows = [];
        $candidate_diag = [];
        foreach ( $normalized['candidates'] as $candidate ) {
            $slot = (string) $candidate['slot'];
            if ( ! isset( $sample1[ $slot ] ) ) return new WP_Error( 'elementize_multisample_slot_missing', 'A candidate slot is missing from anchored sample #1.' );

            $samples = [ self::normalize_sample1( $sample1[ $slot ] ) ];
            for ( $i = 0; $i < 2; $i++ ) {
                $sample = self::score_dimensions( (string) $candidate['thumbnail_base64'], $context );
                if ( is_wp_error( $sample ) ) return new WP_Error( 'elementize_multisample_candidate_failed', 'Additional anonymous sample failed for candidate ' . $slot . ': ' . $sample->get_error_message() );
                $samples[] = self::normalize_model_sample( $sample );
            }

            $dimensions = [];
            foreach ( [ 'composition', 'whitespace', 'density', 'hierarchy', 'design_dna', 'transition', 'conversion', 'restraint' ] as $axis ) {
                $dimensions[ $axis ] = self::median_int( array_map( static fn ( array $s ): int => (int) $s['dimensions'][ $axis ], $samples ) );
            }
            $model_intensity_median = self::median_int( array_map( static fn ( array $s ): int => (int) $s['estimated_visual_intensity'], $samples ) );
            $mismatch = self::median_mismatch( array_map( static fn ( array $s ): string => (string) $s['mismatch_severity'], $samples ) );
            $confidence_ceiling = self::lowest_confidence( array_map( static fn ( array $s ): string => (string) $s['confidence'], $samples ) );

            $measurement = self::measure_image( (string) $candidate['thumbnail_base64'] );
            if ( is_wp_error( $measurement ) ) return $measurement;
            $measured_intensity = (int) $measurement['intensity_proxy'];
            $derived = self::derive_score( $dimensions, $measured_intensity, $target_intensity, $mismatch );
            $rhythm_axis = (int) round( ( $dimensions['whitespace'] + $dimensions['density'] + $dimensions['transition'] ) / 3 );
            $rows[] = [
                'slot' => $slot,
                'template_id' => (string) $candidate['template_id'],
                'score' => $derived['score'],
                'overall_fit' => self::overall_fit_from_score( $derived['score'] ),
                'design_dna_fit' => self::fit_from_axis( $dimensions['design_dna'] ),
                'rhythm_fit' => self::fit_from_axis( $rhythm_axis ),
                'hierarchy_fit' => self::fit_from_axis( $dimensions['hierarchy'] ),
                'conversion_fit' => self::fit_from_axis( $dimensions['conversion'] ),
                'strength' => 'Median of three anonymous anchored visual-fit samples.',
                'risk' => $derived['score'] < self::SELECTION_FLOOR ? 'Median anchored score remains below the absolute selection floor.' : '',
            ];
            $candidate_diag[] = [
                'slot' => $slot,
                'template_id' => (string) $candidate['template_id'],
                'sample_scores' => array_map( static fn ( array $s ): array => [ 'dimension_sum' => array_sum( $s['dimensions'] ), 'estimated_visual_intensity' => $s['estimated_visual_intensity'], 'mismatch_severity' => $s['mismatch_severity'], 'confidence' => $s['confidence'] ], $samples ),
                'median_dimension_scores' => $dimensions,
                'median_dimension_sum' => array_sum( $dimensions ),
                'model_visual_intensity_median' => $model_intensity_median,
                'measured_visual_intensity_proxy' => $measured_intensity,
                'target_visual_intensity' => $target_intensity,
                'median_mismatch_severity' => $mismatch,
                'confidence_ceiling' => $confidence_ceiling,
                'raw_axis_score' => $derived['raw_axis_score'],
                'intensity_penalty' => $derived['intensity_penalty'],
                'mismatch_penalty' => $derived['mismatch_penalty'],
                'derived_score' => $derived['score'],
                'thumbnail_signature' => $measurement,
            ];
        }

        $ranked = $rows;
        usort( $ranked, static function ( array $a, array $b ): int {
            if ( (int) $a['score'] === (int) $b['score'] ) return strcmp( (string) $a['template_id'], (string) $b['template_id'] );
            return (int) $b['score'] <=> (int) $a['score'];
        } );
        $top = $ranked[0];
        $runner = $ranked[1];
        $top_score = (int) $top['score'];
        $runner_score = (int) $runner['score'];
        $gap = max( 0, $top_score - $runner_score );
        $tie = $top_score === $runner_score;
        $winner_slot = $tie ? 'none' : (string) $top['slot'];
        $winner_template_id = $tie ? null : (string) $top['template_id'];

        $confidence = 'low';
        if ( ! $tie && $top_score >= 6 && $gap >= 1 ) $confidence = 'medium';
        if ( ! $tie && $top_score >= 8 && $gap >= 3 ) $confidence = 'high';
        if ( ! $tie ) {
            foreach ( $candidate_diag as $d ) if ( (string) $d['template_id'] === (string) $top['template_id'] ) $confidence = self::lower_confidence( $confidence, (string) $d['confidence_ceiling'] );
        }
        $margin = ( ! $tie && $gap >= 3 ) ? 'clear' : 'slight';
        $rationale = $tie
            ? 'Three-sample anonymous anchored consensus still produced a top-score tie, so no visual winner is retained.'
            : 'Three-sample anonymous anchored consensus ranked ' . (string) $top['template_id'] . ' at ' . $top_score . '/10 versus ' . (string) $runner['template_id'] . ' at ' . $runner_score . '/10. Every fit axis was aggregated by median and image intensity was measured deterministically.';

        return [
            'candidate_judgments' => $rows,
            'winner_slot' => $winner_slot,
            'winner_template_id' => $winner_template_id,
            'confidence' => $confidence,
            'winner_margin' => $margin,
            'professional_rationale' => self::clip( $rationale, 900 ),
            'diagnostics' => [
                'candidate_count' => count( $rows ),
                'candidate_results' => $candidate_diag,
                'candidate_image_normalization' => $normalized['diagnostics'],
                'target_visual_intensity' => $target_intensity,
                'top_score' => $top_score,
                'runner_up_score' => $runner_score,
                'score_gap' => $gap,
                'tied_top_score' => $tie,
                'effective_winner_template_id' => $winner_template_id,
                'page_state_hash' => $state_hash,
            ],
        ];
    }

    private static function normalize_sample1( array $row ): array {
        $d = is_array( $row['dimension_scores'] ?? null ) ? $row['dimension_scores'] : [];
        return [
            'dimensions' => [
                'composition' => self::axis( $d['composition'] ?? 1 ), 'whitespace' => self::axis( $d['whitespace'] ?? 1 ),
                'density' => self::axis( $d['density'] ?? 1 ), 'hierarchy' => self::axis( $d['hierarchy'] ?? 1 ),
                'design_dna' => self::axis( $d['design_dna'] ?? 1 ), 'transition' => self::axis( $d['transition'] ?? 1 ),
                'conversion' => self::axis( $d['conversion'] ?? 1 ), 'restraint' => self::axis( $d['restraint'] ?? 1 ),
            ],
            'estimated_visual_intensity' => self::clamp_score( $row['estimated_visual_intensity'] ?? 5 ),
            'mismatch_severity' => self::mismatch( $row['mismatch_severity'] ?? 'medium' ),
            'confidence' => self::confidence( $row['model_confidence'] ?? 'low' ),
        ];
    }

    private static function normalize_model_sample( array $row ): array {
        return [
            'dimensions' => [
                'composition' => self::axis( $row['composition_fit_score'] ?? 1 ), 'whitespace' => self::axis( $row['whitespace_fit_score'] ?? 1 ),
                'density' => self::axis( $row['density_fit_score'] ?? 1 ), 'hierarchy' => self::axis( $row['hierarchy_fit_score'] ?? 1 ),
                'design_dna' => self::axis( $row['design_dna_fit_score'] ?? 1 ), 'transition' => self::axis( $row['transition_fit_score'] ?? 1 ),
                'conversion' => self::axis( $row['conversion_fit_score'] ?? 1 ), 'restraint' => self::axis( $row['restraint_fit_score'] ?? 1 ),
            ],
            'estimated_visual_intensity' => self::clamp_score( $row['estimated_visual_intensity'] ?? 5 ),
            'mismatch_severity' => self::mismatch( $row['mismatch_severity'] ?? 'medium' ),
            'confidence' => self::confidence( $row['confidence'] ?? 'low' ),
        ];
    }

    private static function score_dimensions( string $thumbnail_base64, array $context ) {
        $context_json = wp_json_encode( $context, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
        if ( ! is_string( $context_json ) ) return new WP_Error( 'elementize_multisample_context_encode', 'Grounded context could not be encoded.' );
        if ( strlen( $context_json ) > self::MAX_PROMPT_CONTEXT_BYTES ) $context_json = substr( $context_json, 0, self::MAX_PROMPT_CONTEXT_BYTES );
        $prompt = "You are a demanding senior SaaS web designer evaluating ONE anonymous section candidate against an exact grounded page context. There is exactly one attached image. You are NOT given its slot, template ID, title, or any competing candidate.\n\nGROUNDED PAGE CONTEXT:\n" . $context_json . "\n\nScore these axes independently from 1 to 5: composition fit, whitespace fit, density fit, typography/hierarchy fit, design-DNA fit, likely transition fit, conversion-emphasis fit, and visual-restraint fit. Use 5 only for exceptional alignment with almost no visible mismatch; 4 for good fit with minor visible mismatch; 3 for mixed fit; 2 for clear mismatch; 1 for strong incompatibility. Do not default every axis to the same value. Also estimate visual intensity 1-10 and set mismatch_severity to none, low, medium, or high. Ignore demo copy meaning, product category, brand names, logos, testimonial claims, stats, and semantic promises. Judge visible design behavior only. Return JSON only.";
        $schema = [ 'type' => 'object', 'required' => [ 'composition_fit_score','whitespace_fit_score','density_fit_score','hierarchy_fit_score','design_dna_fit_score','transition_fit_score','conversion_fit_score','restraint_fit_score','estimated_visual_intensity','mismatch_severity','confidence' ], 'properties' => [
            'composition_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5], 'whitespace_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5],
            'density_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5], 'hierarchy_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5],
            'design_dna_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5], 'transition_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5],
            'conversion_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5], 'restraint_fit_score'=>['type'=>'integer','minimum'=>1,'maximum'=>5],
            'estimated_visual_intensity'=>['type'=>'integer','minimum'=>1,'maximum'=>10], 'mismatch_severity'=>['type'=>'string','enum'=>['none','low','medium','high']],
            'confidence'=>['type'=>'string','enum'=>['low','medium','high']],
        ], 'additionalProperties' => false ];
        $payload = [ 'model'=>self::model(), 'prompt'=>$prompt, 'images'=>[$thumbnail_base64], 'stream'=>false, 'format'=>$schema, 'options'=>['temperature'=>0] ];
        $response = wp_remote_post( self::ollama_generate_url(), [ 'timeout'=>self::VISION_TIMEOUT_SECONDS, 'redirection'=>0, 'sslverify'=>false, 'headers'=>['Content-Type'=>'application/json','Accept'=>'application/json'], 'body'=>wp_json_encode($payload,JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE), 'limit_response_size'=>2097152 ] );
        if ( is_wp_error( $response ) ) return new WP_Error( 'elementize_multisample_ollama_failed', 'Local Ollama multi-sample request failed.' );
        $code = (int) wp_remote_retrieve_response_code( $response );
        if ( 200 !== $code ) return new WP_Error( 'elementize_multisample_ollama_http', 'Local Ollama multi-sample request returned HTTP ' . $code . '.' );
        $outer = json_decode( (string) wp_remote_retrieve_body( $response ), true );
        if ( ! is_array( $outer ) || ! is_string( $outer['response'] ?? null ) ) return new WP_Error( 'elementize_multisample_envelope', 'Local Ollama multi-sample response envelope is invalid.' );
        $decoded = json_decode( self::strip_json_fence( (string) $outer['response'] ), true );
        return is_array( $decoded ) ? $decoded : new WP_Error( 'elementize_multisample_json', 'Local Ollama multi-sample response is invalid JSON.' );
    }

    private static function derive_score( array $dimensions, int $measured_intensity, int $target_intensity, string $mismatch ): array {
        $sum = array_sum( $dimensions );
        $raw = 1.0 + 9.0 * ( ( $sum - 8.0 ) / 32.0 );
        $delta = abs( $measured_intensity - $target_intensity );
        $ip = $delta >= 5 ? 2.0 : ( $delta >= 4 ? 1.5 : ( $delta >= 3 ? 1.0 : ( $delta >= 2 ? 0.5 : 0.0 ) ) );
        $mp = [ 'none'=>0.0, 'low'=>0.0, 'medium'=>0.5, 'high'=>1.0 ][ $mismatch ] ?? 0.5;
        $score = self::clamp_score( round( $raw - $ip - $mp ) );
        $score = min( $score, self::clamp_score( round( $raw ) ) );
        return [ 'raw_axis_score'=>round($raw,2), 'intensity_penalty'=>$ip, 'mismatch_penalty'=>$mp, 'score'=>$score ];
    }

    private static function extract_candidates( array $data ) {
        $files = $data['openaiFileResponse'] ?? null;
        if ( ! is_array( $files ) || ! is_array( $files[0] ?? null ) ) return new WP_Error( 'elementize_multisample_proof_missing', 'Visual proof file is unavailable.' );
        $json = base64_decode( (string) ( $files[0]['content'] ?? '' ), true );
        $proof = false !== $json ? json_decode( $json, true ) : null;
        if ( ! is_array( $proof ) || ! is_array( $proof['candidates'] ?? null ) ) return new WP_Error( 'elementize_multisample_proof_invalid', 'Visual proof is invalid.' );
        $out = [];
        foreach ( $proof['candidates'] as $row ) {
            if ( ! is_array( $row ) ) continue;
            $slot = strtoupper( trim( (string) ( $row['slot'] ?? '' ) ) ); $id = sanitize_key( (string) ( $row['template_id'] ?? '' ) ); $img = trim( (string) ( $row['thumbnail_base64'] ?? '' ) );
            if ( in_array( $slot, [ 'A','B','C','D' ], true ) && '' !== $id && '' !== $img ) $out[] = [ 'slot'=>$slot, 'template_id'=>$id, 'thumbnail_mime'=>strtolower(trim((string)($row['thumbnail_mime']??''))), 'thumbnail_base64'=>$img ];
        }
        return count( $out ) >= 2 && count( $out ) <= 4 ? $out : new WP_Error( 'elementize_multisample_proof_incomplete', 'Visual proof must contain two to four candidates.' );
    }

    private static function normalize_images( array $candidates ) {
        $out=[]; $diag=[];
        foreach ( $candidates as $candidate ) {
            $bytes=base64_decode((string)$candidate['thumbnail_base64'],true);
            if(false===$bytes||''===$bytes||strlen($bytes)>self::MAX_IMAGE_BYTES)return new WP_Error('elementize_multisample_image_invalid','A candidate thumbnail could not be decoded within the local size limit.');
            $source=self::detect_mime($bytes,(string)$candidate['thumbnail_mime']); $transport=$bytes; $mime=$source; $transcoded=false;
            if(!in_array($source,['image/png','image/jpeg'],true)){ $png=self::to_png($bytes); if(is_wp_error($png))return $png; $transport=$png; $mime='image/png'; $transcoded=true; }
            $candidate['thumbnail_base64']=base64_encode($transport); $out[]=$candidate; $diag[]=['slot'=>$candidate['slot'],'template_id'=>$candidate['template_id'],'source_mime'=>$source,'transport_mime'=>$mime,'transcoded'=>$transcoded];
        }
        return ['candidates'=>$out,'diagnostics'=>$diag];
    }

    private static function measure_image( string $base64 ) {
        if(!function_exists('imagecreatefromstring')||!function_exists('imagesx')||!function_exists('imagecolorat'))return new WP_Error('elementize_multisample_gd_missing','PHP GD is unavailable for deterministic thumbnail measurement.');
        $bytes=base64_decode($base64,true); if(false===$bytes||''===$bytes)return new WP_Error('elementize_multisample_measure_decode','A thumbnail could not be decoded.');
        $im=@imagecreatefromstring($bytes); if(false===$im)return new WP_Error('elementize_multisample_measure_open','PHP GD could not open a thumbnail.');
        try{
            $w=max(1,imagesx($im));$h=max(1,imagesy($im));$g=self::GRID;$l=[];$s=[];$bins=[];$edges=0;$edgePairs=0;
            for($gy=0;$gy<$g;$gy++){
                $y=min($h-1,(int)floor(($gy+0.5)*$h/$g));
                for($gx=0;$gx<$g;$gx++){
                    $x=min($w-1,(int)floor(($gx+0.5)*$w/$g));$rgb=imagecolorat($im,$x,$y);$r=($rgb>>16)&255;$gg=($rgb>>8)&255;$b=$rgb&255;
                    $lum=(0.2126*$r+0.7152*$gg+0.0722*$b)/255.0;$max=max($r,$gg,$b);$min=min($r,$gg,$b);$sat=$max>0?($max-$min)/$max:0.0;$idx=$gy*$g+$gx;$l[$idx]=$lum;$s[$idx]=$sat;
                    $bin=(intdiv($r,32)<<6)|(intdiv($gg,32)<<3)|intdiv($b,32);$bins[$bin]=($bins[$bin]??0)+1;
                    if($gx>0){$edgePairs++;if(abs($lum-$l[$idx-1])>0.12)$edges++;}if($gy>0){$edgePairs++;if(abs($lum-$l[$idx-$g])>0.12)$edges++;}
                }
            }
            $n=count($l);$mean=array_sum($l)/max(1,$n);$var=0.0;foreach($l as $v)$var+=($v-$mean)*($v-$mean);$var/=(max(1,$n));$satMean=array_sum($s)/max(1,$n);$edgeDensity=$edgePairs?($edges/$edgePairs):0.0;$colorDiversity=min(1.0,count($bins)/96.0);$dominant=$n?max($bins)/$n:1.0;
            $complexity=min(1.0,(min(1.0,$var/0.08)*0.30)+(min(1.0,$satMean/0.45)*0.18)+(min(1.0,$edgeDensity/0.42)*0.30)+($colorDiversity*0.14)+((1.0-$dominant)*0.08));
            $intensity=max(1,min(10,(int)round(1+9*$complexity)));
            return ['intensity_proxy'=>$intensity,'luminance_variance'=>round($var,4),'mean_saturation'=>round($satMean,4),'edge_density'=>round($edgeDensity,4),'color_diversity'=>round($colorDiversity,4),'dominant_color_ratio'=>round($dominant,4),'complexity'=>round($complexity,4)];
        }finally{ if(is_resource($im)||is_object($im))@imagedestroy($im); }
    }

    private static function target_context(array $snapshot,string $marker){$ledger=array_values(array_filter((array)($snapshot['section_ledger']??[]),'is_array'));$idx=null;foreach($ledger as $i=>$r)if($marker===strtoupper((string)($r['marker']??''))){$idx=$i;break;}if(null===$idx)return new WP_Error('elementize_multisample_target_missing','The target marker is missing from grounded context.');return ['design_dna'=>$snapshot['design_dna']??[],'art_direction'=>$snapshot['art_direction']??[],'section_coherence'=>$snapshot['section_coherence']??[],'previous_section'=>$idx>0?$ledger[$idx-1]:null,'target'=>$ledger[$idx],'next_section'=>isset($ledger[$idx+1])?$ledger[$idx+1]:null];}
    private static function page_state_hash(int $page_id):string{$raw=get_post_meta($page_id,'_elementor_data',true);if(is_string($raw))return hash('sha256',$raw);$json=wp_json_encode($raw,JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE);return is_string($json)?hash('sha256',$json):'';}
    private static function cache_key(int $page_id,string $hash):string{return 'elementize_ab_'.$page_id.'_'.substr(preg_replace('/[^a-f0-9]/i','',$hash),0,24);}
    private static function detect_mime(string $b,string $h):string{if(str_starts_with($b,"\x89PNG\r\n\x1a\n"))return'image/png';if(str_starts_with($b,"\xff\xd8\xff"))return'image/jpeg';if(str_starts_with($b,'GIF87a')||str_starts_with($b,'GIF89a'))return'image/gif';if(strlen($b)>=12&&'RIFF'===substr($b,0,4)&&'WEBP'===substr($b,8,4))return'image/webp';return in_array($h,['image/png','image/jpeg','image/gif','image/webp'],true)?$h:'application/octet-stream';}
    private static function to_png(string $b){if(function_exists('imagecreatefromstring')&&function_exists('imagepng')){$im=@imagecreatefromstring($b);if(false!==$im){ob_start();$ok=@imagepng($im,null,6);$png=ob_get_clean();if(is_resource($im)||is_object($im))@imagedestroy($im);if($ok&&is_string($png)&&str_starts_with($png,"\x89PNG\r\n\x1a\n"))return$png;}}return new WP_Error('elementize_multisample_png_failed','A candidate thumbnail could not be normalized to PNG locally.');}
    private static function model():string{if(defined(self::MODEL_CONST)){$v=trim((string)constant(self::MODEL_CONST));if(''!==$v)return$v;}return self::DEFAULT_MODEL;}
    private static function ollama_generate_url():string{$b=self::DEFAULT_OLLAMA_URL;if(defined(self::OLLAMA_URL_CONST)){$v=trim((string)constant(self::OLLAMA_URL_CONST));if(''!==$v)$b=$v;}return rtrim($b,'/').'/api/generate';}
    private static function strip_json_fence(string $v):string{$v=trim($v);$v=preg_replace('/^```(?:json)?\s*/i','',$v)??$v;$v=preg_replace('/\s*```$/','',$v)??$v;return trim($v);}
    private static function median_int(array $v):int{sort($v,SORT_NUMERIC);return(int)$v[(int)floor(count($v)/2)];}
    private static function median_mismatch(array $v):string{$map=['none'=>0,'low'=>1,'medium'=>2,'high'=>3];$rev=['none','low','medium','high'];$n=[];foreach($v as $x)$n[]=$map[self::mismatch($x)];sort($n,SORT_NUMERIC);return$rev[$n[(int)floor(count($n)/2)]];}
    private static function lowest_confidence(array $v):string{$r=['low'=>0,'medium'=>1,'high'=>2];$best='high';foreach($v as $x){$x=self::confidence($x);if($r[$x]<$r[$best])$best=$x;}return$best;}
    private static function lower_confidence(string $a,string $b):string{$r=['low'=>0,'medium'=>1,'high'=>2];return($r[$a]??0)<=($r[$b]??0)?$a:$b;}
    private static function axis($v):int{return max(1,min(5,is_numeric($v)?(int)round((float)$v):1));}
    private static function clamp_score($v):int{return max(1,min(10,is_numeric($v)?(int)round((float)$v):1));}
    private static function mismatch($v):string{$v=strtolower(trim((string)$v));return in_array($v,['none','low','medium','high'],true)?$v:'medium';}
    private static function confidence($v):string{$v=strtolower(trim((string)$v));return in_array($v,['low','medium','high'],true)?$v:'low';}
    private static function fit_from_axis(int $v):string{return$v>=4?'strong':($v===3?'mixed':'weak');}
    private static function overall_fit_from_score(int $v):string{return$v>=7?'strong':($v>=4?'acceptable':'weak');}
    private static function truthy($v):bool{if(is_bool($v))return$v;if(is_int($v)||is_float($v))return 0!==(int)$v;return in_array(strtolower(trim((string)$v)),['1','true','yes','on'],true);}
    private static function clip(string $v,int $m):string{$v=trim(preg_replace('/\s+/u',' ',$v)??$v);return function_exists('mb_strlen')&&function_exists('mb_substr')?(mb_strlen($v)>$m?mb_substr($v,0,$m):$v):(strlen($v)>$m?substr($v,0,$m):$v);}

    private static function status(WP_REST_Response $response):WP_REST_Response{$d=$response->get_data();if(!is_array($d))$d=[];$d['aesthetic_ab_multisample_consensus_version']=self::VERSION;$d['aesthetic_ab_multisample_consensus']=true;$d['aesthetic_ab_multisample_consensus_samples_per_candidate']=3;$d['aesthetic_ab_multisample_consensus_trigger_below_score']=self::SELECTION_FLOOR;$d['aesthetic_ab_multisample_consensus_trigger_max_gap']=self::AMBIGUOUS_MAX_GAP;$d['aesthetic_ab_multisample_consensus_slot_order_blind']=true;$d['aesthetic_ab_multisample_consensus_pairwise_comparison']=false;$d['aesthetic_ab_multisample_consensus_read_only']=true;$d['aesthetic_ab_multisample_consensus_automatic_write_allowed']=false;$response->set_data($d);return$response;}
}

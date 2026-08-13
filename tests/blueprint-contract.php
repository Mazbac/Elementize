<?php
define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-blueprint.inc';

function fail_blueprint( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$blueprint = [
    'page_goal' => 'Explain SecureFlow and generate demo requests.',
    'audience' => 'Security and compliance teams.',
    'conversion_goal' => 'Request a demo.',
    'visual_direction' => 'Calm, structured B2B SaaS with restrained contrast.',
    'design_tokens' => [ 'radius' => '10px', 'accent' => 'primary' ],
    'responsive_strategy' => 'Preserve hierarchy and stack split layouts on narrow screens.',
    'interaction_strategy' => 'Use motion only when it clarifies state.',
    'acceptance_criteria' => [ 'No horizontal overflow', 'One CTA system' ],
    'reference_basis' => [ 'source_count' => 2, 'observed_viewports' => [ 'desktop', 'mobile' ], 'unresolved_hypotheses' => [ 'Tablet stacking is inferred until verified.' ] ],
    'reference_evidence' => [
        'observations' => [
            [ 'id' => 'hero-layout', 'category' => 'layout', 'statement' => 'Desktop hero uses a two-column split.', 'viewport' => 'desktop', 'evidence_kind' => 'observed', 'confidence' => 'high', 'transferability' => 'structural', 'source_ref' => 'reference-desktop' ],
            [ 'id' => 'mobile-stack', 'category' => 'responsive', 'statement' => 'The same hero stacks copy before media on mobile.', 'viewport' => 'cross_viewport', 'evidence_kind' => 'observed', 'confidence' => 'high', 'transferability' => 'structural' ],
            [ 'id' => 'tablet-stack', 'category' => 'responsive', 'statement' => 'Tablet probably follows the mobile stack.', 'viewport' => 'tablet', 'evidence_kind' => 'inferred', 'confidence' => 'medium', 'transferability' => 'structural' ],
        ],
    ],
    'acceptance_plan' => [
        'required_viewports' => [ 'desktop', 'tablet', 'mobile' ],
        'native_vision_viewports' => [ 'desktop', 'tablet', 'mobile' ],
        'live_motion_viewports' => [ 'desktop' ],
        'safe_interaction_viewports' => [ 'desktop', 'mobile' ],
        'quality_gates' => [ 'visual_render_verified', 'viewport_exact', 'document_capture_complete', 'capture_bounds_ready', 'browser_probe_verified', 'no_horizontal_overflow', 'no_bad_internal_anchors', 'safe_interactions_pass' ],
        'max_repair_cycles' => 2,
    ],
    'sections' => [
        [
            'id' => 'problem-grid',
            'purpose' => 'Show the three coordination problems SecureFlow removes.',
            'narrative_role' => 'Problem agitation before the workflow explanation.',
            'contract' => 'feature_grid',
            'layout_pattern' => 'Three equal cards below one heading.',
            'component_constraints' => [ '3 feature cards', 'one icon treatment' ],
            'keywords' => [ 'problems', 'features', 'cards' ],
        ],
        [
            'id' => 'demo-cta',
            'purpose' => 'Convert interested visitors into demo requests.',
            'narrative_role' => 'Final conversion close.',
            'contract' => 'cta',
            'layout_pattern' => 'Focused CTA with one primary action.',
            'keywords' => [ 'demo', 'contact' ],
        ],
    ],
];

$result = Elementize_Blueprint::normalize( $blueprint, 'problem-grid' );
if ( ! is_array( $result ) ) fail_blueprint( 'Valid blueprint did not normalize.' );
if ( ( $result['selected_section']['contract'] ?? '' ) !== 'feature_grid' ) fail_blueprint( 'Selected blueprint section was not grounded.' );
$fingerprint = (string) ( $result['fingerprint'] ?? '' );
if ( 64 !== strlen( $fingerprint ) ) fail_blueprint( 'Blueprint fingerprint is not SHA-256.' );
if ( ( $result['blueprint_version'] ?? '' ) !== '3' ) fail_blueprint( 'Progressive-checkpoint blueprint version did not advance.' );
if ( ( $result['normalized']['reference_evidence']['observation_count'] ?? 0 ) !== 3 ) fail_blueprint( 'Reference evidence count was not normalized.' );
if ( ( $result['normalized']['reference_evidence']['observed_categories'] ?? [] ) !== [ 'layout', 'responsive' ] ) fail_blueprint( 'Observed reference categories are not deterministic.' );
$matrix = (array) ( $result['normalized']['acceptance_plan']['qa_matrix'] ?? [] );
if ( count( $matrix ) !== 6 ) fail_blueprint( 'Acceptance QA matrix should contain 3 settled, 1 live and 2 safe rows.' );
if ( ( $matrix[0]['id'] ?? '' ) !== 'desktop-settled' || ( $matrix[0]['analyze'] ?? null ) !== true ) fail_blueprint( 'Desktop settled native-vision row is missing.' );
if ( ( $matrix[3]['id'] ?? '' ) !== 'desktop-live' || ( $matrix[3]['analyze'] ?? null ) !== false ) fail_blueprint( 'Live-motion QA row is not deterministic.' );
$short_checkpoints = (array) ( $result['normalized']['build_checkpoints'] ?? [] );
if ( array_column( $short_checkpoints, 'stage' ) !== [ 'design_language_lock', 'whole_page_close' ] ) fail_blueprint( 'Two-section blueprint did not derive the expected progressive checkpoints.' );
if ( ( $short_checkpoints[0]['after_section_id'] ?? '' ) !== 'problem-grid' || ( $short_checkpoints[1]['after_section_id'] ?? '' ) !== 'demo-cta' ) fail_blueprint( 'Checkpoint section anchors are not deterministic.' );
foreach ( $short_checkpoints as $checkpoint ) if ( empty( $checkpoint['analyze'] ) || 'desktop' !== ( $checkpoint['viewport'] ?? '' ) || 1 !== ( $checkpoint['max_repair_cycles'] ?? null ) ) fail_blueprint( 'Progressive checkpoint native-vision contract changed.' );

$long = $blueprint;
$long['sections'] = [];
for ( $i = 0; $i < 8; $i++ ) {
    $section = $blueprint['sections'][0];
    $section['id'] = 'section-' . ( $i + 1 );
    $section['purpose'] = 'Purpose ' . ( $i + 1 );
    $long['sections'][] = $section;
}
$long_result = Elementize_Blueprint::normalize( $long );
$long_checkpoints = (array) ( $long_result['normalized']['build_checkpoints'] ?? [] );
if ( array_column( $long_checkpoints, 'stage' ) !== [ 'design_language_lock', 'early_narrative', 'mid_page_rhythm', 'whole_page_close' ] ) fail_blueprint( 'Eight-section blueprint did not derive all four design checkpoints.' );
if ( array_column( $long_checkpoints, 'after_section_index' ) !== [ 0, 2, 4, 7 ] ) fail_blueprint( 'Eight-section checkpoint placement changed unexpectedly.' );

$reordered = $blueprint;
$reordered['design_tokens'] = [ 'accent' => 'primary', 'radius' => '10px' ];
$second = Elementize_Blueprint::normalize( $reordered, 'problem-grid', $fingerprint );
if ( ( $second['fingerprint'] ?? '' ) !== $fingerprint ) fail_blueprint( 'Equivalent normalized blueprint changed fingerprint.' );

$changed = $blueprint;
$changed['sections'][0]['purpose'] = 'Different purpose';
$rejected = false;
try {
    Elementize_Blueprint::normalize( $changed, 'problem-grid', $fingerprint );
} catch ( InvalidArgumentException $error ) {
    $rejected = false !== strpos( $error->getMessage(), 'elementize_blueprint_changed' );
}
if ( ! $rejected ) fail_blueprint( 'Changed blueprint was not rejected by expected fingerprint.' );

if ( ( $result['normalized']['reference_basis']['observed_viewports'] ?? [] ) !== [ 'desktop', 'mobile' ] ) fail_blueprint( 'Observed viewport evidence changed unexpectedly.' );

$bad_observed = $blueprint;
$bad_observed['reference_basis']['observed_viewports'] = [ 'desktop' ];
$bad_observed['reference_evidence']['observations'][1]['evidence_kind'] = 'inferred';
$bad_observed['reference_evidence']['observations'][2] = [ 'id' => 'tablet-observed', 'category' => 'responsive', 'statement' => 'Tablet stack was observed.', 'viewport' => 'tablet', 'evidence_kind' => 'observed', 'confidence' => 'high', 'transferability' => 'structural' ];
$bad_rejected = false;
try { Elementize_Blueprint::normalize( $bad_observed ); } catch ( InvalidArgumentException $error ) { $bad_rejected = false !== strpos( $error->getMessage(), 'elementize_reference_viewport_unobserved' ); }
if ( ! $bad_rejected ) fail_blueprint( 'Unobserved viewport was allowed to masquerade as observed reference evidence.' );

$outside_scope = $blueprint;
$outside_scope['acceptance_plan']['required_viewports'] = [ 'desktop' ];
$outside_scope['acceptance_plan']['safe_interaction_viewports'] = [ 'mobile' ];
$scope_rejected = false;
try { Elementize_Blueprint::normalize( $outside_scope ); } catch ( InvalidArgumentException $error ) { $scope_rejected = false !== strpos( $error->getMessage(), 'elementize_acceptance_viewport_outside_scope' ); }
if ( ! $scope_rejected ) fail_blueprint( 'Acceptance sub-check escaped required viewport scope.' );

$missing_native = $blueprint;
$missing_native['acceptance_plan']['native_vision_viewports'] = [ 'desktop', 'mobile' ];
$native_rejected = false;
try { Elementize_Blueprint::normalize( $missing_native ); } catch ( InvalidArgumentException $error ) { $native_rejected = false !== strpos( $error->getMessage(), 'elementize_acceptance_native_vision_required' ); }
if ( ! $native_rejected ) fail_blueprint( 'A required viewport was allowed without native vision acceptance.' );

$core_only = $blueprint;
$core_only['acceptance_plan']['quality_gates'] = [];
$core = Elementize_Blueprint::normalize( $core_only );
$core_gates = (array) ( $core['normalized']['acceptance_plan']['quality_gates'] ?? [] );
foreach ( [ 'visual_render_verified', 'viewport_exact', 'document_capture_complete', 'capture_bounds_ready', 'browser_probe_verified', 'no_horizontal_overflow', 'no_bad_internal_anchors' ] as $gate ) if ( ! in_array( $gate, $core_gates, true ) ) fail_blueprint( 'Core acceptance gate disappeared: ' . $gate );

$too_many = $blueprint;
$too_many['reference_evidence']['observations'] = array_fill( 0, 49, $blueprint['reference_evidence']['observations'][0] );
$limit_rejected = false;
try { Elementize_Blueprint::normalize( $too_many ); } catch ( InvalidArgumentException $error ) { $limit_rejected = false !== strpos( $error->getMessage(), 'elementize_reference_observation_limit' ); }
if ( ! $limit_rejected ) fail_blueprint( 'Oversized reference evidence was silently truncated instead of rejected.' );

echo "Blueprint contract OK\n";

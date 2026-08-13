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
    'reference_basis' => [ 'source_count' => 2, 'observed_viewports' => [ 'desktop' ], 'unresolved_hypotheses' => [ 'Mobile stacking is inferred until verified.' ] ],
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

if ( ( $result['normalized']['reference_basis']['observed_viewports'] ?? [] ) !== [ 'desktop' ] ) fail_blueprint( 'Observed viewport evidence changed unexpectedly.' );
echo "Blueprint contract OK\n";

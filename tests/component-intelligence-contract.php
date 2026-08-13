<?php
define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-component-intelligence.inc';
function fail_component_intelligence( string $message ): void { fwrite( STDERR, $message . PHP_EOL ); exit( 1 ); }

$families = Elementize_Component_Intelligence::widget_families( [
    'pix-feature' => 4,
    'pix-heading' => 1,
    'pix-text' => 1,
    'pix-button' => 1,
] );
if ( ( $families['feature'] ?? 0 ) !== 4 ) fail_component_intelligence( 'Feature-family classification failed.' );
if ( ( $families['heading'] ?? 0 ) !== 1 ) fail_component_intelligence( 'Heading-family classification failed.' );

$summary = [ 'element_count' => 30, 'max_depth' => 5 ];
$feature = Elementize_Component_Intelligence::structure_score( 'feature_grid', $families, $summary );
$cta = Elementize_Component_Intelligence::structure_score( 'cta', $families, $summary );
if ( $feature <= $cta ) fail_component_intelligence( 'Feature-grid structure should outrank CTA for feature-heavy fixtures.' );
$source = file_get_contents( __DIR__ . '/../includes/elementize-component-intelligence.inc' );
$schema = file_get_contents( __DIR__ . '/../config/gpt/actions.openapi.yaml' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
foreach ( [ 'metadata_shortlist_count', 'structural_fit', 'design_compatibility', 'responsive_confidence', 'dependency_safety', 'edit_efficiency', 'estimated_normalization_cost', 'blueprint_grounding', 'expected_blueprint_fingerprint', 'selected_section_id', 'acceptance_plan' ] as $needle ) {
    if ( false === strpos( $source, $needle ) ) fail_component_intelligence( 'Missing component scoring marker: ' . $needle );
}
if ( false === strpos( $schema, 'operationId: rankElementizeComponentCandidates' ) ) fail_component_intelligence( 'Component ranking Action is missing.' );
if ( false === strpos( $instructions, '`rankElementizeComponentCandidates`' ) ) fail_component_intelligence( 'GPT component-ranking workflow is missing.' );
if ( false === strpos( $source, "'read_only' => true" ) ) fail_component_intelligence( 'Component ranking must remain read-only.' );
echo "Component intelligence contract OK\n";

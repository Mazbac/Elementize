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

$summary = [ 'element_count' => 30, 'widget_count' => 7, 'max_depth' => 5 ];
$feature = Elementize_Component_Intelligence::structure_score( 'feature_grid', $families, $summary );
$cta = Elementize_Component_Intelligence::structure_score( 'cta', $families, $summary );
if ( $feature <= $cta ) fail_component_intelligence( 'Feature-grid structure should outrank CTA for feature-heavy fixtures.' );
$process_signature = Elementize_Component_Intelligence::composition_signature( [ 'title' => 'Workflow Steps' ], $families, $summary );
$cards_signature = Elementize_Component_Intelligence::composition_signature( [ 'title' => 'Feature Cards' ], $families, $summary );
if ( ( $process_signature['composition_role'] ?? '' ) !== 'process_steps' ) fail_component_intelligence( 'Workflow fixture did not classify as process_steps.' );
if ( ( $cards_signature['composition_role'] ?? '' ) !== 'card_grid' ) fail_component_intelligence( 'Feature fixture did not classify as card_grid.' );
$process_intent = [ 'composition_role' => 'process_steps', 'visual_scale' => 'standard', 'media_emphasis' => 'none', 'density' => 'balanced' ];
$exact_fit = Elementize_Component_Intelligence::composition_fit( $process_intent, $process_signature );
$compatible_fit = Elementize_Component_Intelligence::composition_fit( $process_intent, $cards_signature );
if ( (int) ( $exact_fit['adjustment'] ?? -99 ) <= (int) ( $compatible_fit['adjustment'] ?? -99 ) ) fail_component_intelligence( 'Exact composition role should outrank a merely compatible card grid.' );
$faq_signature = Elementize_Component_Intelligence::composition_signature( [ 'title' => 'FAQ Accordion' ], [ 'accordion' => 1, 'heading' => 1 ], [ 'element_count' => 10, 'widget_count' => 2, 'max_depth' => 3 ] );
$faq_title_only = Elementize_Component_Intelligence::composition_signature( [ 'title' => 'FAQ Page CTA' ], [ 'heading' => 1, 'button' => 1 ], [ 'element_count' => 8, 'widget_count' => 2, 'max_depth' => 2 ] );
if ( ( $faq_title_only['composition_role'] ?? '' ) === 'faq' ) fail_component_intelligence( 'FAQ metadata without an accordion was falsely classified as FAQ structure.' );
$mismatch_fit = Elementize_Component_Intelligence::composition_fit( $process_intent, $faq_signature );
if ( (int) ( $mismatch_fit['adjustment'] ?? 0 ) >= 0 || ( $mismatch_fit['role_match'] ?? '' ) !== 'mismatch' ) fail_component_intelligence( 'Strong composition mismatch was not penalized.' );
$source = file_get_contents( __DIR__ . '/../includes/elementize-component-intelligence.inc' );
$schema = file_get_contents( __DIR__ . '/../config/gpt/actions.openapi.yaml' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
foreach ( [ "private const VERSION = '5'", 'metadata_shortlist_count', 'structural_fit', 'design_compatibility', 'responsive_confidence', 'dependency_safety', 'edit_efficiency', 'composition_adjustment', 'composition_signature', 'composition_fit', 'estimated_normalization_cost', 'blueprint_grounding', 'composition_rhythm', 'expected_blueprint_fingerprint', 'selected_section_id', 'acceptance_plan', 'build_checkpoints', 'checkpoint_after_selected_section' ] as $needle ) {
    if ( false === strpos( $source, $needle ) ) fail_component_intelligence( 'Missing component scoring marker: ' . $needle );
}
if ( false === strpos( $schema, 'operationId: rankElementizeComponentCandidates' ) ) fail_component_intelligence( 'Component ranking Action is missing.' );
if ( false === strpos( $instructions, '`rankElementizeComponentCandidates`' ) ) fail_component_intelligence( 'GPT component-ranking workflow is missing.' );
if ( false === strpos( $instructions, '`build_checkpoints`' ) ) fail_component_intelligence( 'GPT progressive component-build checkpoint workflow is missing.' );
if ( false === strpos( $source, "'read_only' => true" ) ) fail_component_intelligence( 'Component ranking must remain read-only.' );
echo "Component intelligence contract OK\n";

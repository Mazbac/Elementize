<?php
define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-blueprint.inc';
require_once __DIR__ . '/../includes/elementize-execution-trace.inc';

function fail_trace( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}
function rejected_trace( callable $fn, string $needle ): bool {
    try { $fn(); }
    catch ( InvalidArgumentException $error ) { return false !== strpos( $error->getMessage(), $needle ); }
    return false;
}

$blueprint = [
    'page_goal' => 'Generate demo requests.',
    'audience' => 'Security leaders.',
    'conversion_goal' => 'Request a demo.',
    'visual_direction' => 'Coherent B2B SaaS.',
    'sections' => [
        [ 'id' => 'features', 'purpose' => 'Explain benefits.', 'narrative_role' => 'Education.', 'contract' => 'feature_grid' ],
        [ 'id' => 'close', 'purpose' => 'Convert interest.', 'narrative_role' => 'Final conversion.', 'contract' => 'cta' ],
    ],
];

$normalized = Elementize_Blueprint::normalize( $blueprint );
$fingerprint = (string) $normalized['fingerprint'];
$context = [
    'blueprint' => $blueprint,
    'expected_blueprint_fingerprint' => $fingerprint,
    'phase' => 'build',
    'section_ids' => [ 'features', 'close' ],
    'selected_candidates' => [
        [ 'section_id' => 'features', 'source_id' => 'pixfort', 'template_id' => 'feature-template', 'score' => 86, 'recommendation' => 'strong_candidate' ],
        [ 'section_id' => 'close', 'source_id' => 'pixfort', 'template_id' => 'cta-template', 'score' => 79, 'recommendation' => 'inspect_and_compare' ],
    ],
];
$operations = [
    [ 'type' => 'insert_template', 'source_id' => 'pixfort', 'template_id' => 'feature-template', 'contract' => 'feature_grid' ],
    [ 'type' => 'insert_template', 'source_id' => 'pixfort', 'template_id' => 'cta-template', 'contract' => 'cta' ],
];
$trace = Elementize_Execution_Trace::normalize( $context, $operations );
if ( ! is_array( $trace ) || empty( $trace['trace_verified'] ) ) fail_trace( 'Valid execution trace was not verified.' );
if ( ( $trace['blueprint_fingerprint'] ?? '' ) !== $fingerprint ) fail_trace( 'Verified trace lost blueprint fingerprint.' );
if ( 2 !== (int) ( $trace['insertion_count_verified'] ?? 0 ) ) fail_trace( 'Insertion count did not verify.' );
if ( isset( $trace['blueprint'] ) ) fail_trace( 'Full blueprint leaked into compact execution trace.' );

$wrong_fingerprint = $context;
$wrong_fingerprint['blueprint']['page_goal'] = 'Changed plan.';
if ( ! rejected_trace( static fn() => Elementize_Execution_Trace::normalize( $wrong_fingerprint, $operations ), 'elementize_blueprint_changed' ) ) fail_trace( 'Changed blueprint fingerprint was not rejected.' );

$wrong_template = $context;
$wrong_template['selected_candidates'][0]['template_id'] = 'other-template';
if ( ! rejected_trace( static fn() => Elementize_Execution_Trace::normalize( $wrong_template, $operations ), 'elementize_plan_context_candidate_not_inserted' ) ) fail_trace( 'Candidate/template mismatch was not rejected.' );

$wrong_contract_ops = $operations;
$wrong_contract_ops[0]['contract'] = 'cta';
if ( ! rejected_trace( static fn() => Elementize_Execution_Trace::normalize( $context, $wrong_contract_ops ), 'elementize_plan_context_contract_mismatch' ) ) fail_trace( 'Insertion/blueprint contract mismatch was not rejected.' );

$extra_ops = $operations;
$extra_ops[] = [ 'type' => 'insert_template', 'source_id' => 'pixfort', 'template_id' => 'unmapped-template', 'contract' => 'generic' ];
if ( ! rejected_trace( static fn() => Elementize_Execution_Trace::normalize( $context, $extra_ops ), 'elementize_plan_context_insertion_unmapped' ) ) fail_trace( 'Unmapped extra insertion was not rejected.' );

$repair = $context;
$repair['phase'] = 'repair';
$repair['selected_candidates'] = [];
$repair_trace = Elementize_Execution_Trace::normalize( $repair, [ [ 'type' => 'style' ] ] );
if ( ! is_array( $repair_trace ) || 0 !== (int) ( $repair_trace['insertion_count_verified'] ?? -1 ) ) fail_trace( 'Blueprint-grounded non-insertion repair should remain valid.' );
echo "Execution trace contract OK\n";

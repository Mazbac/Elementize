<?php

define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-designer.inc';

function fail_designer_agent( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$cases = [
    [ [ 'padding' ], 'desktop' ],
    [ [ 'padding_tablet' ], 'tablet' ],
    [ [ 'margin_tablet_extra' ], 'tablet' ],
    [ [ 'font_size_mobile' ], 'mobile' ],
    [ [ 'gap_mobile_extra' ], 'mobile' ],
    [ [], 'unknown' ],
];
foreach ( $cases as [ $path, $expected ] ) {
    $actual = Elementize_Designer::breakpoint_from_path( $path );
    if ( $actual !== $expected ) fail_designer_agent( 'Breakpoint classification failed: expected ' . $expected . ', got ' . $actual );
}

$designer = file_get_contents( __DIR__ . '/../includes/elementize-designer.inc' );
$design = file_get_contents( __DIR__ . '/../includes/elementize-design.inc' );
$visual = file_get_contents( __DIR__ . '/../includes/elementize-visual-qa.inc' );
$schema = file_get_contents( __DIR__ . '/../config/gpt/actions.openapi.yaml' );
$instructions = file_get_contents( __DIR__ . '/../config/gpt/wp-builder-instructions.md' );
foreach ( [ "private const VERSION = '8'", 'blueprint_contract', 'execution_trace_contract', 'reference_analysis_contract', 'visual_critic_contract', 'checkpoint_rubrics', 'whole_page_questions', 'quality_contract', 'composition_rhythm', 'candidate_composition_signature_fit', 'responsive_control_summary', 'page_coherence', 'observation_contract' ] as $needle ) {
    if ( false === strpos( $designer, $needle ) ) fail_designer_agent( 'Designer context missing: ' . $needle );
}
foreach ( [ "'responsive_breakpoint'", 'responsive_breakpoint( array $path )' ] as $needle ) {
    if ( false === strpos( $design, $needle ) ) fail_designer_agent( 'Design controls missing breakpoint metadata: ' . $needle );
}
foreach ( [ "'desktop', 'tablet', 'mobile'", 'browser_probe_script', 'browser_probe_for_job', 'capture_job_complete', 'receive_browser_probe' ] as $needle ) {
    if ( false === strpos( $visual, $needle ) ) fail_designer_agent( 'Visual QA designer capability missing: ' . $needle );
}
if ( false === strpos( $schema, 'operationId: getElementizeDesignerContext' ) ) fail_designer_agent( 'Designer context Action is missing.' );
if ( false === strpos( $schema, 'enum: [desktop, tablet, mobile]' ) ) fail_designer_agent( 'Visual QA viewport enum is missing.' );
foreach ( [ 'ElementizeReferenceEvidence', 'ElementizeReferenceObservation', 'ElementizeAcceptancePlan', 'safe_interaction_viewports', 'quality_gates' ] as $needle ) {
    if ( false === strpos( $schema, $needle ) ) fail_designer_agent( 'Reference/acceptance schema missing: ' . $needle );
}
foreach ( [ 'Designer workflow', 'Reference pages', 'build_checkpoints', 'visual_critic_contract', 'desktop, tablet and mobile', 'browser diagnostics', 'acceptance_plan.qa_matrix', 'evidence_kind=observed|inferred' ] as $needle ) {
    if ( false === strpos( $instructions, $needle ) ) fail_designer_agent( 'GPT designer instruction missing: ' . $needle );
}

echo "Designer agent contract OK\n";

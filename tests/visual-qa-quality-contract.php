<?php

define( 'ABSPATH', __DIR__ . '/../' );
if ( ! function_exists( 'sanitize_textarea_field' ) ) { function sanitize_textarea_field( $value ) { return trim( strip_tags( (string) $value ) ); } }
require_once __DIR__ . '/../includes/elementize-visual-qa.inc';

function fail_visual_quality( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$payload = [
    'viewport' => 'mobile',
    'interaction_probe' => 'passive',
    'visual_render_verified' => true,
    'screenshot' => [ 'viewport_exact' => true, 'document_capture_complete' => true ],
    'browser_probe_verified' => true,
    'browser_diagnostics' => [
        'document' => [ 'horizontalOverflow' => false ],
        'counts' => [ 'overflow' => 0, 'anchorIssues' => 2, 'hiddenMotion' => 1 ],
        'overflow' => [],
        'anchorIssues' => [
            [ 'href' => '#', 'text' => 'Learn more', 'issue' => 'empty_or_non_navigation', 'elementId' => 'abc123', 'documentId' => '953851' ],
            [ 'href' => '#missing', 'text' => 'Details', 'issue' => 'missing_internal_target', 'documentId' => '953851' ],
        ],
        'hiddenMotion' => [ [ 'tag' => 'div', 'className' => 'elementor-element-ghi789 animated', 'elementId' => 'ghi789', 'documentId' => '953851' ] ],
        'tinyInteractive' => [ [ 'tag' => 'a', 'text' => 'x', 'width' => 20, 'height' => 20, 'elementId' => 'tiny1', 'documentId' => '953851' ] ],
        'interactions' => [ 'mode' => 'passive', 'tested' => 0, 'passed' => 0, 'failed' => 0, 'restorationFailures' => 0, 'items' => [] ],
    ],
];

$report = Elementize_Visual_QA::quality_report( $payload );
$gates = $report['quality_gate_evaluation']['gates'] ?? [];
foreach ( [ 'visual_render_verified', 'viewport_exact', 'document_capture_complete', 'browser_probe_verified', 'no_horizontal_overflow' ] as $gate ) {
    if ( 'pass' !== ( $gates[ $gate ]['state'] ?? null ) ) fail_visual_quality( 'Expected pass gate: ' . $gate );
}
if ( 'not_applicable' !== ( $gates['capture_bounds_ready']['state'] ?? null ) ) fail_visual_quality( 'Capture-only QA must not pretend native handoff bounds were verified.' );
if ( 'fail' !== ( $gates['no_bad_internal_anchors']['state'] ?? null ) ) fail_visual_quality( 'Anchor issues were not converted into a failed gate.' );
if ( 'fail' !== ( $gates['no_hidden_animated_elements']['state'] ?? null ) ) fail_visual_quality( 'Hidden motion was not converted into a failed gate.' );
if ( 'not_applicable' !== ( $gates['safe_interactions_pass']['state'] ?? null ) ) fail_visual_quality( 'Passive QA must not claim a safe interaction result.' );

$signals = $report['repair_signals'] ?? [];
$categories = array_count_values( array_column( $signals, 'category' ) );
if ( 2 !== ( $categories['content_link'] ?? 0 ) ) fail_visual_quality( 'Expected one content-link signal per anchor issue.' );
if ( 1 !== ( $categories['motion_visibility'] ?? 0 ) ) fail_visual_quality( 'Expected a localized hidden-motion signal.' );
if ( 1 !== ( $categories['touch_target_review'] ?? 0 ) ) fail_visual_quality( 'Expected bounded touch-target review evidence.' );
foreach ( $signals as $signal ) if ( ! empty( $signal['safe_to_autorepair'] ) ) fail_visual_quality( 'Repair signals must never authorize automatic writes.' );
if ( 'abc123' !== ( $signals[0]['element_id'] ?? null ) ) fail_visual_quality( 'Localized Elementor evidence was not preserved.' );
if ( 'localize_first' !== ( $signals[1]['repair_scope'] ?? null ) || null !== ( $signals[1]['element_id'] ?? null ) ) fail_visual_quality( 'Unlocalized anchor evidence was presented as a local repair target.' );
$signal_summary = $report['repair_signal_summary'] ?? [];
if ( 4 !== ( $signal_summary['count'] ?? null ) || 3 !== ( $signal_summary['localized'] ?? null ) || 1 !== ( $signal_summary['unlocalized'] ?? null ) ) fail_visual_quality( 'Repair-signal localization summary is incorrect.' );
if ( true === ( $signal_summary['automatic_writes_authorized'] ?? null ) ) fail_visual_quality( 'Repair summary must never authorize automatic writes.' );

$native = $payload;
$native['capture_bounds_ready'] = true;
$native_report = Elementize_Visual_QA::quality_report( $native );
if ( 'pass' !== ( $native_report['quality_gate_evaluation']['gates']['capture_bounds_ready']['state'] ?? null ) ) fail_visual_quality( 'Native handoff bounds did not upgrade to pass.' );

$safe = $payload;
$safe['interaction_probe'] = 'safe';
$safe['browser_diagnostics']['interactions'] = [
    'mode' => 'safe', 'tested' => 1, 'passed' => 0, 'failed' => 1, 'restorationFailures' => 1,
    'items' => [ [ 'tag' => 'button', 'role' => 'button', 'text' => 'FAQ', 'changed' => true, 'restored' => false, 'passed' => false, 'elementId' => 'faq1', 'documentId' => '953851' ] ],
];
$safe_report = Elementize_Visual_QA::quality_report( $safe );
if ( 'fail' !== ( $safe_report['quality_gate_evaluation']['gates']['safe_interactions_pass']['state'] ?? null ) ) fail_visual_quality( 'Failed restoration was not converted into a safe-interaction gate failure.' );
$safe_categories = array_count_values( array_column( $safe_report['repair_signals'] ?? [], 'category' ) );
if ( 1 !== ( $safe_categories['interaction_restoration'] ?? 0 ) ) fail_visual_quality( 'Failed safe interaction was not localized into a repair signal.' );

$infra = [
    'viewport' => 'desktop', 'interaction_probe' => 'passive', 'visual_render_verified' => true,
    'screenshot' => [ 'viewport_exact' => false, 'document_capture_complete' => false ],
    'browser_probe_verified' => false,
];
$infra_report = Elementize_Visual_QA::quality_report( $infra );
$infra_gates = $infra_report['quality_gate_evaluation']['gates'] ?? [];
if ( 'fail' !== ( $infra_gates['viewport_exact']['state'] ?? null ) || 'fail' !== ( $infra_gates['document_capture_complete']['state'] ?? null ) || 'fail' !== ( $infra_gates['browser_probe_verified']['state'] ?? null ) ) fail_visual_quality( 'Infrastructure failures were not preserved.' );
foreach ( [ 'no_horizontal_overflow', 'no_bad_internal_anchors', 'no_hidden_animated_elements' ] as $gate ) if ( 'unavailable' !== ( $infra_gates[ $gate ]['state'] ?? null ) ) fail_visual_quality( 'Page-level gate must be unavailable without a verified browser probe: ' . $gate );
foreach ( $infra_report['repair_signals'] ?? [] as $signal ) if ( 'infrastructure' === ( $signal['category'] ?? '' ) && 'system' !== ( $signal['repair_scope'] ?? '' ) ) fail_visual_quality( 'Infrastructure signal attempted to enter page-write scope.' );

if ( count( $report['repair_signals'] ?? [] ) > 24 ) fail_visual_quality( 'Repair signal list exceeded its hard bound.' );
echo "Visual QA quality contract OK\n";
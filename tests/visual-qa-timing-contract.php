<?php

define( 'ABSPATH', __DIR__ . '/../' );
require_once __DIR__ . '/../includes/elementize-visual-qa.inc';

function fail_visual_qa_timing( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

$delay = new ReflectionMethod( Elementize_Visual_QA::class, 'browser_probe_delay_ms' );
$budget = new ReflectionMethod( Elementize_Visual_QA::class, 'capture_settle_ms' );

$settled_delay = (int) $delay->invoke( null, 'settled' );
$live_delay = (int) $delay->invoke( null, 'live' );
$settled = (int) $budget->invoke( null, 'settled', 'passive' );
$live = (int) $budget->invoke( null, 'live', 'passive' );
$safe = (int) $budget->invoke( null, 'settled', 'safe' );
$safe_live = (int) $budget->invoke( null, 'live', 'safe' );

if ( 500 !== $settled_delay || 3500 !== $live_delay ) fail_visual_qa_timing( 'Probe start delays changed unexpectedly.' );
if ( $settled < 3000 ) fail_visual_qa_timing( 'Settled passive telemetry budget is too small.' );
if ( $live < 10000 ) fail_visual_qa_timing( 'Live passive telemetry budget is too small for full-page motion warmup.' );
if ( $safe < 14000 ) fail_visual_qa_timing( 'Settled safe-interaction telemetry budget is too small.' );
if ( $safe_live < 20000 ) fail_visual_qa_timing( 'Live safe-interaction telemetry budget is too small.' );
if ( $live <= $live_delay || $safe_live <= $live_delay ) fail_visual_qa_timing( 'Live completion budgets must exceed the deliberate live probe delay.' );

echo "Visual QA timing contract OK\n";
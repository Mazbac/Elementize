param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER,

    [Parameter(Mandatory = $false)]
    [switch]$ConfirmExperiment
)

$ErrorActionPreference = 'Stop'

if (-not $ConfirmExperiment) {
    throw 'This shadow-decision harness runs the existing reversible repair experiment first. Re-run with -ConfirmExperiment to explicitly authorize that temporary draft-only write/restore test.'
}

$repoRoot = Split-Path $PSScriptRoot -Parent
$experiment = Join-Path $PSScriptRoot 'test-repair-experiment.ps1'
if (-not (Test-Path $experiment)) { throw 'test-repair-experiment.ps1 was not found.' }

Write-Host ''
Write-Host 'Elementize shadow keep-or-rollback decision' -ForegroundColor Cyan
Write-Host 'Policy: run the guarded reversible experiment, restore the page, then compute what the autonomous decision WOULD have been.'
Write-Host 'This harness never keeps the temporary change.' -ForegroundColor Yellow
Write-Host ''

$experimentArgs = @('-PageId', $PageId, '-SiteUrl', $SiteUrl, '-ConfirmExperiment')
if (-not [string]::IsNullOrWhiteSpace($Username)) { $experimentArgs += @('-Username', $Username) }

& $experiment @experimentArgs
if ($LASTEXITCODE -ne 0) { throw "The reversible repair experiment failed with exit code $LASTEXITCODE. No shadow decision will be made." }

$devDir = Join-Path $repoRoot '.elementize-dev'
$before = Get-ChildItem $devDir -Filter "experiment-before-$PageId-*.json" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
$after = Get-ChildItem $devDir -Filter "experiment-after-$PageId-*.json" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($null -eq $before -or $null -eq $after) { throw 'Could not locate the latest reversible experiment audit pair.' }
if ($after.LastWriteTime -lt $before.LastWriteTime) { throw 'Latest after-audit predates the latest before-audit; refusing to compare mismatched runs.' }
if (($after.LastWriteTime - $before.LastWriteTime).TotalMinutes -gt 5) { throw 'Latest before/after audits are too far apart to trust as one experiment pair.' }

$beforeAudit = Get-Content $before.FullName -Raw | ConvertFrom-Json
$afterAudit = Get-Content $after.FullName -Raw | ConvertFrom-Json

$plans = @($beforeAudit.visual.repair_plan.plans)
if ($plans.Count -ne 1) { throw "Expected one baseline plan in the matched audit; found $($plans.Count)." }
$plan = $plans[0]
$topId = [string]$plan.top_level_element_id
$sourceTargetId = [string]$plan.source_target_id
$side = [string]$plan.changed_component
$expectedAfter = [double]$plan.proposed_value.$side

$beforeSections = @($beforeAudit.visual.render_metrics.sections | Where-Object { [string]$_.top_level_element_id -eq $topId })
$afterSections = @($afterAudit.visual.render_metrics.sections | Where-Object { [string]$_.top_level_element_id -eq $topId })
if ($beforeSections.Count -ne 1 -or $afterSections.Count -ne 1) { throw 'Target section metrics were not uniquely available in the matched before/after audits.' }

$b = $beforeSections[0]
$a = $afterSections[0]
$beforePadding = [double]$b.padding.$side
$afterPadding = [double]$a.padding.$side
$paddingMovedAsPlanned = [Math]::Abs($afterPadding - $expectedAfter) -le 1.0
$sectionHeightDidNotIncrease = [double]$a.height -le ([double]$b.height + 1.0)
$gapBeforeDidNotIncrease = [double]$a.gap_before -le ([double]$b.gap_before + 1.0)
$gapAfterDidNotIncrease = [double]$a.gap_after -le ([double]$b.gap_after + 1.0)

$afterPromoted = @($afterAudit.visual.repair_convergence.promoted_targets)
$sameTargetStillPromoted = @($afterPromoted | Where-Object { [string]$_.target_id -eq $sourceTargetId }).Count -gt 0
$afterPlans = @($afterAudit.visual.repair_plan.plans)
$sameTargetStillPlanned = @($afterPlans | Where-Object { [string]$_.source_target_id -eq $sourceTargetId }).Count -gt 0

$focusedAvailable = [bool]$afterAudit.visual.focused_verification.available
$sectionAvailable = [bool]$afterAudit.visual.focused_section_verification.available
$renderMetricsAvailable = [bool]$afterAudit.visual.render_metrics.available

$gates = [ordered]@{
    rendered_target_moved_as_planned = $paddingMovedAsPlanned
    section_height_did_not_increase = $sectionHeightDidNotIncrease
    gap_before_did_not_increase = $gapBeforeDidNotIncrease
    gap_after_did_not_increase = $gapAfterDidNotIncrease
    same_target_no_longer_promoted = (-not $sameTargetStillPromoted)
    same_target_no_longer_planned = (-not $sameTargetStillPlanned)
    render_metrics_available_after = $renderMetricsAvailable
    focused_pipeline_available_after = $focusedAvailable
    section_focus_pipeline_available_after = $sectionAvailable
}

$allPass = $true
foreach ($entry in $gates.GetEnumerator()) {
    if (-not [bool]$entry.Value) { $allPass = $false }
}

$decision = if ($allPass) { 'WOULD_KEEP' } else { 'WOULD_ROLLBACK' }

Write-Host ''
Write-Host '--- Shadow autonomous outcome decision ---' -ForegroundColor Cyan
Write-Host ("Target: {0} / {1} padding" -f $topId, $side)
Write-Host ("Rendered move: {0}px -> {1}px (planned {2}px)" -f $beforePadding, $afterPadding, $expectedAfter)
Write-Host ''

$rows = foreach ($entry in $gates.GetEnumerator()) {
    [pscustomobject]@{
        Gate = $entry.Key
        Pass = [bool]$entry.Value
    }
}
$rows | Format-Table -AutoSize

if ($decision -eq 'WOULD_KEEP') {
    Write-Host ("[SHADOW DECISION] {0}" -f $decision) -ForegroundColor Green
    Write-Host 'The temporary repair satisfied every current conservative keep gate. The underlying experiment already restored the original value, so this is decision proof only.' -ForegroundColor Green
} else {
    Write-Host ("[SHADOW DECISION] {0}" -f $decision) -ForegroundColor Yellow
    Write-Host 'At least one conservative keep gate failed. The underlying experiment already restored the original value.' -ForegroundColor Yellow
}

Write-Host ''
Write-Host ("Before audit: {0}" -f $before.FullName) -ForegroundColor DarkGray
Write-Host ("After audit:  {0}" -f $after.FullName) -ForegroundColor DarkGray
Write-Host 'No publish action was performed. No temporary repair was kept.' -ForegroundColor Cyan

param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER,

    [Parameter(Mandatory = $false)]
    [string]$TargetMarker = 'S1',

    [Parameter(Mandatory = $false)]
    [ValidateRange(2,4)]
    [int]$ShortlistLimit = 4
)

$ErrorActionPreference = 'Stop'
$inner = Join-Path $PSScriptRoot 'test-aesthetic-semantic-shortlist.ps1'
if (-not (Test-Path $inner)) { throw 'Base semantic shortlist harness is missing.' }

& $inner -PageId $PageId -SiteUrl $SiteUrl -Username $Username -TargetMarker $TargetMarker -ShortlistLimit $ShortlistLimit
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$target = $TargetMarker.ToUpperInvariant()
$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
$file = Get-ChildItem (Join-Path $outDir "aesthetic-semantic-shortlist-$PageId-$target-*.json") |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($null -eq $file) { throw 'Integrated semantic shortlist audit was not found after the base harness completed.' }

$j = Get-Content $file.FullName -Raw | ConvertFrom-Json
$c = $j.aesthetic_comparison
$r = $c.semantic_visual_tie_resolution
$cal = $c.judgment_calibration

Write-Host ''
Write-Host '--- Final semantic + visual selection resolution ---' -ForegroundColor Cyan
if ($null -eq $r) {
    Write-Host '[INFO] Semantic visual tie resolution was not required for this run.' -ForegroundColor DarkGray
} else {
    Write-Host ("[INFO] Tie resolution: attempted={0}; applied={1}; trigger={2}; visualScoreModified={3}; semanticGap={4}" -f $r.attempted, $r.applied, $r.trigger, $r.visual_score_modified, $r.semantic_score_gap)
    if (-not [bool]$r.applied -and -not [string]::IsNullOrWhiteSpace([string]$r.reason)) {
        Write-Host ("[WARN] Tie resolution reason: {0}" -f $r.reason) -ForegroundColor Yellow
    }
}

[pscustomobject]@{
    CalibratedVisualTopScore = [int]$cal.top_score
    CalibratedVisualRunnerUpScore = [int]$cal.runner_up_score
    CalibratedVisualTie = [bool]$cal.tied_top_score
    SemanticVisualTieResolutionApplied = $null -ne $r -and [bool]$r.applied
    SemanticWinnerTemplateId = if ($null -ne $r) { [string]$r.semantic_winner_template_id } else { '' }
    SemanticWinnerScore = if ($null -ne $r) { [int]$r.semantic_winner_score } else { 0 }
    SemanticRunnerUpTemplateId = if ($null -ne $r) { [string]$r.semantic_runner_up_template_id } else { '' }
    SemanticRunnerUpScore = if ($null -ne $r) { [int]$r.semantic_runner_up_score } else { 0 }
    SemanticScoreGap = if ($null -ne $r) { [int]$r.semantic_score_gap } else { 0 }
    EffectiveWinnerTemplateId = [string]$c.winner_template_id
    EffectiveSelectionScore = [int]$c.effective_selection_score
    EffectiveConfidence = [string]$c.confidence
    EffectiveUsableForSelection = [bool]$c.usable_for_selection
    SelectionBasis = [string]$c.selection_basis
    WritesPerformed = [bool]$c.writes_performed
    AutomaticWriteAllowed = [bool]$c.automatic_write_allowed
} | Format-List

if ($null -ne $r -and [bool]$r.applied) {
    if ([bool]$r.visual_score_modified) { throw 'Semantic visual tie resolution modified a visual score, which violates the acceptance contract.' }
    if (-not [bool]$r.candidate_set_exact_match) { throw 'Semantic visual tie resolution did not prove exact candidate-set agreement.' }
    if ([int]$r.semantic_score_gap -lt 5) { throw 'Semantic visual tie resolution applied below the minimum semantic gap.' }
    if (-not [bool]$c.usable_for_selection) { throw 'Tie resolution applied but the final read-only selection is not usable.' }
    Write-Host ("[PASS] Visual scores remained untouched and exact-state semantic structure safely resolved the tie: {0}." -f $c.winner_template_id) -ForegroundColor Green
} elseif ([bool]$cal.tied_top_score) {
    Write-Host '[PASS] The visual tie remained unresolved because the semantic tie-break contract was not strong enough. No winner was forced.' -ForegroundColor Yellow
} else {
    Write-Host '[PASS] Visual calibration itself established the selection; semantic tie resolution was not needed.' -ForegroundColor Green
}

Write-Host ''
Write-Host 'Done. This wrapper called only the existing read-only shortlist/ranking harness. No insert or write endpoint was called.' -ForegroundColor Cyan

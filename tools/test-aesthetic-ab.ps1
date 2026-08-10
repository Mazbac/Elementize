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
    [string[]]$TemplateIds = @('ai-agency-home-intro-video', 'app-intro-left')
)

$ErrorActionPreference = 'Stop'

function Get-PlainTextFromSecureString {
    param([Security.SecureString]$SecureString)
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

function New-BasicAuthHeader {
    param([string]$User, [Security.SecureString]$Password)
    $plain = Get-PlainTextFromSecureString $Password
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes("$User`:$plain")
        return 'Basic ' + [Convert]::ToBase64String($bytes)
    }
    finally { $plain = $null }
}

function Invoke-ElementizeCurl {
    param(
        [ValidateSet('GET','POST')]
        [string]$Method,
        [string]$Uri,
        [string]$Authorization,
        [string]$JsonBody = ''
    )

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) { throw 'curl.exe is required for the local aesthetic A/B harness.' }

    $tempBody = $null
    if ($Method -eq 'POST') {
        $tempBody = [IO.Path]::GetTempFileName()
        [IO.File]::WriteAllText($tempBody, $JsonBody, (New-Object Text.UTF8Encoding($false)))
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $curl.Source
    $psi.Arguments = '--config -'
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    if (-not $proc.Start()) { throw 'Could not start curl.exe.' }

    try {
        $proc.StandardInput.WriteLine('silent')
        $proc.StandardInput.WriteLine('show-error')
        $proc.StandardInput.WriteLine('insecure')
        $proc.StandardInput.WriteLine('header = "Authorization: ' + $Authorization + '"')
        $proc.StandardInput.WriteLine('header = "Accept: application/json"')
        if ($Method -eq 'POST') {
            $proc.StandardInput.WriteLine('request = "POST"')
            $proc.StandardInput.WriteLine('header = "Content-Type: application/json"')
            $path = $tempBody.Replace('\','/')
            $proc.StandardInput.WriteLine('data-binary = "@' + $path + '"')
        }
        $proc.StandardInput.WriteLine('url = "' + $Uri + '"')
        $proc.StandardInput.WriteLine('write-out = "\n__ELEMENTIZE_HTTP_CODE__:%{http_code}"')
        $proc.StandardInput.Close()

        $stdout = $proc.StandardOutput.ReadToEnd()
        $stderr = $proc.StandardError.ReadToEnd()
        $proc.WaitForExit()
        if ($proc.ExitCode -ne 0) {
            $safe = if ([string]::IsNullOrWhiteSpace($stderr)) { "curl.exe failed with exit code $($proc.ExitCode)." } else { $stderr.Trim() }
            throw $safe
        }

        $marker = '__ELEMENTIZE_HTTP_CODE__:'
        $idx = $stdout.LastIndexOf($marker)
        if ($idx -lt 0) { throw 'curl.exe returned no HTTP status marker.' }
        $body = $stdout.Substring(0, $idx).Trim()
        $codeText = $stdout.Substring($idx + $marker.Length).Trim()
        $httpCode = 0
        [void][int]::TryParse($codeText, [ref]$httpCode)
        if ($httpCode -lt 200 -or $httpCode -ge 300) {
            $snippet = if ($body.Length -gt 1200) { $body.Substring(0, 1200) + '...' } else { $body }
            throw "Elementize returned HTTP $httpCode. $snippet"
        }
        if ([string]::IsNullOrWhiteSpace($body)) { throw 'Elementize returned an empty response body.' }
        return $body | ConvertFrom-Json
    }
    finally {
        if (-not $proc.HasExited) { try { $proc.Kill() } catch {} }
        $proc.Dispose()
        if ($null -ne $tempBody -and (Test-Path $tempBody)) { Remove-Item $tempBody -Force -ErrorAction SilentlyContinue }
    }
}

if ([string]::IsNullOrWhiteSpace($Username)) { $Username = Read-Host 'WordPress username' }
if ([string]::IsNullOrWhiteSpace($Username)) { throw 'A WordPress username is required.' }
if ($TemplateIds.Count -lt 2 -or $TemplateIds.Count -gt 4) { throw 'TemplateIds must contain between 2 and 4 Pixfort section IDs.' }
if ($TargetMarker -notmatch '^S\d{1,2}$') { throw 'TargetMarker must look like S1, S2, or S10.' }

$securePassword = if (-not [string]::IsNullOrWhiteSpace($env:ELEMENTIZE_WP_APP_PASSWORD)) {
    ConvertTo-SecureString $env:ELEMENTIZE_WP_APP_PASSWORD -AsPlainText -Force
} else {
    Read-Host 'WordPress Application Password (input hidden; do not paste it into ChatGPT)' -AsSecureString
}

$auth = New-BasicAuthHeader -User $Username -Password $securePassword
$base = $SiteUrl.TrimEnd('/')
$statusUri = "$base/wp-json/elementize/v1/status"
$auditUri = "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true"
$probeUri = "$base/wp-json/elementize/v1/pixfort/visual-probe"

Write-Host ''
Write-Host 'Elementize contextual aesthetic A/B runtime test' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host "Target marker: $TargetMarker"
Write-Host ("Candidates: {0}" -f ($TemplateIds -join ', '))
Write-Host 'Policy: read-only candidate comparison through the existing visual-probe action; no insertion or write endpoint is called.'
Write-Host ''

$status = Invoke-ElementizeCurl -Method GET -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green
Write-Host ("[INFO] A/B judgment: {0}; action slot cost: {1}; automatic writes: {2}" -f $status.aesthetic_ab_judgment_version, $status.aesthetic_ab_judgment_action_slot_cost, $status.aesthetic_ab_automatic_write_allowed)
if ($status.aesthetic_ab_judgment_calibration) {
    Write-Host ("[INFO] A/B calibration: {0}; absolute selection score floor: {1}/10; confidence can only decrease: {2}" -f $status.aesthetic_ab_judgment_calibration_version, $status.aesthetic_ab_absolute_selection_score_floor, $status.aesthetic_ab_calibration_confidence_can_only_decrease)
}

Write-Host '[STEP] Refreshing grounded page context...' -ForegroundColor DarkCyan
$auditStarted = Get-Date
$audit = Invoke-ElementizeCurl -Method GET -Uri $auditUri -Authorization $auth
$auditSeconds = [Math]::Round(((Get-Date) - $auditStarted).TotalSeconds, 2)
$context = $audit.visual.aesthetic_ab_context
if ($null -eq $context -or -not $context.available) {
    $reason = if ($null -ne $context) { [string]$context.reason } else { 'aesthetic_ab_context was not returned.' }
    throw "Grounded A/B context unavailable: $reason"
}
Write-Host ("[PASS] Fresh grounded context cached in {0}s for {1} markers." -f $auditSeconds, $context.marker_count) -ForegroundColor Green

$body = [ordered]@{
    template_ids = @($TemplateIds)
    aesthetic_compare = $true
    context_page_id = $PageId
    target_marker = $TargetMarker.ToUpperInvariant()
} | ConvertTo-Json -Depth 5 -Compress

Write-Host '[STEP] Running local relative candidate judgment...' -ForegroundColor DarkCyan
$compareStarted = Get-Date
$probe = Invoke-ElementizeCurl -Method POST -Uri $probeUri -Authorization $auth -JsonBody $body
$compareSeconds = [Math]::Round(((Get-Date) - $compareStarted).TotalSeconds, 2)
$comparison = $probe.aesthetic_comparison
if ($null -eq $comparison) { throw 'The visual-probe response did not return aesthetic_comparison.' }
$calibration = $comparison.judgment_calibration

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rawPath = Join-Path $outDir "aesthetic-ab-$PageId-$($TargetMarker.ToUpperInvariant())-$stamp.json"
[pscustomobject]@{
    mapping = $probe.mapping
    aesthetic_comparison = $comparison
} | ConvertTo-Json -Depth 100 | Set-Content -Path $rawPath -Encoding UTF8
Write-Host ("[SAVE] Comparison audit: {0}" -f $rawPath) -ForegroundColor DarkGray

Write-Host ''
Write-Host '--- Contextual aesthetic comparison ---' -ForegroundColor Cyan
$comparison | ConvertTo-Json -Depth 100

Write-Host ''
Write-Host '--- Compact A/B health ---' -ForegroundColor Cyan
[pscustomobject]@{
    Version = [string]$comparison.version
    Available = [bool]$comparison.available
    AssessmentComplete = [bool]$comparison.assessment_complete
    ExactCandidateCoverage = [bool]$comparison.exact_candidate_coverage
    CandidateCount = $comparison.candidate_count
    ModelWinnerSlot = [string]$comparison.model_winner_slot
    WinnerSlot = [string]$comparison.winner_slot
    WinnerTemplateId = [string]$comparison.winner_template_id
    ModelConfidence = [string]$comparison.model_confidence
    Confidence = [string]$comparison.confidence
    ModelWinnerMargin = [string]$comparison.model_winner_margin
    WinnerMargin = [string]$comparison.winner_margin
    UsableForSelection = [bool]$comparison.usable_for_selection
    JudgmentCalibrated = [bool]$calibration.applied
    WinnerScore = $calibration.winner_score
    AbsoluteSelectionScoreFloor = $calibration.absolute_selection_score_floor
    WinnerMeetsAbsoluteFitFloor = [bool]$calibration.winner_meets_absolute_fit_floor
    ScoreGap = $calibration.score_gap
    ConsistencyConflictCount = $calibration.consistency_conflict_count
    RequiresPostInsertVerification = [bool]$comparison.requires_post_insert_visual_verification
    ReusesVisualProbeAction = [bool]$comparison.reuses_existing_visual_probe_action
    ActionSlotCost = $comparison.action_slot_cost
    WritesPerformed = [bool]$comparison.writes_performed
    AutomaticWriteAllowed = [bool]$comparison.automatic_write_allowed
    ContextGroundingSource = [string]$comparison.context_grounding_source
    CompareSeconds = $compareSeconds
} | Format-List

Write-Host ''
Write-Host '--- Candidate ranking ---' -ForegroundColor Cyan
@($comparison.candidate_judgments) | Select-Object slot, template_id, score, overall_fit, design_dna_fit, rhythm_fit, hierarchy_fit, conversion_fit | Format-Table -AutoSize

if ($null -ne $calibration -and $calibration.applied) {
    Write-Host ''
    Write-Host '--- Deterministic judgment calibration ---' -ForegroundColor Cyan
    [pscustomobject]@{
        Version = [string]$calibration.version
        ModelWinner = [string]$calibration.model_winner_slot
        EffectiveWinner = [string]$calibration.effective_winner_slot
        ModelConfidence = [string]$calibration.model_confidence
        EffectiveConfidence = [string]$calibration.effective_confidence
        ModelMargin = [string]$calibration.model_winner_margin
        EffectiveMargin = [string]$calibration.effective_winner_margin
        TopScore = $calibration.top_score
        RunnerUpScore = $calibration.runner_up_score
        ScoreGap = $calibration.score_gap
        WinnerScore = $calibration.winner_score
        WinnerMeetsFloor = [bool]$calibration.winner_meets_absolute_fit_floor
        SevereWinnerConflict = [bool]$calibration.winner_severe_consistency_conflict
        ConsistencyConflictCount = $calibration.consistency_conflict_count
        ModelUsable = [bool]$calibration.model_usable_for_selection
        EffectiveUsable = [bool]$calibration.effective_usable_for_selection
    } | Format-List
    if (@($calibration.calibration_reasons).Count -gt 0) {
        Write-Host 'Calibration reasons:' -ForegroundColor DarkCyan
        @($calibration.calibration_reasons) | ForEach-Object { Write-Host (" - {0}" -f $_) }
    }
}

$calibrationApplied = $null -ne $calibration -and [bool]$calibration.applied
if ($comparison.available -and $comparison.assessment_complete -and $comparison.exact_candidate_coverage -and $calibrationApplied -and -not $comparison.writes_performed -and -not $comparison.automatic_write_allowed) {
    Write-Host '[PASS] Contextual A/B judgment produced a complete read-only candidate comparison with deterministic calibration.' -ForegroundColor Green
    if ($comparison.usable_for_selection) {
        Write-Host ("[PASS] Calibrated read-only selection recommendation: {0} -> {1} ({2} confidence)." -f $comparison.winner_slot, $comparison.winner_template_id, $comparison.confidence) -ForegroundColor Green
    } elseif (-not [string]::IsNullOrWhiteSpace([string]$comparison.winner_slot) -and [string]$comparison.winner_slot -ne 'none') {
        Write-Host ("[INFO] Relative winner preserved: {0} -> {1}, but deterministic calibration intentionally blocks selection usability." -f $comparison.winner_slot, $comparison.winner_template_id) -ForegroundColor Yellow
    } else {
        Write-Host '[INFO] Comparison completed but calibration retained no winner safe for selection.' -ForegroundColor Yellow
    }
} else {
    Write-Host ("[FAIL] A/B comparison unavailable, incomplete, or uncalibrated: {0}" -f $comparison.reason) -ForegroundColor Red
}

Write-Host ''
Write-Host 'Done. No write endpoint was called and no page state was changed.' -ForegroundColor Cyan

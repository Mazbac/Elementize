param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER
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

function Invoke-ElementizeCurlGet {
    param([string]$Uri, [string]$Authorization)
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) { throw 'curl.exe is required for the local Aesthetic Brain harness.' }

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
            $snippet = if ($body.Length -gt 900) { $body.Substring(0, 900) + '...' } else { $body }
            throw "Elementize returned HTTP $httpCode. $snippet"
        }
        if ([string]::IsNullOrWhiteSpace($body)) { throw 'Elementize returned an empty response body.' }
        return $body | ConvertFrom-Json
    }
    finally {
        if (-not $proc.HasExited) { try { $proc.Kill() } catch {} }
        $proc.Dispose()
    }
}

if ([string]::IsNullOrWhiteSpace($Username)) { $Username = Read-Host 'WordPress username' }
if ([string]::IsNullOrWhiteSpace($Username)) { throw 'A WordPress username is required.' }

$securePassword = if (-not [string]::IsNullOrWhiteSpace($env:ELEMENTIZE_WP_APP_PASSWORD)) {
    ConvertTo-SecureString $env:ELEMENTIZE_WP_APP_PASSWORD -AsPlainText -Force
} else {
    Read-Host 'WordPress Application Password (input hidden; do not paste it into ChatGPT)' -AsSecureString
}

$auth = New-BasicAuthHeader -User $Username -Password $securePassword
$base = $SiteUrl.TrimEnd('/')
$statusUri = "$base/wp-json/elementize/v1/status"
$auditUri = "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true"

Write-Host ''
Write-Host 'Elementize Aesthetic Brain v1 runtime test' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host 'Policy: read-only professional art-direction judgment; no write endpoint is called.'
Write-Host ''

$status = Invoke-ElementizeCurlGet -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green
Write-Host ("[INFO] Aesthetic Brain: {0}; coverage recovery: {1}; hardening: {2}; calibration: {3}" -f $status.aesthetic_brain_version, $status.aesthetic_coverage_recovery_version, $status.aesthetic_brain_hardening_version, $status.aesthetic_judgment_calibration_version)

$started = Get-Date
$audit = Invoke-ElementizeCurlGet -Uri $auditUri -Authorization $auth
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
Write-Host ("[PASS] Completion audit returned in {0}s" -f $seconds) -ForegroundColor Green

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rawPath = Join-Path $outDir "aesthetic-brain-$PageId-$stamp.json"
$audit | ConvertTo-Json -Depth 100 | Set-Content -Path $rawPath -Encoding UTF8
Write-Host ("[SAVE] Full audit: {0}" -f $rawPath) -ForegroundColor DarkGray

$visual = $audit.visual
if ($null -eq $visual) { throw 'The completion audit did not return a visual object.' }

Write-Host ''
Write-Host '--- visual.design_dna ---' -ForegroundColor Cyan
$visual.design_dna | ConvertTo-Json -Depth 100

Write-Host ''
Write-Host '--- visual.page_art_direction ---' -ForegroundColor Cyan
$visual.page_art_direction | ConvertTo-Json -Depth 100

Write-Host ''
Write-Host '--- visual.section_coherence ---' -ForegroundColor Cyan
$visual.section_coherence | ConvertTo-Json -Depth 100

Write-Host ''
Write-Host '--- visual.aesthetic_coverage_recovery ---' -ForegroundColor Cyan
$visual.aesthetic_coverage_recovery | ConvertTo-Json -Depth 100

Write-Host ''
Write-Host '--- visual.aesthetic_judgment_calibration ---' -ForegroundColor Cyan
$visual.aesthetic_judgment_calibration | ConvertTo-Json -Depth 100

Write-Host ''
Write-Host '--- Compact aesthetic health ---' -ForegroundColor Cyan
$art = $visual.page_art_direction
$coherence = $visual.section_coherence
$dna = $visual.design_dna
$recovery = $visual.aesthetic_coverage_recovery
$calibration = $visual.aesthetic_judgment_calibration

[pscustomobject]@{
    AestheticBrainAvailable = [bool]$audit.summary.aesthetic_brain_available
    DesignDNAAvailable = [bool]$dna.available
    ArtDirectionAvailable = [bool]$art.available
    SectionCoherenceAvailable = [bool]$coherence.available
    ModelProfessionalApproval = [string]$art.model_professional_approval
    EffectiveProfessionalApproval = [string]$art.effective_professional_approval
    ApprovalUsable = [bool]$art.approval_usable
    RawAestheticScore = $art.aesthetic_score
    CalibratedAestheticScore = $art.calibrated_aesthetic_score
    RawCoherenceScore = $art.coherence_score
    CalibratedCoherenceScore = $art.calibrated_coherence_score
    RawPolishScore = $art.polish_score
    CalibratedPolishScore = $art.calibrated_polish_score
    RawConversionScore = $art.conversion_focus_score
    CalibratedConversionScore = $art.calibrated_conversion_focus_score
    ModelSectionCoherence = [string]$coherence.model_coherence_verdict
    EffectiveSectionCoherence = [string]$coherence.effective_coherence_verdict
    ModelDesignSystemCoherence = [string]$dna.model_design_system_coherence
    EffectiveDesignSystemCoherence = [string]$dna.effective_design_system_coherence
    CoveredSections = $coherence.covered_section_count
    SectionCoverageRatio = $coherence.section_coverage_ratio
    IssueBearingSections = $art.issue_bearing_section_count
    IssueBearingSectionRatio = $art.issue_bearing_section_ratio
    HighSeverityIssues = $art.high_severity_issue_count
    MediumSeverityIssues = $art.medium_severity_issue_count
    CoverageRecoveryAvailable = [bool]$recovery.available
    CoverageRecoveryApproval = [string]$recovery.overall_approval
    CrossCheckRevisionRequired = [bool]$art.revision_required_by_cross_checks
    EvidenceCrossChecks = @($art.evidence_cross_checks).Count
    CalibrationAvailable = [bool]$calibration.available
    WritesPerformed = [bool]$art.writes_performed
    AutomaticWriteAllowed = [bool]$art.automatic_write_allowed
} | Format-List

Write-Host ''
Write-Host '--- Calibrated interventions ---' -ForegroundColor Cyan
$calibratedIssues = @($art.calibrated_issues)
if ($calibratedIssues.Count -eq 0) {
    Write-Host '[INFO] No calibrated aesthetic issues returned.' -ForegroundColor DarkGray
}
else {
    $calibratedIssues | Select-Object category, severity, @{N='sections';E={@($_.section_markers) -join ','}}, model_intervention_level, calibrated_intervention_level | Format-Table -AutoSize
}

if ($art.available -and $art.approval_usable -and $calibration.available) {
    Write-Host '[PASS] Aesthetic Brain produced a complete, evidence-covered, calibrated page-level judgment.' -ForegroundColor Green
}
elseif ($art.available) {
    Write-Host '[WARN] Aesthetic Brain returned model output, but the effective calibrated professional judgment is not yet usable.' -ForegroundColor Yellow
}
else {
    Write-Host ("[FAIL] Aesthetic Brain unavailable: {0}" -f $art.reason) -ForegroundColor Red
}

Write-Host ''
Write-Host 'Done. No write endpoint was called and no page state was changed.' -ForegroundColor Cyan

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

function Invoke-ElementizeCurl {
    param(
        [string]$Uri,
        [string]$Authorization
    )

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) { throw 'curl.exe is required for this diagnostic harness.' }

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
            $snippet = if ($body.Length -gt 1600) { $body.Substring(0, 1600) + '...' } else { $body }
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

function Write-Eligibility {
    param(
        [string]$Title,
        $Eligibility
    )

    Write-Host ''
    Write-Host $Title -ForegroundColor Cyan
    if ($null -eq $Eligibility) {
        Write-Host '(not returned)' -ForegroundColor DarkGray
        return
    }

    $Eligibility.PSObject.Properties | ForEach-Object {
        $value = $_.Value
        if ($value -is [bool]) {
            $color = if ($value) { 'Green' } else { 'Red' }
            Write-Host ("{0,-48} : {1}" -f $_.Name, $value) -ForegroundColor $color
        } else {
            Write-Host ("{0,-48} : {1}" -f $_.Name, $value)
        }
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
$status = Invoke-ElementizeCurl -Uri "$base/wp-json/elementize/v1/status" -Authorization $auth

Write-Host ''
Write-Host 'Elementize A/B grounded-context diagnostics' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host ("Plugin version: {0}" -f $status.elementize_version)
Write-Host ("Context hardening: {0}" -f $status.aesthetic_ab_context_hardening_version)
Write-Host ("Context source stabilizer: {0}" -f $status.aesthetic_ab_context_source_stabilizer_version)
Write-Host 'Policy: diagnostic GET only; no write endpoint is called.'
Write-Host ''
Write-Host '[STEP] Running fresh visual completion audit...' -ForegroundColor DarkCyan

$audit = Invoke-ElementizeCurl -Uri "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true" -Authorization $auth
$visual = $audit.visual
$context = $visual.aesthetic_ab_context
$readiness = $context.context_readiness_recovery
$stabilizer = $context.context_source_stabilization
$semantic = $visual.aesthetic_semantic_grounding
$reassessment = $visual.aesthetic_grounded_reassessment
$coherence = $visual.section_coherence
$art = $visual.page_art_direction

Write-Host ''
Write-Host '--- Final A/B context ---' -ForegroundColor Cyan
[pscustomobject]@{
    Available = [bool]$context.available
    Reason = [string]$context.reason
    GroundingSource = [string]$context.grounding_source
    MarkerCount = $context.marker_count
    ContextEvidenceComplete = [bool]$context.context_evidence_complete
} | Format-List

Write-Host '--- 0.27.2 context readiness recovery ---' -ForegroundColor Cyan
[pscustomobject]@{
    Version = [string]$readiness.version
    Attempted = [bool]$readiness.attempted
    Succeeded = [bool]$readiness.succeeded
    Reason = [string]$readiness.reason
    ReassessmentConfidence = [string]$readiness.reassessment_confidence
    OverallApprovalUsable = [bool]$readiness.overall_approval_usable
} | Format-List
Write-Eligibility -Title '0.27.2 eligibility checks' -Eligibility $readiness.eligibility

Write-Host ''
Write-Host '--- 0.27.4 context source stabilizer ---' -ForegroundColor Cyan
[pscustomobject]@{
    Version = [string]$stabilizer.version
    Attempted = [bool]$stabilizer.attempted
    Succeeded = [bool]$stabilizer.succeeded
    Reason = [string]$stabilizer.reason
    ReassessmentNeeded = $stabilizer.reassessment_needed
    Source = [string]$stabilizer.source
} | Format-List
Write-Eligibility -Title '0.27.4 semantic-source eligibility checks' -Eligibility $stabilizer.eligibility

Write-Host ''
Write-Host '--- Semantic grounding health ---' -ForegroundColor Cyan
[pscustomobject]@{
    Version = [string]$semantic.version
    Available = [bool]$semantic.available
    ApprovalGroundingComplete = [bool]$semantic.approval_grounding_complete
    SectionFactCount = $semantic.section_fact_count
    MarkerCount = $semantic.marker_count
    SectionGroundingCount = @($semantic.section_grounding).Count
    SemanticConflictCount = $semantic.semantic_conflict_count
    MaterialSemanticConflictCount = $semantic.material_semantic_conflict_count
    TransitionContractValid = [bool]$semantic.transition_contract_valid
    RhythmJudgmentUsable = [bool]$semantic.rhythm_judgment_usable
} | Format-List

Write-Host ''
Write-Host '--- Grounded reassessment health ---' -ForegroundColor Cyan
[pscustomobject]@{
    Version = [string]$reassessment.version
    Available = [bool]$reassessment.available
    Needed = $reassessment.needed
    AssessmentComplete = [bool]$reassessment.assessment_complete
    ExactSectionCoverageComplete = [bool]$reassessment.exact_section_coverage_complete
    SectionFactCount = $reassessment.section_fact_count
    SectionLedgerCount = @($reassessment.section_ledger).Count
    SemanticConflictCount = $reassessment.semantic_conflict_count
    TransitionContractValid = [bool]$reassessment.transition_contract_valid
    Confidence = [string]$reassessment.confidence
    Reason = [string]$reassessment.reason
} | Format-List

Write-Host ''
Write-Host '--- Shared gate health ---' -ForegroundColor Cyan
[pscustomobject]@{
    ArtApprovalGroundingComplete = [bool]$art.approval_grounding_complete
    ArtApprovalUsable = [bool]$art.approval_usable
    CoherenceTransitionContractValid = [bool]$coherence.transition_contract_valid
    CoherenceRhythmJudgmentUsable = [bool]$coherence.rhythm_judgment_usable
} | Format-List

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rawPath = Join-Path $outDir "aesthetic-ab-context-diagnostics-$PageId-$stamp.json"
[pscustomobject]@{
    context = $context
    semantic_grounding = $semantic
    grounded_reassessment = $reassessment
    section_coherence = $coherence
    page_art_direction = $art
    summary = $audit.summary
} | ConvertTo-Json -Depth 100 | Set-Content -Path $rawPath -Encoding UTF8

Write-Host ''
Write-Host ("[SAVE] Diagnostic audit: {0}" -f $rawPath) -ForegroundColor DarkGray
Write-Host 'Done. Diagnostic GET only; no write endpoint was called and no page state was changed.' -ForegroundColor Cyan

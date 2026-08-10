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
    if ($null -eq $curl) { throw 'curl.exe is required for the semantic shortlist harness.' }

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
            $snippet = if ($body.Length -gt 1600) { $body.Substring(0, 1600) + '...' } else { $body }
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
if ($TargetMarker -notmatch '^S\d{1,2}$') { throw 'TargetMarker must look like S1, S2, or S10.' }

$securePassword = if (-not [string]::IsNullOrWhiteSpace($env:ELEMENTIZE_WP_APP_PASSWORD)) {
    ConvertTo-SecureString $env:ELEMENTIZE_WP_APP_PASSWORD -AsPlainText -Force
} else {
    Read-Host 'WordPress Application Password (input hidden; do not paste it into ChatGPT)' -AsSecureString
}

$auth = New-BasicAuthHeader -User $Username -Password $securePassword
$base = $SiteUrl.TrimEnd('/')
$target = $TargetMarker.ToUpperInvariant()
$statusUri = "$base/wp-json/elementize/v1/status"
$auditUri = "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true"
$shortlistUri = "$base/wp-json/elementize/v1/pixfort/templates?type=section&page=1&per_page=25&semantic_shortlist=true&context_page_id=$PageId&target_marker=$target&shortlist_limit=$ShortlistLimit"
$probeUri = "$base/wp-json/elementize/v1/pixfort/visual-probe"

Write-Host ''
Write-Host 'Elementize semantic shortlist -> calibrated visual ranking test' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host "Target marker: $target"
Write-Host "Shortlist limit: $ShortlistLimit"
Write-Host 'Policy: read-only catalogue discovery, real template-structure inspection, deterministic plateau hardening, and visual ranking. No insert/write endpoint is called.'
Write-Host ''

$status = Invoke-ElementizeCurl -Method GET -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green
Write-Host ("[INFO] Semantic shortlist: {0}; structure-grounded: {1}; metadata discovery only: {2}; max candidates: {3}; automatic writes: {4}" -f $status.aesthetic_semantic_shortlist_version, $status.aesthetic_semantic_shortlist_structure_grounded, $status.aesthetic_semantic_shortlist_metadata_discovery_only, $status.aesthetic_semantic_shortlist_max_candidates, $status.aesthetic_semantic_shortlist_automatic_write_allowed)
Write-Host ("[INFO] Independent visual scoring: {0}; anchored discrimination: {1}; calibration: {2}" -f $status.aesthetic_ab_independent_scoring_version, $status.aesthetic_ab_independent_discrimination_version, $status.aesthetic_ab_judgment_calibration_version)
Write-Host ("[INFO] Intensity plateau hardening: {0}; deterministic: {1}; pairwise comparison: {2}; automatic writes: {3}" -f $status.aesthetic_ab_intensity_plateau_hardening_version, $status.aesthetic_ab_intensity_plateau_hardening_deterministic, $status.aesthetic_ab_intensity_plateau_hardening_pairwise_comparison, $status.aesthetic_ab_intensity_plateau_hardening_automatic_write_allowed)
if (-not $status.aesthetic_semantic_shortlist) { throw 'Semantic shortlist capability is not active in plugin status.' }
if (-not $status.aesthetic_ab_intensity_plateau_hardening) { throw 'Deterministic intensity plateau hardening is not active in plugin status.' }

Write-Host '[STEP] Refreshing exact-state grounded context...' -ForegroundColor DarkCyan
$auditStarted = Get-Date
$audit = Invoke-ElementizeCurl -Method GET -Uri $auditUri -Authorization $auth
$auditSeconds = [Math]::Round(((Get-Date) - $auditStarted).TotalSeconds, 2)
$context = $audit.visual.aesthetic_ab_context
if ($null -eq $context -or -not $context.available) {
    $reason = if ($null -ne $context) { [string]$context.reason } else { 'aesthetic_ab_context was not returned.' }
    throw "Grounded A/B context unavailable: $reason"
}
$stateHash = [string]$context.page_state_hash
if ([string]::IsNullOrWhiteSpace($stateHash)) { throw 'Grounded context did not expose page_state_hash.' }
Write-Host ("[PASS] Grounded context ready in {0}s for {1} markers; state hash {2}..." -f $auditSeconds, $context.marker_count, $stateHash.Substring(0, [Math]::Min(12, $stateHash.Length))) -ForegroundColor Green

Write-Host '[STEP] Building semantic/structural Pixfort shortlist...' -ForegroundColor DarkCyan
$shortStarted = Get-Date
$catalogue = Invoke-ElementizeCurl -Method GET -Uri $shortlistUri -Authorization $auth
$shortSeconds = [Math]::Round(((Get-Date) - $shortStarted).TotalSeconds, 2)
$shortlist = $catalogue.semantic_shortlist
if ($null -eq $shortlist -or -not $shortlist.available) {
    $reason = if ($null -ne $shortlist) { [string]$shortlist.reason } else { 'semantic_shortlist was not returned.' }
    throw "Semantic shortlist unavailable: $reason"
}
if ([string]$shortlist.page_state_hash -ne $stateHash) { throw 'Semantic shortlist page_state_hash does not match the freshly grounded page state.' }
if (-not $shortlist.read_only -or $shortlist.writes_performed -or $shortlist.automatic_write_allowed) { throw 'Semantic shortlist violated the read-only contract.' }
$templateIds = @($shortlist.template_ids | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($templateIds.Count -lt 2 -or $templateIds.Count -gt 4) { throw "Semantic shortlist returned $($templateIds.Count) candidate IDs; expected 2-4." }
if (-not $shortlist.usable_for_visual_ranking) { throw 'Semantic shortlist is not marked usable_for_visual_ranking.' }
Write-Host ("[PASS] Semantic shortlist built in {0}s: role={1} ({2}); catalogue={3}; discovered={4}; inspected={5}; shortlisted={6}" -f $shortSeconds, $shortlist.target_role, $shortlist.target_role_confidence, $shortlist.catalogue_candidate_count, $shortlist.metadata_discovery_count, $shortlist.structurally_inspected_count, $shortlist.shortlist_count) -ForegroundColor Green

Write-Host ''
Write-Host '--- Semantic shortlist ---' -ForegroundColor Cyan
@($shortlist.candidates) | Select-Object template_id, semantic_fit_score, structural_match, role_requirements_met, dependency_risk, approximate_density, dominant_alignment, image_prominence, reason | Format-Table -AutoSize

Write-Host '[STEP] Running existing contextual visual ranking on shortlisted candidates...' -ForegroundColor DarkCyan
$body = [ordered]@{
    template_ids = $templateIds
    aesthetic_compare = $true
    context_page_id = $PageId
    target_marker = $target
} | ConvertTo-Json -Depth 6 -Compress
$rankStarted = Get-Date
$probe = Invoke-ElementizeCurl -Method POST -Uri $probeUri -Authorization $auth -JsonBody $body
$rankSeconds = [Math]::Round(((Get-Date) - $rankStarted).TotalSeconds, 2)
$comparison = $probe.aesthetic_comparison
if ($null -eq $comparison) { throw 'Visual probe did not return aesthetic_comparison.' }
$independent = $comparison.independent_candidate_scoring
$discrimination = $comparison.independent_discrimination
$plateau = $comparison.intensity_plateau_hardening
$calibration = $comparison.judgment_calibration
$complete = [bool]$comparison.available -and [bool]$comparison.assessment_complete -and [bool]$comparison.exact_candidate_coverage -and
    $null -ne $independent -and [bool]$independent.applied -and $null -ne $calibration -and [bool]$calibration.applied -and
    -not [bool]$comparison.writes_performed -and -not [bool]$comparison.automatic_write_allowed
if (-not $complete) { throw 'Shortlisted visual ranking did not complete the independent calibrated read-only comparison contract.' }
if ([string]$comparison.page_state_hash -ne $stateHash) { throw 'Visual ranking page_state_hash does not match the fresh grounded state.' }
if ([int]$comparison.candidate_count -ne $templateIds.Count) { throw 'Visual ranking candidate count does not match semantic shortlist count.' }

Write-Host ("[PASS] Visual ranking complete in {0}s across {1} semantic candidates." -f $rankSeconds, $templateIds.Count) -ForegroundColor Green
if ($null -ne $discrimination) {
    Write-Host ("[INFO] Anchored discrimination: attempted={0}; applied={1}; trigger={2}; top={3}; runner={4}; tied={5}" -f $discrimination.attempted, $discrimination.applied, $discrimination.trigger, $discrimination.top_score, $discrimination.runner_up_score, $discrimination.tied_top_score)
    if (-not [bool]$discrimination.applied -and -not [string]::IsNullOrWhiteSpace([string]$discrimination.reason)) {
        Write-Host ("[WARN] Anchored discrimination reason: {0}" -f $discrimination.reason) -ForegroundColor Yellow
    }
}
if ($null -ne $plateau) {
    Write-Host ("[INFO] Intensity plateau hardening: attempted={0}; applied={1}; trigger={2}; top={3}; runner={4}; tied={5}" -f $plateau.attempted, $plateau.applied, $plateau.trigger, $plateau.top_score, $plateau.runner_up_score, $plateau.tied_top_score)
    if (-not [bool]$plateau.applied -and -not [string]::IsNullOrWhiteSpace([string]$plateau.reason)) {
        Write-Host ("[WARN] Plateau hardening reason: {0}" -f $plateau.reason) -ForegroundColor Yellow
    }
    if ([bool]$plateau.applied) {
        Write-Host ''
        Write-Host '--- Deterministic intensity plateau measurements ---' -ForegroundColor Cyan
        @($plateau.candidate_measurements) | Select-Object template_id, model_estimated_visual_intensity, measured_visual_intensity_proxy, target_visual_intensity, measured_intensity_delta, model_intensity_penalty, measured_intensity_penalty, raw_axis_score_preserved, adjusted_float_score, effective_score | Format-Table -AutoSize
    }
}

Write-Host ''
Write-Host '--- Calibrated visual ranking ---' -ForegroundColor Cyan
@($comparison.candidate_judgments) | Sort-Object score -Descending | Select-Object slot, template_id, score, overall_fit, design_dna_fit, rhythm_fit, hierarchy_fit, conversion_fit, strength, risk | Format-Table -AutoSize

Write-Host ''
Write-Host '--- Integrated verdict ---' -ForegroundColor Cyan
$winner = [string]$comparison.winner_template_id
$winnerInShortlist = [string]::IsNullOrWhiteSpace($winner) -or $winner -eq 'none' -or $templateIds -contains $winner
[pscustomobject]@{
    TargetRole = [string]$shortlist.target_role
    TargetRoleConfidence = [string]$shortlist.target_role_confidence
    SemanticShortlistCount = $templateIds.Count
    SemanticShortlistUsableForVisualRanking = [bool]$shortlist.usable_for_visual_ranking
    VisualComparisonComplete = $complete
    IndependentScoringApplied = [bool]$independent.applied
    AnchoredDiscriminationAttempted = $null -ne $discrimination -and [bool]$discrimination.attempted
    AnchoredDiscriminationApplied = $null -ne $discrimination -and [bool]$discrimination.applied
    IntensityPlateauHardeningAttempted = $null -ne $plateau -and [bool]$plateau.attempted
    IntensityPlateauHardeningApplied = $null -ne $plateau -and [bool]$plateau.applied
    JudgmentCalibrationApplied = [bool]$calibration.applied
    WinnerTemplateId = $winner
    WinnerScore = [int]$calibration.winner_score
    Confidence = [string]$comparison.confidence
    WinnerMargin = [string]$comparison.winner_margin
    UsableForSelection = [bool]$comparison.usable_for_selection
    WinnerBelongsToSemanticShortlist = $winnerInShortlist
    RequiresPostInsertVisualVerification = [bool]$comparison.requires_post_insert_visual_verification
    WritesPerformed = [bool]$comparison.writes_performed
    AutomaticWriteAllowed = [bool]$comparison.automatic_write_allowed
} | Format-List
if (-not $winnerInShortlist) { throw 'Effective visual winner does not belong to the semantic shortlist.' }

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outPath = Join-Path $outDir "aesthetic-semantic-shortlist-$PageId-$target-$stamp.json"
[pscustomobject]@{
    page_id = $PageId
    target_marker = $target
    page_state_hash = $stateHash
    semantic_shortlist = $shortlist
    visual_mapping = $probe.mapping
    aesthetic_comparison = $comparison
} | ConvertTo-Json -Depth 100 | Set-Content -Path $outPath -Encoding UTF8
Write-Host ("[SAVE] Integrated audit: {0}" -f $outPath) -ForegroundColor DarkGray

if ($comparison.usable_for_selection -and -not [string]::IsNullOrWhiteSpace($winner) -and $winner -ne 'none') {
    Write-Host ("[PASS] Semantic shortlist and calibrated visual ranking agree on a read-only recommendation: {0}." -f $winner) -ForegroundColor Green
} else {
    Write-Host '[PASS] Semantic shortlist -> visual ranking pipeline completed safely, but calibration keeps the final recommendation advisory-only.' -ForegroundColor Yellow
}

Write-Host ''
Write-Host 'Done. No insert or write endpoint was called and no page state was changed.' -ForegroundColor Cyan

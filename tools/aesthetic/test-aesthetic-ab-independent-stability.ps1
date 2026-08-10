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
    [string[]]$TemplateIds = @('ai-agency-home-intro-video', 'app-intro-left'),

    [Parameter(Mandatory = $false)]
    [ValidateRange(1,3)]
    [int]$RunsPerOrder = 2,

    [Parameter(Mandatory = $false)]
    [ValidateRange(0,5)]
    [int]$MaxScoreRange = 2
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
    if ($null -eq $curl) { throw 'curl.exe is required for the local independent A/B stability harness.' }

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

function Get-CandidateScore {
    param($Comparison, [string]$TemplateId)
    foreach ($row in @($Comparison.candidate_judgments)) {
        if ([string]$row.template_id -eq $TemplateId) { return [int]$row.score }
    }
    return $null
}

if ([string]::IsNullOrWhiteSpace($Username)) { $Username = Read-Host 'WordPress username' }
if ([string]::IsNullOrWhiteSpace($Username)) { throw 'A WordPress username is required.' }
if ($TemplateIds.Count -ne 2) { throw 'The independent order-stability harness currently requires exactly two TemplateIds.' }
if ([string]::IsNullOrWhiteSpace($TemplateIds[0]) -or [string]::IsNullOrWhiteSpace($TemplateIds[1]) -or $TemplateIds[0] -eq $TemplateIds[1]) { throw 'TemplateIds must contain two distinct non-empty Pixfort section IDs.' }
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
$target = $TargetMarker.ToUpperInvariant()
$totalRuns = $RunsPerOrder * 2

Write-Host ''
Write-Host 'Elementize slot-blind independent A/B stability test' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host "Target marker: $target"
Write-Host ("Candidates: {0} vs {1}" -f $TemplateIds[0], $TemplateIds[1])
Write-Host ("Runs: {0} total ({1} forward + {1} swapped), alternating order" -f $totalRuns, $RunsPerOrder)
Write-Host ("Score drift threshold: <= {0} points per template" -f $MaxScoreRange)
Write-Host 'Policy: each candidate is model-scored alone against one shared grounded context; anchored discrimination may run for ties/near-ties; no insertion, replacement, or write endpoint is called.'
Write-Host ''

$status = Invoke-ElementizeCurl -Method GET -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green
Write-Host ("[INFO] Independent scoring: {0}; slot-order blind: {1}; template identity hidden: {2}; automatic writes: {3}" -f $status.aesthetic_ab_independent_scoring_version, $status.aesthetic_ab_independent_scoring_slot_order_blind, $status.aesthetic_ab_independent_scoring_template_identity_hidden_from_model, $status.aesthetic_ab_independent_scoring_automatic_write_allowed)
if ($status.aesthetic_ab_independent_discrimination) {
    Write-Host ("[INFO] Anchored discrimination: {0}; trigger gap <= {1}; PHP-derived score: {2}; automatic writes: {3}" -f $status.aesthetic_ab_independent_discrimination_version, $status.aesthetic_ab_independent_discrimination_trigger_max_gap, $status.aesthetic_ab_independent_discrimination_php_derived_overall_score, $status.aesthetic_ab_independent_discrimination_automatic_write_allowed)
}
Write-Host ("[INFO] Calibration: {0}; score floor: {1}/10" -f $status.aesthetic_ab_judgment_calibration_version, $status.aesthetic_ab_absolute_selection_score_floor)
if (-not $status.aesthetic_ab_independent_scoring) { throw 'Independent candidate scoring is not active in plugin status.' }

Write-Host '[STEP] Refreshing one grounded page context for all runs...' -ForegroundColor DarkCyan
$auditStarted = Get-Date
$audit = Invoke-ElementizeCurl -Method GET -Uri $auditUri -Authorization $auth
$auditSeconds = [Math]::Round(((Get-Date) - $auditStarted).TotalSeconds, 2)
$context = $audit.visual.aesthetic_ab_context
if ($null -eq $context -or -not $context.available) {
    $reason = if ($null -ne $context) { [string]$context.reason } else { 'aesthetic_ab_context was not returned.' }
    throw "Grounded A/B context unavailable: $reason"
}
$stateHash = [string]$context.page_state_hash
if ([string]::IsNullOrWhiteSpace($stateHash)) { throw 'Grounded A/B context did not expose page_state_hash.' }
Write-Host ("[PASS] Grounded context ready in {0}s for {1} markers; state hash {2}..." -f $auditSeconds, $context.marker_count, $stateHash.Substring(0, [Math]::Min(12, $stateHash.Length))) -ForegroundColor Green

$forward = @($TemplateIds[0], $TemplateIds[1])
$swapped = @($TemplateIds[1], $TemplateIds[0])
$runPlan = @()
for ($i = 1; $i -le $RunsPerOrder; $i++) {
    $runPlan += [pscustomobject]@{ Label = "F$i"; OrderKind = 'forward'; TemplateIds = $forward }
    $runPlan += [pscustomobject]@{ Label = "S$i"; OrderKind = 'swapped'; TemplateIds = $swapped }
}

$results = @()
$rawRuns = @()
foreach ($plan in $runPlan) {
    $orderedIds = @($plan.TemplateIds)
    Write-Host ("[STEP] {0}/{1} {2}: request A={3}; B={4}" -f ($results.Count + 1), $totalRuns, $plan.Label, $orderedIds[0], $orderedIds[1]) -ForegroundColor DarkCyan
    $body = [ordered]@{
        template_ids = $orderedIds
        aesthetic_compare = $true
        context_page_id = $PageId
        target_marker = $target
    } | ConvertTo-Json -Depth 5 -Compress

    $started = Get-Date
    $probe = Invoke-ElementizeCurl -Method POST -Uri $probeUri -Authorization $auth -JsonBody $body
    $seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    $comparison = $probe.aesthetic_comparison
    if ($null -eq $comparison) { throw "Run $($plan.Label) did not return aesthetic_comparison." }
    $independent = $comparison.independent_candidate_scoring
    $discrimination = $comparison.independent_discrimination
    $calibration = $comparison.judgment_calibration

    $method = [string]$comparison.comparison_method
    $independentApplied = $null -ne $independent -and [bool]$independent.applied -and $method.StartsWith('slot_blind_independent_candidate_scoring')
    $triggerGap = if ($null -ne $status.aesthetic_ab_independent_discrimination_trigger_max_gap) { [int]$status.aesthetic_ab_independent_discrimination_trigger_max_gap } else { 1 }
    $independentGap = if ($null -ne $independent -and $null -ne $independent.score_gap) { [int]$independent.score_gap } else { 999 }
    $independentTie = $null -ne $independent -and [bool]$independent.tied_top_score
    $discriminationRequired = [bool]$status.aesthetic_ab_independent_discrimination -and ($independentTie -or $independentGap -le $triggerGap)
    $discriminationApplied = $null -ne $discrimination -and [bool]$discrimination.applied
    $discriminationContractOk = -not $discriminationRequired -or $discriminationApplied

    $safeContract = [bool]$comparison.available -and [bool]$comparison.assessment_complete -and [bool]$comparison.exact_candidate_coverage -and
        $independentApplied -and $discriminationContractOk -and $null -ne $calibration -and [bool]$calibration.applied -and
        -not [bool]$comparison.writes_performed -and -not [bool]$comparison.automatic_write_allowed

    $returnedHash = [string]$comparison.page_state_hash
    $hashReturned = -not [string]::IsNullOrWhiteSpace($returnedHash)
    $hashMismatch = $hashReturned -and $returnedHash -ne $stateHash

    $score0 = Get-CandidateScore -Comparison $comparison -TemplateId $TemplateIds[0]
    $score1 = Get-CandidateScore -Comparison $comparison -TemplateId $TemplateIds[1]
    $record = [pscustomobject]@{
        Run = [string]$plan.Label
        Order = [string]$plan.OrderKind
        RequestSlotA = [string]$orderedIds[0]
        RequestSlotB = [string]$orderedIds[1]
        Winner = [string]$comparison.winner_template_id
        WinnerSlot = [string]$comparison.winner_slot
        Confidence = [string]$comparison.confidence
        Margin = [string]$comparison.winner_margin
        Usable = [bool]$comparison.usable_for_selection
        IndependentApplied = $independentApplied
        DiscriminationRequired = $discriminationRequired
        DiscriminationApplied = $discriminationApplied
        WinnerScore = if ($null -ne $calibration) { [int]$calibration.winner_score } else { 0 }
        ScoreGap = if ($null -ne $calibration) { [int]$calibration.score_gap } else { 0 }
        Conflicts = if ($null -ne $calibration) { [int]$calibration.consistency_conflict_count } else { -1 }
        ScoreCandidate1 = $score0
        ScoreCandidate2 = $score1
        HashReturned = $hashReturned
        HashMismatch = $hashMismatch
        SafeContract = $safeContract
        FailureReason = if (-not $independentApplied -and $null -ne $independent) { [string]$independent.reason } elseif ($discriminationRequired -and -not $discriminationApplied -and $null -ne $discrimination) { [string]$discrimination.reason } else { [string]$comparison.reason }
        Seconds = $seconds
    }
    $results += $record
    $rawRuns += [pscustomobject]@{
        run = [string]$plan.Label
        order = [string]$plan.OrderKind
        template_ids = $orderedIds
        mapping = $probe.mapping
        aesthetic_comparison = $comparison
    }

    $ok = $safeContract -and -not $hashMismatch
    Write-Host ("[{0}] {1}: winner={2}; score={3}; candidateScores={4}/{5}; confidence={6}; usable={7}; independent={8}; discriminator={9}/{10}; conflicts={11}; {12}s" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $plan.Label, $record.Winner, $record.WinnerScore, $record.ScoreCandidate1, $record.ScoreCandidate2, $record.Confidence, $record.Usable, $record.IndependentApplied, $record.DiscriminationApplied, $record.DiscriminationRequired, $record.Conflicts, $seconds) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
    if (-not $ok -and -not [string]::IsNullOrWhiteSpace($record.FailureReason)) { Write-Host ("       reason: {0}" -f $record.FailureReason) -ForegroundColor Yellow }
}

$winnerValues = @($results | ForEach-Object { [string]$_.Winner } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne 'none' })
$winnerUnique = @($winnerValues | Sort-Object -Unique)
$allRunsHaveWinner = $winnerValues.Count -eq $totalRuns
$sameWinnerIdentity = $allRunsHaveWinner -and $winnerUnique.Count -eq 1
$allIndependent = @($results | Where-Object { -not $_.IndependentApplied }).Count -eq 0
$allDiscriminationRequirementsMet = @($results | Where-Object { $_.DiscriminationRequired -and -not $_.DiscriminationApplied }).Count -eq 0
$allSafe = @($results | Where-Object { -not $_.SafeContract }).Count -eq 0
$allUsable = @($results | Where-Object { -not $_.Usable }).Count -eq 0
$allConflictFree = @($results | Where-Object { $_.Conflicts -ne 0 }).Count -eq 0
$hashEvidenceComplete = @($results | Where-Object { -not $_.HashReturned }).Count -eq 0
$observedHashMismatch = @($results | Where-Object { $_.HashMismatch }).Count -gt 0
$pageStateStableWhereObserved = -not $observedHashMismatch

$scoreSummaries = @()
$allScoreRangesAcceptable = $true
foreach ($templateId in $TemplateIds) {
    $scores = @()
    foreach ($run in $rawRuns) {
        $score = Get-CandidateScore -Comparison $run.aesthetic_comparison -TemplateId $templateId
        if ($null -ne $score) { $scores += [int]$score }
    }
    $min = if ($scores.Count -gt 0) { ($scores | Measure-Object -Minimum).Minimum } else { $null }
    $max = if ($scores.Count -gt 0) { ($scores | Measure-Object -Maximum).Maximum } else { $null }
    $range = if ($scores.Count -eq $totalRuns) { [int]$max - [int]$min } else { $null }
    $rangeOk = $null -ne $range -and $range -le $MaxScoreRange
    if (-not $rangeOk) { $allScoreRangesAcceptable = $false }
    $scoreSummaries += [pscustomobject]@{
        TemplateId = $templateId
        Scores = ($scores -join ', ')
        Min = $min
        Max = $max
        Range = $range
        RangeAcceptable = $rangeOk
    }
}

$forwardWinners = @($results | Where-Object { $_.Order -eq 'forward' } | ForEach-Object { [string]$_.Winner } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
$swappedWinners = @($results | Where-Object { $_.Order -eq 'swapped' } | ForEach-Object { [string]$_.Winner } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
$orderInvariant = $sameWinnerIdentity -and $forwardWinners.Count -eq 1 -and $swappedWinners.Count -eq 1 -and $forwardWinners[0] -eq $swappedWinners[0]
$preferenceStable = $allIndependent -and $allDiscriminationRequirementsMet -and $allSafe -and $pageStateStableWhereObserved -and $sameWinnerIdentity -and $orderInvariant -and $allScoreRangesAcceptable
$stableForSelection = $preferenceStable -and $hashEvidenceComplete -and $allUsable -and $allConflictFree
$stableWinner = if ($sameWinnerIdentity) { [string]$winnerUnique[0] } else { '' }

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rawPath = Join-Path $outDir "aesthetic-ab-independent-stability-$PageId-$target-$stamp.json"
[pscustomobject]@{
    page_id = $PageId
    target_marker = $target
    expected_page_state_hash = $stateHash
    runs = $rawRuns
    verdict = [ordered]@{
        independent_scoring_every_run = $allIndependent
        anchored_discrimination_when_required = $allDiscriminationRequirementsMet
        safe_read_only_contract_every_run = $allSafe
        page_state_hash_evidence_complete = $hashEvidenceComplete
        observed_page_state_hash_mismatch = $observedHashMismatch
        same_winner_identity_every_run = $sameWinnerIdentity
        stable_winner_template_id = $stableWinner
        order_invariant = $orderInvariant
        usable_every_run = $allUsable
        conflict_free_every_run = $allConflictFree
        score_ranges_acceptable = $allScoreRangesAcceptable
        preference_stable = $preferenceStable
        stable_for_read_only_selection = $stableForSelection
    }
} | ConvertTo-Json -Depth 100 | Set-Content -Path $rawPath -Encoding UTF8

Write-Host ''
Write-Host '--- Independent stability runs ---' -ForegroundColor Cyan
$results | Select-Object Run, Order, RequestSlotA, RequestSlotB, Winner, ScoreCandidate1, ScoreCandidate2, Confidence, Usable, IndependentApplied, DiscriminationRequired, DiscriminationApplied, HashReturned, HashMismatch, Conflicts | Format-Table -AutoSize

Write-Host ''
Write-Host '--- Score stability by template identity ---' -ForegroundColor Cyan
$scoreSummaries | Format-Table -AutoSize

Write-Host ''
Write-Host '--- Independent stability verdict ---' -ForegroundColor Cyan
[pscustomobject]@{
    TotalRuns = $totalRuns
    IndependentScoringEveryRun = $allIndependent
    AnchoredDiscriminationWhenRequired = $allDiscriminationRequirementsMet
    SafeReadOnlyContractEveryRun = $allSafe
    PageStateHashEvidenceComplete = $hashEvidenceComplete
    ObservedPageStateHashMismatch = $observedHashMismatch
    SameWinnerIdentityEveryRun = $sameWinnerIdentity
    StableWinnerTemplateId = $stableWinner
    OrderInvariant = $orderInvariant
    UsableEveryRun = $allUsable
    ConflictFreeEveryRun = $allConflictFree
    MaxAllowedScoreRange = $MaxScoreRange
    ScoreRangesAcceptable = $allScoreRangesAcceptable
    PreferenceStable = $preferenceStable
    StableForReadOnlySelection = $stableForSelection
} | Format-List

Write-Host ("[SAVE] Stability audit: {0}" -f $rawPath) -ForegroundColor DarkGray
if ($stableForSelection) {
    Write-Host ("[PASS] Slot-blind independent scoring is stable, order-invariant, calibrated, and usable for read-only selection: {0}." -f $stableWinner) -ForegroundColor Green
} elseif ($preferenceStable) {
    Write-Host ("[PASS] Slot-blind preference is stable and order-invariant for {0}, but one or more calibration/traceability conditions still block selection usability." -f $stableWinner) -ForegroundColor Yellow
} else {
    Write-Host '[WARN] Independent candidate scoring is not yet stable enough for a future autonomous selection gate.' -ForegroundColor Yellow
    if (-not $allIndependent) { Write-Host ' - One or more runs did not complete the independent one-image scoring method.' }
    if (-not $allDiscriminationRequirementsMet) { Write-Host ' - Anchored discrimination was required for at least one tie/near-tie but did not complete.' }
    if (-not $allSafe) { Write-Host ' - One or more runs failed the complete calibrated read-only comparison contract.' }
    if ($observedHashMismatch) { Write-Host ' - At least one returned page-state hash actually differed from the grounded context hash.' }
    if (-not $hashEvidenceComplete) { Write-Host ' - One or more failed runs omitted page-state hash evidence; this is not treated as proof that the page changed.' }
    if (-not $sameWinnerIdentity) { Write-Host ' - Effective winner template identity changed across repeated runs.' }
    if (-not $orderInvariant) { Write-Host ' - Winner identity was not invariant when request slot order was swapped.' }
    if (-not $allScoreRangesAcceptable) { Write-Host (" - At least one template score varied by more than {0} points." -f $MaxScoreRange) }
    if (-not $allConflictFree) { Write-Host ' - One or more runs contained numeric-vs-categorical consistency conflicts.' }
    if (-not $allUsable) { Write-Host ' - One or more calibrated runs were advisory-only / unusable for selection.' }
}

Write-Host ''
Write-Host 'Done. No write endpoint was called and no page state was changed.' -ForegroundColor Cyan
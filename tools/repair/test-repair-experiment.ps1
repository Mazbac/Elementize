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
    throw 'This harness temporarily changes exactly one planned design control, runs a visual audit, and then restores the original value. Re-run with -ConfirmExperiment to explicitly authorize that reversible draft-only test.'
}

function Get-PlainTextFromSecureString {
    param([Security.SecureString]$SecureString)
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

function New-BasicAuthHeader {
    param(
        [string]$User,
        [Security.SecureString]$Password
    )
    $plain = Get-PlainTextFromSecureString $Password
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes("$User`:$plain")
        return 'Basic ' + [Convert]::ToBase64String($bytes)
    }
    finally { $plain = $null }
}

function Invoke-ElementizeJson {
    param(
        [ValidateSet('GET','POST')]
        [string]$Method,
        [string]$Uri,
        [string]$Authorization,
        $Body = $null
    )

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) { throw 'curl.exe is required for this local guarded experiment harness.' }

    $bodyPath = $null
    if ($Method -eq 'POST') {
        if ($null -eq $Body) { throw 'POST requires a JSON body.' }
        $bodyPath = [IO.Path]::GetTempFileName()
        $json = $Body | ConvertTo-Json -Depth 100 -Compress
        [IO.File]::WriteAllText($bodyPath, $json, (New-Object Text.UTF8Encoding($false)))
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
            $safePath = $bodyPath.Replace('\','/')
            $proc.StandardInput.WriteLine('request = "POST"')
            $proc.StandardInput.WriteLine('header = "Content-Type: application/json"')
            $proc.StandardInput.WriteLine('data-binary = "@' + $safePath + '"')
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

        $responseBody = $stdout.Substring(0, $idx).Trim()
        $codeText = $stdout.Substring($idx + $marker.Length).Trim()
        $httpCode = 0
        [void][int]::TryParse($codeText, [ref]$httpCode)

        if ($httpCode -lt 200 -or $httpCode -ge 300) {
            $snippet = if ($responseBody.Length -gt 900) { $responseBody.Substring(0, 900) + '...' } else { $responseBody }
            throw "Elementize returned HTTP $httpCode. $snippet"
        }
        if ([string]::IsNullOrWhiteSpace($responseBody)) { throw 'Elementize returned an empty response body.' }
        return $responseBody | ConvertFrom-Json
    }
    finally {
        if (-not $proc.HasExited) { try { $proc.Kill() } catch {} }
        $proc.Dispose()
        if ($bodyPath -and (Test-Path $bodyPath)) { Remove-Item -Force $bodyPath -ErrorAction SilentlyContinue }
    }
}

function ConvertTo-PathKey {
    param($Path)
    return (@($Path) | ForEach-Object { [string]$_ }) -join [char]31
}

function Test-BoxEqual {
    param($A, $B)
    if ($null -eq $A -or $null -eq $B) { return $false }
    foreach ($name in @('unit','top','right','bottom','left','isLinked')) {
        if ([string]$A.$name -ne [string]$B.$name) { return $false }
    }
    return $true
}

function Get-ExactDesignRead {
    param(
        [string]$Base,
        [int]$Id,
        $Plan,
        [string]$Authorization
    )
    $element = [Uri]::EscapeDataString([string]$Plan.element_id)
    $category = [Uri]::EscapeDataString([string]$Plan.category)
    $scope = [Uri]::EscapeDataString([string]$Plan.responsive_scope)
    $uri = "$Base/wp-json/elementize/v1/pages/$Id/design-settings?category=$category&element_id=$element&responsive_scope=$scope&limit=300"
    $read = Invoke-ElementizeJson -Method GET -Uri $uri -Authorization $Authorization
    $wantedPath = ConvertTo-PathKey $Plan.setting_path
    $matches = @($read.controls | Where-Object {
        ([string]$_.element_id -eq [string]$Plan.element_id) -and
        ([string]$_.category -eq [string]$Plan.category) -and
        ((ConvertTo-PathKey $_.setting_path) -eq $wantedPath)
    })
    if ($matches.Count -ne 1) { throw "Expected exactly one fresh matching design control; found $($matches.Count)." }
    return [pscustomobject]@{ Read = $read; Control = $matches[0] }
}

function New-WriteBody {
    param(
        $DesignRead,
        $Control,
        $Value
    )
    return [ordered]@{
        expected_status = [string]$DesignRead.page.status
        expected_title = [string]$DesignRead.page.title
        content_hash = [string]$DesignRead.content_hash
        updates = @(
            [ordered]@{
                category = [string]$Control.category
                element_id = [string]$Control.element_id
                setting_path = @($Control.setting_path)
                expected_value = $Control.value
                value = $Value
                control_fingerprint = [string]$Control.control_fingerprint
            }
        )
        confirm_design_write = $true
    }
}

function Get-TargetSection {
    param($Audit, [string]$TopId)
    if ($null -eq $Audit.visual -or $null -eq $Audit.visual.render_metrics) { return $null }
    $rows = @($Audit.visual.render_metrics.sections | Where-Object { [string]$_.top_level_element_id -eq $TopId })
    if ($rows.Count -eq 1) { return $rows[0] }
    return $null
}

function Get-SectionSummary {
    param($Section)
    if ($null -eq $Section) { return $null }
    return [pscustomobject]@{
        top_level_element_id = [string]$Section.top_level_element_id
        height = $Section.height
        padding_top = $Section.padding.top
        padding_bottom = $Section.padding.bottom
        gap_before = $Section.gap_before
        gap_after = $Section.gap_after
    }
}

function Save-Audit {
    param($Audit, [string]$Label, [int]$Id, [string]$Root)
    $dir = Join-Path $Root '.elementize-dev'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $path = Join-Path $dir "$Label-$Id-$stamp.json"
    $Audit | ConvertTo-Json -Depth 100 | Set-Content -Path $path -Encoding UTF8
    return $path
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
$repoRoot = Split-Path $PSScriptRoot -Parent
$statusUri = "$base/wp-json/elementize/v1/status"
$auditUri = "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true"
$writeUri = "$base/wp-json/elementize/v1/pages/$PageId/design-settings"

Write-Host ''
Write-Host 'Elementize guarded reversible repair experiment' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host 'Policy: one planned control; draft only; visual compare; always restore original value.'
Write-Host ''

$status = Invoke-ElementizeJson -Method GET -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green

Write-Host '[1/6] Reading fresh baseline completion audit...' -ForegroundColor Cyan
$baselineStarted = Get-Date
$baseline = Invoke-ElementizeJson -Method GET -Uri $auditUri -Authorization $auth
$baselineSeconds = [Math]::Round(((Get-Date) - $baselineStarted).TotalSeconds, 2)
$baselinePath = Save-Audit -Audit $baseline -Label 'experiment-before' -Id $PageId -Root $repoRoot
Write-Host ("[PASS] Baseline audit returned in {0}s" -f $baselineSeconds) -ForegroundColor Green
Write-Host ("[SAVE] Baseline: {0}" -f $baselinePath) -ForegroundColor DarkGray

$planObject = $baseline.visual.repair_plan
if ($null -eq $planObject -or -not $planObject.available) { throw 'Fresh repair_plan is unavailable.' }
$plans = @($planObject.plans)
if ($plans.Count -ne 1 -or [int]$planObject.planned_change_count -ne 1) { throw "Expected exactly one bounded repair plan; found $($plans.Count)." }
if ($planObject.automatic_write_allowed) { throw 'Safety invariant failed: repair_plan unexpectedly grants automatic write authority.' }
$plan = $plans[0]
if (-not $plan.single_change_only -or -not $plan.bounded_change) { throw 'Safety invariant failed: plan is not explicitly bounded to one change.' }
if ([string]$plan.category -ne 'spacing' -or [string]$plan.rendered_property -ne 'padding') { throw 'This first experiment harness accepts only one spacing/padding plan.' }

Write-Host ("[PLAN] {0} {1} padding: {2}px -> {3}px" -f $plan.top_level_element_id, $plan.changed_component, $plan.expected_current_value.($plan.changed_component), $plan.proposed_value.($plan.changed_component)) -ForegroundColor Yellow

Write-Host '[2/6] Fresh exact guarded control read...' -ForegroundColor Cyan
$fresh = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
$design = $fresh.Read
$control = $fresh.Control
if ([string]$design.page.status -ne 'draft') { throw "Page is not a draft; current status is $($design.page.status)." }
if (-not $design.write_available_for_page) { throw 'Fresh design read says the page is not currently writable under the guarded writer contract.' }
if (-not $control.writable_now) { throw 'Fresh exact control is not writable_now.' }
if ([string]$control.source -ne 'explicit') { throw 'Fresh exact control is not explicit.' }
if ([string]$design.content_hash -ne [string]$planObject.page_content_hash) { throw 'Fresh page hash no longer matches the plan. Re-run the experiment.' }
if ([string]$control.control_fingerprint -ne [string]$plan.control_fingerprint) { throw 'Fresh control fingerprint no longer matches the plan.' }
if (-not (Test-BoxEqual $control.value $plan.expected_current_value)) { throw 'Fresh control value no longer matches the planned expected value.' }
Write-Host '[PASS] Fresh hash, value, element, path, writability, and fingerprint all match.' -ForegroundColor Green

$originalValue = $control.value
$proposedValue = $plan.proposed_value
$beforeSection = Get-TargetSection -Audit $baseline -TopId ([string]$plan.top_level_element_id)
$beforeSummary = Get-SectionSummary $beforeSection

$writeApplied = $false
$writeResponse = $null
$afterAudit = $null
$afterPath = $null
$afterSummary = $null
$restoreResponse = $null
$restoreVerified = $false
$experimentError = $null
$restoreError = $null

try {
    Write-Host '[3/6] Applying one guarded planned value...' -ForegroundColor Cyan
    $writeBody = New-WriteBody -DesignRead $design -Control $control -Value $proposedValue
    $writeResponse = Invoke-ElementizeJson -Method POST -Uri $writeUri -Authorization $auth -Body $writeBody
    if (-not $writeResponse.saved -or [int]$writeResponse.updated_count -ne 1) { throw 'Guarded writer did not confirm exactly one saved update.' }
    $writeApplied = $true
    Write-Host ("[WRITE] Revision {0} created." -f $writeResponse.revision_id) -ForegroundColor Yellow

    $written = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
    if (-not (Test-BoxEqual $written.Control.value $proposedValue)) { throw 'Fresh design read did not verify the proposed value after the write.' }
    Write-Host '[PASS] Proposed value verified by a fresh exact read.' -ForegroundColor Green

    Write-Host '[4/6] Running post-change visual completion audit...' -ForegroundColor Cyan
    $afterStarted = Get-Date
    $afterAudit = Invoke-ElementizeJson -Method GET -Uri $auditUri -Authorization $auth
    $afterSeconds = [Math]::Round(((Get-Date) - $afterStarted).TotalSeconds, 2)
    $afterPath = Save-Audit -Audit $afterAudit -Label 'experiment-after' -Id $PageId -Root $repoRoot
    Write-Host ("[PASS] Post-change audit returned in {0}s" -f $afterSeconds) -ForegroundColor Green
    Write-Host ("[SAVE] After: {0}" -f $afterPath) -ForegroundColor DarkGray

    $afterSection = Get-TargetSection -Audit $afterAudit -TopId ([string]$plan.top_level_element_id)
    $afterSummary = Get-SectionSummary $afterSection
    if ($null -eq $afterSummary) { throw 'Post-change rendered metrics did not contain the target section.' }

    $expectedRendered = [double]$proposedValue.($plan.changed_component)
    $actualRendered = [double]$afterSection.padding.($plan.changed_component)
    if ([Math]::Abs($actualRendered - $expectedRendered) -gt 1.0) {
        throw "Rendered target did not move to the expected value. Expected about $expectedRendered px; saw $actualRendered px."
    }
    Write-Host ("[PASS] Rendered {0} padding moved to {1}px." -f $plan.changed_component, $actualRendered) -ForegroundColor Green
}
catch {
    $experimentError = $_
}
finally {
    if ($writeApplied) {
        Write-Host '[5/6] Restoring the exact original value...' -ForegroundColor Cyan
        try {
            $current = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
            if (-not (Test-BoxEqual $current.Control.value $proposedValue)) {
                throw 'Cannot safely restore: the control no longer equals the experiment value.'
            }
            $restoreBody = New-WriteBody -DesignRead $current.Read -Control $current.Control -Value $originalValue
            $restoreResponse = Invoke-ElementizeJson -Method POST -Uri $writeUri -Authorization $auth -Body $restoreBody
            if (-not $restoreResponse.saved -or [int]$restoreResponse.updated_count -ne 1) { throw 'Restore writer did not confirm exactly one saved update.' }
            $finalRead = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
            if (-not (Test-BoxEqual $finalRead.Control.value $originalValue)) { throw 'Fresh read did not verify the exact original value after restore.' }
            $restoreVerified = $true
            Write-Host ("[RESTORE] Revision {0} created; original value verified." -f $restoreResponse.revision_id) -ForegroundColor Green
        }
        catch {
            $restoreError = $_
            Write-Host ("[CRITICAL] Restore failed: {0}" -f $_.Exception.Message) -ForegroundColor Red
        }
    }
}

if ($null -ne $restoreError) {
    throw 'The experiment changed the draft but automatic restore failed. Do not continue visual repair testing until the page is inspected and restored manually.'
}
if ($null -ne $experimentError) {
    if ($restoreVerified) { Write-Host '[PASS] Experiment error occurred, but the original value was safely restored.' -ForegroundColor Green }
    throw $experimentError
}

Write-Host '[6/6] Before/after comparison (page is already restored)...' -ForegroundColor Cyan
Write-Host ''
Write-Host '--- Target rendered metrics ---' -ForegroundColor Cyan
[pscustomobject]@{
    Metric = 'padding_top'
    Before = $beforeSummary.padding_top
    After = $afterSummary.padding_top
    Delta = ([double]$afterSummary.padding_top - [double]$beforeSummary.padding_top)
} | Format-Table -AutoSize
[pscustomobject]@{
    Metric = 'section_height'
    Before = $beforeSummary.height
    After = $afterSummary.height
    Delta = ([double]$afterSummary.height - [double]$beforeSummary.height)
} | Format-Table -AutoSize
[pscustomobject]@{
    Metric = 'gap_before'
    Before = $beforeSummary.gap_before
    After = $afterSummary.gap_before
    Delta = ([double]$afterSummary.gap_before - [double]$beforeSummary.gap_before)
} | Format-Table -AutoSize
[pscustomobject]@{
    Metric = 'gap_after'
    Before = $beforeSummary.gap_after
    After = $afterSummary.gap_after
    Delta = ([double]$afterSummary.gap_after - [double]$beforeSummary.gap_after)
} | Format-Table -AutoSize

Write-Host ''
Write-Host '--- Visual pipeline after temporary change ---' -ForegroundColor Cyan
[pscustomobject]@{
    RepairConvergencePromoted = $afterAudit.visual.repair_convergence.promoted_target_count
    RepairPlanCount = $afterAudit.visual.repair_plan.planned_change_count
    FocusedReviewable = $afterAudit.visual.focused_verification.hardened_convergence_review_count
    SectionFocusReviewable = $afterAudit.visual.focused_section_verification.reviewable_count
} | Format-List

Write-Host ("Write revision:   {0}" -f $writeResponse.revision_id)
Write-Host ("Restore revision: {0}" -f $restoreResponse.revision_id)
Write-Host ("Original value restored: {0}" -f $restoreVerified) -ForegroundColor Green
Write-Host ''
Write-Host 'Done. The experiment temporarily changed one draft control, visually re-audited it, and restored the exact original value. The page was not published.' -ForegroundColor Cyan

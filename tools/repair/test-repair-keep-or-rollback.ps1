param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER,

    [Parameter(Mandatory = $false)]
    [switch]$ConfirmKeep
)

$ErrorActionPreference = 'Stop'

if (-not $ConfirmKeep) {
    throw 'This acceptance harness may KEEP one visually verified draft-only repair when every conservative gate passes, or automatically restore the original value when any gate fails. Re-run with -ConfirmKeep to explicitly authorize that behavior.'
}

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

function Invoke-ElementizeJson {
    param(
        [ValidateSet('GET','POST')]
        [string]$Method,
        [string]$Uri,
        [string]$Authorization,
        $Body = $null
    )

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) { throw 'curl.exe is required for this local guarded acceptance harness.' }

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
    param([string]$Base, [int]$Id, $Plan, [string]$Authorization)
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
    param($DesignRead, $Control, $Value)
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

function Save-Audit {
    param($Audit, [string]$Label, [int]$Id, [string]$Root)
    $dir = Join-Path $Root '.elementize-dev'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $path = Join-Path $dir "$Label-$Id-$stamp.json"
    $Audit | ConvertTo-Json -Depth 100 | Set-Content -Path $path -Encoding UTF8
    return $path
}

function Get-TargetSection {
    param($Audit, [string]$TopId)
    $rows = @($Audit.visual.render_metrics.sections | Where-Object { [string]$_.top_level_element_id -eq $TopId })
    if ($rows.Count -ne 1) { return $null }
    return $rows[0]
}

function Get-OutcomeGates {
    param($BeforeAudit, $AfterAudit, $Plan)

    $topId = [string]$Plan.top_level_element_id
    $side = [string]$Plan.changed_component
    $sourceTargetId = [string]$Plan.source_target_id
    $expectedAfter = [double]$Plan.proposed_value.$side

    $beforeSection = Get-TargetSection -Audit $BeforeAudit -TopId $topId
    $afterSection = Get-TargetSection -Audit $AfterAudit -TopId $topId
    if ($null -eq $beforeSection -or $null -eq $afterSection) { throw 'Target section metrics were not uniquely available in the before/after audits.' }

    $beforePadding = [double]$beforeSection.padding.$side
    $afterPadding = [double]$afterSection.padding.$side
    $afterPromoted = @($AfterAudit.visual.repair_convergence.promoted_targets)
    $afterPlans = @($AfterAudit.visual.repair_plan.plans)

    $gates = [ordered]@{
        rendered_target_moved_as_planned = ([Math]::Abs($afterPadding - $expectedAfter) -le 1.0)
        section_height_did_not_increase = ([double]$afterSection.height -le ([double]$beforeSection.height + 1.0))
        gap_before_did_not_increase = ([double]$afterSection.gap_before -le ([double]$beforeSection.gap_before + 1.0))
        gap_after_did_not_increase = ([double]$afterSection.gap_after -le ([double]$beforeSection.gap_after + 1.0))
        same_target_no_longer_promoted = (@($afterPromoted | Where-Object { [string]$_.target_id -eq $sourceTargetId }).Count -eq 0)
        same_target_no_longer_planned = (@($afterPlans | Where-Object { [string]$_.source_target_id -eq $sourceTargetId }).Count -eq 0)
        render_metrics_available_after = [bool]$AfterAudit.visual.render_metrics.available
        focused_pipeline_available_after = [bool]$AfterAudit.visual.focused_verification.available
        section_focus_pipeline_available_after = [bool]$AfterAudit.visual.focused_section_verification.available
    }

    $allPass = $true
    foreach ($entry in $gates.GetEnumerator()) {
        if (-not [bool]$entry.Value) { $allPass = $false }
    }

    return [pscustomobject]@{
        Gates = $gates
        AllPass = $allPass
        BeforePadding = $beforePadding
        AfterPadding = $afterPadding
        ExpectedAfter = $expectedAfter
    }
}

$repoRoot = Split-Path $PSScriptRoot -Parent
$devDir = Join-Path $repoRoot '.elementize-dev'
$beforeFile = Get-ChildItem $devDir -Filter "experiment-before-$PageId-*.json" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
$afterFile = Get-ChildItem $devDir -Filter "experiment-after-$PageId-*.json" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($null -eq $beforeFile -or $null -eq $afterFile) { throw 'Run the shadow keep-or-rollback test successfully first; no recent experiment audit pair was found.' }
if ($afterFile.LastWriteTime -lt $beforeFile.LastWriteTime) { throw 'Latest after-audit predates the latest before-audit; refusing to use a mismatched pair.' }
if (($afterFile.LastWriteTime - $beforeFile.LastWriteTime).TotalMinutes -gt 5) { throw 'Latest before/after audits are too far apart to trust as one experiment pair.' }
if (((Get-Date) - $afterFile.LastWriteTime).TotalMinutes -gt 15) { throw 'The latest successful experiment evidence is older than 15 minutes. Re-run the shadow decision first.' }

$beforeAudit = Get-Content $beforeFile.FullName -Raw | ConvertFrom-Json
$afterAudit = Get-Content $afterFile.FullName -Raw | ConvertFrom-Json
$plans = @($beforeAudit.visual.repair_plan.plans)
if ($plans.Count -ne 1 -or [int]$beforeAudit.visual.repair_plan.planned_change_count -ne 1) { throw "Expected exactly one bounded baseline plan; found $($plans.Count)." }
$plan = $plans[0]
if (-not $plan.single_change_only -or -not $plan.bounded_change) { throw 'Safety invariant failed: baseline plan is not explicitly bounded to one change.' }
if ([string]$plan.category -ne 'spacing' -or [string]$plan.rendered_property -ne 'padding') { throw 'This first keep/rollback acceptance harness accepts only one spacing/padding plan.' }

$shadow = Get-OutcomeGates -BeforeAudit $beforeAudit -AfterAudit $afterAudit -Plan $plan
if (-not $shadow.AllPass) {
    Write-Host '[ABORT] The latest reversible experiment does not satisfy every conservative keep gate. No write will be attempted.' -ForegroundColor Yellow
    $shadow.Gates.GetEnumerator() | ForEach-Object { [pscustomobject]@{ Gate = $_.Key; Pass = [bool]$_.Value } } | Format-Table -AutoSize
    exit 2
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
$writeUri = "$base/wp-json/elementize/v1/pages/$PageId/design-settings"

Write-Host ''
Write-Host 'Elementize REAL guarded keep-or-rollback acceptance test' -ForegroundColor Cyan
Write-Host "Page: $PageId"
Write-Host 'Policy: max one exact draft control. KEEP only if every fresh post-write gate passes; otherwise restore the exact original value.' -ForegroundColor Yellow
Write-Host ''

$status = Invoke-ElementizeJson -Method GET -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green
Write-Host ("[EVIDENCE] Shadow decision already passed for {0} {1} padding: {2}px -> {3}px" -f $plan.top_level_element_id, $plan.changed_component, $plan.expected_current_value.($plan.changed_component), $plan.proposed_value.($plan.changed_component)) -ForegroundColor Green

Write-Host '[1/5] Fresh exact guarded control read on the restored draft...' -ForegroundColor Cyan
$fresh = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
$design = $fresh.Read
$control = $fresh.Control
if ([string]$design.page.status -ne 'draft') { throw "Page is not a draft; current status is $($design.page.status)." }
if (-not $design.write_available_for_page -or -not $control.writable_now) { throw 'Fresh guarded design control is not writable now.' }
if ([string]$control.source -ne 'explicit') { throw 'Fresh exact control is not explicit.' }
if ([string]$design.content_hash -ne [string]$beforeAudit.visual.repair_plan.page_content_hash) { throw 'Current restored page hash no longer matches the accepted baseline evidence. Re-run the shadow decision.' }
if ([string]$control.control_fingerprint -ne [string]$plan.control_fingerprint) { throw 'Fresh control fingerprint no longer matches the accepted plan.' }
if (-not (Test-BoxEqual $control.value $plan.expected_current_value)) { throw 'Fresh control value no longer matches the accepted baseline value.' }
Write-Host '[PASS] Fresh page hash, exact value, setting path, writability, and fingerprint match the accepted evidence.' -ForegroundColor Green

$originalValue = $control.value
$proposedValue = $plan.proposed_value
$writeApplied = $false
$keep = $false
$writeResponse = $null
$restoreResponse = $null
$finalAudit = $null
$finalAuditPath = $null
$failure = $null
$restoreFailure = $null

try {
    Write-Host '[2/5] Applying the one accepted guarded repair...' -ForegroundColor Cyan
    $writeBody = New-WriteBody -DesignRead $design -Control $control -Value $proposedValue
    $writeResponse = Invoke-ElementizeJson -Method POST -Uri $writeUri -Authorization $auth -Body $writeBody
    if (-not $writeResponse.saved -or [int]$writeResponse.updated_count -ne 1) { throw 'Guarded writer did not confirm exactly one saved update.' }
    $writeApplied = $true
    Write-Host ("[WRITE] Revision {0} created." -f $writeResponse.revision_id) -ForegroundColor Yellow

    $written = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
    if (-not (Test-BoxEqual $written.Control.value $proposedValue)) { throw 'Fresh exact read did not verify the proposed value after write.' }
    Write-Host '[PASS] Proposed value verified.' -ForegroundColor Green

    Write-Host '[3/5] Running fresh visual completion audit on the candidate kept state...' -ForegroundColor Cyan
    $started = Get-Date
    $finalAudit = Invoke-ElementizeJson -Method GET -Uri $auditUri -Authorization $auth
    $seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    $finalAuditPath = Save-Audit -Audit $finalAudit -Label 'keep-candidate' -Id $PageId -Root $repoRoot
    Write-Host ("[PASS] Candidate-state audit returned in {0}s" -f $seconds) -ForegroundColor Green
    Write-Host ("[SAVE] Candidate state: {0}" -f $finalAuditPath) -ForegroundColor DarkGray

    Write-Host '[4/5] Evaluating conservative KEEP gates...' -ForegroundColor Cyan
    $freshDecision = Get-OutcomeGates -BeforeAudit $beforeAudit -AfterAudit $finalAudit -Plan $plan
    $freshDecision.Gates.GetEnumerator() | ForEach-Object { [pscustomobject]@{ Gate = $_.Key; Pass = [bool]$_.Value } } | Format-Table -AutoSize
    if (-not $freshDecision.AllPass) { throw 'At least one fresh post-write KEEP gate failed.' }

    $keptRead = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
    if ([string]$keptRead.Read.page.status -ne 'draft') { throw 'Page status changed unexpectedly during the acceptance test.' }
    if (-not (Test-BoxEqual $keptRead.Control.value $proposedValue)) { throw 'Candidate value was not still present after the final visual audit.' }
    $keep = $true
}
catch {
    $failure = $_
}
finally {
    if ($writeApplied -and -not $keep) {
        Write-Host '[ROLLBACK] A KEEP gate or verification failed; restoring the exact original value...' -ForegroundColor Yellow
        try {
            $current = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
            if (-not (Test-BoxEqual $current.Control.value $proposedValue)) { throw 'Cannot safely rollback because the control no longer equals the candidate value.' }
            $restoreBody = New-WriteBody -DesignRead $current.Read -Control $current.Control -Value $originalValue
            $restoreResponse = Invoke-ElementizeJson -Method POST -Uri $writeUri -Authorization $auth -Body $restoreBody
            if (-not $restoreResponse.saved -or [int]$restoreResponse.updated_count -ne 1) { throw 'Rollback writer did not confirm exactly one saved update.' }
            $restored = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
            if (-not (Test-BoxEqual $restored.Control.value $originalValue)) { throw 'Fresh exact read did not verify the original value after rollback.' }
            Write-Host ("[ROLLBACK PASS] Revision {0}; original value restored." -f $restoreResponse.revision_id) -ForegroundColor Green
        }
        catch {
            $restoreFailure = $_
            Write-Host ("[CRITICAL] Automatic rollback failed: {0}" -f $_.Exception.Message) -ForegroundColor Red
        }
    }
}

if ($null -ne $restoreFailure) {
    throw 'The acceptance test changed the draft and automatic rollback failed. Inspect the draft before any further repair testing.'
}
if ($null -ne $failure) {
    Write-Host '[DECISION] ROLLBACK' -ForegroundColor Yellow
    throw $failure
}

Write-Host '[5/5] Final kept-state verification...' -ForegroundColor Cyan
$finalRead = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
if ([string]$finalRead.Read.page.status -ne 'draft') { throw 'Final page status is not draft.' }
if (-not (Test-BoxEqual $finalRead.Control.value $proposedValue)) { throw 'Final exact control does not contain the kept value.' }

Write-Host ''
Write-Host '[DECISION] KEEP' -ForegroundColor Green
Write-Host ("Kept revision: {0}" -f $writeResponse.revision_id) -ForegroundColor Green
Write-Host ("Final value: {0} {1} padding = {2}px" -f $plan.top_level_element_id, $plan.changed_component, $proposedValue.($plan.changed_component)) -ForegroundColor Green
Write-Host 'The page remains a DRAFT. Nothing was published.' -ForegroundColor Cyan
Write-Host 'If any fresh gate had failed, this harness would have restored the exact original value instead.' -ForegroundColor DarkGray

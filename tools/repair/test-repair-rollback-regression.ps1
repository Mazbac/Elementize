param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER,

    [Parameter(Mandatory = $false)]
    [switch]$ConfirmRegression
)

$ErrorActionPreference = 'Stop'

if (-not $ConfirmRegression) {
    throw 'This harness temporarily restores the previously rejected spacing value to prove the rollback path, then MUST restore the currently kept value. Re-run with -ConfirmRegression to explicitly authorize that draft-only reversible regression test.'
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
    if ($null -eq $curl) { throw 'curl.exe is required for this local guarded rollback harness.' }

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
        updates = @([ordered]@{
            category = [string]$Control.category
            element_id = [string]$Control.element_id
            setting_path = @($Control.setting_path)
            expected_value = $Control.value
            value = $Value
            control_fingerprint = [string]$Control.control_fingerprint
        })
        confirm_design_write = $true
    }
}

function Get-TargetSection {
    param($Audit, [string]$TopId)
    $rows = @($Audit.visual.render_metrics.sections | Where-Object { [string]$_.top_level_element_id -eq $TopId })
    if ($rows.Count -ne 1) { throw "Expected one rendered target section; found $($rows.Count)." }
    return $rows[0]
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
$devDir = Join-Path $repoRoot '.elementize-dev'
$statusUri = "$base/wp-json/elementize/v1/status"
$auditUri = "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true"
$writeUri = "$base/wp-json/elementize/v1/pages/$PageId/design-settings"

Write-Host ''
Write-Host 'Elementize deliberate rollback regression test' -ForegroundColor Cyan
Write-Host 'Policy: start from the currently kept improvement, temporarily restore the known previous value, require a regression signal, and always restore the kept value.'
Write-Host ''

$status = Invoke-ElementizeJson -Method GET -Uri $statusUri -Authorization $auth
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green

$evidenceFile = Get-ChildItem $devDir -Filter "experiment-before-$PageId-*.json" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($null -eq $evidenceFile) { throw 'No prior accepted reversible experiment evidence was found.' }
$evidence = Get-Content $evidenceFile.FullName -Raw | ConvertFrom-Json
$plans = @($evidence.visual.repair_plan.plans)
if ($plans.Count -ne 1) { throw "Expected one prior accepted plan; found $($plans.Count)." }
$plan = $plans[0]
$side = [string]$plan.changed_component
$keptValue = $plan.proposed_value
$previousValue = $plan.expected_current_value
$topId = [string]$plan.top_level_element_id
$sourceTargetId = [string]$plan.source_target_id

Write-Host ("[EVIDENCE] Kept state expected: {0} {1} padding = {2}px; deliberate regression value = {3}px" -f $topId, $side, $keptValue.$side, $previousValue.$side) -ForegroundColor Yellow

Write-Host '[1/5] Fresh kept-state audit and exact control read...' -ForegroundColor Cyan
$beforeAudit = Invoke-ElementizeJson -Method GET -Uri $auditUri -Authorization $auth
$beforePath = Save-Audit -Audit $beforeAudit -Label 'rollback-before' -Id $PageId -Root $repoRoot
$fresh = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
if ([string]$fresh.Read.page.status -ne 'draft') { throw "Page is not a draft; current status is $($fresh.Read.page.status)." }
if (-not $fresh.Read.write_available_for_page -or -not $fresh.Control.writable_now -or [string]$fresh.Control.source -ne 'explicit') { throw 'Fresh exact target is not writable under the guarded contract.' }
if (-not (Test-BoxEqual $fresh.Control.value $keptValue)) { throw 'Current value does not match the previously kept value. Refusing deliberate regression.' }
$beforeSection = Get-TargetSection -Audit $beforeAudit -TopId $topId
Write-Host '[PASS] Current kept value and fresh rendered baseline verified.' -ForegroundColor Green

$regressionApplied = $false
$regressionRevision = $null
$restoreRevision = $null
$restored = $false
$testError = $null
$decision = 'UNKNOWN'

try {
    Write-Host '[2/5] Applying the known previous value as a deliberate temporary regression...' -ForegroundColor Cyan
    $regressionBody = New-WriteBody -DesignRead $fresh.Read -Control $fresh.Control -Value $previousValue
    $regressionWrite = Invoke-ElementizeJson -Method POST -Uri $writeUri -Authorization $auth -Body $regressionBody
    if (-not $regressionWrite.saved -or [int]$regressionWrite.updated_count -ne 1) { throw 'Regression probe writer did not confirm exactly one saved update.' }
    $regressionApplied = $true
    $regressionRevision = $regressionWrite.revision_id
    $written = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
    if (-not (Test-BoxEqual $written.Control.value $previousValue)) { throw 'Fresh read did not verify the deliberate regression value.' }
    Write-Host ("[WRITE] Regression probe revision {0} created and verified." -f $regressionRevision) -ForegroundColor Yellow

    Write-Host '[3/5] Running fresh visual completion audit on the regressed candidate...' -ForegroundColor Cyan
    $afterAudit = Invoke-ElementizeJson -Method GET -Uri $auditUri -Authorization $auth
    $afterPath = Save-Audit -Audit $afterAudit -Label 'rollback-regressed' -Id $PageId -Root $repoRoot
    $afterSection = Get-TargetSection -Audit $afterAudit -TopId $topId

    $beforeHeight = [double]$beforeSection.height
    $afterHeight = [double]$afterSection.height
    $beforeGapBefore = [double]$beforeSection.gap_before
    $afterGapBefore = [double]$afterSection.gap_before
    $beforeGapAfter = [double]$beforeSection.gap_after
    $afterGapAfter = [double]$afterSection.gap_after
    $expectedRegression = [double]$previousValue.$side
    $actualRegression = [double]$afterSection.padding.$side

    $sameTargetPromoted = @($afterAudit.visual.repair_convergence.promoted_targets | Where-Object { [string]$_.target_id -eq $sourceTargetId }).Count -gt 0
    $sameTargetPlanned = @($afterAudit.visual.repair_plan.plans | Where-Object { [string]$_.source_target_id -eq $sourceTargetId }).Count -gt 0

    $signals = [ordered]@{
        regression_value_rendered = ([Math]::Abs($actualRegression - $expectedRegression) -le 1.0)
        section_height_increased = ($afterHeight -gt ($beforeHeight + 1.0))
        gap_before_increased = ($afterGapBefore -gt ($beforeGapBefore + 1.0))
        gap_after_increased = ($afterGapAfter -gt ($beforeGapAfter + 1.0))
        same_target_repromoted = $sameTargetPromoted
        same_target_replanned = $sameTargetPlanned
    }

    $regressionDetected = [bool]$signals.section_height_increased -or [bool]$signals.gap_before_increased -or [bool]$signals.gap_after_increased -or [bool]$signals.same_target_repromoted -or [bool]$signals.same_target_replanned

    Write-Host '[4/5] Evaluating rollback signals...' -ForegroundColor Cyan
    $rows = foreach ($entry in $signals.GetEnumerator()) { [pscustomobject]@{ Signal = $entry.Key; Present = [bool]$entry.Value } }
    $rows | Format-Table -AutoSize

    if (-not [bool]$signals.regression_value_rendered) { throw 'The deliberate regression did not render at the expected value.' }
    if (-not $regressionDetected) { throw 'No regression signal was detected. The test will still restore the kept value, but rollback detection is NOT proven.' }

    $decision = 'ROLLBACK'
    Write-Host '[DECISION] ROLLBACK' -ForegroundColor Green
    Write-Host 'At least one conservative regression signal fired; the kept improvement must be restored.' -ForegroundColor Green
}
catch {
    $testError = $_
}
finally {
    if ($regressionApplied) {
        Write-Host '[5/5] Restoring the kept value...' -ForegroundColor Cyan
        try {
            $current = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
            if (-not (Test-BoxEqual $current.Control.value $previousValue)) { throw 'Cannot safely restore: current value no longer equals the deliberate regression value.' }
            $restoreBody = New-WriteBody -DesignRead $current.Read -Control $current.Control -Value $keptValue
            $restoreWrite = Invoke-ElementizeJson -Method POST -Uri $writeUri -Authorization $auth -Body $restoreBody
            if (-not $restoreWrite.saved -or [int]$restoreWrite.updated_count -ne 1) { throw 'Rollback restore writer did not confirm exactly one saved update.' }
            $restoreRevision = $restoreWrite.revision_id
            $final = Get-ExactDesignRead -Base $base -Id $PageId -Plan $plan -Authorization $auth
            if (-not (Test-BoxEqual $final.Control.value $keptValue)) { throw 'Final exact read did not verify the kept value after rollback.' }
            $restored = $true
            Write-Host ("[RESTORE] Revision {0}; kept value {1}px verified." -f $restoreRevision, $keptValue.$side) -ForegroundColor Green
        }
        catch {
            Write-Host ("[CRITICAL] Rollback restore failed: {0}" -f $_.Exception.Message) -ForegroundColor Red
            throw
        }
    }
}

if ($null -ne $testError) { throw $testError }
if ($decision -ne 'ROLLBACK' -or -not $restored) { throw 'Rollback acceptance test did not complete with a verified restored kept state.' }

Write-Host ''
Write-Host '[PASS] Deliberate regression rollback proven.' -ForegroundColor Green
Write-Host ("Regression revision: {0}" -f $regressionRevision)
Write-Host ("Rollback revision:   {0}" -f $restoreRevision)
Write-Host ("Final value: {0} {1} padding = {2}px" -f $topId, $side, $keptValue.$side)
Write-Host 'The page remains a DRAFT. Nothing was published.' -ForegroundColor Cyan

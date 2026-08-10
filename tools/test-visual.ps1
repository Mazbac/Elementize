param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [ValidateSet('focused_verification','focused_section_verification','repair_convergence','render_observations','repair_plan','repair_correlation','render_metrics','localization','all')]
    [string]$Object = 'focused_verification',

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER
)

$ErrorActionPreference = 'Stop'

function Get-PlainTextFromSecureString {
    param([Security.SecureString]$SecureString)
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
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
    finally {
        $plain = $null
    }
}

function Invoke-ElementizeCurlGet {
    param(
        [string]$Uri,
        [string]$Authorization
    )

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) {
        throw 'curl.exe is required for Windows PowerShell 5.1 local HTTPS testing.'
    }

    # Feed the Authorization header to curl over stdin via --config - so the
    # Application Password-derived credential is not placed in the process argv.
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
    if (-not $proc.Start()) {
        throw 'Could not start curl.exe.'
    }

    try {
        $proc.StandardInput.WriteLine('silent')
        $proc.StandardInput.WriteLine('show-error')
        $proc.StandardInput.WriteLine('insecure')
        $proc.StandardInput.WriteLine('header = "Authorization: ' + $Authorization + '"')
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
        if ($idx -lt 0) {
            throw 'curl.exe returned no HTTP status marker.'
        }

        $body = $stdout.Substring(0, $idx).Trim()
        $codeText = $stdout.Substring($idx + $marker.Length).Trim()
        $httpCode = 0
        [void][int]::TryParse($codeText, [ref]$httpCode)

        if ($httpCode -lt 200 -or $httpCode -ge 300) {
            $snippet = if ($body.Length -gt 500) { $body.Substring(0, 500) + '...' } else { $body }
            throw "Elementize returned HTTP $httpCode. $snippet"
        }

        if ([string]::IsNullOrWhiteSpace($body)) {
            throw 'Elementize returned an empty response body.'
        }

        return $body | ConvertFrom-Json
    }
    finally {
        if (-not $proc.HasExited) {
            try { $proc.Kill() } catch {}
        }
        $proc.Dispose()
    }
}

function Invoke-ElementizeGet {
    param(
        [string]$Uri,
        [hashtable]$Headers
    )

    if ($PSVersionTable.PSVersion.Major -lt 7) {
        return Invoke-ElementizeCurlGet -Uri $Uri -Authorization ([string]$Headers.Authorization)
    }

    $params = @{
        Uri = $Uri
        Method = 'GET'
        Headers = $Headers
        ErrorAction = 'Stop'
        SkipCertificateCheck = $true
    }
    return Invoke-RestMethod @params
}

if ([string]::IsNullOrWhiteSpace($Username)) {
    $Username = Read-Host 'WordPress username'
}

if ([string]::IsNullOrWhiteSpace($Username)) {
    throw 'A WordPress username is required.'
}

$securePassword = $null
if (-not [string]::IsNullOrWhiteSpace($env:ELEMENTIZE_WP_APP_PASSWORD)) {
    $securePassword = ConvertTo-SecureString $env:ELEMENTIZE_WP_APP_PASSWORD -AsPlainText -Force
}
else {
    $securePassword = Read-Host 'WordPress Application Password (input hidden; do not paste it into ChatGPT)' -AsSecureString
}

$auth = New-BasicAuthHeader -User $Username -Password $securePassword
$headers = @{ Authorization = $auth }
$base = $SiteUrl.TrimEnd('/')

Write-Host ''
Write-Host 'Elementize local dev test' -ForegroundColor Cyan
Write-Host "Site: $base"
Write-Host "Page: $PageId"
Write-Host "Object: $Object"
Write-Host ''

$statusUri = "$base/wp-json/elementize/v1/status"
$auditUri = "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true"

$started = Get-Date
$status = Invoke-ElementizeGet -Uri $statusUri -Headers $headers
Write-Host ("[PASS] Plugin status reachable: {0}" -f $status.elementize_version) -ForegroundColor Green

$auditStarted = Get-Date
$audit = Invoke-ElementizeGet -Uri $auditUri -Headers $headers
$auditSeconds = [Math]::Round(((Get-Date) - $auditStarted).TotalSeconds, 2)
Write-Host ("[PASS] Completion audit returned in {0}s" -f $auditSeconds) -ForegroundColor Green

$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.elementize-dev'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rawPath = Join-Path $outDir "audit-$PageId-$stamp.json"
$audit | ConvertTo-Json -Depth 100 | Set-Content -Path $rawPath -Encoding UTF8
Write-Host ("[SAVE] Full audit: {0}" -f $rawPath) -ForegroundColor DarkGray

$visual = $audit.visual
if ($null -eq $visual) {
    throw 'The completion audit did not return a visual object.'
}

$selected = switch ($Object) {
    'focused_verification' { $visual.focused_verification }
    'focused_section_verification' { $visual.focused_section_verification }
    'repair_convergence' { $visual.repair_convergence }
    'render_observations' { $visual.render_observations }
    'repair_plan' { $visual.repair_plan }
    'repair_correlation' { $visual.repair_correlation }
    'render_metrics' { $visual.render_metrics }
    'localization' { $visual.localization }
    'all' { $visual }
}

Write-Host ''
Write-Host '--- Selected result ---' -ForegroundColor Cyan
if ($null -eq $selected) {
    Write-Host "[WARN] visual.$Object was not present." -ForegroundColor Yellow
}
else {
    $selected | ConvertTo-Json -Depth 100
}

Write-Host ''
Write-Host '--- Compact health ---' -ForegroundColor Cyan
if ($visual.render_metrics) {
    if ($visual.render_metrics.available) {
        Write-Host ("[PASS] CDP metrics: {0} sections" -f $visual.render_metrics.section_count) -ForegroundColor Green
    }
    else {
        Write-Host ("[FAIL] CDP metrics: {0} | {1}" -f $visual.render_metrics.failure_stage, $visual.render_metrics.reason) -ForegroundColor Red
    }
}
if ($visual.render_observations) {
    Write-Host ("[{0}] Render observations: available={1}, count={2}" -f ($(if ($visual.render_observations.available) {'PASS'} else {'FAIL'})), $visual.render_observations.available, $visual.render_observations.observation_count) -ForegroundColor $(if ($visual.render_observations.available) {'Green'} else {'Red'})
}
if ($visual.repair_convergence) {
    Write-Host ("[{0}] Repair convergence: available={1}, promoted={2}" -f ($(if ($visual.repair_convergence.available) {'PASS'} else {'FAIL'})), $visual.repair_convergence.available, $visual.repair_convergence.promoted_target_count) -ForegroundColor $(if ($visual.repair_convergence.available) {'Green'} else {'Red'})
}
if ($visual.focused_verification) {
    Write-Host ("[{0}] Focused verification: available={1}, candidates={2}, reviewable={3}" -f ($(if ($visual.focused_verification.available) {'PASS'} else {'FAIL'})), $visual.focused_verification.available, $visual.focused_verification.candidate_count, $visual.focused_verification.hardened_convergence_review_count) -ForegroundColor $(if ($visual.focused_verification.available) {'Green'} else {'Red'})
}
if ($visual.focused_section_verification) {
    Write-Host ("[{0}] Section focus: available={1}, candidates={2}, reviewable={3}" -f ($(if ($visual.focused_section_verification.available) {'PASS'} else {'FAIL'})), $visual.focused_section_verification.available, $visual.focused_section_verification.candidate_count, $visual.focused_section_verification.reviewable_count) -ForegroundColor $(if ($visual.focused_section_verification.available) {'Green'} else {'Red'})
}

$totalSeconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
Write-Host ''
Write-Host ("Done in {0}s. No write endpoint was called." -f $totalSeconds) -ForegroundColor Cyan
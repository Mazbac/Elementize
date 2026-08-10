param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [ValidateSet('focused_verification','repair_convergence','render_observations','repair_plan','repair_correlation','render_metrics','localization','all')]
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

function Invoke-ElementizeGet {
    param(
        [string]$Uri,
        [hashtable]$Headers
    )

    $params = @{
        Uri = $Uri
        Method = 'GET'
        Headers = $Headers
        ErrorAction = 'Stop'
    }

    if ($PSVersionTable.PSVersion.Major -ge 7) {
        $params['SkipCertificateCheck'] = $true
        return Invoke-RestMethod @params
    }

    # Windows PowerShell 5.1 can negotiate an obsolete TLS default even when
    # curl.exe can reach the same Local HTTPS site. Force TLS 1.2 for this
    # request only and temporarily accept Local's development certificate.
    $oldCallback = [System.Net.ServicePointManager]::ServerCertificateValidationCallback
    $oldProtocol = [System.Net.ServicePointManager]::SecurityProtocol
    try {
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12
        [System.Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
        return Invoke-RestMethod @params
    }
    finally {
        [System.Net.ServicePointManager]::ServerCertificateValidationCallback = $oldCallback
        [System.Net.ServicePointManager]::SecurityProtocol = $oldProtocol
    }
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

$totalSeconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
Write-Host ''
Write-Host ("Done in {0}s. No write endpoint was called." -f $totalSeconds) -ForegroundColor Cyan
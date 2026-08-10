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
        throw 'curl.exe is required for Windows PowerShell 5.1 local HTTPS diagnostics.'
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

function Get-SettingPathText {
    param($Control)
    if ($null -eq $Control.setting_path) { return '' }
    return (@($Control.setting_path) -join '.')
}

function Test-FontSizeControl {
    param($Control)
    $text = (([string]$Control.control_key) + ' ' + (Get-SettingPathText $Control)).ToLowerInvariant()
    return $text -match 'font[_-]?size'
}

function Get-ControlSizePx {
    param($Control)
    $value = $Control.value
    if ($null -eq $value) { return $null }

    if ($value -is [ValueType] -or $value -is [string]) {
        $number = 0.0
        if ([double]::TryParse([string]$value, [ref]$number)) { return $number }
        return $null
    }

    $unit = [string]$value.unit
    $sizeText = [string]$value.size
    $size = 0.0
    if ($unit.ToLowerInvariant() -ne 'px') { return $null }
    if (-not [double]::TryParse($sizeText, [ref]$size)) { return $null }
    return $size
}

$repoRoot = Split-Path $PSScriptRoot -Parent
$auditDir = Join-Path $repoRoot '.elementize-dev'
$latest = Get-ChildItem -Path $auditDir -Filter "audit-$PageId-*.json" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($null -eq $latest) {
    throw "No saved audit was found for page $PageId under $auditDir. Run tools\test-visual.ps1 first."
}

$audit = Get-Content $latest.FullName -Raw | ConvertFrom-Json
$observations = @($audit.visual.render_observations.observations)
$microObservation = $observations | Where-Object { $_.type -eq 'micro_text_present' } | Select-Object -First 1

if ($null -eq $microObservation) {
    Write-Host "No micro_text_present observation exists in $($latest.Name)." -ForegroundColor Yellow
    exit 0
}

$microSamples = @($microObservation.samples)
if ($microSamples.Count -eq 0) {
    Write-Host "The micro_text_present observation has no samples in $($latest.Name)." -ForegroundColor Yellow
    exit 0
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
Write-Host 'Elementize microtext control diagnostics' -ForegroundColor Cyan
Write-Host "Audit: $($latest.Name)"
Write-Host "Page: $PageId"
Write-Host "Micro samples: $($microSamples.Count)"
Write-Host "Visual convergence: $($microObservation.visual_convergence)"
Write-Host ''

$summaryRows = @()
$detailRows = @()

foreach ($sample in $microSamples) {
    $elementId = [string]$sample.element_id
    $rendered = [double]$sample.font_size_px
    $text = [string]$sample.text
    $top = [string]$sample.top_level_element_id

    if ([string]::IsNullOrWhiteSpace($elementId)) {
        $summaryRows += [pscustomobject]@{
            Top = $top
            Element = ''
            RenderedPx = $rendered
            Controls = 0
            Typography = 0
            FontSize = 0
            WritableFontSize = 0
            ExactWritableMatch = 0
            Diagnosis = 'Rendered sample has no Elementor element_id.'
            Text = $text
        }
        continue
    }

    $encodedElement = [Uri]::EscapeDataString($elementId)
    $uri = "$base/wp-json/elementize/v1/pages/$PageId/design-settings?element_id=$encodedElement&limit=300"
    $read = Invoke-ElementizeGet -Uri $uri -Headers $headers
    $controls = @($read.controls)
    $typography = @($controls | Where-Object { $_.category -eq 'typography' })
    $fontSize = @($typography | Where-Object { Test-FontSizeControl $_ })
    $writable = @($fontSize | Where-Object { $_.writable_now -eq $true })
    $exact = @($writable | Where-Object {
        $current = Get-ControlSizePx $_
        $null -ne $current -and [Math]::Abs([double]$current - $rendered) -le 1.0
    })

    $diagnosis = if ($controls.Count -eq 0) {
        'No discovered design controls exist on this exact rendered element.'
    }
    elseif ($typography.Count -eq 0) {
        'Controls exist, but none are classified as typography on this exact element.'
    }
    elseif ($fontSize.Count -eq 0) {
        'Typography controls exist, but no explicit font-size-like control was discovered on this exact element.'
    }
    elseif ($writable.Count -eq 0) {
        'Font-size-like controls exist, but none are currently inside the guarded writable subset.'
    }
    elseif ($exact.Count -eq 0) {
        'Writable font-size controls exist, but their explicit values do not match the rendered computed font size.'
    }
    else {
        'An exact writable font-size control match exists; base convergence should have mapped this and needs investigation.'
    }

    $summaryRows += [pscustomobject]@{
        Top = $top
        Element = $elementId
        RenderedPx = $rendered
        Controls = $controls.Count
        Typography = $typography.Count
        FontSize = $fontSize.Count
        WritableFontSize = $writable.Count
        ExactWritableMatch = $exact.Count
        Diagnosis = $diagnosis
        Text = $text
    }

    foreach ($control in ($fontSize | Select-Object -First 8)) {
        $detailRows += [pscustomobject]@{
            Element = $elementId
            ControlKey = [string]$control.control_key
            Path = Get-SettingPathText $control
            Source = [string]$control.source
            Scope = [string]$control.responsive_scope
            Writable = [bool]$control.writable_now
            ExplicitPx = Get-ControlSizePx $control
            Reason = [string]$control.writable_now_reason
        }
    }
}

Write-Host '--- Per-sample diagnosis ---' -ForegroundColor Cyan
$summaryRows | Format-List

if ($detailRows.Count -gt 0) {
    Write-Host '--- Font-size controls found ---' -ForegroundColor Cyan
    $detailRows | Format-Table -AutoSize -Wrap
}
else {
    Write-Host '--- Font-size controls found ---' -ForegroundColor Cyan
    Write-Host 'None.' -ForegroundColor Yellow
}

Write-Host ''
Write-Host 'Read-only diagnostic complete. No write endpoint was called.' -ForegroundColor Cyan

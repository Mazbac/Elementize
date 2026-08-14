[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('status','start','stop','enable','disable','autostart-on','autostart-off')]
    [string]$Action,
    [Parameter(Mandatory = $true)]
    [string]$LocalOrigin
)

$ErrorActionPreference = 'Stop'

function Get-ShortSha256([string]$Value, [int]$Length = 8) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
        $hash = $sha.ComputeHash($bytes)
    } finally {
        $sha.Dispose()
    }
    $hex = -join ($hash | ForEach-Object { $_.ToString('x2') })
    return $hex.Substring(0, [Math]::Min($Length, $hex.Length))
}

$LocalOrigin = $LocalOrigin.Trim().TrimEnd('/')
try { $localUri = [Uri]$LocalOrigin } catch { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
if ($localUri.Scheme -notin @('http','https') -or -not $localUri.Host) { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
$siteSlug = ($localUri.Host.ToLowerInvariant() -replace '[^a-z0-9]+','-').Trim('-')
if (-not $siteSlug) { $siteSlug = 'site' }
if ($siteSlug.Length -gt 36) { $siteSlug = $siteSlug.Substring(0, 36).TrimEnd('-') }
$siteHash = Get-ShortSha256 $LocalOrigin.ToLowerInvariant() 8
$siteKey = "$siteSlug-$siteHash"

$sitesRoot = Join-Path (Join-Path $env:LOCALAPPDATA 'Elementize') 'sites'
$runtimeRoot = Join-Path $sitesRoot $siteKey
$settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
if (-not (Test-Path $settingsPath) -and (Test-Path $sitesRoot)) {
    $hostMatches = @()
    Get-ChildItem -Path $sitesRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        $candidateSettings = Join-Path $_.FullName 'relay-settings.json'
        if (-not (Test-Path $candidateSettings)) { return }
        try {
            $candidate = Get-Content -Raw $candidateSettings | ConvertFrom-Json
            $candidateUri = [Uri]([string]$candidate.localOrigin)
            if ($candidateUri.Host -ieq $localUri.Host) { $hostMatches += $_.FullName }
        } catch {}
    }
    if ($hostMatches.Count -eq 1) {
        $runtimeRoot = $hostMatches[0]
        $siteKey = Split-Path $runtimeRoot -Leaf
        $settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
    }
}
$startScript = Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1'
$quickLog = Join-Path $runtimeRoot 'elementize-quick-tunnel.log'
$startupDir = [Environment]::GetFolderPath('Startup')
$startupCmd = Join-Path $startupDir ("Elementize-$siteKey.cmd")

function Get-MonitorProcesses {
    @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -in @('powershell.exe','pwsh.exe') -and $_.CommandLine -and $_.CommandLine.Contains($startScript)
    })
}

function Get-TunnelProcesses {
    @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -eq 'cloudflared.exe' -and $_.CommandLine -and $_.CommandLine.Contains($quickLog)
    })
}
function Stop-LegacyRelayForSite {
    $legacyRoot = Join-Path $env:LOCALAPPDATA 'Elementize'
    $legacySettings = Join-Path $legacyRoot 'relay-settings.json'
    $legacyMatches = $false
    if (Test-Path $legacySettings) {
        try {
            $legacy = Get-Content -Raw $legacySettings | ConvertFrom-Json
            $legacyMatches = ([string]$legacy.localOrigin).Trim().TrimEnd('/') -eq $LocalOrigin
        } catch {}
    }
    if ($legacyMatches) {
        $legacyStart = Join-Path $legacyRoot 'start-free-cloudflare-relay.ps1'
        $legacyQuickLog = Join-Path $legacyRoot 'elementize-quick-tunnel.log'
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -in @('powershell.exe','pwsh.exe') -and $_.CommandLine -and $_.CommandLine.Contains($legacyStart)
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -eq 'cloudflared.exe' -and $_.CommandLine -and $_.CommandLine.Contains($legacyQuickLog)
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        $legacyStable = Join-Path $legacyRoot 'ensure-stable-tunnel.ps1'
        if (Test-Path $legacyStable) {
            $legacyText = Get-Content -Raw $legacyStable -ErrorAction SilentlyContinue
            if ([string]$legacyText -like "*$($localUri.Host)*") {
                Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
                    $_.Name -in @('powershell.exe','pwsh.exe') -and $_.CommandLine -and $_.CommandLine.Contains($legacyStable)
                } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
            }
        }
    }
}

function Write-Autostart {
    if (-not (Test-Path $startScript)) { throw 'Elementize relay runtime is not installed for this site.' }
    $cmd = "@echo off`r`nstart `"`" /min powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$startScript`"`r`n"
    Set-Content -Path $startupCmd -Value $cmd -Encoding ASCII
}

function Stop-Relay {
    Get-MonitorProcesses | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 250
    Get-TunnelProcesses | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Stop-LegacyRelayForSite
}

function Start-Relay {
    if (-not (Test-Path $settingsPath) -or -not (Test-Path $startScript)) { throw 'Elementize relay runtime is not installed for this site.' }
    Stop-LegacyRelayForSite
    if (@(Get-MonitorProcesses).Count -eq 0) {
        Start-Process powershell.exe -ArgumentList @('-NoProfile','-WindowStyle','Hidden','-ExecutionPolicy','Bypass','-File',$startScript) -WindowStyle Hidden
        Start-Sleep -Milliseconds 500
    }
}

function Get-State {
    $stableOrigin = ''
    if (Test-Path $settingsPath) {
        try { $stableOrigin = [string]((Get-Content -Raw $settingsPath | ConvertFrom-Json).stableOrigin) } catch {}
    }
    [ordered]@{
        supported = $true
        installed = (Test-Path $settingsPath) -and (Test-Path $startScript)
        running = @(Get-MonitorProcesses).Count -gt 0
        tunnel_running = @(Get-TunnelProcesses).Count -gt 0
        autostart = Test-Path $startupCmd
        site_key = $siteKey
        stable_origin = $stableOrigin
    }
}
switch ($Action) {
    'start' { Start-Relay }
    'stop' { Stop-Relay }
    'enable' { Write-Autostart; Start-Relay }
    'disable' { Stop-Relay; Remove-Item -LiteralPath $startupCmd -Force -ErrorAction SilentlyContinue }
    'autostart-on' { Write-Autostart }
    'autostart-off' { Remove-Item -LiteralPath $startupCmd -Force -ErrorAction SilentlyContinue }
    'status' { }
}

Get-State | ConvertTo-Json -Compress

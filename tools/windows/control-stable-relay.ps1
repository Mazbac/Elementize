param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('status','start','stop','enable','disable','autostart-on','autostart-off')]
    [string]$Action,
    [Parameter(Mandatory = $true)]
    [string]$LocalOrigin,
    [string]$RuntimeRoot = '',
    [string]$StartupCmd = ''
)

$ErrorActionPreference = 'Stop'

function Get-ShortSha256([string]$Value, [int]$Length = 8) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Value))
    } finally { $sha.Dispose() }
    $hex = -join ($hash | ForEach-Object { $_.ToString('x2') })
    return $hex.Substring(0, [Math]::Min($Length, $hex.Length))
}

$LocalOrigin = $LocalOrigin.Trim().TrimEnd('/')
try { $localUri = [Uri]$LocalOrigin } catch { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
if ($localUri.Scheme -notin @('http','https') -or -not $localUri.Host) { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
$siteSlug = ($localUri.Host.ToLowerInvariant() -replace '[^a-z0-9]+','-').Trim('-')
if (-not $siteSlug) { $siteSlug = 'site' }
if ($siteSlug.Length -gt 36) { $siteSlug = $siteSlug.Substring(0, 36).TrimEnd('-') }
$siteKey = "$siteSlug-$(Get-ShortSha256 $LocalOrigin.ToLowerInvariant() 8)"
$localAppData = [string]$env:LOCALAPPDATA
if (-not $localAppData -and $env:USERPROFILE) { $localAppData = Join-Path $env:USERPROFILE 'AppData\Local' }
if (-not $localAppData) { $localAppData = [Environment]::GetFolderPath('LocalApplicationData') }
$sitesRoot = if ($localAppData) { Join-Path (Join-Path $localAppData 'Elementize') 'sites' } else { '' }

$runtimeRoot = $RuntimeRoot.Trim()
if (-not $runtimeRoot) { $runtimeRoot = if ($sitesRoot) { Join-Path $sitesRoot $siteKey } else { '' } }
$settingsPath = if ($runtimeRoot) { Join-Path $runtimeRoot 'relay-settings.json' } else { '' }
if ((-not $settingsPath -or -not (Test-Path $settingsPath)) -and $sitesRoot -and (Test-Path $sitesRoot)) {
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
$startScript = if ($runtimeRoot) { Join-Path $runtimeRoot 'start-ngrok-relay.ps1' } else { '' }
$legacyStartScript = if ($runtimeRoot) { Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1' } else { '' }
$startupCmd = $StartupCmd.Trim()
if (-not $startupCmd) {
    $startupDir = [Environment]::GetFolderPath('Startup')
    if (-not $startupDir -and $env:APPDATA) { $startupDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup' }
    if (-not $startupDir -and $env:USERPROFILE) { $startupDir = Join-Path $env:USERPROFILE 'AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup' }
    if ($startupDir) { $startupCmd = Join-Path $startupDir ("Elementize-$siteKey.cmd") }
}

function Read-Settings {
    if (-not $settingsPath -or -not (Test-Path $settingsPath)) { return $null }
    try { return Get-Content -Raw $settingsPath | ConvertFrom-Json } catch { return $null }
}

function Get-MonitorProcesses {
    if (-not $startScript) { return @() }
    @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -in @('powershell.exe','pwsh.exe') -and $_.CommandLine -and $_.CommandLine.Contains($startScript)
    })
}

function Get-NgrokProcesses {
    $settings = Read-Settings
    $configPath = if ($settings) { [string]$settings.ngrokConfig } else { '' }
    if (-not $configPath) { return @() }
    @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -eq 'ngrok.exe' -and $_.CommandLine -and $_.CommandLine.Contains($configPath)
    })
}
function Test-NgrokEndpoint {
    $settings = Read-Settings
    if (-not $settings) { return $false }
    $webPort = [int]$settings.ngrokWebPort
    $expected = [string]$settings.ngrokOrigin
    if ($webPort -lt 1 -or -not $expected) { return $false }
    try {
        $state = Invoke-RestMethod -Uri "http://127.0.0.1:$webPort/api/tunnels" -Method Get -TimeoutSec 2
        foreach ($tunnel in @($state.tunnels)) {
            if (([string]$tunnel.public_url).TrimEnd('/') -eq $expected.TrimEnd('/')) { return $true }
        }
    } catch {}
    return $false
}

function Stop-LegacyRelayForSite {
    if ($legacyStartScript) {
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -in @('powershell.exe','pwsh.exe') -and $_.CommandLine -and $_.CommandLine.Contains($legacyStartScript)
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    }
    if ($runtimeRoot) {
        $legacyQuickLog = Join-Path $runtimeRoot 'elementize-quick-tunnel.log'
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -eq 'cloudflared.exe' -and $_.CommandLine -and $_.CommandLine.Contains($legacyQuickLog)
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    }
}
function Sync-RuntimeAssets {
    $settings = Read-Settings
    if (-not $settings -or [string]$settings.provider -ne 'ngrok') { throw 'Stable ngrok relay setup is required for this site.' }
    $pluginRoot = [string]$settings.pluginRoot
    if (-not $pluginRoot) { throw 'Elementize relay plugin path is missing. Rerun the relay setup.' }
    $sourceStarter = Join-Path $pluginRoot 'tools\windows\start-ngrok-relay.ps1'
    if (-not (Test-Path $sourceStarter)) { throw 'Current Elementize ngrok relay starter is missing from the plugin.' }
    Copy-Item -LiteralPath $sourceStarter -Destination $startScript -Force
}

function Write-Autostart {
    Sync-RuntimeAssets
    if (-not $startupCmd) { throw 'Windows Startup path could not be resolved for this Elementize site.' }
    $cmd = "@echo off`r`nstart `"`" /min powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action start -LocalOrigin `"$LocalOrigin`" -RuntimeRoot `"$runtimeRoot`" -StartupCmd `"$startupCmd`"`r`n"
    Set-Content -Path $startupCmd -Value $cmd -Encoding ASCII
}

function Remove-Autostart {
    if (-not $startupCmd) { throw 'Windows Startup path could not be resolved for this Elementize site.' }
    Remove-Item -LiteralPath $startupCmd -Force -ErrorAction SilentlyContinue
}

function Stop-Relay {
    Get-MonitorProcesses | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 250
    Get-NgrokProcesses | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Stop-LegacyRelayForSite
}
function Start-Relay {
    Sync-RuntimeAssets
    $settings = Read-Settings
    if (-not $settings -or [string]$settings.provider -ne 'ngrok') { throw 'Stable ngrok relay setup is required for this site.' }
    if (-not (Test-Path ([string]$settings.ngrokPath))) { throw 'ngrok executable is missing. Rerun the relay setup.' }
    if (-not (Test-Path ([string]$settings.ngrokConfig))) { throw 'ngrok config is missing. Rerun the relay setup.' }
    if (-not (Test-Path ([string]$settings.ngrokPolicy))) { throw 'ngrok Traffic Policy is missing. Rerun the relay setup.' }
    Stop-LegacyRelayForSite
    if (@(Get-MonitorProcesses).Count -eq 0) {
        Start-Process powershell.exe -ArgumentList @('-NoProfile','-WindowStyle','Hidden','-ExecutionPolicy','Bypass','-File',$startScript) -WindowStyle Hidden
        Start-Sleep -Milliseconds 500
    }
}

function Get-State {
    $settings = Read-Settings
    $provider = if ($settings) { [string]$settings.provider } else { '' }
    $stableOrigin = if ($settings) { [string]$settings.stableOrigin } else { '' }
    $ngrokOrigin = if ($settings) { [string]$settings.ngrokOrigin } else { '' }
    $legacy = [bool]($settings -and $provider -ne 'ngrok' -and $stableOrigin -match '\.workers\.dev$')
    $installed = [bool]($settings -and $provider -eq 'ngrok' -and (Test-Path $startScript) -and (Test-Path ([string]$settings.ngrokPath)) -and (Test-Path ([string]$settings.ngrokConfig)) -and (Test-Path ([string]$settings.ngrokPolicy)))
    [ordered]@{
        supported = $true
        installed = $installed
        migration_required = $legacy
        provider = $provider
        running = $installed -and @(Get-MonitorProcesses).Count -gt 0
        tunnel_running = $installed -and (Test-NgrokEndpoint)
        autostart = [bool]($startupCmd -and (Test-Path $startupCmd))
        site_key = $siteKey
        stable_origin = $stableOrigin
        ngrok_origin = $ngrokOrigin
    }
}
switch ($Action) {
    'start' { Start-Relay }
    'stop' { Stop-Relay }
    'enable' { Write-Autostart; Start-Relay }
    'disable' { Stop-Relay; Remove-Autostart }
    'autostart-on' { Write-Autostart }
    'autostart-off' { Remove-Autostart }
    'status' { }
}

Get-State | ConvertTo-Json -Compress

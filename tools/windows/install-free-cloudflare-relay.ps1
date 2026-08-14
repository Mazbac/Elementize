[CmdletBinding()]
param(
    [string]$LocalOrigin = '',
    [string]$WorkerName = ''
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Find-Cloudflared {
    $cmd = Get-Command cloudflared.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($path in @('C:\Program Files\cloudflared\cloudflared.exe','C:\Program Files (x86)\cloudflared\cloudflared.exe')) {
        if (Test-Path $path) { return $path }
    }
    return ''
}

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

$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $LocalOrigin) { $LocalOrigin = Read-Host 'Local WordPress origin (for example https://site.local)' }
$LocalOrigin = $LocalOrigin.Trim().TrimEnd('/')
try { $localUri = [Uri]$LocalOrigin } catch { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
if ($localUri.Scheme -notin @('http','https') -or -not $localUri.Host) { throw 'LocalOrigin must be a valid http:// or https:// URL.' }

$siteSlug = ($localUri.Host.ToLowerInvariant() -replace '[^a-z0-9]+','-').Trim('-')
if (-not $siteSlug) { $siteSlug = 'site' }
if ($siteSlug.Length -gt 36) { $siteSlug = $siteSlug.Substring(0, 36).TrimEnd('-') }
$siteHash = Get-ShortSha256 $LocalOrigin.ToLowerInvariant() 8
$siteKey = "$siteSlug-$siteHash"
if (-not $WorkerName) { $WorkerName = "elementize-relay-$siteKey" }
if ($WorkerName -notmatch '^[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$') {
    throw 'WorkerName must contain only lowercase letters, numbers and dashes and be at most 63 characters.'
}

Write-Step 'Check free Cloudflare tools'
$cloudflared = Find-Cloudflared
if (-not $cloudflared) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) { throw 'cloudflared is missing and winget is unavailable. Install Cloudflare cloudflared, then rerun this script.' }
    & $winget.Source install --id Cloudflare.cloudflared --exact --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { throw 'cloudflared installation failed.' }
    $cloudflared = Find-Cloudflared
    if (-not $cloudflared) { throw 'cloudflared was installed but could not be located.' }
}

$nodePath = ''
foreach ($candidate in @('C:\Program Files\nodejs\node.exe','C:\Program Files (x86)\nodejs\node.exe')) {
    if (Test-Path $candidate) { $nodePath = $candidate; break }
}
if (-not $nodePath) {
    $nodeCommand = Get-Command node.exe -ErrorAction SilentlyContinue
    if ($nodeCommand) { $nodePath = $nodeCommand.Source }
}
if (-not $nodePath) { throw 'Node.js and npm are required for the free workers.dev relay.' }
$nodeDir = Split-Path -Parent $nodePath
$npmPath = Join-Path $nodeDir 'npm.cmd'
if (-not (Test-Path $npmPath)) { throw "npm.cmd is missing beside Node.js: $nodeDir" }
$env:Path = $nodeDir + ';' + (($env:Path -split ';' | Where-Object { $_ -and $_ -ne $nodeDir }) -join ';')
Write-Host "cloudflared: $cloudflared"
Write-Host "Node: $nodePath"
Write-Host "npm: $npmPath"

$elementizeRoot = Join-Path $env:LOCALAPPDATA 'Elementize'
$runtimeRoot = Join-Path (Join-Path $elementizeRoot 'sites') $siteKey
$projectDir = Join-Path $runtimeRoot 'worker'
$wranglerDir = Join-Path $runtimeRoot 'wrangler'
New-Item -ItemType Directory -Force -Path $projectDir, $wranglerDir | Out-Null
Copy-Item -Force (Join-Path $pluginRoot 'tools\cloudflare-relay\worker.js') (Join-Path $projectDir 'worker.js')
Copy-Item -Force (Join-Path $pluginRoot 'tools\windows\start-free-cloudflare-relay.ps1') (Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1')

@{
    name = 'elementize-cloudflare-relay-tooling'
    private = $true
} | ConvertTo-Json | Set-Content -Path (Join-Path $wranglerDir 'package.json') -Encoding UTF8

Write-Step 'Check Wrangler runtime'
$wrangler = Join-Path $wranglerDir 'node_modules\.bin\wrangler.cmd'
$reuseWrangler = $false
if (Test-Path $wrangler) {
    $versionOutput = @(& $wrangler --version 2>&1)
    $versionText = ($versionOutput | ForEach-Object { [string]$_ }) -join "`n"
    $versionMatch = [regex]::Match($versionText, '\b(\d+)\.\d+\.\d+\b')
    $reuseWrangler = $LASTEXITCODE -eq 0 -and $versionMatch.Success -and [int]$versionMatch.Groups[1].Value -ge 4
    if ($reuseWrangler) { Write-Host "Wrangler: reuse $($versionMatch.Value)" }
}

if (-not $reuseWrangler) {
    Write-Step 'Install Wrangler locally once'
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($wranglerDir) } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    $nodeModules = Join-Path $wranglerDir 'node_modules'
    if (Test-Path $nodeModules) {
        1..3 | ForEach-Object {
            if (Test-Path $nodeModules) {
                try { Remove-Item -LiteralPath $nodeModules -Recurse -Force -ErrorAction Stop } catch { Start-Sleep -Milliseconds 500 }
            }
        }
    }
    Push-Location $wranglerDir
    try {
        & $npmPath install --no-audit --no-fund --save-dev wrangler@latest
        if ($LASTEXITCODE -ne 0) {
            if (Test-Path $nodeModules) { Remove-Item -LiteralPath $nodeModules -Recurse -Force -ErrorAction SilentlyContinue }
            & $npmPath install --no-audit --no-fund --save-dev wrangler@latest
        }
        if ($LASTEXITCODE -ne 0) { throw 'Wrangler installation failed after one clean retry.' }
    } finally { Pop-Location }
}
if (-not (Test-Path $wrangler)) { throw 'Wrangler executable was not created.' }

$legacyRelayDir = Join-Path $runtimeRoot 'cloudflare-relay'
if (Test-Path $legacyRelayDir) {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($legacyRelayDir) } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $legacyRelayDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Step 'Authorize the free Cloudflare account once'
$who = @(& $wrangler whoami 2>&1)
$whoText = ($who | ForEach-Object { [string]$_ }) -join "`n"
if ($LASTEXITCODE -ne 0 -or $whoText -match 'not authenticated|not logged in|login required') {
    Write-Host 'Cloudflare authorization is required once. The device flow avoids the fragile localhost callback timeout.'
    & $wrangler login --device --install-skills=false
    if ($LASTEXITCODE -ne 0) { throw 'Cloudflare authorization was not completed.' }
    $who = @(& $wrangler whoami 2>&1)
    $whoText = ($who | ForEach-Object { [string]$_ }) -join "`n"
}

$accountIdMatch = [regex]::Match($whoText, '\b[a-f0-9]{32}\b', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
if (-not $accountIdMatch.Success) { throw 'Could not determine the Cloudflare account ID after login.' }
$workersDevSubdomain = 'elementize-' + $accountIdMatch.Value.Substring(0, 12).ToLowerInvariant()

$settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
$existingStableOrigin = ''
if (Test-Path $settingsPath) {
    try {
        $existingSettings = Get-Content -Raw $settingsPath | ConvertFrom-Json
        if ([string]$existingSettings.siteKey -eq $siteKey -and [string]$existingSettings.workerName -eq $WorkerName) {
            $candidate = [string]$existingSettings.stableOrigin
            if ($candidate -match '^https://[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev$') { $existingStableOrigin = $candidate }
        }
    } catch {}
}
$settings = [ordered]@{
    siteKey = $siteKey
    localOrigin = $LocalOrigin
    pluginRoot = $pluginRoot
    cloudflared = $cloudflared
    nodeDir = $nodeDir
    projectDir = $projectDir
    wranglerDir = $wranglerDir
    workerName = $WorkerName
    workersDevSubdomain = $workersDevSubdomain
    stableOrigin = $existingStableOrigin
}
$settings | ConvertTo-Json -Depth 5 | Set-Content -Path $settingsPath -Encoding UTF8

if (-not $existingStableOrigin) {
    Write-Host "`nFIRST-TIME workers.dev registration (only if Wrangler asks):" -ForegroundColor Yellow
    Write-Host '  1. "Would you like to register a workers.dev subdomain now?" -> press Enter for Yes'
    Write-Host "  2. Subdomain -> type EXACTLY: $workersDevSubdomain" -ForegroundColor Yellow
    Write-Host '  3. Final confirmation -> press Enter for Yes'
    Write-Host 'After this one-time registration, future starts are automatic.' -ForegroundColor DarkGray
}

Write-Step 'Create the stable workers.dev relay'
$startScript = Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1'
Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'powershell.exe' -and $_.CommandLine -and $_.CommandLine.Contains($startScript) } |
    ForEach-Object { if ($_.ProcessId -ne $PID) { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $startScript -Once
if ($LASTEXITCODE -ne 0) { throw 'The first Cloudflare relay deployment failed.' }
$originFile = Join-Path $pluginRoot 'elementize-public-origin.txt'
if (-not (Test-Path $originFile)) { throw 'The stable workers.dev URL was not written back to Elementize.' }
$stableOrigin = (Get-Content -Raw $originFile).Trim()
if ($stableOrigin -notmatch '^https://[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev$') { throw 'The returned workers.dev URL is invalid.' }

Write-Step 'Install zero-hassle startup'
$startupDir = [Environment]::GetFolderPath('Startup')
foreach ($legacy in @('Elementize.cmd','Elementize Cloudflare Tunnel.cmd','Elementize Tailscale Funnel.cmd','Elementize Persistent Tunnel.cmd')) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $startupDir $legacy)
}
$startupCmd = Join-Path $startupDir ("Elementize-$siteKey.cmd")
$controller = Join-Path $pluginRoot 'tools\windows\control-free-cloudflare-relay.ps1'
$cmd = "@echo off`r`nstart `"`" /min powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$controller`" -Action start -LocalOrigin `"$LocalOrigin`" -RuntimeRoot `"$runtimeRoot`" -StartupCmd `"$startupCmd`"`r`n"
Set-Content -Path $startupCmd -Value $cmd -Encoding ASCII

$runtimePointer = [ordered]@{
    siteKey = $siteKey
    localOrigin = $LocalOrigin
    runtimeRoot = $runtimeRoot
    startupCmd = $startupCmd
    stableOrigin = $stableOrigin
}
$runtimePointer | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $pluginRoot 'elementize-relay-runtime.json') -Encoding UTF8

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $controller -Action start -LocalOrigin $LocalOrigin -RuntimeRoot $runtimeRoot -StartupCmd $startupCmd | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Elementize relay was installed but could not be started.' }

Write-Host "`nElementize free Cloudflare relay installed." -ForegroundColor Green
Write-Host "Stable CustomGPT URL: $stableOrigin"
Write-Host "Local WordPress: $LocalOrigin"
Write-Host "Startup: $startupCmd"
Write-Host 'After future Windows sign-ins the hidden relay monitor starts automatically, recreates the rotating Quick Tunnel if needed, and repoints the stable workers.dev URL.' -ForegroundColor Green
Write-Host 'This setup does not require a paid Cloudflare plan or a purchased domain.' -ForegroundColor Green

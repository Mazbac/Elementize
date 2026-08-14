[CmdletBinding()]
param(
    [string]$LocalOrigin = '',
    [string]$WorkerName = 'elementize-relay'
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

if ($WorkerName -notmatch '^[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$') {
    throw 'WorkerName must contain only lowercase letters, numbers and dashes.'
}

$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $LocalOrigin) { $LocalOrigin = Read-Host 'Local WordPress origin (for example https://site.local)' }
$LocalOrigin = $LocalOrigin.Trim().TrimEnd('/')
try { $localUri = [Uri]$LocalOrigin } catch { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
if ($localUri.Scheme -notin @('http','https') -or -not $localUri.Host) { throw 'LocalOrigin must be a valid http:// or https:// URL.' }

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

$node = Get-Command node.exe -ErrorAction SilentlyContinue
$npm = Get-Command npm.cmd -ErrorAction SilentlyContinue
if (-not $node -or -not $npm) { throw 'Node.js and npm are required for the free workers.dev relay.' }
Write-Host "cloudflared: $cloudflared"
Write-Host "Node: $($node.Source)"

$runtimeRoot = Join-Path $env:LOCALAPPDATA 'Elementize'
$workerDir = Join-Path $runtimeRoot 'cloudflare-relay'
New-Item -ItemType Directory -Force -Path $workerDir | Out-Null
Copy-Item -Force (Join-Path $pluginRoot 'tools\cloudflare-relay\worker.js') (Join-Path $workerDir 'worker.js')
Copy-Item -Force (Join-Path $pluginRoot 'tools\windows\start-free-cloudflare-relay.ps1') (Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1')

@{
    name = 'elementize-cloudflare-relay-runtime'
    private = $true
} | ConvertTo-Json | Set-Content -Path (Join-Path $workerDir 'package.json') -Encoding UTF8

Write-Step 'Install Wrangler locally'
Push-Location $workerDir
try {
    & $npm.Source install --no-audit --no-fund --save-dev wrangler@latest
    if ($LASTEXITCODE -ne 0) { throw 'Wrangler installation failed.' }
} finally { Pop-Location }
$wrangler = Join-Path $workerDir 'node_modules\.bin\wrangler.cmd'
if (-not (Test-Path $wrangler)) { throw 'Wrangler executable was not created.' }

Write-Step 'Authorize the free Cloudflare account once'
$who = @(& $wrangler whoami 2>&1)
$whoText = ($who | ForEach-Object { [string]$_ }) -join "`n"
if ($LASTEXITCODE -ne 0 -or $whoText -match 'not authenticated|not logged in|login required') {
    Write-Host 'A browser will open for Cloudflare login/authorization. No paid plan is required.'
    & $wrangler login
    if ($LASTEXITCODE -ne 0) { throw 'Cloudflare authorization was not completed.' }
}

$settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
$settings = [ordered]@{
    localOrigin = $LocalOrigin
    pluginRoot = $pluginRoot
    cloudflared = $cloudflared
    workerDir = $workerDir
    workerName = $WorkerName
    stableOrigin = ''
}
$settings | ConvertTo-Json -Depth 5 | Set-Content -Path $settingsPath -Encoding UTF8

Write-Step 'Create the stable workers.dev relay'
$startScript = Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $startScript -Once
if ($LASTEXITCODE -ne 0) { throw 'The first Cloudflare relay deployment failed.' }
$originFile = Join-Path $pluginRoot 'elementize-public-origin.txt'
if (-not (Test-Path $originFile)) { throw 'The stable workers.dev URL was not written back to Elementize.' }
$stableOrigin = (Get-Content -Raw $originFile).Trim()
if ($stableOrigin -notmatch '^https://[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev$') { throw 'The returned workers.dev URL is invalid.' }

Write-Step 'Install zero-hassle startup'
$startupDir = [Environment]::GetFolderPath('Startup')
foreach ($legacy in @('Elementize Cloudflare Tunnel.cmd','Elementize Tailscale Funnel.cmd','Elementize Persistent Tunnel.cmd')) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $startupDir $legacy)
}
$startupCmd = Join-Path $startupDir 'Elementize.cmd'
$cmd = "@echo off`r`nstart `"`" /min powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$startScript`"`r`n"
Set-Content -Path $startupCmd -Value $cmd -Encoding ASCII

Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'powershell.exe' -and $_.CommandLine -like '*start-free-cloudflare-relay.ps1*' } |
    ForEach-Object { if ($_.ProcessId -ne $PID) { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } }
Start-Process powershell.exe -ArgumentList @('-NoProfile','-WindowStyle','Hidden','-ExecutionPolicy','Bypass','-File',$startScript) -WindowStyle Hidden

Write-Host "`nElementize free Cloudflare relay installed." -ForegroundColor Green
Write-Host "Stable CustomGPT URL: $stableOrigin"
Write-Host "Local WordPress: $LocalOrigin"
Write-Host "Startup: $startupCmd"
Write-Host 'After future Windows sign-ins the hidden relay monitor starts automatically, recreates the rotating Quick Tunnel if needed, and repoints the stable workers.dev URL.' -ForegroundColor Green
Write-Host 'This setup does not require a paid Cloudflare plan or a purchased domain.' -ForegroundColor Green

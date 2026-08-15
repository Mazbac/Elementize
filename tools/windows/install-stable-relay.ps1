param(
    [string]$LocalOrigin = '',
    [string]$WorkerName = '',
    [string]$NgrokAuthtoken = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$workerNameExplicit = $PSBoundParameters.ContainsKey('WorkerName') -and -not [string]::IsNullOrWhiteSpace($WorkerName)

function Write-Step([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Get-ShortSha256([string]$Value, [int]$Length = 8) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Value)) }
    finally { $sha.Dispose() }
    $hex = -join ($hash | ForEach-Object { $_.ToString('x2') })
    return $hex.Substring(0, [Math]::Min($Length, $hex.Length))
}

function ConvertTo-YamlScalar([string]$Value) {
    return "'" + $Value.Replace("'", "''") + "'"
}
function Find-Node {
    foreach ($path in @('C:\Program Files\nodejs\node.exe','C:\Program Files (x86)\nodejs\node.exe')) {
        if (Test-Path $path) { return $path }
    }
    $cmd = Get-Command node.exe -ErrorAction SilentlyContinue
    return if ($cmd) { $cmd.Source } else { '' }
}

function Find-Ngrok([string]$RuntimeRoot) {
    $cmd = Get-Command ngrok.exe -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -and (Test-Path $cmd.Source)) { return $cmd.Source }
    $candidates = @('C:\Program Files\ngrok\ngrok.exe')
    if ($env:LOCALAPPDATA) {
        $candidates += Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links\ngrok.exe'
        $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
        if (Test-Path $packages) {
            $candidates += Get-ChildItem -Path $packages -Directory -Filter 'Ngrok.Ngrok_*' -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName 'ngrok.exe' }
        }
    }
    foreach ($path in $candidates) {
        if ($path -and (Test-Path $path)) { return $path }
    }
    return ''
}

function Assert-NgrokSignature([string]$Path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch 'O="?ngrok, Inc\."?') {
        throw 'The installed ngrok executable does not have a valid ngrok, Inc. Authenticode signature. Elementize will not run it.'
    }
}

function Get-NgrokVersion([string]$Path) {
    $output = @(& $Path version 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'The signed ngrok executable could not be started.' }
    $text = ($output | ForEach-Object { [string]$_ }) -join ' '
    $match = [regex]::Match($text, '(\d+\.\d+\.\d+)')
    if (-not $match.Success) { throw "Could not determine ngrok version from: $text" }
    return [Version]$match.Groups[1].Value
}

function Find-FreePort([int]$Start = 4040, [int]$Count = 300) {
    foreach ($port in $Start..($Start + $Count)) {
        $listener = $null
        try {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $port)
            $listener.Start()
            $listener.Stop()
            return $port
        } catch {
            if ($listener) { try { $listener.Stop() } catch {} }
        }
    }
    throw 'No free local port could be found for the ngrok status API.'
}

function Get-NgrokEndpoint([int]$Port) {
    try {
        $state = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/tunnels" -Method Get -TimeoutSec 3
        foreach ($tunnel in @($state.tunnels)) {
            $url = ([string]$tunnel.public_url).TrimEnd('/')
            if ($url.StartsWith('https://')) { return $url }
        }
    } catch {}
    return ''
}

function Get-HttpStatus([string]$Url) {
    try {
        $response = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 12 -Headers @{ 'ngrok-skip-browser-warning' = '1' }
        return [int]$response.StatusCode
    } catch {
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            return [int]$_.Exception.Response.StatusCode
        }
        throw "Could not reach $Url : $($_.Exception.Message)"
    }
}

function Stop-SiteRelayProcesses([string]$RuntimeRoot) {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.CommandLine -and $_.CommandLine.Contains($RuntimeRoot) -and
        $_.Name -in @('powershell.exe','pwsh.exe','cloudflared.exe','ngrok.exe')
    } | ForEach-Object {
        if ($_.ProcessId -ne $PID) {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
}

$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $LocalOrigin) {
    $LocalOrigin = Read-Host 'Local WordPress origin (for example https://site.local)'
}
$LocalOrigin = $LocalOrigin.Trim().TrimEnd('/')
try { $localUri = [Uri]$LocalOrigin }
catch { throw 'LocalOrigin must be a valid http:// or https:// URL.' }
if ($localUri.Scheme -notin @('http','https') -or -not $localUri.Host) {
    throw 'LocalOrigin must be a valid http:// or https:// URL.'
}
$siteSlug = ($localUri.Host.ToLowerInvariant() -replace '[^a-z0-9]+','-').Trim('-')
if (-not $siteSlug) { $siteSlug = 'site' }
if ($siteSlug.Length -gt 36) {
    $siteSlug = $siteSlug.Substring(0, 36).TrimEnd('-')
}
$siteHash = Get-ShortSha256 $LocalOrigin.ToLowerInvariant() 8
$siteKey = "$siteSlug-$siteHash"
if (-not $WorkerName) { $WorkerName = "elementize-relay-$siteKey" }

$localAppData = [string]$env:LOCALAPPDATA
if (-not $localAppData -and $env:USERPROFILE) {
    $localAppData = Join-Path $env:USERPROFILE 'AppData\Local'
}
if (-not $localAppData) {
    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
}
if (-not $localAppData) { throw 'Windows LocalAppData could not be resolved.' }

$elementizeRoot = Join-Path $localAppData 'Elementize'
$runtimeRoot = Join-Path (Join-Path $elementizeRoot 'sites') $siteKey
$projectDir = Join-Path $runtimeRoot 'worker'
$wranglerDir = Join-Path $runtimeRoot 'wrangler'
$settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
$ngrokConfig = Join-Path $runtimeRoot 'ngrok.yml'
$ngrokPolicy = Join-Path $runtimeRoot 'ngrok-policy.yml'
New-Item -ItemType Directory -Force -Path $runtimeRoot,$projectDir,$wranglerDir | Out-Null
$existing = $null
if (Test-Path $settingsPath) {
    try { $existing = Get-Content -Raw $settingsPath | ConvertFrom-Json } catch {}
}
if (-not $workerNameExplicit -and $existing -and [string]$existing.workerName) {
    $WorkerName = [string]$existing.workerName
}
if ($WorkerName -notmatch '^[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$') {
    throw 'WorkerName must contain only lowercase letters, numbers and dashes and be at most 63 characters.'
}
$existingStableOrigin = if ($existing) { [string]$existing.stableOrigin } else { '' }
$existingNgrokOrigin = if ($existing -and [string]$existing.provider -eq 'ngrok') {
    [string]$existing.ngrokOrigin
} else { '' }
$existingWebPort = if ($existing -and [string]$existing.provider -eq 'ngrok') {
    [int]$existing.ngrokWebPort
} else { 0 }

Stop-SiteRelayProcesses $runtimeRoot
Start-Sleep -Milliseconds 300

Write-Step 'Check stable ngrok transport'
$ngrok = Find-Ngrok $runtimeRoot
if (-not $ngrok) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw 'ngrok is not installed and WinGet is unavailable. Install ngrok from the Microsoft Store or WinGet, then rerun Elementize setup.'
    }
    Write-Host 'ngrok is not installed. Installing the official ngrok Windows package through WinGet.'
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $winget.Source install --id Ngrok.Ngrok --exact --accept-package-agreements --accept-source-agreements --silent
        $wingetExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($wingetExit -ne 0) { throw 'WinGet could not install ngrok.' }
    $ngrok = Find-Ngrok $runtimeRoot
}
if (-not $ngrok -or -not (Test-Path $ngrok)) {
    throw 'WinGet installed ngrok but Elementize could not locate the package binary. Close PowerShell, open a new PowerShell window, and rerun the same setup command.'
}
Assert-NgrokSignature $ngrok
$minimumNgrokVersion = [Version]'3.12.1'
$ngrokVersion = Get-NgrokVersion $ngrok
if ($ngrokVersion -lt $minimumNgrokVersion) {
    Write-Host "ngrok $ngrokVersion is too old for Elementize Traffic Policy. Updating through ngrok's built-in verified updater." -ForegroundColor Yellow
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $ngrok update; $updateExit = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previousPreference }
    if ($updateExit -ne 0) { throw 'ngrok could not update itself to the minimum supported version 3.12.1.' }
    Assert-NgrokSignature $ngrok
    $ngrokVersion = Get-NgrokVersion $ngrok
}
if ($ngrokVersion -lt $minimumNgrokVersion) { throw "ngrok $ngrokVersion is too old. Elementize requires ngrok 3.12.1 or newer." }
Write-Host "ngrok: version $ngrokVersion"
if (-not $NgrokAuthtoken -and (Test-Path $ngrokConfig)) {
    $tokenLine = Get-Content $ngrokConfig | Where-Object { $_ -match '^\s*authtoken:\s*' } | Select-Object -First 1
    if ($tokenLine) {
        $NgrokAuthtoken = ($tokenLine -replace '^\s*authtoken:\s*', '').Trim()
        $NgrokAuthtoken = $NgrokAuthtoken.Trim([char[]]@([char]39,[char]34))
    }
}
if (-not $NgrokAuthtoken) {
    Write-Host "`nA free ngrok account is required once. The browser will open the authtoken page." -ForegroundColor Yellow
    Start-Process 'https://dashboard.ngrok.com/get-started/your-authtoken'
    $secureToken = Read-Host 'Paste your ngrok authtoken' -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
    try { $NgrokAuthtoken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}
if (-not $NgrokAuthtoken -or $NgrokAuthtoken.Length -lt 20) {
    throw 'A valid ngrok authtoken is required.'
}

$ngrokWebPort = if ($existingWebPort -gt 0) { $existingWebPort } else { Find-FreePort }
$ngrokLog = Join-Path $runtimeRoot 'ngrok.log'
$configLines = @(
    'version: 2',
    ('authtoken: ' + (ConvertTo-YamlScalar $NgrokAuthtoken)),
    ('web_addr: ' + (ConvertTo-YamlScalar "127.0.0.1:$ngrokWebPort")),
    ('log: ' + (ConvertTo-YamlScalar $ngrokLog)),
    'log_format: json',
    'update_check: true'
)
$configLines | Set-Content -Path $ngrokConfig -Encoding UTF8

$hostValue = ConvertTo-YamlScalar $localUri.Host
$policyLines = @(
    'on_http_request:',
    '  - expressions:',
    '      - "!req.url.path.startsWith(''/wp-json/elementize/v1/'')"',
    '    actions:',
    '      - type: custom-response',
    '        config:',
    '          status_code: 404',
    '          body: Not found',
    '  - actions:',
    '      - type: add-headers',
    '        config:',
    '          headers:',
    ("            host: $hostValue")
)
$policyLines | Set-Content -Path $ngrokPolicy -Encoding UTF8

& $ngrok config check --config $ngrokConfig | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'ngrok configuration validation failed.' }

Write-Step 'Start the assigned ngrok development endpoint'
$bootstrapArgs = @('http',$LocalOrigin,'--config',$ngrokConfig,'--traffic-policy-file',$ngrokPolicy)
$bootstrapNgrok = Start-Process -FilePath $ngrok -ArgumentList $bootstrapArgs -WindowStyle Hidden -PassThru
$ngrokOrigin = ''
try {
    for ($i = 0; $i -lt 45; $i++) {
        Start-Sleep -Seconds 1
        if ($bootstrapNgrok.HasExited) {
            $tail = if (Test-Path $ngrokLog) { (Get-Content $ngrokLog -Tail 8) -join "`n" } else { '' }
            throw "ngrok exited before the endpoint became ready. $tail"
        }
        $ngrokOrigin = Get-NgrokEndpoint $ngrokWebPort
        if ($ngrokOrigin) { break }
    }
    if (-not $ngrokOrigin) { throw 'ngrok did not return a public development domain.' }
    $ngrokHost = ([Uri]$ngrokOrigin).Host.ToLowerInvariant()
    if ($ngrokHost -notmatch '\.ngrok(-free)?\.(app|dev)$') {
        throw "Unexpected ngrok development domain: $ngrokOrigin"
    }
    if ($existingNgrokOrigin -and $ngrokOrigin.TrimEnd('/') -ne $existingNgrokOrigin.TrimEnd('/')) {
        throw "ngrok returned a different development domain than this Elementize site expects. Expected $existingNgrokOrigin but received $ngrokOrigin."
    }
    Write-Host "Stable ngrok origin: $ngrokOrigin" -ForegroundColor Green

    $rootStatus = Get-HttpStatus ($ngrokOrigin + '/')
    if ($rootStatus -ne 404) {
        throw "ngrok path restriction failed: root returned HTTP $rootStatus instead of 404."
    }
    $apiStatus = Get-HttpStatus ($ngrokOrigin + '/wp-json/elementize/v1/status')
    if ($apiStatus -notin @(200,401,403)) {
        throw "Elementize API was not reachable through ngrok (HTTP $apiStatus)."
    }
    Write-Host "ngrok path guard: OK; Elementize API reachability: HTTP $apiStatus" -ForegroundColor Green
} finally {
    if ($bootstrapNgrok -and -not $bootstrapNgrok.HasExited) {
        Stop-Process -Id $bootstrapNgrok.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 300
    }
}

Write-Step 'Check Cloudflare Worker tooling'
$nodePath = Find-Node
if (-not $nodePath) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw 'Node.js is required for the one-time workers.dev deployment and WinGet is unavailable.'
    }
    & $winget.Source install --id OpenJS.NodeJS.LTS --exact --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { throw 'Node.js installation failed.' }
    $nodePath = Find-Node
}
if (-not $nodePath) { throw 'Node.js could not be located after installation.' }
$nodeDir = Split-Path -Parent $nodePath
$npmPath = Join-Path $nodeDir 'npm.cmd'
if (-not (Test-Path $npmPath)) { throw "npm.cmd is missing beside Node.js: $nodeDir" }
$env:Path = $nodeDir + ';' + (($env:Path -split ';' | Where-Object { $_ -and $_ -ne $nodeDir }) -join ';')
Write-Host "Node: $nodePath"
Copy-Item -Force (Join-Path $pluginRoot 'tools\cloudflare-relay\worker.js') (Join-Path $projectDir 'worker.js')
@{ name = 'elementize-cloudflare-relay-tooling'; private = $true } |
    ConvertTo-Json | Set-Content -Path (Join-Path $wranglerDir 'package.json') -Encoding UTF8
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
    $nodeModules = Join-Path $wranglerDir 'node_modules'
    if (Test-Path $nodeModules) {
        Remove-Item -LiteralPath $nodeModules -Recurse -Force -ErrorAction SilentlyContinue
    }
    Push-Location $wranglerDir
    try {
        & $npmPath install --no-audit --no-fund --save-dev wrangler@latest
        if ($LASTEXITCODE -ne 0) { throw 'Wrangler installation failed.' }
    } finally { Pop-Location }
}
if (-not (Test-Path $wrangler)) { throw 'Wrangler executable was not created.' }
Write-Step 'Authorize the free Cloudflare account once'
$who = @(& $wrangler whoami 2>&1)
$whoText = ($who | ForEach-Object { [string]$_ }) -join "`n"
if ($LASTEXITCODE -ne 0 -or $whoText -match 'not authenticated|not logged in|login required') {
    & $wrangler login --device --install-skills=false
    if ($LASTEXITCODE -ne 0) { throw 'Cloudflare authorization was not completed.' }
    $who = @(& $wrangler whoami 2>&1)
    $whoText = ($who | ForEach-Object { [string]$_ }) -join "`n"
}
$accountIdMatch = [regex]::Match(
    $whoText,
    '\b[a-f0-9]{32}\b',
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (-not $accountIdMatch.Success) { throw 'Could not determine the Cloudflare account ID after login.' }

$originFile = Join-Path $pluginRoot 'elementize-public-origin.txt'
if (-not $existingStableOrigin -and (Test-Path $originFile)) {
    $candidate = (Get-Content -Raw $originFile).Trim()
    if ($candidate -match '^https://[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev$') {
        $existingStableOrigin = $candidate
    }
}

$workersDevSubdomain = ''
if ($existing -and [string]$existing.workersDevSubdomain) {
    $workersDevSubdomain = [string]$existing.workersDevSubdomain
}
if (-not $workersDevSubdomain -and $existingStableOrigin -match '^https://[^.]+\.([^.]+)\.workers\.dev$') {
    $workersDevSubdomain = $Matches[1]
}
if (-not $workersDevSubdomain) {
    $workersDevSubdomain = 'elementize-' + $accountIdMatch.Value.Substring(0, 12).ToLowerInvariant()
}

$workerConfig = [ordered]@{
    name = $WorkerName
    main = 'worker.js'
    compatibility_date = '2026-08-15'
    no_bundle = $true
    workers_dev = $true
    vars = [ordered]@{ TARGET_ORIGIN = $ngrokOrigin }
}
$workerConfig | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $projectDir 'wrangler.jsonc') -Encoding UTF8

if (-not $existingStableOrigin) {
    Write-Host "`nFIRST-TIME workers.dev registration (only if Wrangler asks):" -ForegroundColor Yellow
    Write-Host '  1. Register workers.dev now -> press Enter for Yes'
    Write-Host "  2. Subdomain -> type EXACTLY: $workersDevSubdomain" -ForegroundColor Yellow
    Write-Host '  3. Final confirmation -> press Enter for Yes'
    Push-Location $projectDir
    try {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & $wrangler deploy; $firstDeployExit = $LASTEXITCODE }
        finally { $ErrorActionPreference = $previousPreference }
    } finally { Pop-Location }
    if ($firstDeployExit -ne 0) {
        throw "Initial workers.dev deployment failed with code $firstDeployExit."
    }
}

Write-Step 'Point the stable workers.dev URL at ngrok'
Push-Location $projectDir
try {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $deployOutput = @(& $wrangler deploy 2>&1 | Tee-Object -FilePath (Join-Path $runtimeRoot 'wrangler-deploy.log'))
        $deployExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
} finally { Pop-Location }
if ($deployExit -ne 0) { throw "Wrangler deploy failed with code $deployExit." }

$joined = ($deployOutput | ForEach-Object { [string]$_ }) -join "`n"
$stableMatch = [regex]::Match($joined, 'https://[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev')
$deployedStableOrigin = if ($stableMatch.Success) { $stableMatch.Value } else { '' }
if ($existingStableOrigin) {
    if ($deployedStableOrigin -and $deployedStableOrigin -ne $existingStableOrigin) {
        throw "Migration attempted to change the stable workers.dev URL. Expected $existingStableOrigin but deployed $deployedStableOrigin."
    }
    $stableOrigin = $existingStableOrigin
} else {
    $stableOrigin = $deployedStableOrigin
}
if (-not $stableOrigin) { throw 'Wrangler deployed the relay but its workers.dev URL could not be resolved.' }
Set-Content -Path $originFile -Value $stableOrigin -Encoding ASCII
$startScript = Join-Path $runtimeRoot 'start-ngrok-relay.ps1'
Copy-Item -Force (Join-Path $pluginRoot 'tools\windows\start-ngrok-relay.ps1') $startScript
$settings = [ordered]@{
    provider = 'ngrok'
    siteKey = $siteKey
    localOrigin = $LocalOrigin
    pluginRoot = $pluginRoot
    ngrokPath = $ngrok
    ngrokConfig = $ngrokConfig
    ngrokPolicy = $ngrokPolicy
    ngrokOrigin = $ngrokOrigin
    ngrokWebPort = $ngrokWebPort
    nodeDir = $nodeDir
    projectDir = $projectDir
    wranglerDir = $wranglerDir
    workerName = $WorkerName
    workersDevSubdomain = $workersDevSubdomain
    stableOrigin = $stableOrigin
}
$settings | ConvertTo-Json -Depth 5 | Set-Content -Path $settingsPath -Encoding UTF8

Remove-Item -LiteralPath (Join-Path $runtimeRoot 'start-free-cloudflare-relay.ps1') -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $runtimeRoot 'elementize-quick-tunnel.log') -Force -ErrorAction SilentlyContinue

Write-Step 'Install restart-safe relay controls'
$startupDir = [Environment]::GetFolderPath('Startup')
if (-not $startupDir -and $env:APPDATA) {
    $startupDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup'
}
if (-not $startupDir -and $env:USERPROFILE) {
    $startupDir = Join-Path $env:USERPROFILE 'AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup'
}
if (-not $startupDir) { throw 'Windows Startup folder could not be resolved.' }
foreach ($legacy in @(
    'Elementize.cmd',
    'Elementize Cloudflare Tunnel.cmd',
    'Elementize Tailscale Funnel.cmd',
    'Elementize Persistent Tunnel.cmd'
)) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $startupDir $legacy)
}
$startupCmd = Join-Path $startupDir ("Elementize-$siteKey.cmd")
$runtimePointer = [ordered]@{
    provider = 'ngrok'
    siteKey = $siteKey
    localOrigin = $LocalOrigin
    runtimeRoot = $runtimeRoot
    startupCmd = $startupCmd
    stableOrigin = $stableOrigin
    ngrokOrigin = $ngrokOrigin
}
$runtimePointer | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $pluginRoot 'elementize-relay-runtime.json') -Encoding UTF8

$controller = Join-Path $pluginRoot 'tools\windows\control-stable-relay.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $controller -Action enable -LocalOrigin $LocalOrigin -RuntimeRoot $runtimeRoot -StartupCmd $startupCmd | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Elementize stable relay was configured but could not be started.' }
$tunnelReady = $false
for ($i = 0; $i -lt 45; $i++) {
    Start-Sleep -Seconds 1
    $stateJson = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $controller -Action status -LocalOrigin $LocalOrigin -RuntimeRoot $runtimeRoot -StartupCmd $startupCmd
    try { $state = $stateJson | ConvertFrom-Json } catch { $state = $null }
    if ($state -and $state.tunnel_running) {
        $tunnelReady = $true
        break
    }
}
if (-not $tunnelReady) { throw 'ngrok was configured but did not become ready after startup.' }

$workerStatus = Get-HttpStatus ($stableOrigin + '/wp-json/elementize/v1/status')
if ($workerStatus -notin @(200,401,403)) {
    throw "Stable workers.dev relay did not reach WordPress (HTTP $workerStatus)."
}

Write-Host "`nElementize stable relay installed." -ForegroundColor Green
Write-Host "Stable CustomGPT URL: $stableOrigin"
Write-Host "Stable ngrok transport: $ngrokOrigin"
Write-Host "Local WordPress: $LocalOrigin"
Write-Host "Startup: $startupCmd"
Write-Host 'Future Windows sign-ins only restart the same assigned ngrok development domain; no rotating Quick Tunnel is used.' -ForegroundColor Green
Write-Host 'The public ngrok endpoint is restricted to /wp-json/elementize/v1/* and WordPress authentication is still required.' -ForegroundColor Green
Write-Host 'This setup uses a free ngrok development domain and the Cloudflare Workers Free plan; no custom domain is required.' -ForegroundColor Green

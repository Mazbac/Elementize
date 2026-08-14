[CmdletBinding()]
param([switch]$Once)

$ErrorActionPreference = 'Stop'
$runtimeRoot = Join-Path $env:LOCALAPPDATA 'Elementize'
$settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
$runtimeLog = Join-Path $runtimeRoot 'relay-runtime.log'
$quickLog = Join-Path $runtimeRoot 'elementize-quick-tunnel.log'

function Write-RelayLog([string]$Message) {
    $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $Message"
    Add-Content -Path $runtimeLog -Value $line -Encoding UTF8
    if ($Once) { Write-Host $line }
}

if (-not (Test-Path $settingsPath)) {
    throw "Elementize relay settings are missing: $settingsPath"
}

$mutex = New-Object System.Threading.Mutex($false, 'Local\ElementizeCloudflareRelay')
if (-not $mutex.WaitOne(0)) {
    Write-RelayLog 'Another Elementize relay monitor is already running.'
    exit 0
}

try {
    while ($true) {
        $cloudflaredProcess = $null
        try {
            $settings = Get-Content -Raw $settingsPath | ConvertFrom-Json
            $cloudflared = [string]$settings.cloudflared
            $localOrigin = [string]$settings.localOrigin
            $pluginRoot = [string]$settings.pluginRoot
            $workerDir = [string]$settings.workerDir
            $workerName = [string]$settings.workerName
            $nodeDir = [string]$settings.nodeDir
            if (-not $nodeDir) {
                foreach ($candidate in @('C:\Program Files\nodejs','C:\Program Files (x86)\nodejs')) {
                    if (Test-Path (Join-Path $candidate 'node.exe')) { $nodeDir = $candidate; break }
                }
            }
            if (-not $nodeDir -or -not (Test-Path (Join-Path $nodeDir 'node.exe'))) { throw 'Permanent Node.js runtime is missing.' }
            $env:Path = $nodeDir + ';' + (($env:Path -split ';' | Where-Object { $_ -and $_ -ne $nodeDir }) -join ';')
            if (-not (Test-Path $cloudflared)) { throw 'cloudflared executable is missing.' }
            if (-not (Test-Path $workerDir)) { throw 'Elementize Cloudflare Worker runtime is missing.' }
            $wrangler = Join-Path $workerDir 'node_modules\.bin\wrangler.cmd'
            if (-not (Test-Path $wrangler)) { throw 'Wrangler runtime is missing. Run the Elementize relay installer again.' }

            try { $localUri = [Uri]$localOrigin } catch { throw 'Configured localOrigin is invalid.' }
            if ($localUri.Scheme -notin @('http','https') -or -not $localUri.Host) { throw 'Configured localOrigin is invalid.' }

            Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -eq 'cloudflared.exe' -and $_.CommandLine -like '*elementize-quick-tunnel.log*' } |
                ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

            Set-Content -Path $quickLog -Value '' -Encoding UTF8
            $arguments = @('tunnel','--no-autoupdate','--url',$localOrigin,'--http-host-header',$localUri.Host,'--loglevel','info','--logfile',$quickLog)
            if ($localUri.Scheme -eq 'https') { $arguments += '--no-tls-verify' }
            Write-RelayLog "Starting Cloudflare Quick Tunnel for $localOrigin"
            $cloudflaredProcess = Start-Process -FilePath $cloudflared -ArgumentList $arguments -WindowStyle Hidden -PassThru

            $quickOrigin = ''
            for ($i = 0; $i -lt 60; $i++) {
                Start-Sleep -Seconds 1
                if ($cloudflaredProcess.HasExited) { throw "cloudflared exited with code $($cloudflaredProcess.ExitCode)." }
                $text = Get-Content -Raw $quickLog -ErrorAction SilentlyContinue
                $match = [regex]::Match([string]$text, 'https://[a-z0-9-]+\.trycloudflare\.com')
                if ($match.Success) { $quickOrigin = $match.Value; break }
            }
            if (-not $quickOrigin) { throw 'Cloudflare did not return a Quick Tunnel URL.' }
            Write-RelayLog "Quick Tunnel ready: $quickOrigin"

            $config = [ordered]@{
                name = $workerName
                main = 'worker.js'
                compatibility_date = '2026-08-14'
                no_bundle = $true
                workers_dev = $true
                vars = [ordered]@{ TARGET_ORIGIN = $quickOrigin }
            }
            $config | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $workerDir 'wrangler.jsonc') -Encoding UTF8

            Push-Location $workerDir
            try {
                $deployOutput = @(& $wrangler deploy 2>&1 | Tee-Object -FilePath (Join-Path $runtimeRoot 'wrangler-deploy.log'))
                if ($LASTEXITCODE -ne 0) { throw "Wrangler deploy failed with code $LASTEXITCODE." }
            } finally { Pop-Location }

            $joined = ($deployOutput | ForEach-Object { [string]$_ }) -join "`n"
            $stableMatch = [regex]::Match($joined, 'https://[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev')
            $stableOrigin = if ($stableMatch.Success) { $stableMatch.Value } else { [string]$settings.stableOrigin }
            if (-not $stableOrigin) { throw 'Wrangler deployed the relay but its workers.dev URL could not be resolved.' }

            $originFile = Join-Path $pluginRoot 'elementize-public-origin.txt'
            Set-Content -Path $originFile -Value $stableOrigin -Encoding ASCII
            if ([string]$settings.stableOrigin -ne $stableOrigin) {
                $settings.stableOrigin = $stableOrigin
                $settings | ConvertTo-Json -Depth 5 | Set-Content -Path $settingsPath -Encoding UTF8
            }
            Write-RelayLog "Stable Elementize URL: $stableOrigin -> $quickOrigin"

            if ($Once) {
                Stop-Process -Id $cloudflaredProcess.Id -Force -ErrorAction SilentlyContinue
                return
            }

            Wait-Process -Id $cloudflaredProcess.Id
            Write-RelayLog 'Quick Tunnel stopped; rebuilding the relay target.'
            Start-Sleep -Seconds 5
        } catch {
            Write-RelayLog ("Relay error: " + $_.Exception.Message)
            if ($cloudflaredProcess -and -not $cloudflaredProcess.HasExited) {
                Stop-Process -Id $cloudflaredProcess.Id -Force -ErrorAction SilentlyContinue
            }
            if ($Once) { throw }
            Start-Sleep -Seconds 15
        }
    }
} finally {
    try { $mutex.ReleaseMutex() } catch {}
    $mutex.Dispose()
}

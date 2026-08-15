$ErrorActionPreference = 'Stop'
$runtimeRoot = Split-Path -Parent $PSCommandPath
$settingsPath = Join-Path $runtimeRoot 'relay-settings.json'
$runtimeLog = Join-Path $runtimeRoot 'relay-runtime.log'

function Write-RelayLog([string]$Message) {
    $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $Message"
    Add-Content -Path $runtimeLog -Value $line -Encoding UTF8
}

if (-not (Test-Path $settingsPath)) { throw "Elementize relay settings are missing: $settingsPath" }
$settings = Get-Content -Raw $settingsPath | ConvertFrom-Json
if ([string]$settings.provider -ne 'ngrok') { throw 'This Elementize runtime has not been migrated to the stable ngrok transport.' }
$siteKey = [string]$settings.siteKey
if (-not $siteKey) { $siteKey = Split-Path $runtimeRoot -Leaf }
$mutexName = 'Local\ElementizeNgrokRelay_' + ($siteKey -replace '[^A-Za-z0-9_-]','_')
$mutex = New-Object System.Threading.Mutex($false, $mutexName)
if (-not $mutex.WaitOne(0)) {
    Write-RelayLog 'Another Elementize relay monitor is already running.'
    exit 0
}

function Get-NgrokEndpoint([int]$Port, [string]$ExpectedOrigin) {
    try {
        $state = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/tunnels" -Method Get -TimeoutSec 3
        foreach ($tunnel in @($state.tunnels)) {
            $url = [string]$tunnel.public_url
            if (-not $url -or -not $url.StartsWith('https://')) { continue }
            if (-not $ExpectedOrigin -or $url.TrimEnd('/') -eq $ExpectedOrigin.TrimEnd('/')) { return $url.TrimEnd('/') }
        }
    } catch {}
    return ''
}

function Stop-OwnNgrok([string]$ConfigPath) {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq 'ngrok.exe' -and $_.CommandLine -and $_.CommandLine.Contains($ConfigPath) } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

$retryDelaySeconds = 5
try {
    while ($true) {
        $ngrokProcess = $null
        try {
            $settings = Get-Content -Raw $settingsPath | ConvertFrom-Json
            $ngrok = [string]$settings.ngrokPath
            $configPath = [string]$settings.ngrokConfig
            $policyPath = [string]$settings.ngrokPolicy
            $localOrigin = [string]$settings.localOrigin
            $expectedOrigin = [string]$settings.ngrokOrigin
            $webPort = [int]$settings.ngrokWebPort
            if (-not $ngrok -or -not (Test-Path $ngrok)) { throw 'ngrok executable is missing. Rerun the Elementize relay setup.' }
            if (-not $configPath -or -not (Test-Path $configPath)) { throw 'ngrok config is missing. Rerun the Elementize relay setup.' }
            if (-not $policyPath -or -not (Test-Path $policyPath)) { throw 'ngrok Traffic Policy is missing. Rerun the Elementize relay setup.' }
            if (-not $localOrigin) { throw 'Local WordPress origin is missing from relay settings.' }
            if (-not $expectedOrigin) { throw 'Stable ngrok development domain is missing from relay settings.' }
            if ($webPort -lt 1) { throw 'ngrok local API port is missing from relay settings.' }
            Stop-OwnNgrok $configPath
            Write-RelayLog "Starting ngrok endpoint: $expectedOrigin"
            $arguments = @('http',$localOrigin,'--config',$configPath,'--traffic-policy-file',$policyPath)
            $ngrokProcess = Start-Process -FilePath $ngrok -ArgumentList $arguments -WindowStyle Hidden -PassThru

            $activeOrigin = ''
            for ($i = 0; $i -lt 45; $i++) {
                Start-Sleep -Seconds 1
                if ($ngrokProcess.HasExited) { throw "ngrok exited with code $($ngrokProcess.ExitCode)." }
                $activeOrigin = Get-NgrokEndpoint $webPort $expectedOrigin
                if ($activeOrigin) { break }
            }
            if (-not $activeOrigin) { throw 'ngrok did not expose the configured development domain.' }

            Write-RelayLog "ngrok ready: $activeOrigin"
            $retryDelaySeconds = 5
            Wait-Process -Id $ngrokProcess.Id
            Write-RelayLog 'ngrok stopped; restarting the stable endpoint.'
        } catch {
            Write-RelayLog ("Relay error: " + $_.Exception.Message)
            if ($ngrokProcess -and -not $ngrokProcess.HasExited) {
                Stop-Process -Id $ngrokProcess.Id -Force -ErrorAction SilentlyContinue
            }
            Start-Sleep -Seconds $retryDelaySeconds
            $retryDelaySeconds = [Math]::Min($retryDelaySeconds * 2, 120)
        }
    }
} finally {
    try { $mutex.ReleaseMutex() } catch {}
    $mutex.Dispose()
}

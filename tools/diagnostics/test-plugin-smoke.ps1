param(
    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER
)

$ErrorActionPreference = 'Stop'

function Get-PlainTextFromSecureString {
    param([Security.SecureString]$SecureString)
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

if ([string]::IsNullOrWhiteSpace($Username)) {
    $Username = Read-Host 'WordPress username'
}
if ([string]::IsNullOrWhiteSpace($Username)) {
    throw 'A WordPress username is required.'
}

$securePassword = if (-not [string]::IsNullOrWhiteSpace($env:ELEMENTIZE_WP_APP_PASSWORD)) {
    ConvertTo-SecureString $env:ELEMENTIZE_WP_APP_PASSWORD -AsPlainText -Force
} else {
    Read-Host 'WordPress Application Password (input hidden; do not paste it into ChatGPT)' -AsSecureString
}

$plain = Get-PlainTextFromSecureString $securePassword
try {
    $authBytes = [Text.Encoding]::UTF8.GetBytes("$Username`:$plain")
    $authorization = 'Basic ' + [Convert]::ToBase64String($authBytes)
} finally {
    $plain = $null
}

$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if ($null -eq $curl) { throw 'curl.exe is required.' }

$uri = $SiteUrl.TrimEnd('/') + '/wp-json/elementize/v1/status'
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
if (-not $proc.Start()) { throw 'Could not start curl.exe.' }

try {
    $proc.StandardInput.WriteLine('silent')
    $proc.StandardInput.WriteLine('show-error')
    $proc.StandardInput.WriteLine('insecure')
    $proc.StandardInput.WriteLine('header = "Authorization: ' + $authorization + '"')
    $proc.StandardInput.WriteLine('header = "Accept: application/json"')
    $proc.StandardInput.WriteLine('url = "' + $uri + '"')
    $proc.StandardInput.WriteLine('write-out = "\n__ELEMENTIZE_HTTP_CODE__:%{http_code}"')
    $proc.StandardInput.Close()

    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) {
        throw ($(if ([string]::IsNullOrWhiteSpace($stderr)) { "curl.exe failed with exit code $($proc.ExitCode)." } else { $stderr.Trim() }))
    }

    $marker = '__ELEMENTIZE_HTTP_CODE__:'
    $idx = $stdout.LastIndexOf($marker)
    if ($idx -lt 0) { throw 'No HTTP status marker returned.' }
    $body = $stdout.Substring(0, $idx).Trim()
    $codeText = $stdout.Substring($idx + $marker.Length).Trim()
    $httpCode = 0
    [void][int]::TryParse($codeText, [ref]$httpCode)

    if ($httpCode -ne 200) {
        $snippet = if ($body.Length -gt 1000) { $body.Substring(0, 1000) + '...' } else { $body }
        throw "Elementize status returned HTTP $httpCode. $snippet"
    }

    $status = $body | ConvertFrom-Json
    if ($null -eq $status) { throw 'Status JSON could not be parsed.' }
    if ([string]::IsNullOrWhiteSpace([string]$status.elementize_version)) {
        throw 'Status did not expose elementize_version.'
    }

    Write-Host ("[PASS] Elementize loaded: {0}" -f $status.elementize_version) -ForegroundColor Green
    Write-Host ("[PASS] Status endpoint: HTTP {0}" -f $httpCode) -ForegroundColor Green

    $signals = [ordered]@{
        StatusVersionSource = [string]$status.elementize_status_version_source
        StatusFinalizedLast = [bool]$status.elementize_status_version_finalized_last
        IndependentScoring = [string]$status.aesthetic_ab_independent_scoring_version
        AnchoredDiscrimination = [string]$status.aesthetic_ab_independent_discrimination_version
        MultiSampleConsensus = [string]$status.aesthetic_ab_multisample_consensus_version
        SemanticShortlist = [string]$status.aesthetic_semantic_shortlist_version
        AutomaticWriteAllowed = [bool]$status.aesthetic_semantic_shortlist_automatic_write_allowed
    }

    Write-Host ''
    $signals | Format-Table -AutoSize
    Write-Host ''
    Write-Host '[PASS] Bootstrap smoke test completed. No write endpoint was called.' -ForegroundColor Green
}
finally {
    if (-not $proc.HasExited) { try { $proc.Kill() } catch {} }
    $proc.Dispose()
}

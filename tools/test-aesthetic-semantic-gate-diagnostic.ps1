param(
    [Parameter(Mandatory = $false)]
    [int]$PageId = 952239,

    [Parameter(Mandatory = $false)]
    [string]$SiteUrl = 'https://mijn-ibp.local',

    [Parameter(Mandatory = $false)]
    [string]$Username = $env:ELEMENTIZE_WP_USER,

    [Parameter(Mandatory = $false)]
    [string]$TargetMarker = 'S1'
)

$ErrorActionPreference = 'Stop'

function Get-PlainTextFromSecureString {
    param([Security.SecureString]$SecureString)
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

function New-BasicAuthHeader {
    param([string]$User, [Security.SecureString]$Password)
    $plain = Get-PlainTextFromSecureString $Password
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes("$User`:$plain")
        return 'Basic ' + [Convert]::ToBase64String($bytes)
    } finally { $plain = $null }
}

function Invoke-ElementizeCurl {
    param(
        [ValidateSet('GET','POST')][string]$Method,
        [string]$Uri,
        [string]$Authorization,
        [string]$JsonBody = ''
    )
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curl) { throw 'curl.exe is required.' }
    $tempBody = $null
    if ($Method -eq 'POST') {
        $tempBody = [IO.Path]::GetTempFileName()
        [IO.File]::WriteAllText($tempBody, $JsonBody, (New-Object Text.UTF8Encoding($false)))
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
    if (-not $proc.Start()) { throw 'Could not start curl.exe.' }
    try {
        $proc.StandardInput.WriteLine('silent')
        $proc.StandardInput.WriteLine('show-error')
        $proc.StandardInput.WriteLine('insecure')
        $proc.StandardInput.WriteLine('header = "Authorization: ' + $Authorization + '"')
        $proc.StandardInput.WriteLine('header = "Accept: application/json"')
        if ($Method -eq 'POST') {
            $proc.StandardInput.WriteLine('request = "POST"')
            $proc.StandardInput.WriteLine('header = "Content-Type: application/json"')
            $path = $tempBody.Replace('\','/')
            $proc.StandardInput.WriteLine('data-binary = "@' + $path + '"')
        }
        $proc.StandardInput.WriteLine('url = "' + $Uri + '"')
        $proc.StandardInput.WriteLine('write-out = "\n__ELEMENTIZE_HTTP_CODE__:%{http_code}"')
        $proc.StandardInput.Close()
        $stdout = $proc.StandardOutput.ReadToEnd()
        $stderr = $proc.StandardError.ReadToEnd()
        $proc.WaitForExit()
        if ($proc.ExitCode -ne 0) { throw ($(if ($stderr) { $stderr.Trim() } else { "curl failed: $($proc.ExitCode)" })) }
        $marker = '__ELEMENTIZE_HTTP_CODE__:'
        $idx = $stdout.LastIndexOf($marker)
        if ($idx -lt 0) { throw 'No HTTP status marker.' }
        $body = $stdout.Substring(0,$idx).Trim()
        $code = [int]$stdout.Substring($idx + $marker.Length).Trim()
        if ($code -lt 200 -or $code -ge 300) { throw "HTTP $code $body" }
        return $body | ConvertFrom-Json
    } finally {
        if (-not $proc.HasExited) { try { $proc.Kill() } catch {} }
        $proc.Dispose()
        if ($tempBody -and (Test-Path $tempBody)) { Remove-Item $tempBody -Force -ErrorAction SilentlyContinue }
    }
}

function Get-PropertyInt($Object, [string]$Name) {
    if ($null -eq $Object) { return 0 }
    $p = $Object.PSObject.Properties[$Name]
    if ($null -eq $p -or $null -eq $p.Value) { return 0 }
    return [int]$p.Value
}

function Get-WidgetCount($Template) {
    $sum = 0
    if ($null -ne $Template.widget_type_counts) {
        foreach ($p in $Template.widget_type_counts.PSObject.Properties) { $sum += [int]$p.Value }
    }
    return $sum
}

function Get-DiscoveryScore($Row) {
    $cats = if ($null -ne $Row.categories) { (@($Row.categories) -join ' ') } else { '' }
    $text = (([string]$Row.id) + ' ' + ([string]$Row.title) + ' ' + ([string]$Row.subtype) + ' ' + $cats).ToLowerInvariant()
    $score = 0
    $positive = [ordered]@{
        'hero' = 45; 'home-intro' = 35; 'home intro' = 35; 'intro-left' = 28; 'intro-center' = 28;
        'intro-video' = 24; 'intro' = 18; 'header' = 12; 'banner' = 10
    }
    foreach ($k in $positive.Keys) { if ($text.Contains($k)) { $score += [int]$positive[$k] } }
    $negative = [ordered]@{
        'pricing'=65; 'price'=45; 'case-stud'=55; 'case stud'=55; 'feature'=35; 'progress'=45; 'table'=50;
        'numbers'=32; 'number'=25; 'counter'=35; 'stats'=30; 'statistics'=30; 'testimonial'=45; 'review'=35;
        'faq'=55; 'accordion'=45; 'contact'=40; 'form'=40; 'newsletter'=45; 'team'=35; 'blog'=40; 'post'=30;
        'portfolio'=35; 'products'=35; 'clients-marquee'=30; 'client-marquee'=30; 'logos-marquee'=30;
        'marquee-numbers'=40; 'solutions-intro-numbers'=35
    }
    foreach ($k in $negative.Keys) { if ($text.Contains($k)) { $score -= [int]$negative[$k] } }
    return $score
}

function Get-HeroStructureScore($Template, $Target) {
    $roles = $Template.role_counts
    $targetRoles = $Target.role_counts
    $heading = Get-PropertyInt $roles 'heading'
    $text = Get-PropertyInt $roles 'text'
    $button = Get-PropertyInt $roles 'button_cta'
    $form = Get-PropertyInt $roles 'form'
    $shortcode = Get-PropertyInt $roles 'shortcode'
    $targetText = Get-PropertyInt $targetRoles 'text'
    $targetButton = Get-PropertyInt $targetRoles 'button_cta'

    $requirements = $true
    $reason = @()
    if ($heading -lt 1) { $requirements = $false; $reason += 'missing heading' }
    if ($targetText -gt 0 -and $text -lt 1) { $requirements = $false; $reason += 'missing text' }
    if ($targetButton -gt 0 -and $button -lt 1) { $requirements = $false; $reason += 'missing CTA' }
    if ($form -gt 0 -or $shortcode -gt 0) { $requirements = $false; $reason += 'form/shortcode dependency' }

    $specialized = @()
    if ($null -ne $Template.widget_type_counts) {
        foreach ($p in $Template.widget_type_counts.PSObject.Properties) {
            if ($p.Name -match 'pricing|price[-_]?table|progress|counter|statistic|accordion|faq|testimonial|review|posts|portfolio|products|product[-_]?grid|case[-_]?stud|timeline') { $specialized += $p.Name }
        }
    }
    $targetSpecialized = $false
    if ($null -ne $Target.widget_types) {
        foreach ($p in $Target.widget_types.PSObject.Properties) {
            if ($p.Name -match 'pricing|price[-_]?table|progress|counter|statistic|accordion|faq|testimonial|review|posts|portfolio|products|product[-_]?grid|case[-_]?stud|timeline') { $targetSpecialized = $true; break }
        }
    }
    if ($specialized.Count -gt 0 -and -not $targetSpecialized) { $requirements = $false; $reason += 'specialized widget absent from target' }

    $score = 100
    $weights = [ordered]@{ heading=9; text=7; button_cta=10; image=7; icon=4; form=14; shortcode=14 }
    $countPenalty = 0
    foreach ($k in $weights.Keys) {
        $a = Get-PropertyInt $targetRoles $k
        $b = Get-PropertyInt $roles $k
        $countPenalty += [Math]::Min(18, [Math]::Abs($a-$b) * [int]$weights[$k])
    }
    $countPenalty = [Math]::Min(45,$countPenalty)
    $score -= $countPenalty

    $targetWidgetCount = [Math]::Max(1, [int]$Target.widget_count)
    $candidateWidgetCount = [Math]::Max(1, (Get-WidgetCount $Template))
    $allowedExcess = [Math]::Max(3, [int][Math]::Ceiling($targetWidgetCount * 0.55))
    $excess = [Math]::Max(0, $candidateWidgetCount - $targetWidgetCount - $allowedExcess)
    $deficit = [Math]::Max(0, $targetWidgetCount - $candidateWidgetCount - 3)
    $complexityPenalty = [Math]::Min(32, ($excess * 3) + ($deficit * 2))
    $targetElements = [Math]::Max(1, [int]$Target.total_elements)
    $candidateElements = [Math]::Max(1, [int]$Template.total_elements)
    if ($candidateElements -gt [Math]::Ceiling($targetElements * 2.1)) { $complexityPenalty += 12 }
    $complexityPenalty = [Math]::Min(40,$complexityPenalty)
    $score -= $complexityPenalty

    $alignmentPenalty = 0
    $ta = ([string]$Target.dominant_alignment).ToLowerInvariant()
    $ca = ([string]$Template.dominant_alignment).ToLowerInvariant()
    if ($ta -notin @('','unknown','mixed') -and $ca -notin @('','unknown','mixed') -and $ta -ne $ca) { $alignmentPenalty = 5; $score -= 5 }

    $dependencyRisk = 'low'
    $dependencyPenalty = 0
    if ($form -gt 0 -or $shortcode -gt 0) { $dependencyRisk = 'high'; $dependencyPenalty = 24 }
    elseif ($null -ne $Template.dependency_indicators -and $null -ne $Template.dependency_indicators.widgets -and @($Template.dependency_indicators.widgets).Count -gt 0) { $dependencyRisk = 'medium'; $dependencyPenalty = 8 }
    $score -= $dependencyPenalty

    $specializedPenalty = [Math]::Min(35, $specialized.Count * 18)
    $score -= $specializedPenalty
    $score = [Math]::Max(0,[Math]::Min(100,$score))
    $eligible = $requirements -and $score -ge 62 -and $dependencyRisk -ne 'high'

    [pscustomobject]@{
        TemplateId = [string]$Template.template_id
        StructuralScore = [int]$score
        Eligible = [bool]$eligible
        CountPenalty = [int]$countPenalty
        ComplexityPenalty = [int]$complexityPenalty
        AlignmentPenalty = [int]$alignmentPenalty
        DependencyPenalty = [int]$dependencyPenalty
        SpecializedPenalty = [int]$specializedPenalty
        TargetWidgets = [int]$targetWidgetCount
        CandidateWidgets = [int]$candidateWidgetCount
        TargetElements = [int]$targetElements
        CandidateElements = [int]$candidateElements
        Alignment = [string]$Template.dominant_alignment
        Density = [string]$Template.approximate_density
        ImageProminence = [string]$Template.image_prominence
        Reason = ($reason -join '; ')
    }
}

if ([string]::IsNullOrWhiteSpace($Username)) { $Username = Read-Host 'WordPress username' }
$securePassword = if (-not [string]::IsNullOrWhiteSpace($env:ELEMENTIZE_WP_APP_PASSWORD)) { ConvertTo-SecureString $env:ELEMENTIZE_WP_APP_PASSWORD -AsPlainText -Force } else { Read-Host 'WordPress Application Password (hidden)' -AsSecureString }
$auth = New-BasicAuthHeader -User $Username -Password $securePassword
$base = $SiteUrl.TrimEnd('/')
$targetMarker = $TargetMarker.ToUpperInvariant()

Write-Host ''
Write-Host 'Elementize semantic gate diagnostic' -ForegroundColor Cyan
Write-Host "Page: $PageId  Target: $targetMarker"
Write-Host 'Policy: read-only. Reproduces discovery and structural scoring; no visual model and no write endpoint.'

$shortUri = "$base/wp-json/elementize/v1/pixfort/templates?type=section&page=1&per_page=25&semantic_shortlist=true&context_page_id=$PageId&target_marker=$targetMarker&shortlist_limit=4"
try {
    $catalogueResponse = Invoke-ElementizeCurl GET $shortUri $auth
} catch {
    Write-Host '[STEP] Exact-state semantic context was not reusable; refreshing completion audit once...' -ForegroundColor DarkCyan
    [void](Invoke-ElementizeCurl GET "$base/wp-json/elementize/v1/pages/$PageId/completion-audit?include_visual=true" $auth)
    $catalogueResponse = Invoke-ElementizeCurl GET $shortUri $auth
}
$short = $catalogueResponse.semantic_shortlist
if ($null -eq $short -or -not $short.available) { throw 'Semantic shortlist unavailable.' }
$target = $short.target_structure

Write-Host ("[PASS] Current hardened shortlist: {0}" -f (@($short.template_ids) -join ', ')) -ForegroundColor Green

Write-Host '[STEP] Reproducing catalogue discovery ranking...' -ForegroundColor DarkCyan
$all = @()
for ($page=1; $page -le 8; $page++) {
    $resp = Invoke-ElementizeCurl GET "$base/wp-json/elementize/v1/pixfort/templates?type=section&page=$page&per_page=100" $auth
    $all += @($resp.templates)
    if ($page -ge [int]$resp.total_pages) { break }
}
$discovery = foreach ($row in $all) {
    [pscustomobject]@{ TemplateId=[string]$row.id; Title=[string]$row.title; DiscoveryScore=(Get-DiscoveryScore $row) }
}
$discovery = @($discovery | Sort-Object @{Expression='DiscoveryScore';Descending=$true}, @{Expression='TemplateId';Descending=$false})
$rankById = @{}
for ($i=0; $i -lt $discovery.Count; $i++) { $rankById[$discovery[$i].TemplateId] = $i + 1 }

$known = @('app-intro-left','ai-agency-home-intro-video','corporate-intro-center','finance-intro')
$current = @($short.template_ids | ForEach-Object { [string]$_ })
$candidateIds = @($current + $known | Select-Object -Unique | Select-Object -First 8)

Write-Host ''
Write-Host '--- Discovery gate ---' -ForegroundColor Cyan
$gateRows = foreach ($id in $candidateIds) {
    $d = $discovery | Where-Object TemplateId -eq $id | Select-Object -First 1
    [pscustomobject]@{
        TemplateId = $id
        DiscoveryRank = if ($rankById.ContainsKey($id)) { [int]$rankById[$id] } else { 0 }
        DiscoveryScore = if ($null -ne $d) { [int]$d.DiscoveryScore } else { 0 }
        InTop32Discovery = $rankById.ContainsKey($id) -and [int]$rankById[$id] -le 32
        InCurrentShortlist = $current -contains $id
    }
}
$gateRows | Format-Table -AutoSize

Write-Host '[STEP] Inspecting actual Elementor structure for current + proven candidates...' -ForegroundColor DarkCyan
$body = @{ template_ids = $candidateIds } | ConvertTo-Json -Depth 4 -Compress
$structure = Invoke-ElementizeCurl POST "$base/wp-json/elementize/v1/pages/$PageId/pixfort/template-structure" $auth $body
$scoreRows = foreach ($template in @($structure.templates)) {
    $s = Get-HeroStructureScore $template $target
    $g = $gateRows | Where-Object TemplateId -eq $s.TemplateId | Select-Object -First 1
    [pscustomobject]@{
        TemplateId = $s.TemplateId
        DiscoveryRank = [int]$g.DiscoveryRank
        InTop32 = [bool]$g.InTop32Discovery
        StructuralScore = $s.StructuralScore
        Eligible = $s.Eligible
        CountPenalty = $s.CountPenalty
        ComplexityPenalty = $s.ComplexityPenalty
        AlignmentPenalty = $s.AlignmentPenalty
        DependencyPenalty = $s.DependencyPenalty
        SpecializedPenalty = $s.SpecializedPenalty
        CandidateWidgets = $s.CandidateWidgets
        CandidateElements = $s.CandidateElements
        Alignment = $s.Alignment
        Density = $s.Density
        ImageProminence = $s.ImageProminence
    }
}

Write-Host ''
Write-Host '--- Exact 0.28.1 structural gate reproduction ---' -ForegroundColor Cyan
$scoreRows | Sort-Object StructuralScore -Descending | Format-Table -AutoSize

$app = $scoreRows | Where-Object TemplateId -eq 'app-intro-left' | Select-Object -First 1
Write-Host ''
Write-Host '--- app-intro-left diagnosis ---' -ForegroundColor Cyan
if ($null -eq $app) {
    Write-Host '[FAIL] app-intro-left could not be structurally inspected.' -ForegroundColor Red
} elseif (-not $app.InTop32) {
    Write-Host ("[DIAG] app-intro-left is blocked at DISCOVERY: rank {0}; structural score if inspected now = {1}; eligible={2}." -f $app.DiscoveryRank,$app.StructuralScore,$app.Eligible) -ForegroundColor Yellow
} elseif (-not $app.Eligible) {
    Write-Host ("[DIAG] app-intro-left reaches structure inspection but is REJECTED structurally at {0}/100." -f $app.StructuralScore) -ForegroundColor Yellow
} elseif ($current -notcontains 'app-intro-left') {
    Write-Host ("[DIAG] app-intro-left is eligible ({0}/100) but loses the TOP-4 structural ranking/diversity gate." -f $app.StructuralScore) -ForegroundColor Yellow
} else {
    Write-Host '[PASS] app-intro-left is already in the current shortlist.' -ForegroundColor Green
}

Write-Host ''
Write-Host 'Done. No Ollama visual call, insertion, or write endpoint was used.' -ForegroundColor Cyan

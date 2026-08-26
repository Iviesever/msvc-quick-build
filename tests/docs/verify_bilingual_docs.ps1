[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-Utf8Text([string]$RelativePath) {
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing bilingual documentation file: $RelativePath"
    }

    $text = Get-Content -LiteralPath $path -Raw -Encoding utf8
    if ([string]::IsNullOrWhiteSpace($text)) {
        throw "Empty bilingual documentation file: $RelativePath"
    }
    return $text
}

function Get-H2Count([string]$Text) {
    return ([regex]::Matches($Text, '(?m)^##\s+[^#].*$')).Count
}

$pairs = @(
    @{ English = 'README.md'; Chinese = 'README_ZH.md' },
    @{ English = 'CONTRIBUTING.md'; Chinese = 'CONTRIBUTING_ZH.md' },
    @{ English = 'docs/README.md'; Chinese = 'docs/README_ZH.md' },
    @{ English = 'docs/ARCHITECTURE_EN.md'; Chinese = 'docs/ARCHITECTURE.md' },
    @{ English = 'docs/DEVELOPMENT_EN.md'; Chinese = 'docs/DEVELOPMENT.md' },
    @{ English = 'docs/INSTALLATION_EN.md'; Chinese = 'docs/INSTALLATION.md' },
    @{ English = 'docs/MQB_CONFIG_EN.md'; Chinese = 'docs/MQB_CONFIG.md' },
    @{ English = 'docs/PARALLELISM_EN.md'; Chinese = 'docs/PARALLELISM.md' },
    @{ English = 'docs/PRECOMPILED_HEADERS_EN.md'; Chinese = 'docs/PRECOMPILED_HEADERS.md' },
    @{ English = 'docs/SELF_HOSTING_EN.md'; Chinese = 'docs/SELF_HOSTING.md' },
    @{ English = 'cpp/README_EN.md'; Chinese = 'cpp/README.md' },
    @{ English = 'docs/MSVC_PARAMETER_COVERAGE.md'; Chinese = 'docs/MSVC_PARAMETER_COVERAGE_ZH.md' },
    @{ English = 'docs/MSVC_PARAMETER_ENGINE.md'; Chinese = 'docs/MSVC_PARAMETER_ENGINE_ZH.md' },
    @{ English = 'docs/MSVC_PARAMETER_INVENTORY.md'; Chinese = 'docs/MSVC_PARAMETER_INVENTORY_ZH.md' },
    @{ English = 'docs/PERFORMANCE_GOVERNANCE.md'; Chinese = 'docs/PERFORMANCE_GOVERNANCE_ZH.md' },
    @{ English = 'docs/WARM_FAST_PATH.md'; Chinese = 'docs/WARM_FAST_PATH_ZH.md' }
)

$failures = [System.Collections.Generic.List[string]]::new()

foreach ($pair in $pairs) {
    try {
        $english = Read-Utf8Text $pair.English
        $chinese = Read-Utf8Text $pair.Chinese

        $englishPeer = [System.IO.Path]::GetFileName($pair.Chinese)
        $chinesePeer = [System.IO.Path]::GetFileName($pair.English)

        if (-not $english.Contains($englishPeer, [System.StringComparison]::Ordinal)) {
            $failures.Add("$($pair.English) does not link/reference its Chinese peer $englishPeer")
        }
        if (-not $chinese.Contains($chinesePeer, [System.StringComparison]::Ordinal)) {
            $failures.Add("$($pair.Chinese) does not link/reference its English peer $chinesePeer")
        }

        $englishH2 = Get-H2Count $english
        $chineseH2 = Get-H2Count $chinese
        if ($englishH2 -ne $chineseH2) {
            $failures.Add("Section-count drift: $($pair.English) has $englishH2 H2 sections, $($pair.Chinese) has $chineseH2")
        }
    }
    catch {
        $failures.Add($_.Exception.Message)
    }
}

try {
    $canonicalReadme = Read-Utf8Text 'README.md'
    $legacyEnglishReadme = Read-Utf8Text 'README_EN.md'
    if (-not [string]::Equals($canonicalReadme, $legacyEnglishReadme, [System.StringComparison]::Ordinal)) {
        $failures.Add('README_EN.md must remain byte-for-byte text-equivalent to canonical README.md while the compatibility alias exists.')
    }

    if (-not $canonicalReadme.Contains('README_ZH.md', [System.StringComparison]::Ordinal)) {
        $failures.Add('Canonical README.md must link to README_ZH.md.')
    }
    $chineseReadme = Read-Utf8Text 'README_ZH.md'
    if (-not $chineseReadme.Contains('(README.md)', [System.StringComparison]::Ordinal)) {
        $failures.Add('README_ZH.md must link back to canonical README.md.')
    }
}
catch {
    $failures.Add($_.Exception.Message)
}

if ($failures.Count -gt 0) {
    Write-Host 'Bilingual documentation parity check FAILED:' -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Bilingual documentation parity check passed for $($pairs.Count) maintained pairs." -ForegroundColor Green

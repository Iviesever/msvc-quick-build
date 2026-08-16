[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$fixture = Join-Path $RepoRoot 'native-test-work/has-include-freshness'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $fixture 'high/nested') | Out-Null

Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8 -Value @(
    '#if __has_include(<nested/optional_feature.hpp>)',
    '#include <nested/optional_feature.hpp>',
    '#else',
    'inline int feature_value() { return 61; }',
    '#endif',
    'int main() { return feature_value(); }'
)

function Invoke-Build {
    Push-Location $fixture
    try {
        $output = @(& $MqbPath main.cpp --release --no-discover --env vs -I high -o has-include 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "MQB build failed (exit $exitCode):`n$($output -join [Environment]::NewLine)"
    }
    return ($output -join [Environment]::NewLine)
}

function Invoke-Probe {
    $program = Join-Path $fixture '.mqb/bin/has-include.exe'
    if (-not (Test-Path -LiteralPath $program -PathType Leaf)) {
        throw "program output missing: $program"
    }
    & $program
    return $LASTEXITCODE
}

$cold = Invoke-Build
if ((Invoke-Probe) -ne 61) {
    throw '__has_include cold build did not take the absent-header branch'
}

$warm = Invoke-Build
if ($warm -notmatch '\[up-to-date\]\s+main\.cpp') {
    throw "unchanged __has_include build was not warm:`n$warm"
}
if ($warm -match '\[compile\]\s+main\.cpp') {
    throw "unchanged __has_include build launched an unnecessary compiler:`n$warm"
}

# The parent directory already exists. Only the nested directory mtime changes,
# which proves that freshness is not accidentally coming from the top-level /I root.
Start-Sleep -Milliseconds 100
Set-Content -LiteralPath (Join-Path $fixture 'high/nested/optional_feature.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'inline int feature_value() { return 62; }'
)

$shadow = Invoke-Build
if ($shadow -notmatch '\[compile\]\s+main\.cpp') {
    throw "new __has_include candidate did not invalidate the warm compile cache:`n$shadow"
}
if ((Invoke-Probe) -ne 62) {
    throw '__has_include did not observe the newly available nested header'
}

Write-Host '__has_include absent-to-present freshness check passed.'
exit 0

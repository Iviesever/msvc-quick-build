[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$classifier = Join-Path $RepoRoot 'tests/ci/classify_native_ci_impact.ps1'
if (-not (Test-Path -LiteralPath $classifier -PathType Leaf)) {
    throw "Native-impact classifier is missing: $classifier"
}

$cases = @(
    @{ Name = 'version'; Expected = 'true'; Paths = @('VERSION') },
    @{ Name = 'product source'; Expected = 'true'; Paths = @('cpp/src/main.cpp') },
    @{ Name = 'native tests'; Expected = 'true'; Paths = @('tests/native/run_native_tests.ps1') },
    @{ Name = 'self-host tests'; Expected = 'true'; Paths = @('tests/selfhost/selfhost.ps1') },
    @{ Name = 'native workflow'; Expected = 'true'; Paths = @('.github/workflows/native-ci.yml') },
    @{ Name = 'classifier'; Expected = 'true'; Paths = @('tests/ci/classify_native_ci_impact.ps1') },
    @{ Name = 'CI helper'; Expected = 'true'; Paths = @('tests/ci/future_helper.ps1') },
    @{ Name = 'Windows separators'; Expected = 'true'; Paths = @('.\cpp\src\main.cpp') },
    @{ Name = 'mixed paths'; Expected = 'true'; Paths = @('README.md', 'docs/ARCHITECTURE.md', 'cpp/include/mqb/config.hpp') },
    @{ Name = 'documentation only'; Expected = 'false'; Paths = @('README.md', 'README_ZH.md', 'docs/ARCHITECTURE.md') },
    @{ Name = 'unrelated workflow'; Expected = 'false'; Paths = @('.github/workflows/docs-ci.yml') },
    @{ Name = 'empty input'; Expected = 'false'; Paths = @() }
)

$failures = [System.Collections.Generic.List[string]]::new()

foreach ($case in $cases) {
    $actual = (& $classifier -ChangedPath ([string[]]$case.Paths)).Trim()
    if ($LASTEXITCODE -ne 0) {
        $failures.Add("$($case.Name): classifier exited with $LASTEXITCODE")
        continue
    }
    if ($actual -ne $case.Expected) {
        $failures.Add("$($case.Name): expected '$($case.Expected)', got '$actual'")
    }
}

if ($failures.Count -gt 0) {
    Write-Host 'Native-impact classifier verification FAILED:' -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Native-impact classifier verification passed for $($cases.Count) cases." -ForegroundColor Green

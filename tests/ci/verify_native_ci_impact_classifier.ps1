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
$resolver = Join-Path $RepoRoot 'tests/ci/resolve_native_ci_impact.ps1'
if (-not (Test-Path -LiteralPath $resolver -PathType Leaf)) {
    throw "Native-impact resolver is missing: $resolver"
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

$resolverCases = @(
    @{
        Name = 'complete documentation response'
        Expected = 'false'
        Count = 2
        Json = '[[{"filename":"README.md"}],[{"filename":"docs/ARCHITECTURE.md"}]]'
    },
    @{
        Name = 'rename from native path'
        Expected = 'true'
        Count = 1
        Json = '[[{"filename":"docs/retired.cpp","previous_filename":"cpp/src/retired.cpp"}]]'
    },
    @{
        Name = 'rename into native path'
        Expected = 'true'
        Count = 1
        Json = '[[{"filename":"cpp/src/promoted.cpp","previous_filename":"docs/promoted.cpp"}]]'
    }
)

foreach ($case in $resolverCases) {
    $actual = (& $resolver `
        -ExpectedChangedFileCount $case.Count `
        -PullRequestFilesJson $case.Json `
        -RepoRoot $RepoRoot).Trim()
    if ($LASTEXITCODE -ne 0) {
        $failures.Add("$($case.Name): resolver exited with $LASTEXITCODE")
        continue
    }
    if ($actual -ne $case.Expected) {
        $failures.Add("$($case.Name): expected '$($case.Expected)', got '$actual'")
    }
}

$rejectedCases = @(
    @{ Name = 'truncated response'; Count = 3; Json = '[[{"filename":"README.md"},{"filename":"docs/ARCHITECTURE.md"}]]' },
    @{ Name = 'empty response'; Count = 0; Json = '[]' },
    @{ Name = 'non-array response'; Count = 1; Json = '{"filename":"README.md"}' },
    @{ Name = 'truncated JSON'; Count = 1; Json = '[[{"filename":"README.md"}]' },
    @{ Name = 'trailing-comma JSON'; Count = 1; Json = '[[{"filename":"README.md"}], ]' },
    @{ Name = 'non-array page'; Count = 1; Json = '[{"filename":"README.md"}]' },
    @{ Name = 'missing filename'; Count = 1; Json = '[[{"status":"modified"}]]' }
)

foreach ($case in $rejectedCases) {
    try {
        $null = & $resolver `
            -ExpectedChangedFileCount $case.Count `
            -PullRequestFilesJson $case.Json `
            -RepoRoot $RepoRoot
        $failures.Add("$($case.Name): resolver accepted an incomplete or malformed response")
    }
    catch {
        continue
    }
}

if ($failures.Count -gt 0) {
    Write-Host 'Native-impact classifier verification FAILED:' -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Native-impact classifier verification passed for $($cases.Count + $resolverCases.Count + $rejectedCases.Count) cases." -ForegroundColor Green

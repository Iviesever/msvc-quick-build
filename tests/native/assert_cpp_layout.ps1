[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CppRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-DirectDirectories {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$Allowed
    )

    $actual = @(
        Get-ChildItem -LiteralPath $Root -Directory |
            Select-Object -ExpandProperty Name |
            Sort-Object -Unique
    )
    $unexpected = @($actual | Where-Object { $_ -notin $Allowed })
    if ($unexpected.Count -ne 0) {
        throw "Unclassified responsibility director$(if ($unexpected.Count -eq 1) { 'y' } else { 'ies' }) under '$Root': $($unexpected -join ', ')"
    }
}

function Assert-ExactFiles {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$Expected
    )

    $actual = @(
        Get-ChildItem -LiteralPath $Root -File |
            Select-Object -ExpandProperty Name |
            Sort-Object -Unique
    )
    $expectedSorted = @($Expected | Sort-Object -Unique)
    $diff = @(Compare-Object -ReferenceObject $expectedSorted -DifferenceObject $actual)
    if ($diff.Count -ne 0) {
        $details = @(
            $diff | ForEach-Object {
                $kind = if ($_.SideIndicator -eq '<=') { 'missing' } else { 'unexpected' }
                "  ${kind}: $($_.InputObject)"
            }
        ) -join [Environment]::NewLine
        throw "Responsibility layout drift under '$Root':`n$details"
    }
}

$CppRoot = Get-FullPath $CppRoot
if (-not (Test-Path -LiteralPath $CppRoot -PathType Container)) {
    throw "C++ source root not found: $CppRoot"
}

$includeRoot = Join-Path $CppRoot 'include/mqb'
$srcRoot = Join-Path $CppRoot 'src'
$testsRoot = Join-Path $CppRoot 'tests'
foreach ($root in @($includeRoot, $srcRoot, $testsRoot)) {
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Required C++ responsibility root not found: $root"
    }
}

# Keep top-level physical directories aligned with the architecture vocabulary.
# Adding a new responsibility is an intentional architecture change and must
# update this contract rather than silently creating another catch-all folder.
Assert-DirectDirectories -Root $includeRoot -Allowed @(
    'config',
    'core',
    'discovery',
    'json',
    'modules',
    'msvc',
    'orchestration',
    'platform',
    'process'
)
Assert-DirectDirectories -Root $srcRoot -Allowed @(
    'app',
    'config',
    'core',
    'discovery',
    'json',
    'modules',
    'msvc',
    'orchestration',
    'platform'
)
Assert-DirectDirectories -Root $testsRoot -Allowed @(
    'app',
    'config',
    'core',
    'discovery',
    'e2e',
    'json',
    'modules',
    'msvc',
    'orchestration',
    'platform',
    'process'
)

# app is an executable-composition layer, but its internal responsibilities are
# distinct enough to deserve physical sublayers. Keep only the executable shell
# at the root; CLI parsing, diagnostics, project policy, and target adapters each
# own a dedicated leaf directory.
$appRoot = Join-Path $srcRoot 'app'
Assert-DirectDirectories -Root $appRoot -Allowed @('cli', 'diagnostics', 'project', 'targets')
Assert-ExactFiles -Root $appRoot -Expected @('Application.cpp', 'Application.hpp', 'main.cpp')

$appLeafFiles = [ordered]@{
    'cli' = @('Cli.cpp', 'Cli.hpp', 'Invocation.cpp', 'Invocation.hpp')
    'diagnostics' = @('Diagnostics.cpp', 'Diagnostics.hpp')
    'project' = @('ProjectSetup.cpp', 'ProjectSetup.hpp')
    'targets' = @('ModuleCliTarget.cpp', 'ModuleCliTarget.hpp', 'StaticCliTarget.cpp', 'StaticCliTarget.hpp')
}
foreach ($leaf in $appLeafFiles.Keys) {
    $leafRoot = Join-Path $appRoot $leaf
    if (-not (Test-Path -LiteralPath $leafRoot -PathType Container)) {
        throw "Required app responsibility directory not found: $leafRoot"
    }
    Assert-ExactFiles -Root $leafRoot -Expected $appLeafFiles[$leaf]
}

$appTestsRoot = Join-Path $testsRoot 'app'
$misplacedAppTests = @(
    Get-ChildItem -LiteralPath $appTestsRoot -File -Filter '*_tests.cpp' |
        Select-Object -ExpandProperty Name
)
if ($misplacedAppTests.Count -ne 0) {
    throw "App tests must mirror an app responsibility subdirectory instead of living at tests/app root: $($misplacedAppTests -join ', ')"
}

Write-Host 'C++ responsibility layout contract passed.'

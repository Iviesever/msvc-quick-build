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

function Assert-NoDirectFiles {
    param([Parameter(Mandatory = $true)][string]$Root)

    $files = @(Get-ChildItem -LiteralPath $Root -File | Select-Object -ExpandProperty Name)
    if ($files.Count -ne 0) {
        throw "Responsibility root '$Root' must contain only subdirectories: $($files -join ', ')"
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

function Assert-LeafLayout {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$LeafFiles
    )

    Assert-DirectDirectories -Root $Root -Allowed @($LeafFiles.Keys)
    Assert-NoDirectFiles -Root $Root
    foreach ($leaf in $LeafFiles.Keys) {
        $leafRoot = Join-Path $Root $leaf
        if (-not (Test-Path -LiteralPath $leafRoot -PathType Container)) {
            throw "Required responsibility directory not found: $leafRoot"
        }
        Assert-ExactFiles -Root $leafRoot -Expected $LeafFiles[$leaf]
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
$appTestLeafFiles = [ordered]@{
    'cli' = @('build_policy_cli_tests.cpp', 'cli_argument_tests.cpp')
}
Assert-LeafLayout -Root $appTestsRoot -LeafFiles $appTestLeafFiles

# orchestration is intentionally a coordination layer, but scheduling,
# incremental ordinary-target execution, named-module coordination, and routing
# are separate implementation responsibilities. Public facade headers stay at
# include/mqb/orchestration for API stability; implementation and tests are
# physically grouped by the work they own.
$orchestrationRoot = Join-Path $srcRoot 'orchestration'
$orchestrationLeafFiles = [ordered]@{
    'incremental' = @(
        'MsvcIncrementalArchiveCoordinator.cpp',
        'MsvcIncrementalCompileCoordinator.cpp',
        'MsvcIncrementalLinkCoordinator.cpp',
        'MsvcIncrementalStaticTargetCoordinator.cpp',
        'MsvcIncrementalTargetCoordinator.cpp'
    )
    'modules' = @('MsvcModuleCompileCoordinator.cpp', 'MsvcModuleTargetCoordinator.cpp')
    'routing' = @('MsvcTargetRouter.cpp')
    'scheduling' = @('BoundedWorkScheduler.cpp')
}
Assert-LeafLayout -Root $orchestrationRoot -LeafFiles $orchestrationLeafFiles

$orchestrationTestsRoot = Join-Path $testsRoot 'orchestration'
$orchestrationTestLeafFiles = [ordered]@{
    'incremental' = @(
        'incremental_compile_coordinator_tests.cpp',
        'incremental_link_coordinator_tests.cpp',
        'incremental_target_coordinator_tests.cpp'
    )
    'modules' = @(
        'header_unit_incremental_integration_tests.cpp',
        'header_unit_target_integration_tests.cpp',
        'header_unit_wave_integration_tests.cpp',
        'module_compile_coordinator_tests.cpp',
        'module_target_coordinator_tests.cpp',
        'module_target_integration_tests.cpp',
        'module_target_validation_tests.cpp'
    )
    'routing' = @('target_router_tests.cpp')
    'scheduling' = @('bounded_work_scheduler_tests.cpp')
}
Assert-LeafLayout -Root $orchestrationTestsRoot -LeafFiles $orchestrationTestLeafFiles

Write-Host 'C++ responsibility layout contract passed.'

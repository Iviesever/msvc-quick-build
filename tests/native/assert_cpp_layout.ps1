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

function Assert-NoDirectDirectories {
    param([Parameter(Mandatory = $true)][string]$Root)

    $directories = @(Get-ChildItem -LiteralPath $Root -Directory | Select-Object -ExpandProperty Name)
    if ($directories.Count -ne 0) {
        throw "Responsibility leaf '$Root' must not contain subdirectories: $($directories -join ', ')"
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

# Top-level directories are the architecture vocabulary. A new responsibility
# must be introduced intentionally by changing this contract.
Assert-DirectDirectories -Root $includeRoot -Allowed @(
    'config', 'core', 'discovery', 'json', 'modules', 'msvc',
    'orchestration', 'platform', 'process'
)
Assert-DirectDirectories -Root $srcRoot -Allowed @(
    'app', 'config', 'core', 'discovery', 'json', 'modules', 'msvc',
    'orchestration', 'platform'
)
Assert-DirectDirectories -Root $testsRoot -Allowed @(
    'app', 'config', 'core', 'discovery', 'e2e', 'json', 'modules',
    'msvc', 'orchestration', 'platform', 'process'
)

# Executable composition.
$appRoot = Join-Path $srcRoot 'app'
Assert-DirectDirectories -Root $appRoot -Allowed @('cli', 'diagnostics', 'project', 'targets')
Assert-ExactFiles -Root $appRoot -Expected @('Application.cpp', 'Application.hpp', 'main.cpp')
$appLeafFiles = [ordered]@{
    'cli' = @('Cli.cpp', 'Cli.hpp', 'Invocation.cpp', 'Invocation.hpp')
    'diagnostics' = @('Diagnostics.cpp', 'Diagnostics.hpp', 'PerformanceTimings.cpp', 'PerformanceTimings.hpp')
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
Assert-LeafLayout -Root (Join-Path $testsRoot 'app') -LeafFiles ([ordered]@{
    'cli' = @('build_policy_cli_tests.cpp', 'cli_argument_tests.cpp', 'mqb_native_msvc_cli_e2e_tests.cpp')
})

# Project configuration. JSON grammar belongs to the json layer; config owns
# only document loading, schema decoding, and effective-option resolution.
Assert-LeafLayout -Root (Join-Path $srcRoot 'config') -LeafFiles ([ordered]@{
    'loading' = @('ProjectConfig.cpp')
    'resolution' = @('ProjectOptions.cpp')
    'schema' = @('ProjectConfigSchema.cpp', 'ProjectConfigSchema.hpp')
})
Assert-LeafLayout -Root (Join-Path $testsRoot 'config') -LeafFiles ([ordered]@{
    'integration' = @('build_policy_config_tests.cpp', 'project_config_tests.cpp')
    'resolution' = @('project_options_tests.cpp')
})

# Source discovery. The public facade remains at the discovery root while
# source-text analysis, filesystem indexing, and graph selection own separate
# private leaves. Module syntax is lexical analysis, not traversal policy.
$discoveryRoot = Join-Path $srcRoot 'discovery'
Assert-DirectDirectories -Root $discoveryRoot -Allowed @('analysis', 'indexing', 'selection')
Assert-ExactFiles -Root $discoveryRoot -Expected @('SourceDiscovery.cpp')
$discoveryLeafFiles = [ordered]@{
    'analysis' = @('ModuleSyntax.cpp', 'SourceTextAnalysis.cpp', 'SourceTextAnalysis.hpp')
    'indexing' = @('DiscoveryPath.hpp', 'SourceIndex.cpp', 'SourceIndex.hpp')
    'selection' = @('SourceSelectionGraph.cpp', 'SourceSelectionGraph.hpp')
}
foreach ($leaf in $discoveryLeafFiles.Keys) {
    $leafRoot = Join-Path $discoveryRoot $leaf
    if (-not (Test-Path -LiteralPath $leafRoot -PathType Container)) {
        throw "Required discovery responsibility directory not found: $leafRoot"
    }
    Assert-ExactFiles -Root $leafRoot -Expected $discoveryLeafFiles[$leaf]
}
$discoveryTestsRoot = Join-Path $testsRoot 'discovery'
Assert-NoDirectDirectories -Root $discoveryTestsRoot
Assert-ExactFiles -Root $discoveryTestsRoot -Expected @(
    'c_source_discovery_tests.cpp',
    'module_source_discovery_tests.cpp',
    'module_syntax_tests.cpp',
    'source_discovery_corrections_tests.cpp',
    'source_discovery_tests.cpp'
)

# Toolchain-independent core. Public headers stay in include/mqb/core as a
# stable facade while implementation/test ownership is explicit.
$coreLeafFiles = [ordered]@{
    'cache' = @(
        'ArchiveCache.cpp', 'ArchiveCacheFile.cpp',
        'CompileCache.cpp', 'CompileCacheFile.cpp',
        'LinkCache.cpp', 'LinkCacheFile.cpp'
    )
    'model' = @('BuildSignature.cpp', 'BuildTypes.cpp', 'TranslationUnitClassifier.cpp')
    'planning' = @('BuildPlanner.cpp', 'DependencyGraph.cpp', 'ProjectArtifactLayout.cpp')
}
Assert-LeafLayout -Root (Join-Path $srcRoot 'core') -LeafFiles $coreLeafFiles
Assert-LeafLayout -Root (Join-Path $testsRoot 'core') -LeafFiles ([ordered]@{
    'cache' = @(
        'compile_cache_file_tests.cpp', 'compile_cache_tests.cpp',
        'link_cache_file_tests.cpp', 'link_state_tests.cpp'
    )
    'model' = @(
        'build_request_tests.cpp', 'build_signature_tests.cpp',
        'c_compile_signature_tests.cpp', 'c_source_recipe_tests.cpp',
        'translation_unit_classifier_tests.cpp'
    )
    'planning' = @(
        'build_plan_tests.cpp', 'build_planner_tests.cpp',
        'dependency_graph_tests.cpp', 'project_artifact_layout_tests.cpp'
    )
})

# MSVC backend primitives. Keep include/mqb/msvc stable, but do not flatten
# compiler, linker, librarian, module-scanning, parameter-routing, and toolchain discovery code.
$msvcLeafFiles = [ordered]@{
    'compiler' = @(
        'CompilerArgumentBuilder.cpp', 'CompilerArgumentBuilder.hpp',
        'CompilerExecution.cpp', 'CompilerExecution.hpp',
        'CompilerInvocationValidation.cpp', 'CompilerInvocationValidation.hpp',
        'MsvcCompileExecutor.cpp', 'MsvcCompiler.cpp', 'MsvcSourceDependenciesReader.cpp'
    )
    'librarian' = @('MsvcLibrarian.cpp')
    'linker' = @('MsvcLibraryResolver.cpp', 'MsvcLinker.cpp')
    'modules' = @('MsvcModuleDependencyScanner.cpp')
    'parameters' = @(
        'MsvcParameterEngine.cpp',
        'MsvcParameterRegistry.cpp',
        'MsvcParameterRegistry.hpp'
    )
    'toolchain' = @(
        'MsvcToolchainLocator.cpp',
        'PortableToolchainDiscovery.cpp', 'PortableToolchainDiscovery.hpp',
        'ToolchainDiscoveryPrimitives.cpp', 'ToolchainDiscoveryPrimitives.hpp',
        'VisualStudioEnvironment.cpp', 'VisualStudioEnvironment.hpp',
        'VisualStudioToolchainDiscovery.cpp', 'VisualStudioToolchainDiscovery.hpp'
    )
}
Assert-LeafLayout -Root (Join-Path $srcRoot 'msvc') -LeafFiles $msvcLeafFiles
Assert-LeafLayout -Root (Join-Path $testsRoot 'msvc') -LeafFiles ([ordered]@{
    'compiler' = @(
        'compile_executor_tests.cpp', 'compile_integration_tests.cpp',
        'compiler_arguments_tests.cpp', 'incremental_loop_tests.cpp',
        'source_dependencies_tests.cpp'
    )
    'librarian' = @('librarian_arguments_tests.cpp')
    'linker' = @('library_resolver_tests.cpp', 'link_integration_tests.cpp', 'linker_arguments_tests.cpp')
    'modules' = @(
        'header_unit_compile_integration_tests.cpp', 'header_unit_compiler_tests.cpp',
        'module_compile_integration_tests.cpp',
        'module_dependency_scanner_integration_tests.cpp',
        'module_dependency_scanner_tests.cpp'
    )
    'parameters' = @('msvc_parameter_engine_tests.cpp')
    'toolchain' = @('portable_tests.cpp', 'visual_studio_tests.cpp')
})

# Orchestration implementation responsibilities. Public facade headers remain
# in include/mqb/orchestration for API stability.
$orchestrationLeafFiles = [ordered]@{
    'incremental' = @(
        'IncrementalFileSnapshot.hpp',
        'MsvcIncrementalArchiveCoordinator.cpp',
        'MsvcIncrementalCompileCoordinator.cpp',
        'MsvcIncrementalLinkCoordinator.cpp',
        'MsvcIncrementalStaticTargetCoordinator.cpp',
        'MsvcIncrementalTargetCoordinator.cpp'
    )
    'modules' = @(
        'ModuleCompilePlan.cpp', 'ModuleCompilePlan.hpp',
        'ModuleCompileRequestFactory.cpp', 'ModuleCompileRequestFactory.hpp',
        'ModuleTargetArtifactRegistry.cpp', 'ModuleTargetArtifactRegistry.hpp',
        'ModuleTargetLinkRequestFactory.cpp', 'ModuleTargetLinkRequestFactory.hpp',
        'ModuleTargetPreparation.cpp', 'ModuleTargetPreparation.hpp',
        'ModuleTargetScanner.cpp', 'ModuleTargetScanner.hpp',
        'MsvcModuleCompileCoordinator.cpp', 'MsvcModuleTargetCoordinator.cpp',
        'StandardLibraryModuleProvider.cpp', 'StandardLibraryModuleProvider.hpp'
    )
    'routing' = @('MsvcTargetRouter.cpp')
    'scheduling' = @('BoundedWorkScheduler.cpp')
}
Assert-LeafLayout -Root (Join-Path $srcRoot 'orchestration') -LeafFiles $orchestrationLeafFiles
Assert-LeafLayout -Root (Join-Path $testsRoot 'orchestration') -LeafFiles ([ordered]@{
    'incremental' = @(
        'incremental_archive_coordinator_tests.cpp',
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
})

Write-Host 'C++ responsibility layout contract passed.'

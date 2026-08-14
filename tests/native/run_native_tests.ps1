[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuilderMqbPath,
    [Parameter(Mandatory = $true)][string]$TestMqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateRange(0, 63)][int]$ShardIndex = 0,
    [ValidateRange(1, 64)][int]$ShardCount = 1,
    [string]$SharedProductLibraryPath,
    [string]$PrepareSharedProductLibraryPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

if ($ShardIndex -ge $ShardCount) {
    throw "ShardIndex must be smaller than ShardCount: $ShardIndex >= $ShardCount"
}
if (-not [string]::IsNullOrWhiteSpace($SharedProductLibraryPath) -and
    -not [string]::IsNullOrWhiteSpace($PrepareSharedProductLibraryPath)) {
    throw 'SharedProductLibraryPath and PrepareSharedProductLibraryPath are mutually exclusive.'
}

$RepoRoot = Get-FullPath $RepoRoot
$BuilderMqbPath = Get-FullPath $BuilderMqbPath
$TestMqbPath = Get-FullPath $TestMqbPath
$cppRoot = Join-Path $RepoRoot 'cpp'
$configPath = Join-Path $cppRoot 'mqb.json'

foreach ($path in @($BuilderMqbPath, $TestMqbPath, $configPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required native-test input not found: $path"
    }
}

if (-not [string]::IsNullOrWhiteSpace($SharedProductLibraryPath)) {
    $SharedProductLibraryPath = Get-FullPath $SharedProductLibraryPath
    if (-not (Test-Path -LiteralPath $SharedProductLibraryPath -PathType Leaf)) {
        throw "Shared native-test product library not found: $SharedProductLibraryPath"
    }
}
if (-not [string]::IsNullOrWhiteSpace($PrepareSharedProductLibraryPath)) {
    $PrepareSharedProductLibraryPath = Get-FullPath $PrepareSharedProductLibraryPath
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$productionSources = @($config.discovery.extra_sources | ForEach-Object { $_.Replace('\', '/') } | Sort-Object -Unique)

# Native tests provide their own entry-point main, so their shared product
# graph is exactly cpp/src/**/*.cpp except src/app/main.cpp. Validate the
# manifest by source-set identity instead of coupling refactors to a TU count.
$expectedProductionSources = @(
    Get-ChildItem -LiteralPath (Join-Path $cppRoot 'src') -Recurse -File -Filter '*.cpp' |
        ForEach-Object {
            [System.IO.Path]::GetRelativePath($cppRoot, $_.FullName).Replace('\', '/')
        } |
        Where-Object { $_ -ne 'src/app/main.cpp' } |
        Sort-Object -Unique
)
$manifestDiff = @(Compare-Object -ReferenceObject $expectedProductionSources -DifferenceObject $productionSources)
if ($manifestDiff.Count -ne 0) {
    $details = @(
        $manifestDiff | ForEach-Object {
            $kind = if ($_.SideIndicator -eq '<=') { 'missing from manifest' } else { 'not a shared production source' }
            "  ${kind}: $($_.InputObject)"
        }
    ) -join [Environment]::NewLine
    throw "Native test production manifest does not exactly match cpp/src excluding app main:`n$details"
}
foreach ($source in $productionSources) {
    if (-not (Test-Path -LiteralPath (Join-Path $cppRoot $source) -PathType Leaf)) {
        throw "Native test production source is missing: $source"
    }
}

$includeDirs = @($config.build.include_dirs)
if ($includeDirs.Count -eq 0) { throw 'Native test config has no include_dirs.' }

$allTestFiles = @(Get-ChildItem -LiteralPath $cppRoot -Recurse -File -Filter '*_tests.cpp' |
    Where-Object { $_.FullName -notmatch '[\\/]\.mqb[\\/]' } |
    Sort-Object FullName)
if ($allTestFiles.Count -ne 72) {
    throw "Native test manifest drift: expected 72 *_tests.cpp files, found $($allTestFiles.Count)."
}

function Get-TestRelativePath {
    param([Parameter(Mandatory = $true)][System.IO.FileInfo]$File)
    return [System.IO.Path]::GetRelativePath($cppRoot, $File.FullName).Replace('\', '/')
}

# Keep the shard plan deterministic while accounting for the few tests whose
# nested MQB builds dominate wall time. These are deliberately coarse relative
# cost classes rather than runner-specific seconds, so normal hosted-runner
# variance does not churn the plan. Every unlisted test has weight 1.
#
# The overrides come from repeated Debug/Release CI observations. In
# particular, the old modulo split placed build-policy, module-CLI, and static-
# library E2E tests on the same shard, making it roughly a minute slower than
# its peers even though all four runners carried similar test counts.
$testWeightOverrides = [ordered]@{
    'tests/e2e/mqb_module_cli_e2e_tests.cpp' = 10
    'tests/e2e/mqb_build_policy_e2e_tests.cpp' = 9
    'tests/e2e/mqb_pch_e2e_tests.cpp' = 7
    'tests/e2e/mqb_static_library_e2e_tests.cpp' = 7
    'tests/e2e/mqb_cli_e2e_tests.cpp' = 4
    'tests/e2e/mqb_runtime_subsystem_config_e2e_tests.cpp' = 4
    'tests/e2e/mqb_dll_target_e2e_tests.cpp' = 3
    'tests/e2e/mqb_parallel_cli_e2e_tests.cpp' = 3
    'tests/e2e/mqb_project_config_e2e_tests.cpp' = 3
    'tests/e2e/mqb_c_source_e2e_tests.cpp' = 2
    # This test also requires building the process_echo_helper executable once
    # on whichever shard owns it, so account for that extra compile/link work.
    'tests/platform/windows/windows_process_runner_tests.cpp' = 4
}

$relativeTestPaths = @($allTestFiles | ForEach-Object { Get-TestRelativePath -File $_ })
foreach ($weightedPath in $testWeightOverrides.Keys) {
    if ($weightedPath -notin $relativeTestPaths) {
        throw "Native test weight override is stale or missing from the 72-test manifest: $weightedPath"
    }
}

$weightedTests = @(
    $allTestFiles | ForEach-Object {
        $relative = Get-TestRelativePath -File $_
        $weight = if ($testWeightOverrides.Contains($relative)) {
            [int]$testWeightOverrides[$relative]
        }
        else {
            1
        }
        [PSCustomObject]@{
            File = $_
            Relative = $relative
            Weight = $weight
        }
    } | Sort-Object `
        @{ Expression = 'Weight'; Descending = $true }, `
        @{ Expression = 'Relative'; Descending = $false }
)

$shardPlan = @(
    for ($index = 0; $index -lt $ShardCount; ++$index) {
        [PSCustomObject]@{
            Index = $index
            Weight = 0
            Tests = [System.Collections.ArrayList]::new()
        }
    }
)
foreach ($test in $weightedTests) {
    $targetShard = @($shardPlan | Sort-Object Weight, Index)[0]
    [void]$targetShard.Tests.Add($test.File)
    $targetShard.Weight += $test.Weight
}

$testFiles = @($shardPlan[$ShardIndex].Tests | Sort-Object FullName)
if ($testFiles.Count -eq 0) {
    throw "Native test shard $ShardIndex/$ShardCount selected no tests."
}
$selectedShardWeight = $shardPlan[$ShardIndex].Weight

$quote = [char]34
$versionDefine = 'MQB_VERSION=' + $quote + 'native-tests' + $quote
$configArg = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
$runtime = if ($Configuration -eq 'Debug') { 'MTd' } else { 'MT' }
$standard = [string]$config.build.standard

function Add-NativeCompilePolicyArguments {
    param([Parameter(Mandatory = $true)][System.Collections.ArrayList]$Arguments)

    [void]$Arguments.Add('--env')
    [void]$Arguments.Add('vs')
    [void]$Arguments.Add($configArg)
    [void]$Arguments.Add('--std')
    [void]$Arguments.Add($standard)
    [void]$Arguments.Add('--runtime')
    [void]$Arguments.Add($runtime)
    foreach ($include in $includeDirs) {
        [void]$Arguments.Add('-I')
        [void]$Arguments.Add('cpp/' + ([string]$include).Replace('\', '/'))
    }
    foreach ($compilerArg in @($config.build.compiler_args)) {
        [void]$Arguments.Add('--compiler-arg')
        [void]$Arguments.Add([string]$compilerArg)
    }
    [void]$Arguments.Add('-D')
    [void]$Arguments.Add($versionDefine)
}

function Invoke-MqbBuild {
    param(
        [Parameter(Mandatory = $true)][string[]]$Sources,
        [Parameter(Mandatory = $true)][string]$OutputName,
        [ValidateSet('Executable', 'Static')][string]$TargetKind = 'Executable',
        [switch]$UseSharedProductLibrary
    )

    $arguments = [System.Collections.ArrayList]::new()
    foreach ($source in $Sources) {
        [void]$arguments.Add($source)
    }
    Add-NativeCompilePolicyArguments -Arguments $arguments

    if ($TargetKind -eq 'Static') {
        [void]$arguments.Add('--type')
        [void]$arguments.Add('static')
    }
    elseif ($UseSharedProductLibrary) {
        # With one requested test source MQB would otherwise run source
        # discovery and re-add the same production graph already archived in
        # the shared library, producing duplicate definitions at link time.
        [void]$arguments.Add('--no-discover')
        [void]$arguments.Add('--lib')
        [void]$arguments.Add($SharedProductLibraryPath)
        [void]$arguments.Add('--linker-arg')
        [void]$arguments.Add("/WHOLEARCHIVE:$SharedProductLibraryPath")
    }

    if ($TargetKind -eq 'Executable') {
        [void]$arguments.Add('--lib')
        [void]$arguments.Add('shell32.lib')
    }
    [void]$arguments.Add('-o')
    [void]$arguments.Add($OutputName)

    Push-Location $RepoRoot
    try {
        $buildOutput = @(& $BuilderMqbPath @arguments 2>&1)
        $exitCode = $LASTEXITCODE
        foreach ($line in $buildOutput) { Write-Host $line }
        if ($exitCode -ne 0) {
            throw "MQB failed to build '$OutputName' with exit code $exitCode"
        }
    }
    finally {
        Pop-Location
    }
}

if (-not [string]::IsNullOrWhiteSpace($PrepareSharedProductLibraryPath)) {
    # Build the exact shared product graph once with the candidate MQB and the
    # same compile policy used by every native test. A static library is used
    # instead of moving .mqb caches across runners because cache freshness also
    # depends on filesystem timestamps. Test shards later link this library with
    # /WHOLEARCHIVE so every production object remains present just as before.
    $stateRoot = Join-Path $RepoRoot '.mqb'
    if (Test-Path -LiteralPath $stateRoot) {
        Remove-Item -LiteralPath $stateRoot -Recurse -Force
    }

    $libraryOutputName = 'native_test_product'
    $sources = @($productionSources | ForEach-Object { 'cpp/' + $_ })
    Write-Host "Preparing shared native-test product library for $Configuration"
    Write-Host "Verified shared production manifest: $($productionSources.Count) non-main translation units"
    Write-Host "Builder MQB: $BuilderMqbPath"
    Invoke-MqbBuild -Sources $sources -OutputName $libraryOutputName -TargetKind Static

    $builtLibrary = Join-Path $RepoRoot ".mqb/bin/$libraryOutputName.lib"
    if (-not (Test-Path -LiteralPath $builtLibrary -PathType Leaf)) {
        throw "Shared native-test library build did not produce: $builtLibrary"
    }
    $parent = Split-Path -Parent $PrepareSharedProductLibraryPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $builtLibrary -Destination $PrepareSharedProductLibraryPath -Force
    Write-Host "Prepared shared native-test product library: $PrepareSharedProductLibraryPath"
    Write-Output $PrepareSharedProductLibraryPath
    exit 0
}

function Get-TestOutputName {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $stem = [System.IO.Path]::ChangeExtension($RelativePath, $null)
    return ($stem -replace '[^0-9A-Za-z_]+', '_').Trim('_')
}

function Invoke-MqbTestBuild {
    param(
        [Parameter(Mandatory = $true)][string]$EntrySource,
        [Parameter(Mandatory = $true)][string]$OutputName
    )

    # Invoke from repository root, where no mqb.json is auto-loaded. The
    # current cpp/mqb.json is still the source of truth for the shared
    # production manifest and policy, but old/new builders see only native CLI.
    $sources = @('cpp/' + $EntrySource)
    $useSharedLibrary = -not [string]::IsNullOrWhiteSpace($SharedProductLibraryPath)
    if (-not $useSharedLibrary) {
        $sources += @($productionSources | ForEach-Object { 'cpp/' + $_ })
    }

    Invoke-MqbBuild `
        -Sources $sources `
        -OutputName $OutputName `
        -TargetKind Executable `
        -UseSharedProductLibrary:$useSharedLibrary

    $exe = Join-Path $RepoRoot ".mqb/bin/$OutputName.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Native test build did not produce: $exe"
    }
    return $exe
}

Write-Host "MQB-native test graph: $($allTestFiles.Count) total tests / $($testFiles.Count) selected / configuration $Configuration"
Write-Host "Shard: $ShardIndex of $ShardCount (zero-based index) / estimated relative weight $selectedShardWeight"
Write-Host 'Deterministic weighted shard plan:'
foreach ($shard in $shardPlan) {
    Write-Host "  shard $($shard.Index): $($shard.Tests.Count) tests / weight $($shard.Weight)"
}
Write-Host "Verified shared production manifest: $($productionSources.Count) non-main translation units"
Write-Host "Builder MQB: $BuilderMqbPath"
Write-Host "Tested MQB:  $TestMqbPath"
if (-not [string]::IsNullOrWhiteSpace($SharedProductLibraryPath)) {
    Write-Host "Shared product library: $SharedProductLibraryPath"
}
else {
    Write-Host 'Shared product library: disabled; each shard compiles the production graph locally.'
}

$helperExe = $null
if (@($testFiles | Where-Object { $_.Name -eq 'windows_process_runner_tests.cpp' }).Count -ne 0) {
    $helperSource = 'tests/process/process_echo_helper.cpp'
    $helperExe = Invoke-MqbTestBuild -EntrySource $helperSource -OutputName 'native_test_process_echo_helper'
}

$workRoot = Join-Path $RepoRoot "native-test-work/$($Configuration.ToLowerInvariant())/shard-$ShardIndex-of-$ShardCount"
if (Test-Path -LiteralPath $workRoot) {
    Remove-Item -LiteralPath $workRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null

$passed = 0
$failures = @()
foreach ($testFile in $testFiles) {
    $relative = Get-TestRelativePath -File $testFile
    $outputName = Get-TestOutputName -RelativePath $relative
    Write-Host ""
    Write-Host "=== [$($passed + $failures.Count + 1)/$($testFiles.Count)] $relative ==="

    $exe = Invoke-MqbTestBuild -EntrySource $relative -OutputName $outputName
    $testArgs = @()
    if ($testFile.Name -match '^mqb_.*_e2e_tests\.cpp$') {
        $testArgs += $TestMqbPath
    }
    elseif ($testFile.Name -eq 'windows_process_runner_tests.cpp') {
        if ($null -eq $helperExe) { throw 'Process helper was not prepared for windows_process_runner_tests.cpp.' }
        $testArgs += $helperExe
    }

    $testWork = Join-Path $workRoot $outputName
    New-Item -ItemType Directory -Force -Path $testWork | Out-Null

    Push-Location $testWork
    try {
        $testOutput = @(& $exe @testArgs 2>&1)
        $exitCode = $LASTEXITCODE
        foreach ($line in $testOutput) { Write-Host $line }
    }
    finally {
        Pop-Location
    }

    if ($exitCode -eq 0) {
        $passed++
        Write-Host "PASS: $relative"
    }
    else {
        $failures += [PSCustomObject]@{
            Test = $relative
            ExitCode = $exitCode
        }
        Write-Host "FAIL: $relative (exit $exitCode)"
    }
}

Write-Host ""
Write-Host "MQB-native shard $ShardIndex/${ShardCount}: $passed/$($testFiles.Count) selected tests passed."
if ($failures.Count -ne 0) {
    $failures | Format-Table -AutoSize | Out-String | Write-Host
    exit 1
}

Write-Host 'Selected MQB-native tests passed without CMake or CTest.'
exit 0

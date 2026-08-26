[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 5,
    [ValidateRange(2, 256)][int]$TranslationUnits = 100,
    [ValidateRange(1, 256)][int]$Jobs = [Environment]::ProcessorCount,
    [string]$CMakePath = 'cmake',
    [string]$NinjaPath = 'ninja',
    [string]$OutputPath,
    [switch]$KeepWorktree
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq 'Core') {
    throw 'The build-system comparison harness requires Windows + MSVC.'
}

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Resolve-ApplicationPath {
    param([Parameter(Mandatory = $true)][string]$Value)

    if (Test-Path -LiteralPath $Value -PathType Leaf) {
        return (Resolve-Path -LiteralPath $Value).Path
    }

    $command = Get-Command $Value -CommandType Application -ErrorAction Stop | Select-Object -First 1
    return $command.Source
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $false)][string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [switch]$Measure
    )

    Push-Location $WorkingDirectory
    try {
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $output = @(& $FilePath @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
        $watch.Stop()
    }
    finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        foreach ($line in $output) { Write-Host $line }
        $display = @($FilePath) + $Arguments -join ' '
        throw "Command failed with exit code $exitCode`: $display"
    }

    return [PSCustomObject]@{
        elapsed_ms = if ($Measure) { [Math]::Round($watch.Elapsed.TotalMilliseconds, 3) } else { $null }
        output = $output
    }
}

function Import-MsvcX64Environment {
    $cl = Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $cl -and ([string]::IsNullOrWhiteSpace($env:VSCMD_ARG_TGT_ARCH) -or $env:VSCMD_ARG_TGT_ARCH -eq 'x64')) {
        return $false
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $candidates.Add((Join-Path $programFilesX86 'Microsoft Visual Studio/Installer/vswhere.exe'))
    }
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $candidates.Add((Join-Path $env:ProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe'))
    }

    $vswhere = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($vswhere)) {
        throw 'cl.exe is not available and vswhere.exe could not be found.'
    }

    $installationPath = @(
        & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>&1
    ) | Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        throw 'No Visual Studio installation with the MSVC x86/x64 tools was found.'
    }

    $vsDevCmd = Join-Path ([string]$installationPath) 'Common7/Tools/VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
        throw "VsDevCmd.bat not found: $vsDevCmd"
    }

    $env:VSCMD_SKIP_SENDTELEMETRY = '1'
    $command = "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    $environmentLines = @(& $env:ComSpec /d /s /c $command 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) {
        foreach ($line in $environmentLines) { Write-Host $line }
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE"
    }

    foreach ($line in $environmentLines) {
        if ($line -notmatch '^([^=]+)=(.*)$') { continue }
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }

    $cl = Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $cl) {
        throw 'VsDevCmd.bat completed but cl.exe is still unavailable.'
    }
    return $true
}

function Get-FirstOutputLine {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $false)][string[]]$Arguments = @()
    )

    $output = @(& $FilePath @Arguments 2>&1 | ForEach-Object { [string]$_ })
    return @($output | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)[0]
}

function Get-FileIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($resolved)
    return [PSCustomObject]@{
        path = $resolved
        sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
        file_version = [string]$version.FileVersion
        product_version = [string]$version.ProductVersion
    }
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)

    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 0) {
        return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
    }
    return $sorted[$middle]
}

function Write-BenchmarkFixture {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][int]$Count,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][bool]$UsePch
    )

    New-Item -ItemType Directory -Path $Root -Force | Out-Null

    if ($UsePch) {
        Set-Content -LiteralPath (Join-Path $Root 'pch.hpp') -Encoding utf8 -Value @'
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
'@
    }

    Set-Content -LiteralPath (Join-Path $Root 'common.hpp') -Encoding utf8 -Value @'
#pragma once
inline int shared_value(const int value) noexcept { return value * 3 + 1; }
'@

    $sourceNames = [System.Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $Count; ++$index) {
        $name = 'unit_{0:D3}.cpp' -f $index
        $sourceNames.Add($name)
        Set-Content -LiteralPath (Join-Path $Root $name) -Encoding utf8 -Value @"
#include "common.hpp"
int unit_$('{0:D3}' -f $index)() noexcept { return shared_value($index); }
"@
    }

    $declarations = @(
        for ($index = 0; $index -lt $Count; ++$index) {
            "int unit_$('{0:D3}' -f $index)() noexcept;"
        }
    ) -join "`r`n"
    $calls = @(
        for ($index = 0; $index -lt $Count; ++$index) {
            "    total += unit_$('{0:D3}' -f $index)();"
        }
    ) -join "`r`n"
    $expected = [int](3 * $Count * ($Count - 1) / 2 + $Count)

    Set-Content -LiteralPath (Join-Path $Root 'main.cpp') -Encoding utf8 -Value @"
$declarations
int main() {
    int total = 0;
$calls
    return total == $expected ? 0 : 1;
}
"@

    $allSources = @('main.cpp') + @($sourceNames)
    $sourceBlock = @($allSources | ForEach-Object { "  $_" }) -join "`r`n"
    $pchLine = if ($UsePch) {
        'target_precompile_headers({0} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/pch.hpp")' -f $Target
    } else {
        ''
    }

    Set-Content -LiteralPath (Join-Path $Root 'CMakeLists.txt') -Encoding utf8 -Value @"
cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(mqb_build_system_evidence LANGUAGES CXX)

add_executable($Target
$sourceBlock
)

set_property(TARGET $Target PROPERTY MSVC_RUNTIME_LIBRARY "")

if(MSVC)
  target_compile_options($Target PRIVATE
    /nologo
    /utf-8
    /W3
    /EHsc
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /diagnostics:column
    /O2
    /Oi
    /MD
    /Z7
    /DNDEBUG
    /std:c++23preview
  )
  target_link_options($Target PRIVATE
    /NOLOGO
    /INCREMENTAL:NO
    /OPT:REF
    /OPT:ICF
    /MACHINE:X64
    /SUBSYSTEM:CONSOLE
  )
endif()

$pchLine
"@

    return $allSources
}

function Configure-CMakeFixture {
    param(
        [Parameter(Mandatory = $true)][string]$CMakeExe,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $buildDirectory = Join-Path $Root 'cmake-build'
    $arguments = @(
        '-S', $Root,
        '-B', $buildDirectory,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_CXX_FLAGS=',
        '-DCMAKE_CXX_FLAGS_RELEASE=',
        '-DCMAKE_EXE_LINKER_FLAGS=',
        '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=',
        '-DCMAKE_CXX_STANDARD_LIBRARIES='
    )
    $result = Invoke-CapturedProcess -FilePath $CMakeExe -Arguments $arguments -WorkingDirectory $Root -Measure
    return [PSCustomObject]@{
        build_directory = $buildDirectory
        elapsed_ms = $result.elapsed_ms
    }
}

function Resolve-MqbExecutableOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Target
    )

    $expected = Join-Path $Root ('.mqb/bin/' + $Target + '.exe')
    if (Test-Path -LiteralPath $expected -PathType Leaf) {
        return (Resolve-Path -LiteralPath $expected).Path
    }

    $matches = @(
        Get-ChildItem -LiteralPath $Root -Filter ($Target + '.exe') -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\.mqb\*' }
    )
    if ($matches.Count -ne 1) {
        throw "Expected one MQB executable output for '$Target', found $($matches.Count)."
    }
    return $matches[0].FullName
}

function Assert-ExecutableRuns {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Benchmark executable is missing: $Executable"
    }
    Push-Location $WorkingDirectory
    try {
        & $Executable *> $null
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "Benchmark executable failed with exit code $exitCode`: $Executable"
    }
}

function Invoke-MqbBuild {
    param(
        [Parameter(Mandatory = $true)][string]$MqbExe,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$Sources,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][int]$JobCount,
        [Parameter(Mandatory = $true)][bool]$UsePch
    )

    $arguments = @('build') + $Sources + @(
        '--release',
        '--std', '23',
        '--runtime', 'MD',
        '--x64',
        '-j', [string]$JobCount,
        '-o', $Target
    )
    if ($UsePch) {
        $arguments += @('--pch', 'pch.hpp')
    }

    $result = Invoke-CapturedProcess -FilePath $MqbExe -Arguments $arguments -WorkingDirectory $Root -Measure
    $executable = Resolve-MqbExecutableOutput -Root $Root -Target $Target
    Assert-ExecutableRuns -Executable $executable -WorkingDirectory $Root
    return $result.elapsed_ms
}

function Invoke-NinjaBuild {
    param(
        [Parameter(Mandatory = $true)][string]$NinjaExe,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][int]$JobCount
    )

    $arguments = @('-C', $BuildDirectory, '-j', [string]$JobCount, $Target)
    $result = Invoke-CapturedProcess -FilePath $NinjaExe -Arguments $arguments -WorkingDirectory $Root -Measure
    $executable = Join-Path $BuildDirectory ($Target + '.exe')
    Assert-ExecutableRuns -Executable $executable -WorkingDirectory $Root
    return $result.elapsed_ms
}

function Add-Mutation {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )
    Add-Content -LiteralPath $Path -Encoding utf8 -Value $Text
}

$MqbPath = Get-FullPath $MqbPath
if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) {
    throw "MQB executable not found: $MqbPath"
}
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Get-FullPath $OutputPath
}

$vsEnvironmentImported = Import-MsvcX64Environment
$MqbPath = Resolve-ApplicationPath $MqbPath
$CMakePath = Resolve-ApplicationPath $CMakePath
$NinjaPath = Resolve-ApplicationPath $NinjaPath
$clPath = Resolve-ApplicationPath 'cl.exe'

$cmakeVersionLine = Get-FirstOutputLine -FilePath $CMakePath -Arguments @('--version')
$ninjaVersionLine = Get-FirstOutputLine -FilePath $NinjaPath -Arguments @('--version')
$clVersionLine = Get-FirstOutputLine -FilePath $clPath

$computerMemory = $null
try {
    $computerSystem = Get-CimInstance Win32_ComputerSystem -ErrorAction Stop
    $computerMemory = [UInt64]$computerSystem.TotalPhysicalMemory
}
catch {
    $computerMemory = $null
}

$environment = [PSCustomObject]@{
    os_version = [Environment]::OSVersion.VersionString
    powershell = $PSVersionTable.PSVersion.ToString()
    processor_identifier = [string]$env:PROCESSOR_IDENTIFIER
    logical_processors = [Environment]::ProcessorCount
    physical_memory_bytes = $computerMemory
    vsdevcmd_imported = $vsEnvironmentImported
    vctools_version = [string]$env:VCToolsVersion
    windows_sdk_version = [string]$env:WindowsSDKVersion
}

$tools = [PSCustomObject]@{
    mqb = Get-FileIdentity -Path $MqbPath
    cmake = [PSCustomObject]@{
        identity = Get-FileIdentity -Path $CMakePath
        version = $cmakeVersionLine
    }
    ninja = [PSCustomObject]@{
        identity = Get-FileIdentity -Path $NinjaPath
        version = $ninjaVersionLine
    }
    cl = [PSCustomObject]@{
        identity = Get-FileIdentity -Path $clPath
        version = $clVersionLine
    }
}

$samples = [System.Collections.Generic.List[object]]::new()
$configureSamples = [System.Collections.Generic.List[object]]::new()
$benchmarkRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('mqb-build-system-evidence-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $benchmarkRoot | Out-Null

$fixtureDefinitions = @(
    [PSCustomObject]@{
        name = 'ordinary'
        target = 'ordinary_bench'
        use_pch = $false
        scenarios = @('ordinary-cold', 'ordinary-no-op', 'ordinary-single-tu', 'ordinary-public-header')
    },
    [PSCustomObject]@{
        name = 'pch'
        target = 'pch_bench'
        use_pch = $true
        scenarios = @('pch-cold', 'pch-no-op', 'pch-single-tu', 'pch-header')
    }
)

try {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
        foreach ($fixture in $fixtureDefinitions) {
            $root = Join-Path $benchmarkRoot ("$($fixture.name)-$iteration")
            $sources = @(Write-BenchmarkFixture `
                -Root $root `
                -Count $TranslationUnits `
                -Target $fixture.target `
                -UsePch $fixture.use_pch)

            $configuration = Configure-CMakeFixture -CMakeExe $CMakePath -Root $root
            $configureSamples.Add([PSCustomObject]@{
                iteration = $iteration
                fixture = $fixture.name
                elapsed_ms = $configuration.elapsed_ms
            })

            for ($scenarioIndex = 0; $scenarioIndex -lt $fixture.scenarios.Count; ++$scenarioIndex) {
                $scenario = [string]$fixture.scenarios[$scenarioIndex]

                if ($scenario -like '*-single-tu') {
                    Add-Mutation -Path (Join-Path $root 'unit_000.cpp') -Text "// single-tu mutation $iteration"
                } elseif ($scenario -eq 'ordinary-public-header') {
                    Add-Mutation -Path (Join-Path $root 'common.hpp') -Text "// public-header mutation $iteration"
                } elseif ($scenario -eq 'pch-header') {
                    Add-Mutation -Path (Join-Path $root 'pch.hpp') -Text "// pch-header mutation $iteration"
                }

                $globalScenarioIndex = if ($fixture.name -eq 'ordinary') { $scenarioIndex } else { 4 + $scenarioIndex }
                $mqbFirst = (($iteration + $globalScenarioIndex) % 2) -eq 0
                $order = if ($mqbFirst) { @('mqb', 'cmake+ninja') } else { @('cmake+ninja', 'mqb') }

                for ($orderIndex = 0; $orderIndex -lt $order.Count; ++$orderIndex) {
                    $system = [string]$order[$orderIndex]
                    Write-Host ("[{0}] iteration {1}/{2}: {3}" -f $system, $iteration, $Iterations, $scenario)

                    $elapsed = if ($system -eq 'mqb') {
                        Invoke-MqbBuild `
                            -MqbExe $MqbPath `
                            -Root $root `
                            -Sources $sources `
                            -Target $fixture.target `
                            -JobCount $Jobs `
                            -UsePch $fixture.use_pch
                    } else {
                        Invoke-NinjaBuild `
                            -NinjaExe $NinjaPath `
                            -Root $root `
                            -BuildDirectory $configuration.build_directory `
                            -Target $fixture.target `
                            -JobCount $Jobs
                    }

                    $samples.Add([PSCustomObject]@{
                        iteration = $iteration
                        scenario = $scenario
                        system = $system
                        execution_order = $orderIndex + 1
                        elapsed_ms = [double]$elapsed
                    })
                }
            }
        }
    }

    $scenarioNames = @($fixtureDefinitions | ForEach-Object { $_.scenarios } | ForEach-Object { $_ })
    $comparison = @(
        foreach ($scenario in $scenarioNames) {
            $mqbRows = @($samples | Where-Object { $_.scenario -eq $scenario -and $_.system -eq 'mqb' })
            $ninjaRows = @($samples | Where-Object { $_.scenario -eq $scenario -and $_.system -eq 'cmake+ninja' })
            if ($mqbRows.Count -ne $Iterations -or $ninjaRows.Count -ne $Iterations) {
                throw "Scenario '$scenario' does not contain exactly $Iterations samples per system."
            }

            $mqbMedian = Get-Median -Values @($mqbRows | ForEach-Object { [double]$_.elapsed_ms })
            $ninjaMedian = Get-Median -Values @($ninjaRows | ForEach-Object { [double]$_.elapsed_ms })
            [PSCustomObject]@{
                scenario = $scenario
                samples_per_system = $Iterations
                mqb_median_ms = [Math]::Round($mqbMedian, 3)
                cmake_ninja_median_ms = [Math]::Round($ninjaMedian, 3)
                mqb_delta_ms_vs_cmake_ninja = [Math]::Round(($mqbMedian - $ninjaMedian), 3)
                mqb_delta_pct_vs_cmake_ninja = if ($ninjaMedian -eq 0.0) {
                    $null
                } else {
                    [Math]::Round((($mqbMedian - $ninjaMedian) / $ninjaMedian) * 100.0, 2)
                }
                cmake_ninja_over_mqb_ratio = if ($mqbMedian -eq 0.0) {
                    $null
                } else {
                    [Math]::Round(($ninjaMedian / $mqbMedian), 3)
                }
            }
        }
    )

    $configureSummary = @(
        $configureSamples |
            Group-Object fixture |
            ForEach-Object {
                [PSCustomObject]@{
                    fixture = $_.Name
                    samples = $_.Count
                    median_ms = [Math]::Round((Get-Median -Values @($_.Group | ForEach-Object { [double]$_.elapsed_ms })), 3)
                }
            } |
            Sort-Object fixture
    )

    Write-Host "`n=== Build-only median comparison ==="
    $comparison | Format-Table -AutoSize
    Write-Host 'Negative MQB deltas mean lower MQB wall-clock time. CMake configure time is measured separately and excluded from build scenarios.'
    Write-Host 'Ninja is invoked directly after configuration, so cmake --build wrapper overhead is not included.'
    Write-Host "`n=== CMake configure medians (not included above) ==="
    $configureSummary | Format-Table -AutoSize

    if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
        $parent = Split-Path -Parent $OutputPath
        if (-not [string]::IsNullOrWhiteSpace($parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }

        [PSCustomObject]@{
            schema_version = 1
            generated_utc = [DateTime]::UtcNow.ToString('o')
            methodology = [PSCustomObject]@{
                translation_units = $TranslationUnits
                iterations = $Iterations
                jobs = $Jobs
                target_architecture = 'x64'
                build_configuration = 'release'
                cpp_standard_flag = '/std:c++23preview'
                same_source_tree_per_iteration = $true
                alternating_execution_order = $true
                cmake_generator = 'Ninja'
                cmake_configure_excluded_from_build_timings = $true
                ninja_invoked_directly = $true
                pch_uses_native_build_system_mechanism = $true
                note = 'This is machine-specific reproducible evidence, not a universal performance claim.'
            }
            environment = $environment
            tools = $tools
            cmake_configure_samples = $configureSamples
            cmake_configure_summary = $configureSummary
            samples = $samples
            comparison = $comparison
            retained_worktree = if ($KeepWorktree) { $benchmarkRoot } else { $null }
        } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding utf8
        Write-Host "Build-system comparison JSON: $OutputPath"
    }
}
finally {
    if ($KeepWorktree) {
        Write-Host "Benchmark worktree retained: $benchmarkRoot"
    } else {
        Remove-Item -LiteralPath $benchmarkRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

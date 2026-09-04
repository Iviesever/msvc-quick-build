[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 3,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$TargetScaleSourceCount = 256

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

$MqbPath = Get-FullPath $MqbPath
if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) {
    throw "MQB executable not found: $MqbPath"
}
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Get-FullPath $OutputPath
}

function Invoke-TimedMqb {
    param(
        [Parameter(Mandatory = $true)][string]$Scenario,
        [Parameter(Mandatory = $true)][int]$Iteration,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Push-Location $WorkingDirectory
    try {
        $output = @(& $MqbPath @Arguments '--timings=json' 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        foreach ($line in $output) { Write-Host $line }
        throw "Benchmark scenario '$Scenario' failed with exit code $exitCode"
    }

    $timingLine = @($output | ForEach-Object { [string]$_ } |
        Where-Object { $_ -like '{"type":"mqb.timings"*' }) | Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($timingLine)) {
        foreach ($line in $output) { Write-Host $line }
        throw "Benchmark scenario '$Scenario' produced no MQB timing JSON record"
    }

    $timing = $timingLine | ConvertFrom-Json
    return [PSCustomObject]@{
        iteration = $Iteration
        scenario = $Scenario
        total_ms = [double]$timing.phases.total
        discovery_ms = [double]$timing.phases.discovery
        dependency_scan_ms = [double]$timing.phases.dependency_scan
        compile_queue_ms = [double]$timing.phases.compile_queue
        compile_ms = [double]$timing.phases.compile
        link_ms = [double]$timing.phases.link
        archive_ms = [double]$timing.phases.archive
        run_startup_ms = [double]$timing.phases.run_startup
        compile_hits = [int]$timing.cache.compile.hits
        compile_misses = [int]$timing.cache.compile.misses
        link_hits = [int]$timing.cache.link.hits
        link_misses = [int]$timing.cache.link.misses
        archive_hits = [int]$timing.cache.archive.hits
        archive_misses = [int]$timing.cache.archive.misses
    }
}

function Assert-CompileCacheCounts {
    param(
        [Parameter(Mandatory = $true)]$Sample,
        [Parameter(Mandatory = $true)][int]$ExpectedHits,
        [Parameter(Mandatory = $true)][int]$ExpectedMisses
    )

    if ([int]$Sample.compile_hits -ne $ExpectedHits
        -or [int]$Sample.compile_misses -ne $ExpectedMisses) {
        throw "Benchmark scenario '$($Sample.scenario)' produced compile cache $($Sample.compile_hits) hit(s) / $($Sample.compile_misses) miss(es); expected $ExpectedHits / $ExpectedMisses"
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

$results = [System.Collections.Generic.List[object]]::new()
$benchmarkRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-benchmark-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $benchmarkRoot | Out-Null

try {
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
        $ordinaryRoot = Join-Path $benchmarkRoot "ordinary-$iteration"
        New-Item -ItemType Directory -Path $ordinaryRoot | Out-Null

        Set-Content -LiteralPath (Join-Path $ordinaryRoot 'common.hpp') -Encoding utf8 -Value @'
#pragma once
inline int shared_value() { return 40; }
'@
        Set-Content -LiteralPath (Join-Path $ordinaryRoot 'helper.cpp') -Encoding utf8 -Value @'
#include "common.hpp"
int helper_value() { return shared_value() + 2; }
'@
        Set-Content -LiteralPath (Join-Path $ordinaryRoot 'main.cpp') -Encoding utf8 -Value @'
#include "common.hpp"
int helper_value();
int main() { return helper_value() == shared_value() + 2 ? 0 : 1; }
'@

        $ordinaryArgs = @('main.cpp', 'helper.cpp', '--output', 'bench')
        $results.Add((Invoke-TimedMqb -Scenario 'cold' -Iteration $iteration -WorkingDirectory $ordinaryRoot -Arguments $ordinaryArgs))
        $results.Add((Invoke-TimedMqb -Scenario 'no-op' -Iteration $iteration -WorkingDirectory $ordinaryRoot -Arguments $ordinaryArgs))

        Add-Content -LiteralPath (Join-Path $ordinaryRoot 'helper.cpp') -Encoding utf8 -Value "// single-tu mutation $iteration"
        $results.Add((Invoke-TimedMqb -Scenario 'single-tu' -Iteration $iteration -WorkingDirectory $ordinaryRoot -Arguments $ordinaryArgs))

        Add-Content -LiteralPath (Join-Path $ordinaryRoot 'common.hpp') -Encoding utf8 -Value "// public-header mutation $iteration"
        $results.Add((Invoke-TimedMqb -Scenario 'public-header' -Iteration $iteration -WorkingDirectory $ordinaryRoot -Arguments $ordinaryArgs))

        $results.Add((Invoke-TimedMqb -Scenario 'build-run' -Iteration $iteration -WorkingDirectory $ordinaryRoot -Arguments ($ordinaryArgs + @('--run'))))
        $results.Add((Invoke-TimedMqb -Scenario 'link-only' -Iteration $iteration -WorkingDirectory $ordinaryRoot -Arguments ($ordinaryArgs + @('--linker-arg', '/MAP'))))

        # Scale fixture for target setup/validation. All source and artifact
        # identities are unique, so the compile_queue phase exposes how target
        # registration grows without mixing in duplicate-error handling.
        $targetScaleRoot = Join-Path $benchmarkRoot "target-scale-$iteration"
        New-Item -ItemType Directory -Path $targetScaleRoot | Out-Null
        Set-Content -LiteralPath (Join-Path $targetScaleRoot 'main.cpp') -Encoding utf8 -Value @'
int main() { return 0; }
'@
        $targetScaleArgs = [System.Collections.Generic.List[string]]::new()
        $targetScaleArgs.Add('main.cpp')
        for ($sourceIndex = 0; $sourceIndex -lt $TargetScaleSourceCount; ++$sourceIndex) {
            $sourceName = 'unit_{0:D3}.cpp' -f $sourceIndex
            Set-Content -LiteralPath (Join-Path $targetScaleRoot $sourceName) -Encoding utf8 -Value "int target_scale_${sourceIndex}() { return $sourceIndex; }"
            $targetScaleArgs.Add($sourceName)
        }
        $targetScaleArgs.Add('--output')
        $targetScaleArgs.Add('target_scale_bench')
        $targetScaleArguments = $targetScaleArgs.ToArray()
        $targetScaleUnitCount = $TargetScaleSourceCount + 1

        $targetScaleCold = Invoke-TimedMqb -Scenario 'target-scale-cold' -Iteration $iteration -WorkingDirectory $targetScaleRoot -Arguments $targetScaleArguments
        Assert-CompileCacheCounts -Sample $targetScaleCold -ExpectedHits 0 -ExpectedMisses $targetScaleUnitCount
        $results.Add($targetScaleCold)

        $targetScaleNoOp = Invoke-TimedMqb -Scenario 'target-scale-no-op' -Iteration $iteration -WorkingDirectory $targetScaleRoot -Arguments $targetScaleArguments
        Assert-CompileCacheCounts -Sample $targetScaleNoOp -ExpectedHits $targetScaleUnitCount -ExpectedMisses 0
        $results.Add($targetScaleNoOp)

        $lastScaleSource = 'unit_{0:D3}.cpp' -f ($TargetScaleSourceCount - 1)
        Add-Content -LiteralPath (Join-Path $targetScaleRoot $lastScaleSource) -Encoding utf8 -Value "// target-scale mutation $iteration"
        $targetScaleSingle = Invoke-TimedMqb -Scenario 'target-scale-single-tu' -Iteration $iteration -WorkingDirectory $targetScaleRoot -Arguments $targetScaleArguments
        Assert-CompileCacheCounts -Sample $targetScaleSingle -ExpectedHits ($targetScaleUnitCount - 1) -ExpectedMisses 1
        $results.Add($targetScaleSingle)

        # Dedicated smart-discovery fixture. Only the entry TU is passed to MQB;
        # helper.cpp must be found through the shared local header graph. The
        # warm sample therefore isolates the cost of repeated filesystem
        # enumeration/source-text analysis that persistent discovery can avoid.
        $discoveryRoot = Join-Path $benchmarkRoot "discovery-$iteration"
        New-Item -ItemType Directory -Path (Join-Path $discoveryRoot 'src') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $discoveryRoot 'src/common.hpp') -Encoding utf8 -Value @'
#pragma once
int helper_value();
'@
        Set-Content -LiteralPath (Join-Path $discoveryRoot 'src/helper.cpp') -Encoding utf8 -Value @'
#include "common.hpp"
int helper_value() { return 42; }
'@
        Set-Content -LiteralPath (Join-Path $discoveryRoot 'main.cpp') -Encoding utf8 -Value @'
#include "src/common.hpp"
int main() { return helper_value() == 42 ? 0 : 1; }
'@
        $discoveryArgs = @('main.cpp', '--output', 'discovery_bench')
        $results.Add((Invoke-TimedMqb -Scenario 'discovery-cold' -Iteration $iteration -WorkingDirectory $discoveryRoot -Arguments $discoveryArgs))
        $results.Add((Invoke-TimedMqb -Scenario 'discovery-no-op' -Iteration $iteration -WorkingDirectory $discoveryRoot -Arguments $discoveryArgs))

        Add-Content -LiteralPath (Join-Path $discoveryRoot 'src/common.hpp') -Encoding utf8 -Value "// discovery invalidation $iteration"
        $results.Add((Invoke-TimedMqb -Scenario 'discovery-header' -Iteration $iteration -WorkingDirectory $discoveryRoot -Arguments $discoveryArgs))

        $moduleRoot = Join-Path $benchmarkRoot "modules-$iteration"
        New-Item -ItemType Directory -Path $moduleRoot | Out-Null
        Set-Content -LiteralPath (Join-Path $moduleRoot 'bench_math.ixx') -Encoding utf8 -Value @'
export module bench_math;
export int bench_value() { return 42; }
'@
        Set-Content -LiteralPath (Join-Path $moduleRoot 'main.cpp') -Encoding utf8 -Value @'
import bench_math;
int main() { return bench_value() == 42 ? 0 : 1; }
'@
        $moduleArgs = @('bench_math.ixx', 'main.cpp', '--std', 'latest', '--output', 'module_bench')
        $results.Add((Invoke-TimedMqb -Scenario 'modules-cold' -Iteration $iteration -WorkingDirectory $moduleRoot -Arguments $moduleArgs))
        $results.Add((Invoke-TimedMqb -Scenario 'modules-no-op' -Iteration $iteration -WorkingDirectory $moduleRoot -Arguments $moduleArgs))
    }
}
finally {
    Remove-Item -LiteralPath $benchmarkRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$ordered = @($results | Sort-Object iteration, scenario)
$ordered | Format-Table -AutoSize

$summary = @(
    $ordered |
        Group-Object scenario |
        ForEach-Object {
            $group = @($_.Group)
            $medianTotal = Get-Median -Values @($group.total_ms)
            $medianDiscovery = Get-Median -Values @($group.discovery_ms)
            $medianCompileQueue = Get-Median -Values @($group.compile_queue_ms)
            [PSCustomObject]@{
                scenario = $_.Name
                samples = $group.Count
                median_total_ms = [Math]::Round($medianTotal, 3)
                median_discovery_ms = [Math]::Round($medianDiscovery, 3)
                median_compile_queue_ms = [Math]::Round($medianCompileQueue, 3)
                min_total_ms = [Math]::Round((($group.total_ms | Measure-Object -Minimum).Minimum), 3)
                max_total_ms = [Math]::Round((($group.total_ms | Measure-Object -Maximum).Maximum), 3)
            }
        } |
        Sort-Object scenario
)

Write-Host "`nMedian wall-clock summary:"
$summary | Format-Table -AutoSize

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [PSCustomObject]@{
        schema_version = 2
        generated_utc = [DateTime]::UtcNow.ToString('o')
        mqb = $MqbPath
        iterations = $Iterations
        target_scale_source_count = $TargetScaleSourceCount
        samples = $ordered
        summary = $summary
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Benchmark JSON: $OutputPath"
}

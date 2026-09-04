[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 3,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$TargetScaleSourceCount = 128

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
    $attribution = if ($null -ne $timing.PSObject.Properties['attribution']) { $timing.attribution } else { $null }
    $counters = if ($null -ne $timing.PSObject.Properties['counters']) { $timing.counters } else { $null }
    $counterBreakdown = if ($null -ne $timing.PSObject.Properties['counter_breakdown']) { $timing.counter_breakdown } else { $null }
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
        measurement_source = 'mqb.timings'
        timing_schema_version = [int]$timing.schema_version
        attribution = $attribution
        counters = $counters
        counter_breakdown = $counterBreakdown
    }
}

function Invoke-UntimedMqb {
    param(
        [Parameter(Mandatory = $true)][string]$Scenario,
        [Parameter(Mandatory = $true)][int]$Iteration,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    Push-Location $WorkingDirectory
    try {
        $output = @(& $MqbPath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
        $stopwatch.Stop()
    }
    if ($exitCode -ne 0) {
        foreach ($line in $output) { Write-Host $line }
        throw "Benchmark scenario '$Scenario' failed with exit code $exitCode"
    }
    $timingLine = @($output | ForEach-Object { [string]$_ } |
        Where-Object { $_ -like '{"type":"mqb.timings"*' })
    if ($timingLine.Count -ne 0) {
        throw "Benchmark scenario '$Scenario' unexpectedly emitted timing JSON"
    }
    return [PSCustomObject]@{
        iteration = $Iteration
        scenario = $Scenario
        total_ms = [double]$stopwatch.Elapsed.TotalMilliseconds
        discovery_ms = 0.0
        dependency_scan_ms = 0.0
        compile_queue_ms = 0.0
        compile_ms = 0.0
        link_ms = 0.0
        archive_ms = 0.0
        run_startup_ms = 0.0
        compile_hits = -1
        compile_misses = -1
        link_hits = -1
        link_misses = -1
        archive_hits = -1
        archive_misses = -1
        measurement_source = 'external_stopwatch'
        timing_schema_version = 0
        attribution = $null
        counters = $null
        counter_breakdown = $null
    }
}

function Assert-CompileCacheCounts {
    param(
        [Parameter(Mandatory = $true)]$Sample,
        [Parameter(Mandatory = $true)][int]$ExpectedHits,
        [Parameter(Mandatory = $true)][int]$ExpectedMisses
    )

    $hitsMatch = [int]$Sample.compile_hits -eq $ExpectedHits
    $missesMatch = [int]$Sample.compile_misses -eq $ExpectedMisses
    if (-not ($hitsMatch -and $missesMatch)) {
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

        # Preserve the historical scale fixture: unique source/artifact identities
        # and no shared header dependency. This keeps target-scale-* comparable
        # with earlier performance evidence while the dedicated fixture below
        # measures dependency-occurrence amplification.
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

        $targetScaleAuto = Invoke-TimedMqb -Scenario 'target-scale-no-op-auto' -Iteration $iteration -WorkingDirectory $targetScaleRoot -Arguments ($targetScaleArguments + @('-j', 'auto'))
        Assert-CompileCacheCounts -Sample $targetScaleAuto -ExpectedHits $targetScaleUnitCount -ExpectedMisses 0
        $results.Add($targetScaleAuto)

        $targetScaleJ1 = Invoke-TimedMqb -Scenario 'target-scale-no-op-j1' -Iteration $iteration -WorkingDirectory $targetScaleRoot -Arguments ($targetScaleArguments + @('-j', '1'))
        Assert-CompileCacheCounts -Sample $targetScaleJ1 -ExpectedHits $targetScaleUnitCount -ExpectedMisses 0
        $results.Add($targetScaleJ1)

        $lastScaleSource = 'unit_{0:D3}.cpp' -f ($TargetScaleSourceCount - 1)
        Add-Content -LiteralPath (Join-Path $targetScaleRoot $lastScaleSource) -Encoding utf8 -Value "// target-scale mutation $iteration"
        $targetScaleSingle = Invoke-TimedMqb -Scenario 'target-scale-single-tu' -Iteration $iteration -WorkingDirectory $targetScaleRoot -Arguments $targetScaleArguments
        Assert-CompileCacheCounts -Sample $targetScaleSingle -ExpectedHits ($targetScaleUnitCount - 1) -ExpectedMisses 1
        $results.Add($targetScaleSingle)

        # Dedicated 129-TU common-header fixture. It retains the same target size
        # while making repeated dependency evidence visible independently of the
        # historical unique-source scale scenarios above.
        $commonHeaderRoot = Join-Path $benchmarkRoot "target-scale-common-header-$iteration"
        New-Item -ItemType Directory -Path $commonHeaderRoot | Out-Null
        Set-Content -LiteralPath (Join-Path $commonHeaderRoot 'common.hpp') -Encoding utf8 -Value @'
#pragma once
inline int target_scale_common() { return 1; }
'@
        Set-Content -LiteralPath (Join-Path $commonHeaderRoot 'main.cpp') -Encoding utf8 -Value @'
#include "common.hpp"
int main() { return target_scale_common() == 1 ? 0 : 1; }
'@
        $commonHeaderArgs = [System.Collections.Generic.List[string]]::new()
        $commonHeaderArgs.Add('main.cpp')
        for ($sourceIndex = 0; $sourceIndex -lt $TargetScaleSourceCount; ++$sourceIndex) {
            $sourceName = 'unit_{0:D3}.cpp' -f $sourceIndex
            Set-Content -LiteralPath (Join-Path $commonHeaderRoot $sourceName) -Encoding utf8 -Value "#include `"common.hpp`"`nint target_scale_common_${sourceIndex}() { return target_scale_common() + $sourceIndex; }"
            $commonHeaderArgs.Add($sourceName)
        }
        $commonHeaderArgs.Add('--output')
        $commonHeaderArgs.Add('target_scale_common_header_bench')
        $commonHeaderArguments = $commonHeaderArgs.ToArray()
        $null = Invoke-UntimedMqb -Scenario 'target-scale-common-header-prime' -Iteration $iteration -WorkingDirectory $commonHeaderRoot -Arguments ($commonHeaderArguments + @('-j', 'auto'))
        $targetScaleCommonHeader = Invoke-TimedMqb -Scenario 'target-scale-common-header-no-op' -Iteration $iteration -WorkingDirectory $commonHeaderRoot -Arguments ($commonHeaderArguments + @('-j', 'auto'))
        Assert-CompileCacheCounts -Sample $targetScaleCommonHeader -ExpectedHits $targetScaleUnitCount -ExpectedMisses 0
        $results.Add($targetScaleCommonHeader)

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

        $timingRoot = Join-Path $benchmarkRoot "timings-$iteration"
        New-Item -ItemType Directory -Path $timingRoot | Out-Null
        Set-Content -LiteralPath (Join-Path $timingRoot 'helper.cpp') -Encoding utf8 -Value 'int timing_helper() { return 42; }'
        Set-Content -LiteralPath (Join-Path $timingRoot 'main.cpp') -Encoding utf8 -Value 'int timing_helper(); int main() { return timing_helper() == 42 ? 0 : 1; }'
        $timingArgs = @('main.cpp', 'helper.cpp', '--output', 'timing_bench', '-j', '1')
        $null = Invoke-UntimedMqb -Scenario 'timings-prime' -Iteration $iteration -WorkingDirectory $timingRoot -Arguments $timingArgs
        $results.Add((Invoke-TimedMqb -Scenario 'timings-enabled-no-op' -Iteration $iteration -WorkingDirectory $timingRoot -Arguments $timingArgs))
        $results.Add((Invoke-UntimedMqb -Scenario 'timings-disabled-no-op' -Iteration $iteration -WorkingDirectory $timingRoot -Arguments $timingArgs))

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
        schema_version = 3
        generated_utc = [DateTime]::UtcNow.ToString('o')
        mqb = $MqbPath
        iterations = $Iterations
        target_scale_source_count = $TargetScaleSourceCount
        samples = $ordered
        summary = $summary
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Benchmark JSON: $OutputPath"
}

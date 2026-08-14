[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 3,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

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
            $sortedTotals = @($group.total_ms | Sort-Object)
            $middle = [int][Math]::Floor($sortedTotals.Count / 2)
            $median = if (($sortedTotals.Count % 2) -eq 0) {
                ($sortedTotals[$middle - 1] + $sortedTotals[$middle]) / 2.0
            }
            else {
                $sortedTotals[$middle]
            }
            [PSCustomObject]@{
                scenario = $_.Name
                samples = $group.Count
                median_total_ms = [Math]::Round($median, 3)
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
        schema_version = 1
        generated_utc = [DateTime]::UtcNow.ToString('o')
        mqb = $MqbPath
        iterations = $Iterations
        samples = $ordered
        summary = $summary
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Benchmark JSON: $OutputPath"
}

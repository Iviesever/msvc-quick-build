[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) {
    throw "MQB executable not found: $MqbPath"
}

function Invoke-MqbCapture {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$ExpectTimings
    )

    Push-Location $WorkingDirectory
    try {
        $output = @(& $MqbPath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        $output | ForEach-Object { Write-Host $_ }
        throw "MQB exited with code $exitCode"
    }

    $timingLines = @($output | ForEach-Object { [string]$_ } |
        Where-Object { $_ -like '{"type":"mqb.timings"*' })
    if ($ExpectTimings) {
        if ($timingLines.Count -ne 1) {
            throw "Expected exactly one timing record, got $($timingLines.Count)"
        }
        $timing = $timingLines[0] | ConvertFrom-Json
        return [PSCustomObject]@{
            output = @($output | ForEach-Object { [string]$_ })
            timing = $timing
        }
    }
    if ($timingLines.Count -ne 0) {
        throw 'Timing-disabled invocation unexpectedly emitted a timing record'
    }
    return [PSCustomObject]@{
        output = @($output | ForEach-Object { [string]$_ })
        timing = $null
    }
}

function Assert-Property {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Context
    )
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        throw "$Context is missing property '$Name'"
    }
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-performance-contract-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

try {
    Set-Content -LiteralPath (Join-Path $root 'common.hpp') -Encoding utf8 -Value @'
#pragma once
inline int shared_contract_value() { return 42; }
'@
    Set-Content -LiteralPath (Join-Path $root 'main.cpp') -Encoding utf8 -Value @'
#include "common.hpp"
int main() { return shared_contract_value() == 42 ? 0 : 1; }
'@

    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add('main.cpp')
    for ($index = 0; $index -lt 128; ++$index) {
        $name = 'unit_{0:D3}.cpp' -f $index
        Set-Content -LiteralPath (Join-Path $root $name) -Encoding utf8 -Value @"
#include "common.hpp"
int performance_contract_${index}() { return shared_contract_value() + $index; }
"@
        $arguments.Add($name)
    }
    $arguments.Add('--output')
    $arguments.Add('performance_contract')
    $baseArguments = $arguments.ToArray()

    # Prime all caches. Correctness is checked again through the instrumented hit.
    $null = Invoke-MqbCapture -WorkingDirectory $root -Arguments ($baseArguments + @('-j', '4'))

    $parallel = Invoke-MqbCapture `
        -WorkingDirectory $root `
        -Arguments ($baseArguments + @('-j', '4', '--timings=json')) `
        -ExpectTimings
    $timing = $parallel.timing

    if ([int]$timing.schema_version -ne 2) {
        throw "Expected mqb.timings schema 2, got $($timing.schema_version)"
    }
    foreach ($name in @('phases', 'cache', 'attribution', 'counters', 'counter_breakdown')) {
        Assert-Property -Object $timing -Name $name -Context 'timing record'
    }
    foreach ($name in @('wall', 'work')) {
        Assert-Property -Object $timing.attribution -Name $name -Context 'attribution'
    }
    foreach ($name in @(
        'project_setup', 'artifact_layout', 'toolchain_discovery',
        'target_validation', 'reporting')) {
        Assert-Property -Object $timing.attribution.wall -Name $name -Context 'wall attribution'
    }
    foreach ($name in @(
        'compile_inspection', 'compile_execution', 'compile_cache_read',
        'compile_cache_write', 'link_inspection', 'link_execution',
        'link_resolution', 'archive_inspection', 'archive_execution',
        'filesystem_snapshot')) {
        Assert-Property -Object $timing.attribution.work -Name $name -Context 'work attribution'
    }
    foreach ($name in @(
        'cache_files_opened', 'cache_bytes_read',
        'filesystem_snapshot_requests', 'unique_filesystem_paths_probed',
        'snapshot_evidence_reuses', 'background_threads_created',
        'cl_processes_launched', 'link_processes_launched',
        'lib_processes_launched', 'output_lines_emitted',
        'output_bytes_emitted')) {
        Assert-Property -Object $timing.counters -Name $name -Context 'counter record'
    }

    if ([int]$timing.cache.compile.hits -ne 129 -or [int]$timing.cache.compile.misses -ne 0) {
        throw 'Warm 129-TU contract did not produce 129 compile hits / 0 misses'
    }
    if ([int]$timing.cache.link.hits -ne 1 -or [int]$timing.cache.link.misses -ne 0) {
        throw 'Warm 129-TU contract did not produce 1 link hit / 0 misses'
    }
    if ([int64]$timing.counters.cl_processes_launched -ne 0 `
        -or [int64]$timing.counters.link_processes_launched -ne 0 `
        -or [int64]$timing.counters.lib_processes_launched -ne 0) {
        throw 'All-hit contract launched an MSVC compiler/linker/librarian process'
    }
    if ([int64]$timing.counters.background_threads_created -ne 3) {
        throw "Fixed -j 4 all-hit target should create exactly 3 background threads; got $($timing.counters.background_threads_created)"
    }
    if ([int64]$timing.counters.cache_files_opened -lt 130) {
        throw "Expected at least 129 compile-cache reads plus link-cache evidence; got $($timing.counters.cache_files_opened)"
    }
    if ([int64]$timing.counters.cache_bytes_read -le 0) {
        throw 'Warm cache evidence reported zero bytes read'
    }
    if ([int64]$timing.counters.filesystem_snapshot_requests -le 0) {
        throw 'Warm target reported zero filesystem snapshot requests'
    }
    if ([int64]$timing.counters.unique_filesystem_paths_probed `
        -ge [int64]$timing.counters.filesystem_snapshot_requests) {
        throw 'Common-header fixture did not expose repeated filesystem evidence'
    }
    if ([double]$timing.attribution.work.compile_inspection -le 0.0 `
        -or [double]$timing.attribution.work.compile_cache_read -le 0.0 `
        -or [double]$timing.attribution.work.link_inspection -le 0.0) {
        throw 'Warm-path attribution did not record compile/link inspection and cache-read work'
    }
    if ([double]$timing.attribution.work.compile_execution -ne 0.0 `
        -or [double]$timing.attribution.work.link_execution -ne 0.0 `
        -or [double]$timing.attribution.work.archive_execution -ne 0.0) {
        throw 'All-hit contract reported execution work despite launching no build tools'
    }

    $nonTimingLines = @($parallel.output | Where-Object { $_ -notlike '{"type":"mqb.timings"*' })
    if ([int64]$timing.counters.output_lines_emitted -ne $nonTimingLines.Count) {
        throw "Timing record leaked into output counters or line accounting drifted: counter=$($timing.counters.output_lines_emitted), non-timing-lines=$($nonTimingLines.Count)"
    }

    $serial = Invoke-MqbCapture `
        -WorkingDirectory $root `
        -Arguments ($baseArguments + @('-j', '1', '--timings=json')) `
        -ExpectTimings
    if ([int64]$serial.timing.counters.background_threads_created -ne 0) {
        throw 'Fixed -j 1 warm target created a background thread'
    }
    if ([int]$serial.timing.cache.compile.hits -ne 129 `
        -or [int]$serial.timing.cache.link.hits -ne 1) {
        throw 'Changing execution policy from -j 4 to -j 1 polluted cache identity'
    }

    $disabled = Invoke-MqbCapture -WorkingDirectory $root -Arguments ($baseArguments + @('-j', '1'))
    if ($null -ne $disabled.timing) {
        throw 'Timing-disabled contract unexpectedly returned timing data'
    }

    Write-Host 'Performance instrumentation contract passed.'
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BaselineMqbPath,
    [Parameter(Mandatory = $true)][string]$CandidateMqbPath,
    [Parameter(Mandatory = $true)][string]$BuildIdentityPath,
    [Parameter(Mandatory = $true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0

function Get-AttributionSchedule {
    param([ValidateRange(1, 16)][int]$Blocks = 8)
    $sequence = 0
    for ($block = 1; $block -le $Blocks; ++$block) {
        $contrasts = if ($block % 2 -eq 1) { @('AA', 'AB') } else { @('AB', 'AA') }
        $sides = if ($block % 2 -eq 1) { @('left', 'right') } else { @('right', 'left') }
        foreach ($contrast in $contrasts) {
            foreach ($side in $sides) {
                ++$sequence
                [pscustomobject]@{
                    sequence = $sequence; block = $block; contrast = $contrast; side = $side
                    orientation = $(if ($block % 2 -eq 1) { 'AB' } else { 'BA' })
                    executable_role = $(if ($contrast -eq 'AB' -and $side -eq 'right') { 'B' } else { 'A' })
                    id = ('{0:D2}-{1}-{2}' -f $block, $contrast, $side)
                }
            }
        }
    }
}
function Get-AttributionMedian {
    param([double[]]$Values)
    if ($Values.Count -eq 0) { throw 'Empty statistics are unavailable, not zero.' }
    $sorted = @($Values | Sort-Object)
    $mid = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return [double]$sorted[$mid] }
    return ([double]$sorted[$mid - 1] + [double]$sorted[$mid]) / 2.0
}
function Get-AttributionStatistics {
    param([object[]]$Pairs)
    if ($Pairs.Count -eq 0) { throw 'Missing paired samples.' }
    $deltas = [double[]]@($Pairs | ForEach-Object { $_.right_external_ms - $_.left_external_ms })
    $percent = [double[]]@($Pairs | ForEach-Object {
        if ($_.left_external_ms -le 0) { throw 'Nonpositive baseline is not a valid denominator.' }
        100.0 * ($_.right_external_ms - $_.left_external_ms) / $_.left_external_ms
    })
    $median = Get-AttributionMedian $deltas
    $sorted = @($deltas | Sort-Object)
    return [pscustomobject]@{
        pairs = $Pairs.Count; median_delta_ms = $median
        median_delta_pct = (Get-AttributionMedian $percent)
        mad_delta_ms = (Get-AttributionMedian ([double[]]@($deltas | ForEach-Object { [Math]::Abs($_ - $median) })))
        p95_delta_ms = $sorted[[int][Math]::Ceiling(0.95 * $sorted.Count) - 1]
        slower_pairs = @($deltas | Where-Object { $_ -gt 0 }).Count
        gate_crossed = ($median -gt 1.0 -and (Get-AttributionMedian $percent) -gt 10.0)
    }
}
function Assert-AttributionInvocation {
    param($Record, [string]$Scenario, [string]$Executable)
    if ($Record -isnot [pscustomobject]) { throw 'Invocation record is not an object.' }
    foreach ($field in @('schema', 'iteration', 'exit_code', 'host_pid')) {
        if ($Record.$field -isnot [long] -and $Record.$field -isnot [int]) { throw "Invalid integer: $field" }
    }
    if ($Record.clock -cne 'Stopwatch.GetTimestamp' -or $Record.host_pid -le 0) { throw 'Invalid clock/host identity.' }
    if ($Record.schema -ne 1 -or $Record.scenario -cne $Scenario -or $Record.iteration -ne 1 -or
        $Record.executable -cne $Executable -or $Record.exit_code -ne 0 -or $null -ne $Record.observer_error) {
        throw "Invalid or failed invocation: $Scenario"
    }
    foreach ($field in @('frequency', 'start_ticks', 'before_native_ticks', 'after_native_ticks', 'end_ticks')) {
        if (($Record.$field -isnot [long] -and $Record.$field -isnot [int]) -or $Record.$field -lt 0) { throw "Invalid clock field: $field" }
    }
    if ($Record.frequency -le 0 -or $Record.before_native_ticks -lt $Record.start_ticks -or
        $Record.after_native_ticks -lt $Record.before_native_ticks -or $Record.end_ticks -lt $Record.after_native_ticks) {
        throw 'Nonmonotonic invocation clocks.'
    }
    $expected = @{
        external_ms = ($Record.end_ticks - $Record.start_ticks) * 1000.0 / $Record.frequency
        push_ms = ($Record.before_native_ticks - $Record.start_ticks) * 1000.0 / $Record.frequency
        native_and_capture_ms = ($Record.after_native_ticks - $Record.before_native_ticks) * 1000.0 / $Record.frequency
        pop_ms = ($Record.end_ticks - $Record.after_native_ticks) * 1000.0 / $Record.frequency
    }
    foreach ($field in $expected.Keys) {
        if (($Record.$field -isnot [double] -and $Record.$field -isnot [long] -and $Record.$field -isnot [int]) -or
            -not [double]::IsFinite([double]$Record.$field) -or [Math]::Abs($Record.$field - $expected[$field]) -gt 0.000001) {
            throw "Invocation clock arithmetic mismatch: $field"
        }
    }
    if ($Record.output_lines -isnot [array] -or $Record.argv -isnot [array]) { throw 'Missing output or argv array.' }
    foreach ($entry in ($Record.output_lines + $Record.argv)) {
        if ($entry -isnot [string]) { throw 'Output and argv must contain only text strings.' }
    }
    if ($null -ne $Record.root_pid) { throw 'PowerShell host identity is not an observed child PID.' }
}
function Assert-AttributionSemantics {
    param($Left, $Right, [bool]$Instrumented)
    if ($Left.scenario -cne $Right.scenario -or $Left.measurement_source -cne $Right.measurement_source) { throw 'Pair identity/clock mismatch.' }
    foreach ($field in @('counters', 'counter_breakdown', 'compile_hits', 'compile_misses',
        'link_hits', 'link_misses', 'archive_hits', 'archive_misses')) {
        $leftJson = ConvertTo-Json -InputObject $Left.$field -Depth 16 -Compress
        $rightJson = ConvertTo-Json -InputObject $Right.$field -Depth 16 -Compress
        if ($leftJson -cne $rightJson) { throw "Semantic mismatch in $($Left.scenario): $field" }
    }
    if ($Instrumented) {
        if ($Left.measurement_source -ne 'mqb.timings' -or $null -eq $Left.counters -or $null -eq $Left.counter_breakdown) { throw 'Missing instrumented semantics.' }
    } elseif ($Left.measurement_source -ne 'external_stopwatch_observed' -or $null -ne $Left.counters -or
        $null -ne $Left.counter_breakdown -or $Left.compile_hits -ne -1 -or $Left.link_hits -ne -1) {
        throw 'Uninstrumented semantics are unavailable, not zero.'
    }
}

$BaselineMqbPath = [IO.Path]::GetFullPath($BaselineMqbPath)
$CandidateMqbPath = [IO.Path]::GetFullPath($CandidateMqbPath)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite a study or partial evidence.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$buildIdentity = Get-Content -LiteralPath $BuildIdentityPath -Raw | ConvertFrom-Json
$hashA = (Get-FileHash -LiteralPath $BaselineMqbPath -Algorithm SHA256).Hash
$hashB = (Get-FileHash -LiteralPath $CandidateMqbPath -Algorithm SHA256).Hash
if ($buildIdentity.baseline_sha256 -cne $hashA -or $buildIdentity.candidate_sha256 -cne $hashB) { throw 'Measured binary differs from build provenance.' }
Copy-Item -LiteralPath $BuildIdentityPath -Destination (Join-Path $OutputRoot 'build-identity.json')
$benchmark = Join-Path $PSScriptRoot 'benchmark_mqb.ps1'
$binaryArchive = Join-Path $OutputRoot 'binaries'
New-Item -ItemType Directory -Path $binaryArchive | Out-Null
Copy-Item -LiteralPath $BaselineMqbPath -Destination (Join-Path $binaryArchive 'baseline-mqb.exe')
Copy-Item -LiteralPath $CandidateMqbPath -Destination (Join-Path $binaryArchive 'candidate-mqb.exe')
Copy-Item -LiteralPath $benchmark -Destination (Join-Path $OutputRoot 'measured-harness.ps1')
# Do not dump all environment variables: CI credentials are not benchmark evidence.
$environment = [ordered]@{}
foreach ($name in @('PATH', 'INCLUDE', 'LIB', 'LIBPATH', 'CL', '_CL_', 'LINK', '_LINK_', 'TEMP', 'TMP',
    'VCToolsInstallDir', 'VCToolsVersion', 'WindowsSdkDir', 'WindowsSDKVersion', 'VisualStudioVersion',
    'VSCMD_ARG_HOST_ARCH', 'VSCMD_ARG_TGT_ARCH', 'MQB_TIMINGS')) {
    $environment[$name] = [Environment]::GetEnvironmentVariable($name)
}
[ordered]@{
    schema = 1; utc_started = [DateTime]::UtcNow.ToString('o'); os = [Runtime.InteropServices.RuntimeInformation]::OSDescription
    framework = [Runtime.InteropServices.RuntimeInformation]::FrameworkDescription
    powershell = $PSVersionTable.PSVersion.ToString(); processors = [Environment]::ProcessorCount
    stopwatch_frequency = [Diagnostics.Stopwatch]::Frequency; stopwatch_high_resolution = [Diagnostics.Stopwatch]::IsHighResolution
    host_pid = $PID; runner_image = $env:ImageVersion; github_run_id = $env:GITHUB_RUN_ID; run_attempt = $env:GITHUB_RUN_ATTEMPT
    environment_allowlist = $environment; baseline_path = $BaselineMqbPath; candidate_path = $CandidateMqbPath
    baseline_sha256 = $hashA; candidate_sha256 = $hashB; binary_bytes_identical = ($hashA -ceq $hashB)
    harness_sha256 = (Get-FileHash -LiteralPath $benchmark -Algorithm SHA256).Hash
    limitation = 'New observed launches, not historical OS traces. AA uses the same A path/bytes in both roles. AB path effects remain possible.'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'identity.json') -Encoding utf8
# Fixed budget, declared and persisted before launching any measured report.
$plan = @(Get-AttributionSchedule -Blocks 8)
$required = @('cold', 'no-op', 'single-tu', 'public-header', 'build-run', 'link-only', 'target-scale-cold',
    'target-scale-no-op', 'target-scale-no-op-auto', 'target-scale-no-op-j1', 'target-scale-common-header-no-op',
    'target-scale-single-tu', 'discovery-cold', 'discovery-no-op', 'discovery-header', 'modules-cold', 'modules-no-op',
    'timings-enabled-no-op', 'timings-disabled-no-op')
[ordered]@{ schema = 1; blocks = 8; contrasts = @('AA', 'AB'); reports = 32; expected_invocations = 672
    measured_scenarios = $required; invocations_per_report = 21; schedule = $plan
    no_op_gate_ms = 1.0; no_op_gate_pct = 10.0; subtraction_of_AA = $false; automatic_merge = $false
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'plan.json') -Encoding utf8
$completed = [System.Collections.Generic.List[object]]::new()
$reports = @{}; $records = @{}; $currentSlot = $null
try {
    foreach ($slot in $plan) {
        $currentSlot = $slot
        $slotRoot = Join-Path $OutputRoot $slot.id
        New-Item -ItemType Directory -Path $slotRoot | Out-Null
        $exe = if ($slot.executable_role -eq 'A') { $BaselineMqbPath } else { $CandidateMqbPath }
        $slot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $slotRoot 'scheduled.json') -Encoding utf8
        Write-Host "=== Attribution $($slot.sequence)/32: $($slot.id), executable $($slot.executable_role) ==="
        & $benchmark -MqbPath $exe -Iterations 1 -OutputPath (Join-Path $slotRoot 'report.json') `
            -InvocationEvidenceDirectory (Join-Path $slotRoot 'invocations') *> (Join-Path $slotRoot 'harness.log')
        $report = Get-Content -LiteralPath (Join-Path $slotRoot 'report.json') -Raw | ConvertFrom-Json
        if ($report.samples.Count -ne 19 -or $report.iterations -ne 1) { throw 'Incomplete fixture report.' }
        $invocations = @(Get-ChildItem -LiteralPath (Join-Path $slotRoot 'invocations') -File -Filter '*.json' |
            Where-Object { $_.Name -notlike '*.started.json' })
        if ($invocations.Count -ne 21) { throw 'Expected 19 measurements and two retained priming invocations.' }
        foreach ($scenario in ($required + @('timings-prime', 'target-scale-common-header-prime'))) {
            $record = Get-Content -LiteralPath (Join-Path $slotRoot "invocations/1-$scenario.json") -Raw | ConvertFrom-Json
            Assert-AttributionInvocation $record $scenario $exe
            $records["$($slot.id)/$scenario"] = $record
        }
        foreach ($scenario in $required) {
            $samples = @($report.samples | Where-Object { $_.scenario -ceq $scenario })
            if ($samples.Count -ne 1) { throw 'Missing or duplicate measured scenario.' }
            $record = $records["$($slot.id)/$scenario"]
            if ($scenario -eq 'timings-disabled-no-op') {
                if ($samples[0].total_ms -ne $record.external_ms -or '--timings=json' -in $record.argv) { throw 'External sample provenance mismatch.' }
            } else {
                $timings = @($record.output_lines | Where-Object { $_ -like '{"type":"mqb.timings"*' })
                if ($timings.Count -ne 1 -or '--timings=json' -notin $record.argv) { throw 'Original timing record missing or ambiguous.' }
                $timing = $timings[0] | ConvertFrom-Json
                if ($timing.phases.total -ne $samples[0].total_ms) { throw 'Original internal timing disagrees with sample.' }
            }
        }
        $reports[$slot.id] = $report
        $completed.Add($slot)
        [ordered]@{ schema = 1; expected_reports = 32; completed_reports = $completed.Count
            completed = @($completed.ToArray()); failed = $false
        } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'progress.json') -Encoding utf8
    }
    $currentSlot = $null
    $pairs = [System.Collections.Generic.List[object]]::new()
    foreach ($contrast in @('AA', 'AB')) {
        for ($block = 1; $block -le 8; ++$block) {
            $leftId = '{0:D2}-{1}-left' -f $block, $contrast
            $rightId = '{0:D2}-{1}-right' -f $block, $contrast
            foreach ($scenario in $required) {
                $left = @($reports[$leftId].samples | Where-Object { $_.scenario -ceq $scenario })[0]
                $right = @($reports[$rightId].samples | Where-Object { $_.scenario -ceq $scenario })[0]
                $instrumented = $scenario -ne 'timings-disabled-no-op'
                Assert-AttributionSemantics $left $right $instrumented
                $l = $records["$leftId/$scenario"]; $r = $records["$rightId/$scenario"]
                $pairs.Add([pscustomobject]@{
                    contrast = $contrast; block = $block; scenario = $scenario; left_id = $leftId; right_id = $rightId
                    orientation = $(if ($block % 2 -eq 1) { 'AB' } else { 'BA' })
                    instrumented = $instrumented; semantics_equal = $(if ($instrumented) { $true } else { $null })
                    left_external_ms = $l.external_ms; right_external_ms = $r.external_ms
                    left_native_capture_ms = $l.native_and_capture_ms; right_native_capture_ms = $r.native_and_capture_ms
                    left_push_pop_ms = $l.push_ms + $l.pop_ms; right_push_pop_ms = $r.push_ms + $r.pop_ms
                    left_internal_ms = $(if ($instrumented) { $left.total_ms } else { $null })
                    right_internal_ms = $(if ($instrumented) { $right.total_ms } else { $null })
                    # Residual includes startup/teardown/host/capture, NOT just scheduler/OS queueing.
                    left_external_minus_internal_ms = $(if ($instrumented) { $l.external_ms - $left.total_ms } else { $null })
                    right_external_minus_internal_ms = $(if ($instrumented) { $r.external_ms - $right.total_ms } else { $null })
                })
            }
        }
    }
    if ((Get-FileHash -LiteralPath $BaselineMqbPath -Algorithm SHA256).Hash -cne $hashA -or
        (Get-FileHash -LiteralPath $CandidateMqbPath -Algorithm SHA256).Hash -cne $hashB) { throw 'Measured executable changed during study.' }
    $summary = @(foreach ($contrast in @('AA', 'AB')) {
        foreach ($scenario in $required) {
            $selected = @($pairs | Where-Object { $_.contrast -ceq $contrast -and $_.scenario -ceq $scenario })
            [pscustomobject]@{ contrast = $contrast; scenario = $scenario; external = (Get-AttributionStatistics $selected) }
        }
    })
    [ordered]@{ schema = 1; complete = $true; observed_external_clock = $true; historical_cause_resolved = $false
        authorizes_merge = $false; pairs = @($pairs.ToArray()); summary = $summary
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputRoot 'comparison.json') -Encoding utf8
    $summary | Where-Object { $_.scenario -eq 'timings-disabled-no-op' } | ConvertTo-Json -Depth 6 | Write-Host
} catch {
    [ordered]@{ schema = 1; expected_reports = 32; completed_reports = $completed.Count
        failed = $true; error = $_.ToString(); completed = @($completed.ToArray())
        failed_slot = $currentSlot
        not_attempted = @($plan | Where-Object { $_.id -notin @($completed | ForEach-Object { $_.id }) -and
            ($null -eq $currentSlot -or $_.id -ne $currentSlot.id) })
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'progress.json') -Encoding utf8
    throw
}

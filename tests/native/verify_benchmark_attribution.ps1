[CmdletBinding()]
param([string]$OutputRoot = (Join-Path $PSScriptRoot '../../.mqb/benchmark-attribution-contracts'))
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Contract evidence already exists.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
# Load actual functions only. Do not run either script's benchmark/build body.
foreach ($source in @(
    @{ file = 'benchmark_mqb.ps1'; names = @('Invoke-ObservedMqb', 'Invoke-TimedMqb', 'Invoke-UntimedMqb') },
    @{ file = 'collect_benchmark_attribution.ps1'; names = @('Get-AttributionSchedule', 'Get-AttributionMedian',
        'Get-AttributionStatistics', 'Assert-AttributionInvocation', 'Assert-AttributionSemantics') }
)) {
    $tokens = $null; $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $source.file), [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count) { throw "Parse failure: $parseErrors" }
    foreach ($name in $source.names) {
        $definitions = @($ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true))
        if ($definitions.Count -ne 1) { throw "Missing or duplicated actual helper: $name" }
        . ([scriptblock]::Create($definitions[0].Extent.Text))
    }
}
$checks = [Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Passed) {
    $checks.Add([pscustomobject]@{ name = $Name; passed = $Passed })
    [ordered]@{ schema = 1; synthetic_only = $true; powershell = $PSVersionTable.PSVersion.ToString()
        cases = @($checks.ToArray())
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'contracts.json') -Encoding utf8
    if (-not $Passed) { throw "Contract failed: $Name" }
}
$schedule = @(Get-AttributionSchedule 8)
Check 'fixed-32-unique-slots' ($schedule.Count -eq 32 -and @($schedule.id | Sort-Object -Unique).Count -eq 32)
Check 'AA-is-same-executable-both-sides' (@($schedule | Where-Object { $_.contrast -eq 'AA' -and $_.executable_role -eq 'B' }).Count -eq 0)
Check 'balanced-AB-roles' (@($schedule | Where-Object { $_.contrast -eq 'AB' -and $_.executable_role -eq 'B' }).Count -eq 8)
Check 'interleaved-contrast-order' (($schedule[0].contrast -eq 'AA') -and ($schedule[4].contrast -eq 'AB'))
foreach ($contrast in @('AA', 'AB')) {
    Check "balanced-orientations-$contrast" (@($schedule | Where-Object { $_.contrast -eq $contrast -and $_.orientation -eq 'AB' }).Count -eq 8)
}
Check 'even-median-not-marginal-difference' ((Get-AttributionMedian @(1.0, 4.0, 2.0, 3.0)) -eq 2.5)
foreach ($case in @(
    @{ name = 'both-gates-crossed'; left = 10.0; right = 11.1; expected = $true },
    @{ name = 'equal-boundary-not-crossed'; left = 10.0; right = 11.0; expected = $false },
    @{ name = 'only-absolute-not-crossed'; left = 100.0; right = 102.0; expected = $false },
    @{ name = 'only-percent-not-crossed'; left = 1.0; right = 1.5; expected = $false }
)) {
    $stat = Get-AttributionStatistics @([pscustomobject]@{ left_external_ms = $case.left; right_external_ms = $case.right })
    Check $case.name ($stat.gate_crossed -eq $case.expected)
}
$InvocationEvidenceDirectory = Join-Path $OutputRoot 'invocations'
New-Item -ItemType Directory -Path $InvocationEvidenceDirectory | Out-Null
$working = Join-Path $OutputRoot '[literal] directory with spaces'
New-Item -ItemType Directory -Path $working | Out-Null
$MqbPath = Join-Path $PSHOME 'pwsh.exe'
$before = (Get-Location).Path
$result = Invoke-ObservedMqb 'empty' 1 $working @('-NoProfile', '-NonInteractive', '-Command', 'exit 0')
$record = Get-Content -LiteralPath (Join-Path $InvocationEvidenceDirectory '1-empty.json') -Raw | ConvertFrom-Json
Assert-AttributionInvocation $record 'empty' $MqbPath
Check 'empty-output-is-array-and-real-interval' ($record.output_lines.Count -eq 0 -and $result.elapsed_ms -gt 0 -and (Get-Location).Path -ceq $before)
$failure = Invoke-ObservedMqb 'nonzero' 1 $working @('-NoProfile', '-NonInteractive', '-Command', "[Console]::Out.WriteLine('original stdout'); [Console]::Error.WriteLine('original stderr'); exit 7")
$bad = Get-Content -LiteralPath (Join-Path $InvocationEvidenceDirectory '1-nonzero.json') -Raw | ConvertFrom-Json
Check 'nonzero-original-stream-text-retained' ($failure.exit_code -eq 7 -and 'original stdout' -in $bad.output_lines -and 'original stderr' -in $bad.output_lines)
$rejected = $false
try { Assert-AttributionInvocation $bad 'nonzero' $MqbPath } catch { $rejected = $true }
Check 'failed-invocation-not-a-passing-sample' $rejected
$rejected = $false
try { Invoke-ObservedMqb 'empty' 1 $working @('-Command', 'exit 0') | Out-Null } catch { $rejected = $true }
Check 'no-overwrite-or-hidden-same-slot-retry' $rejected
$rejected = $false
try { Invoke-ObservedMqb 'missing-cwd' 1 (Join-Path $working 'missing') @('-Command', 'exit 0') | Out-Null } catch { $rejected = $true }
$failedRecord = Get-Content -LiteralPath (Join-Path $InvocationEvidenceDirectory '1-missing-cwd.json') -Raw | ConvertFrom-Json
Check 'prelaunch-failure-retains-record-and-cwd' ($rejected -and $null -ne $failedRecord.observer_error -and $null -eq $failedRecord.exit_code -and (Get-Location).Path -ceq $before)
$null = Invoke-ObservedMqb 'both-stream-volume' 1 $working @('-NoProfile', '-NonInteractive', '-Command', 'for($i=0;$i -lt 1200;$i++){[Console]::Out.WriteLine("out-$i");[Console]::Error.WriteLine("err-$i")};exit 0')
$volume = Get-Content -LiteralPath (Join-Path $InvocationEvidenceDirectory '1-both-stream-volume.json') -Raw | ConvertFrom-Json
Check 'all-2400-merged-lines-retained' (@($volume.output_lines | Where-Object { $_ -like 'out-*' }).Count -eq 1200 -and @($volume.output_lines | Where-Object { $_ -like 'err-*' }).Count -eq 1200)
foreach ($mutation in @('frequency', 'clock-order', 'elapsed', 'string-status', 'wrong-exe', 'missing-output', 'invented-child')) {
    $copy = $record | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    switch ($mutation) {
        frequency { $copy.frequency = 0 }
        clock-order { $copy.after_native_ticks = $copy.start_ticks - 1 }
        elapsed { $copy.external_ms += 1.0 }
        string-status { $copy.exit_code = '0' }
        wrong-exe { $copy.executable = 'other.exe' }
        missing-output { $copy.PSObject.Properties.Remove('output_lines') }
        invented-child { $copy.root_pid = 42 }
    }
    $rejected = $false
    try { Assert-AttributionInvocation $copy 'empty' $MqbPath } catch { $rejected = $true }
    Check "reject-$mutation" $rejected
}
# Confirm persistence precedes timing-JSON validation with a native CMD fixture.
$MqbPath = Join-Path $working 'malformed.cmd'
[IO.File]::WriteAllText($MqbPath, "@echo off`r`necho {`"type`":`"mqb.timings`",broken`r`nexit /b 0`r`n")
$rejected = $false
try { Invoke-TimedMqb 'malformed-timing' 1 $working @('ignored') | Out-Null } catch { $rejected = $true }
$malformed = Get-Content -LiteralPath (Join-Path $InvocationEvidenceDirectory '1-malformed-timing.json') -Raw | ConvertFrom-Json
Check 'malformed-timing-retained-before-parse-failure' ($rejected -and $malformed.exit_code -eq 0 -and $malformed.output_lines.Count -eq 1)
$MqbPath = Join-Path $PSHOME 'pwsh.exe'
$InvocationEvidenceDirectory = ''
$legacy = Invoke-UntimedMqb 'legacy' 1 $working @('-NoProfile', '-NonInteractive', '-Command', 'exit 0')
Check 'inactive-observer-retains-legacy-clock-and-unavailable-counters' ($legacy.measurement_source -ceq 'external_stopwatch' -and $legacy.compile_hits -eq -1 -and $null -eq $legacy.counters)
$semantic = [pscustomobject]@{ scenario = 'no-op'; measurement_source = 'mqb.timings'
    counters = [pscustomobject]@{ cl_processes_launched = 0 }; counter_breakdown = [pscustomobject]@{ cache = 1 }
    compile_hits = 2; compile_misses = 0; link_hits = 1; link_misses = 0; archive_hits = 0; archive_misses = 0 }
Assert-AttributionSemantics $semantic $semantic $true
Check 'identical-instrumented-semantics-accepted' $true
$copy = $semantic | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$copy.counters.cl_processes_launched = 1
$rejected = $false
try { Assert-AttributionSemantics $semantic $copy $true } catch { $rejected = $true }
Check 'counter-drift-rejected' $rejected
$legacy.scenario = 'no-op'
$rejected = $false
try { Assert-AttributionSemantics $legacy $legacy $false } catch { $rejected = $true }
Check 'cannot-mix-legacy-and-observed-clock-provenance' $rejected
$legacy.measurement_source = 'external_stopwatch_observed'
Assert-AttributionSemantics $legacy $legacy $false
Check 'unavailable-counter-semantics-accepted' $true
$legacy.counters = 0
$rejected = $false
try { Assert-AttributionSemantics $legacy $legacy $false } catch { $rejected = $true }
Check 'unavailable-counters-cannot-be-filled-with-zero' $rejected
Write-Host "BENCHMARK_ATTRIBUTION_CONTRACT $($checks.Count) checks passed (synthetic, no MSVC timing claims)"
exit 0

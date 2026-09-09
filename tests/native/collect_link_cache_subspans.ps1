[CmdletBinding(DefaultParameterSetName = 'Study')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Study')][string]$MqbPath,
    [Parameter(Mandatory, ParameterSetName = 'Contract')][switch]$SelfTest,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..')
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite complete or partial evidence.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$names = @('link_cache_serialize', 'link_cache_prepare', 'link_cache_stream',
    'link_cache_write_payload', 'link_cache_flush', 'link_cache_install')
function Assert-LinkCacheSubspans {
    param($Work, [bool]$NoSave)
    $fields = @('link_cache_write', 'link_cache_serialize', 'link_cache_prepare', 'link_cache_stream',
        'link_cache_write_payload', 'link_cache_flush', 'link_cache_install')
    foreach ($field in $fields) {
        if (($Work.$field -isnot [double] -and $Work.$field -isnot [int] -and $Work.$field -isnot [long]) -or
            -not [double]::IsFinite([double]$Work.$field) -or $Work.$field -lt 0) { throw "Invalid work interval: $field" }
        if ($NoSave -and $Work.$field -ne 0) { throw 'A no-save invocation cannot invent write work.' }
    }
    # Each public JSON duration is independently rounded to 0.001 ms. These are
    # serialization tolerances, NOT performance acceptance/noise allowances.
    $disjoint = $Work.link_cache_serialize + $Work.link_cache_prepare + $Work.link_cache_stream + $Work.link_cache_install
    if ($disjoint -gt $Work.link_cache_write + 0.005) { throw 'Save children exceed inclusive save.' }
    if ($Work.link_cache_write_payload + $Work.link_cache_flush -gt $Work.link_cache_stream + 0.003) {
        throw 'Nested payload/flush exceed their stream parent.'
    }
}
if ($SelfTest) {
    $cases = @(
        @{ name = 'valid-nested'; accept = $true; change = {} },
        @{ name = 'valid-zero'; accept = $true; no_save = $true; change = { param($w) foreach($p in @($w.PSObject.Properties)){ $w.($p.Name) = 0 } } },
        @{ name = 'missing-phase'; accept = $false; change = { param($w) $w.PSObject.Properties.Remove('link_cache_flush') } },
        @{ name = 'negative'; accept = $false; change = { param($w) $w.link_cache_prepare = -1 } },
        @{ name = 'string'; accept = $false; change = { param($w) $w.link_cache_flush = '4' } },
        @{ name = 'nonfinite'; accept = $false; change = { param($w) $w.link_cache_flush = [double]::PositiveInfinity } },
        @{ name = 'parent-too-small'; accept = $false; change = { param($w) $w.link_cache_write = 10 } },
        @{ name = 'nested-too-large'; accept = $false; change = { param($w) $w.link_cache_flush = 11 } },
        @{ name = 'no-save-with-work'; accept = $false; no_save = $true; change = {} }
    )
    $results = @(foreach ($case in $cases) {
        $work = [pscustomobject]@{ link_cache_write = 20; link_cache_serialize = 1; link_cache_prepare = 2
            link_cache_stream = 10; link_cache_write_payload = 3; link_cache_flush = 4; link_cache_install = 5 }
        & $case.change $work
        $errorText = $null
        try { Assert-LinkCacheSubspans $work ($case.ContainsKey('no_save') -and $case.no_save) }
        catch { $errorText = $_.Exception.Message }
        [pscustomobject]@{ name = $case.name; expected_accepted = $case.accept; accepted = ($null -eq $errorText)
            passed = (($null -eq $errorText) -eq $case.accept); error = $errorText }
    })
    @{ synthetic_only = $true; cases = $results } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $OutputRoot 'contracts.json') -Encoding utf8
    if (@($results | Where-Object { -not $_.passed }).Count) { throw 'Subspan contract test failed.' }
    Write-Host "LINK_CACHE_SUBSPAN_CONTRACT $($results.Count) checks passed (synthetic)"
    return
}
# Reuse #171's actual clock and semantic validators, without executing its study.
$collectorPath = Join-Path $PSScriptRoot 'collect_benchmark_attribution.ps1'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($collectorPath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count) { throw 'Attribution collector parse failure.' }
foreach ($name in @('Assert-AttributionInvocation', 'Assert-AttributionSemantics', 'Get-AttributionMedian', 'Get-AttributionStatistics')) {
    $defs = @($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name}, $true))
    if ($defs.Count -ne 1) { throw "Missing or duplicated existing validator: $name" }
    . ([scriptblock]::Create($defs[0].Extent.Text))
}
$MqbPath = [IO.Path]::GetFullPath($MqbPath)
$hash = (Get-FileHash -LiteralPath $MqbPath -Algorithm SHA256).Hash
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Source head unavailable.' }
if ((Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim() -cne '5.5.0') { throw 'VERSION changed.' }
$benchmark = Join-Path $PSScriptRoot 'benchmark_mqb.ps1'
Copy-Item -LiteralPath $MqbPath -Destination (Join-Path $OutputRoot 'measured-mqb.exe')
Copy-Item -LiteralPath $benchmark -Destination (Join-Path $OutputRoot 'measured-harness.ps1')
@{ schema = 1; head = $head; binary_sha256 = $hash; binary_path = $MqbPath
    harness_sha256 = (Get-FileHash -LiteralPath $benchmark).Hash; version = '5.5.0'
    runner_image = $env:ImageVersion; powershell = $PSVersionTable.PSVersion.ToString()
    os = [Runtime.InteropServices.RuntimeInformation]::OSDescription; processors = [Environment]::ProcessorCount
    github_run_id = $env:GITHUB_RUN_ID; run_attempt = $env:GITHUB_RUN_ATTEMPT
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'identity.json') -Encoding utf8
$plan = @(foreach ($pair in 1..4) {
    $sides = if ($pair % 2 -eq 1) { @('left', 'right') } else { @('right', 'left') }
    foreach ($side in $sides) { [pscustomobject]@{ pair = $pair; side = $side; id = ('{0:D2}-{1}' -f $pair,$side) } }
})
@{ schema = 1; reports = 8; invocations = 168; runtime = 'same path and bytes on both AA sides'
    plan = $plan; no_adaptive_retries = $true; historical_cause_resolved = $false; authorizes_merge = $false
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'plan.json') -Encoding utf8
$required = @('cold','no-op','single-tu','public-header','build-run','link-only','target-scale-cold',
    'target-scale-no-op','target-scale-no-op-auto','target-scale-no-op-j1','target-scale-common-header-no-op',
    'target-scale-single-tu','discovery-cold','discovery-no-op','discovery-header','modules-cold','modules-no-op',
    'timings-enabled-no-op','timings-disabled-no-op')
$reports = @{}; $records = @{}; $completed = [Collections.Generic.List[string]]::new()
$subspans = [Collections.Generic.List[object]]::new(); $current = $null
try {
    foreach ($slot in $plan) {
        $current = $slot.id; $slotRoot = Join-Path $OutputRoot $slot.id
        New-Item -ItemType Directory -Path $slotRoot | Out-Null
        & $benchmark -MqbPath $MqbPath -Iterations 1 -OutputPath (Join-Path $slotRoot 'report.json') `
            -InvocationEvidenceDirectory (Join-Path $slotRoot 'invocations') *> (Join-Path $slotRoot 'harness.log')
        $report = Get-Content -LiteralPath (Join-Path $slotRoot 'report.json') -Raw | ConvertFrom-Json
        if ($report.samples.Count -ne 19 -or $report.iterations -ne 1) { throw 'Incomplete fixture report.' }
        $invocations = @(Get-ChildItem -LiteralPath (Join-Path $slotRoot 'invocations') -File -Filter '*.json' |
            Where-Object { $_.Name -notlike '*.started.json' })
        if ($invocations.Count -ne 21) { throw 'Expected 19 measurements and two primes.' }
        foreach ($scenario in ($required + @('timings-prime','target-scale-common-header-prime'))) {
            $record = Get-Content -LiteralPath (Join-Path $slotRoot "invocations/1-$scenario.json") -Raw | ConvertFrom-Json
            Assert-AttributionInvocation $record $scenario $MqbPath
            $records["$current/$scenario"] = $record
        }
        foreach ($scenario in $required) {
            $sample = @($report.samples | Where-Object { $_.scenario -ceq $scenario })
            if ($sample.Count -ne 1) { throw 'Missing or duplicate scenario.' }
            $record = $records["$current/$scenario"]
            if ($scenario -eq 'timings-disabled-no-op') {
                if ($sample[0].total_ms -ne $record.external_ms -or '--timings=json' -in $record.argv) { throw 'External clock mismatch.' }
                continue
            }
            $lines = @($record.output_lines | Where-Object { $_ -like '{"type":"mqb.timings"*' })
            if ($lines.Count -ne 1 -or '--timings=json' -notin $record.argv) { throw 'Original timing record missing.' }
            $timing = $lines[0] | ConvertFrom-Json
            if ($timing.phases.total -ne $sample[0].total_ms) { throw 'Internal clock mismatch.' }
            $work = $timing.attribution.work
            Assert-LinkCacheSubspans $work ($sample[0].link_misses -eq 0)
            foreach ($field in $names) {
                if ($sample[0].attribution.work.$field -ne $work.$field) { throw 'Original subspan/report mismatch.' }
            }
            $subspans.Add([pscustomobject]@{ slot = $current; scenario = $scenario; work = $work
                link_cache_writes = $timing.counter_breakdown.cache.link.files_written
                external_ms = $record.external_ms; internal_ms = $timing.phases.total })
        }
        $reports[$current] = $report; $completed.Add($current)
        @{ complete = $false; completed = @($completed.ToArray()); current = $null } | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (Join-Path $OutputRoot 'progress.json') -Encoding utf8
    }
    $current = $null; $pairs = [Collections.Generic.List[object]]::new()
    foreach ($pair in 1..4) {
        $leftId = '{0:D2}-left' -f $pair; $rightId = '{0:D2}-right' -f $pair
        foreach ($scenario in $required) {
            $left = @($reports[$leftId].samples | Where-Object { $_.scenario -ceq $scenario })[0]
            $right = @($reports[$rightId].samples | Where-Object { $_.scenario -ceq $scenario })[0]
            Assert-AttributionSemantics $left $right ($scenario -ne 'timings-disabled-no-op')
            $pairs.Add([pscustomobject]@{ pair = $pair; scenario = $scenario
                left_external_ms = $records["$leftId/$scenario"].external_ms
                right_external_ms = $records["$rightId/$scenario"].external_ms })
        }
    }
    if ((Get-FileHash -LiteralPath $MqbPath).Hash -cne $hash) { throw 'Measured binary changed.' }
    if ($subspans.Count -ne 144) { throw 'Incomplete instrumented subspan population.' }
    $statistics = @(foreach ($scenario in $required) {
        [pscustomobject]@{ scenario = $scenario; external = (Get-AttributionStatistics @($pairs | Where-Object { $_.scenario -ceq $scenario })) }
    })
    @{ complete = $true; subspans = @($subspans.ToArray()); pairs = @($pairs.ToArray()); statistics = $statistics
        historical_cause_resolved = $false; authorizes_merge = $false
        limitation = 'Nested cumulative work, not syscall/CPU/disk/ETW or durable-write evidence. The observer can perturb timings.'
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputRoot 'comparison.json') -Encoding utf8
    @{ complete = $true; completed = @($completed.ToArray()) } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $OutputRoot 'progress.json') -Encoding utf8
    Write-Host 'Completed fixed 8 reports / 168 invocations / 144 instrumented subspan records.'
} catch {
    @{ complete = $false; completed = @($completed.ToArray()); failed_slot = $current; error = $_.ToString()
        not_attempted = @($plan.id | Where-Object { $_ -notin $completed -and $_ -ne $current })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputRoot 'progress.json') -Encoding utf8
    throw
}

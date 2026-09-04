[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BaselineMqbPath,
    [Parameter(Mandatory = $true)][string]$CandidateMqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 4,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RequiredScenarios = @(
    'cold',
    'no-op',
    'single-tu',
    'public-header',
    'build-run',
    'link-only',
    'target-scale-cold',
    'target-scale-no-op',
    'target-scale-no-op-auto',
    'target-scale-no-op-j1',
    'target-scale-common-header-no-op',
    'target-scale-single-tu',
    'discovery-cold',
    'discovery-no-op',
    'discovery-header',
    'modules-cold',
    'modules-no-op',
    'timings-enabled-no-op',
    'timings-disabled-no-op'
)

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 0) {
        return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
    }
    return $sorted[$middle]
}

function Get-Mad {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    $median = Get-Median -Values $Values
    if ($null -eq $median) { return $null }
    $deviations = @($Values | ForEach-Object { [Math]::Abs([double]$_ - [double]$median) })
    return Get-Median -Values $deviations
}

function Get-P95 {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $rank = [Math]::Ceiling(0.95 * $sorted.Count)
    $index = [Math]::Max(0, [int]$rank - 1)
    return [double]$sorted[$index]
}

function Get-DeltaPercent {
    param(
        [Parameter(Mandatory = $true)][double]$Baseline,
        [Parameter(Mandatory = $true)][double]$Candidate
    )
    if ($Baseline -eq 0.0) { return $null }
    return (($Candidate - $Baseline) / $Baseline) * 100.0
}

function Round-Nullable {
    param($Value, [int]$Digits = 3)
    if ($null -eq $Value) { return $null }
    return [Math]::Round([double]$Value, $Digits)
}

function Assert-BenchmarkContract {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)]$Report
    )

    if ([int]$Report.schema_version -lt 3) {
        throw "$Label benchmark schema is too old: expected schema_version >= 3, got $($Report.schema_version)"
    }
    if ([int]$Report.iterations -ne 1) {
        throw "$Label paired benchmark invocation must contain exactly one fixture iteration"
    }

    $summaryByScenario = @{}
    foreach ($row in @($Report.summary)) {
        $scenario = [string]$row.scenario
        if ([string]::IsNullOrWhiteSpace($scenario) -or $summaryByScenario.ContainsKey($scenario)) {
            throw "$Label benchmark contains an empty or duplicate summary scenario '$scenario'"
        }
        $summaryByScenario[$scenario] = $row
    }

    foreach ($scenario in $RequiredScenarios) {
        if (-not $summaryByScenario.ContainsKey($scenario)) {
            throw "$Label benchmark is missing required scenario '$scenario'"
        }
        $samples = @($Report.samples | Where-Object { [string]$_.scenario -eq $scenario })
        if ($samples.Count -ne 1) {
            throw "$Label benchmark scenario '$scenario' has $($samples.Count) raw samples; expected 1"
        }
        foreach ($property in @('total_ms', 'discovery_ms', 'compile_queue_ms', 'measurement_source')) {
            if ($null -eq $samples[0].PSObject.Properties[$property]) {
                throw "$Label benchmark scenario '$scenario' is missing '$property'"
            }
        }
    }
}

function Invoke-OneReport {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$MqbPath,
        [Parameter(Mandatory = $true)][string]$Path
    )

    Write-Host "=== $Label ==="
    & $benchmarkScript -MqbPath $MqbPath -Iterations 1 -OutputPath $Path | Out-Host
    if ($LASTEXITCODE -notin @(0, $null)) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
    $report = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    Assert-BenchmarkContract -Label $Label -Report $report
    return $report
}

function Get-SampleByScenario {
    param(
        [Parameter(Mandatory = $true)]$Report,
        [Parameter(Mandatory = $true)][string]$Scenario
    )
    $matches = @($Report.samples | Where-Object { [string]$_.scenario -eq $Scenario })
    if ($matches.Count -ne 1) {
        throw "Scenario '$Scenario' did not resolve to exactly one sample"
    }
    return $matches[0]
}

$BaselineMqbPath = Get-FullPath $BaselineMqbPath
$CandidateMqbPath = Get-FullPath $CandidateMqbPath
foreach ($path in @($BaselineMqbPath, $CandidateMqbPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "MQB executable not found: $path"
    }
}
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Get-FullPath $OutputPath
}

$benchmarkScript = Join-Path $PSScriptRoot 'benchmark_mqb.ps1'
if (-not (Test-Path -LiteralPath $benchmarkScript -PathType Leaf)) {
    throw "Benchmark harness not found: $benchmarkScript"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-benchmark-paired-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$pairs = [System.Collections.Generic.List[object]]::new()
$executionOrder = [System.Collections.Generic.List[string]]::new()

try {
    for ($pairIndex = 1; $pairIndex -le $Iterations; ++$pairIndex) {
        $baselinePath = Join-Path $tempRoot "pair-$pairIndex-baseline.json"
        $candidatePath = Join-Path $tempRoot "pair-$pairIndex-candidate.json"
        $baselineFirst = ($pairIndex % 2) -eq 1
        $orientation = if ($baselineFirst) { 'baseline-candidate' } else { 'candidate-baseline' }
        $executionOrder.Add($orientation)

        if ($baselineFirst) {
            $baseline = Invoke-OneReport -Label "Pair $pairIndex baseline" -MqbPath $BaselineMqbPath -Path $baselinePath
            $candidate = Invoke-OneReport -Label "Pair $pairIndex candidate" -MqbPath $CandidateMqbPath -Path $candidatePath
        }
        else {
            $candidate = Invoke-OneReport -Label "Pair $pairIndex candidate" -MqbPath $CandidateMqbPath -Path $candidatePath
            $baseline = Invoke-OneReport -Label "Pair $pairIndex baseline" -MqbPath $BaselineMqbPath -Path $baselinePath
        }

        foreach ($scenario in $RequiredScenarios) {
            $baseSample = Get-SampleByScenario -Report $baseline -Scenario $scenario
            $candidateSample = Get-SampleByScenario -Report $candidate -Scenario $scenario
            $baseTotal = [double]$baseSample.total_ms
            $candidateTotal = [double]$candidateSample.total_ms
            $baseDiscovery = [double]$baseSample.discovery_ms
            $candidateDiscovery = [double]$candidateSample.discovery_ms
            $baseQueue = [double]$baseSample.compile_queue_ms
            $candidateQueue = [double]$candidateSample.compile_queue_ms

            $pairs.Add([PSCustomObject]@{
                pair = $pairIndex
                orientation = $orientation
                scenario = $scenario
                baseline_total_ms = $baseTotal
                candidate_total_ms = $candidateTotal
                total_delta_ms = $candidateTotal - $baseTotal
                total_delta_pct = Get-DeltaPercent -Baseline $baseTotal -Candidate $candidateTotal
                baseline_discovery_ms = $baseDiscovery
                candidate_discovery_ms = $candidateDiscovery
                discovery_delta_ms = $candidateDiscovery - $baseDiscovery
                baseline_compile_queue_ms = $baseQueue
                candidate_compile_queue_ms = $candidateQueue
                compile_queue_delta_ms = $candidateQueue - $baseQueue
                baseline = $baseSample
                candidate = $candidateSample
            })
        }
    }

    $comparison = @(
        foreach ($scenario in $RequiredScenarios) {
            $scenarioPairs = @($pairs | Where-Object { [string]$_.scenario -eq $scenario } | Sort-Object pair)
            $baseTotals = [double[]]@($scenarioPairs | ForEach-Object { [double]$_.baseline_total_ms })
            $candidateTotals = [double[]]@($scenarioPairs | ForEach-Object { [double]$_.candidate_total_ms })
            $totalDeltas = [double[]]@($scenarioPairs | ForEach-Object { [double]$_.total_delta_ms })
            $totalDeltaPcts = [double[]]@($scenarioPairs | Where-Object { $null -ne $_.total_delta_pct } | ForEach-Object { [double]$_.total_delta_pct })
            $discoveryDeltas = [double[]]@($scenarioPairs | ForEach-Object { [double]$_.discovery_delta_ms })
            $queueDeltas = [double[]]@($scenarioPairs | ForEach-Object { [double]$_.compile_queue_delta_ms })

            [PSCustomObject]@{
                scenario = $scenario
                pairs = $scenarioPairs.Count
                baseline_median_total_ms = Round-Nullable (Get-Median -Values $baseTotals)
                candidate_median_total_ms = Round-Nullable (Get-Median -Values $candidateTotals)
                paired_median_total_delta_ms = Round-Nullable (Get-Median -Values $totalDeltas)
                paired_mad_total_delta_ms = Round-Nullable (Get-Mad -Values $totalDeltas)
                paired_p95_total_delta_ms = Round-Nullable (Get-P95 -Values $totalDeltas)
                paired_median_total_delta_pct = Round-Nullable -Value (Get-Median -Values $totalDeltaPcts) -Digits 2
                paired_median_discovery_delta_ms = Round-Nullable (Get-Median -Values $discoveryDeltas)
                paired_mad_discovery_delta_ms = Round-Nullable (Get-Mad -Values $discoveryDeltas)
                paired_p95_discovery_delta_ms = Round-Nullable (Get-P95 -Values $discoveryDeltas)
                paired_median_compile_queue_delta_ms = Round-Nullable (Get-Median -Values $queueDeltas)
                paired_mad_compile_queue_delta_ms = Round-Nullable (Get-Mad -Values $queueDeltas)
                paired_p95_compile_queue_delta_ms = Round-Nullable (Get-P95 -Values $queueDeltas)
            }
        }
    )

    Write-Host "`n=== Paired ABBA comparison ==="
    $comparison | Format-Table -AutoSize
    Write-Host 'Negative deltas mean the candidate is faster. P95 is the nearest-rank upper-tail paired delta.'

    if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
        $parent = Split-Path -Parent $OutputPath
        if (-not [string]::IsNullOrWhiteSpace($parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        [PSCustomObject]@{
            schema_version = 3
            generated_utc = [DateTime]::UtcNow.ToString('o')
            pair_count = $Iterations
            pairing = 'alternating baseline-candidate / candidate-baseline'
            p95_method = 'nearest-rank'
            execution_order = $executionOrder
            required_scenarios = $RequiredScenarios
            baseline_mqb = $BaselineMqbPath
            candidate_mqb = $CandidateMqbPath
            comparison = $comparison
            paired_samples = $pairs
        } | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $OutputPath -Encoding utf8
        Write-Host "Benchmark comparison JSON: $OutputPath"
    }
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

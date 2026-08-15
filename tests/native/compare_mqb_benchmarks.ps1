[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BaselineMqbPath,
    [Parameter(Mandatory = $true)][string]$CandidateMqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 5,
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
    'discovery-cold',
    'discovery-no-op',
    'discovery-header',
    'modules-cold',
    'modules-no-op'
)

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-DeltaPercent {
    param(
        [Parameter(Mandatory = $true)][double]$Baseline,
        [Parameter(Mandatory = $true)][double]$Candidate
    )
    if ($Baseline -eq 0.0) { return $null }
    return (($Candidate - $Baseline) / $Baseline) * 100.0
}

function Assert-BenchmarkContract {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)]$Report,
        [Parameter(Mandatory = $true)][int]$ExpectedIterations
    )

    if ([int]$Report.schema_version -lt 2) {
        throw "$Label benchmark schema is too old: expected schema_version >= 2, got $($Report.schema_version)"
    }
    if ([int]$Report.iterations -ne $ExpectedIterations) {
        throw "$Label benchmark iteration contract mismatch: expected $ExpectedIterations, got $($Report.iterations)"
    }

    $summaryRows = @($Report.summary)
    $sampleRows = @($Report.samples)
    if ($summaryRows.Count -eq 0) {
        throw "$Label benchmark produced no summary rows"
    }

    $summaryByScenario = @{}
    foreach ($row in $summaryRows) {
        $scenario = [string]$row.scenario
        if ([string]::IsNullOrWhiteSpace($scenario)) {
            throw "$Label benchmark contains a summary row with an empty scenario name"
        }
        if ($summaryByScenario.ContainsKey($scenario)) {
            throw "$Label benchmark contains duplicate summary scenario '$scenario'"
        }
        $summaryByScenario[$scenario] = $row
    }

    foreach ($scenario in $RequiredScenarios) {
        if (-not $summaryByScenario.ContainsKey($scenario)) {
            throw "$Label benchmark is missing required scenario '$scenario'"
        }

        $summarySamples = [int]$summaryByScenario[$scenario].samples
        if ($summarySamples -ne $ExpectedIterations) {
            throw "$Label benchmark scenario '$scenario' summary has $summarySamples samples; expected $ExpectedIterations"
        }

        $rawSamples = @($sampleRows | Where-Object { [string]$_.scenario -eq $scenario })
        if ($rawSamples.Count -ne $ExpectedIterations) {
            throw "$Label benchmark scenario '$scenario' has $($rawSamples.Count) raw samples; expected $ExpectedIterations"
        }

        $iterationsSeen = @($rawSamples | ForEach-Object { [int]$_.iteration } | Sort-Object -Unique)
        if ($iterationsSeen.Count -ne $ExpectedIterations) {
            throw "$Label benchmark scenario '$scenario' does not contain one distinct sample for every iteration"
        }
        for ($iteration = 1; $iteration -le $ExpectedIterations; ++$iteration) {
            if ($iteration -notin $iterationsSeen) {
                throw "$Label benchmark scenario '$scenario' is missing iteration $iteration"
            }
        }
    }
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

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-benchmark-compare-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$baselineJson = Join-Path $tempRoot 'baseline.json'
$candidateJson = Join-Path $tempRoot 'candidate.json'

try {
    Write-Host '=== Baseline MQB benchmark ==='
    & $benchmarkScript -MqbPath $BaselineMqbPath -Iterations $Iterations -OutputPath $baselineJson
    if ($LASTEXITCODE -notin @(0, $null)) {
        throw "Baseline benchmark failed with exit code $LASTEXITCODE"
    }

    Write-Host "`n=== Candidate MQB benchmark ==="
    & $benchmarkScript -MqbPath $CandidateMqbPath -Iterations $Iterations -OutputPath $candidateJson
    if ($LASTEXITCODE -notin @(0, $null)) {
        throw "Candidate benchmark failed with exit code $LASTEXITCODE"
    }

    $baseline = Get-Content -LiteralPath $baselineJson -Raw | ConvertFrom-Json
    $candidate = Get-Content -LiteralPath $candidateJson -Raw | ConvertFrom-Json

    Assert-BenchmarkContract -Label 'Baseline' -Report $baseline -ExpectedIterations $Iterations
    Assert-BenchmarkContract -Label 'Candidate' -Report $candidate -ExpectedIterations $Iterations

    $candidateByScenario = @{}
    foreach ($row in @($candidate.summary)) {
        $candidateByScenario[[string]$row.scenario] = $row
    }

    $comparison = @(
        foreach ($baselineRow in @($baseline.summary)) {
            $scenario = [string]$baselineRow.scenario
            if (-not $candidateByScenario.ContainsKey($scenario)) {
                throw "Candidate benchmark is missing scenario '$scenario'"
            }
            $candidateRow = $candidateByScenario[$scenario]
            $baselineTotal = [double]$baselineRow.median_total_ms
            $candidateTotal = [double]$candidateRow.median_total_ms
            $baselineDiscovery = [double]$baselineRow.median_discovery_ms
            $candidateDiscovery = [double]$candidateRow.median_discovery_ms
            $totalDeltaPct = Get-DeltaPercent -Baseline $baselineTotal -Candidate $candidateTotal
            $discoveryDeltaPct = Get-DeltaPercent -Baseline $baselineDiscovery -Candidate $candidateDiscovery

            [PSCustomObject]@{
                scenario = $scenario
                baseline_total_ms = [Math]::Round($baselineTotal, 3)
                candidate_total_ms = [Math]::Round($candidateTotal, 3)
                total_delta_ms = [Math]::Round(($candidateTotal - $baselineTotal), 3)
                total_delta_pct = if ($null -eq $totalDeltaPct) { $null } else { [Math]::Round($totalDeltaPct, 2) }
                baseline_discovery_ms = [Math]::Round($baselineDiscovery, 3)
                candidate_discovery_ms = [Math]::Round($candidateDiscovery, 3)
                discovery_delta_ms = [Math]::Round(($candidateDiscovery - $baselineDiscovery), 3)
                discovery_delta_pct = if ($null -eq $discoveryDeltaPct) { $null } else { [Math]::Round($discoveryDeltaPct, 2) }
            }
        }
    )

    $extraCandidateScenarios = @(
        $candidateByScenario.Keys | Where-Object {
            $_ -notin @($baseline.summary | ForEach-Object { [string]$_.scenario })
        }
    )
    if ($extraCandidateScenarios.Count -ne 0) {
        throw "Candidate benchmark has scenarios absent from baseline: $($extraCandidateScenarios -join ', ')"
    }

    Write-Host "`n=== Median wall-clock comparison ==="
    $comparison | Sort-Object scenario | Format-Table -AutoSize
    Write-Host 'Negative deltas mean the candidate is faster. Timings are review evidence, not CI pass/fail thresholds.'

    if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
        $parent = Split-Path -Parent $OutputPath
        if (-not [string]::IsNullOrWhiteSpace($parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        [PSCustomObject]@{
            schema_version = 2
            generated_utc = [DateTime]::UtcNow.ToString('o')
            iterations = $Iterations
            required_scenarios = $RequiredScenarios
            baseline_mqb = $BaselineMqbPath
            candidate_mqb = $CandidateMqbPath
            comparison = @($comparison | Sort-Object scenario)
            baseline = $baseline
            candidate = $candidate
        } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding utf8
        Write-Host "Benchmark comparison JSON: $OutputPath"
    }
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BaselineMqbPath,
    [Parameter(Mandatory = $true)][string]$CandidateMqbPath,
    [ValidateRange(1, 20)][int]$Iterations = 5,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

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

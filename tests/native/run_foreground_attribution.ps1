[CmdletBinding()]
param(
    [string]$BaselineRepoRoot,
    [string]$CandidateRepoRoot,
    [string]$OutputRoot,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0

function Assert-ForegroundStudyIdentity {
    param([System.Collections.IDictionary]$Identity)
    # This investigates the original #599 pair, NOT the latest/favorable head.
    $expected = @{
        baseline_head = 'b16cf049d0c8443288d48a6f712e22e9df6309ea'
        baseline_tree = '24346d15b7f245396c972dfd2478b2ef0ecaa3a4'
        candidate_head = '5835825a0f5337b6491433f9f402ef9434087d09'
        candidate_tree = 'e8819f5deffc29533b852fcd908eca795dd47106'
    }
    foreach ($field in $expected.Keys) {
        if ($null -eq $Identity -or -not $Identity.Contains($field) -or
            $Identity[$field] -isnot [string] -or $Identity[$field] -cne $expected[$field]) {
            throw "Foreground investigation source mismatch: $field"
        }
    }
}

if ($SelfTest) {
    $valid = @{
        baseline_head = 'b16cf049d0c8443288d48a6f712e22e9df6309ea'
        baseline_tree = '24346d15b7f245396c972dfd2478b2ef0ecaa3a4'
        candidate_head = '5835825a0f5337b6491433f9f402ef9434087d09'
        candidate_tree = 'e8819f5deffc29533b852fcd908eca795dd47106'
    }
    Assert-ForegroundStudyIdentity $valid
    $checks = 1
    foreach ($field in @($valid.Keys)) {
        foreach ($mutation in @('wrong', 'missing', 'type')) {
            $bad = $valid.Clone()
            switch ($mutation) {
                wrong { $bad[$field] = '56b8f086e45ddecef56f63a04c347bd80bd8ed84' }
                missing { $bad.Remove($field) }
                type { $bad[$field] = 0 }
            }
            $rejected = $false
            try { Assert-ForegroundStudyIdentity $bad } catch { $rejected = $true }
            if (-not $rejected) { throw "Source guard accepted $mutation $field" }
            ++$checks
        }
    }
    Write-Host "FOREGROUND_ATTRIBUTION_CONTRACT $checks synthetic source checks passed; no benchmark executed"
    return
}

function Get-ForegroundGitValue([string]$Root, [string]$Expression) {
    $value = & git -C $Root rev-parse $Expression
    if ($LASTEXITCODE -ne 0) { throw "Cannot resolve $Root $Expression" }
    return ([string]$value).Trim()
}
foreach ($path in @($BaselineRepoRoot, $CandidateRepoRoot, $OutputRoot)) {
    if ([string]::IsNullOrWhiteSpace($path)) { throw 'Both source roots and a new output root are required.' }
}
$BaselineRepoRoot = [IO.Path]::GetFullPath($BaselineRepoRoot)
$CandidateRepoRoot = [IO.Path]::GetFullPath($CandidateRepoRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$observer = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite a study or partial evidence.' }
$identity = [ordered]@{
    schema = 1
    baseline_head = (Get-ForegroundGitValue $BaselineRepoRoot HEAD)
    baseline_tree = (Get-ForegroundGitValue $BaselineRepoRoot 'HEAD^{tree}')
    candidate_head = (Get-ForegroundGitValue $CandidateRepoRoot HEAD)
    candidate_tree = (Get-ForegroundGitValue $CandidateRepoRoot 'HEAD^{tree}')
    observer_head = (Get-ForegroundGitValue $observer HEAD)
    observer_tree = (Get-ForegroundGitValue $observer 'HEAD^{tree}')
    version = '5.5.0'; configuration = 'Release'
    historical_run_id = '34448181002'; historical_artifact_id = '10140728608'
    historical_cause_resolved = $false; authorizes_merge = $false
    limitation = 'New rebuilt binaries and observed clocks, not historical binaries/OS traces. AB executable-path effects remain possible.'
}
Assert-ForegroundStudyIdentity $identity
foreach ($root in @($BaselineRepoRoot, $CandidateRepoRoot, $observer)) {
    if ((Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim() -cne '5.5.0') { throw 'VERSION must remain 5.5.0.' }
    $status = @(& git -C $root status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) { throw "Tracked source is not clean: $root" }
}
# Reuse the accepted observer and its fixture verbatim. Do not relax #171's
# identical-product experiment: this is a separate, explicitly different pair.
foreach ($path in @('tests/native/benchmark_mqb.ps1', 'tests/native/collect_benchmark_attribution.ps1',
    'tests/native/build_mqb.ps1', 'tests/native/acquire_seed.ps1')) {
    $expected = Get-ForegroundGitValue $BaselineRepoRoot "HEAD:$path"
    foreach ($root in @($CandidateRepoRoot, $observer)) {
        if ((Get-ForegroundGitValue $root "HEAD:$path") -cne $expected) { throw "Measurement/build helper drift: $path" }
    }
}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$bootstrap = Join-Path $OutputRoot 'bootstrap'
New-Item -ItemType Directory -Path $bootstrap | Out-Null
$identity | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bootstrap 'source-identity.json') -Encoding utf8
foreach ($entry in @(@{ role = 'A'; root = $BaselineRepoRoot }, @{ role = 'B'; root = $CandidateRepoRoot },
    @{ role = 'observer'; root = $observer })) {
    & git -C $entry.root archive --format=zip "--output=$(Join-Path $bootstrap "$($entry.role)-source.zip")" HEAD
    if ($LASTEXITCODE -ne 0) { throw "Source archive failed: $($entry.role)" }
}
Start-Transcript -LiteralPath (Join-Path $bootstrap 'build-transcript.txt') | Out-Null
try {
    $seed = & (Join-Path $PSScriptRoot 'acquire_seed.ps1') -RepoRoot $observer -OutputRoot (Join-Path $OutputRoot 'seed')
    if ($LASTEXITCODE -ne 0) { throw 'Pinned seed acquisition failed.' }
    $aExe = & (Join-Path $BaselineRepoRoot 'tests/native/build_mqb.ps1') -BuilderMqbPath $seed `
        -RepoRoot $BaselineRepoRoot -Version '5.5.0' -Configuration Release -Clean -OutputPath (Join-Path $bootstrap 'A/mqb.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Runtime A build failed; no study claimed.' }
    $bExe = & (Join-Path $CandidateRepoRoot 'tests/native/build_mqb.ps1') -BuilderMqbPath $seed `
        -RepoRoot $CandidateRepoRoot -Version '5.5.0' -Configuration Release -Clean -OutputPath (Join-Path $bootstrap 'B/mqb.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Runtime B build failed; no study claimed.' }
    $identity['baseline_sha256'] = (Get-FileHash -LiteralPath $aExe -Algorithm SHA256).Hash
    $identity['candidate_sha256'] = (Get-FileHash -LiteralPath $bExe -Algorithm SHA256).Hash
    $identity['seed_sha256'] = (Get-FileHash -LiteralPath $seed -Algorithm SHA256).Hash
    $identity['github_run_id'] = $env:GITHUB_RUN_ID
    $identity['run_attempt'] = $env:GITHUB_RUN_ATTEMPT
    $identity['runner_image'] = $env:ImageVersion
    $identityPath = Join-Path $bootstrap 'build-identity.json'
    $identity | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $identityPath -Encoding utf8
} finally { Stop-Transcript | Out-Null }
# Original collector persists its 8AA+8AB / 32-report / 672-invocation schedule
# before measured launches, refuses overwrite and retains failures. No retry.
& (Join-Path $PSScriptRoot 'collect_benchmark_attribution.ps1') -BaselineMqbPath $aExe `
    -CandidateMqbPath $bExe -BuildIdentityPath $identityPath -OutputRoot (Join-Path $OutputRoot 'observed')

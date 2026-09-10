[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepoRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$MqbPath,
    [switch]$BuildOnly,
    [string]$PrebuiltProbePath,
    [string]$ProbeIdentityPath
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refuse to overwrite earlier batch evidence.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
function Write-Json($Path, $Value) {
    $Value | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $Path -Encoding utf8
}
Push-Location $RepoRoot
try {
    $head = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read source HEAD.' }
    if (@(& git status --porcelain --untracked-files=no).Count -ne 0 -or $LASTEXITCODE -ne 0) { throw 'Tracked source must be clean.' }
    if ((Get-Content VERSION -Raw).Trim() -cne '5.5.0') { throw 'VERSION must remain 5.5.0.' }
    if ($BuildOnly) {
        if ([string]::IsNullOrWhiteSpace($MqbPath) -or -not [string]::IsNullOrWhiteSpace($PrebuiltProbePath)) { throw 'Invalid build-only input.' }
        $MqbPath = [IO.Path]::GetFullPath($MqbPath)
        & ./tests/native/assert_cpp_layout.ps1 -CppRoot (Join-Path $RepoRoot 'cpp')
        $config = Get-Content cpp/mqb.json -Raw | ConvertFrom-Json
        $sources = @($config.discovery.extra_sources | ForEach-Object { $_.Replace('\','/') } | Sort-Object -Unique)
        $actual = @(Get-ChildItem cpp/src -Recurse -File -Filter '*.cpp' | ForEach-Object {
            [IO.Path]::GetRelativePath((Join-Path $RepoRoot 'cpp'), $_.FullName).Replace('\','/')
        } | Where-Object { $_ -ne 'src/app/main.cpp' } | Sort-Object -Unique)
        if (@(Compare-Object $actual $sources).Count) { throw 'Product source manifest drift.' }
        $arguments = @('cpp/tests/platform/windows/msvc_work_batch_probe.cpp')
        $arguments += @($sources | ForEach-Object { 'cpp/' + $_ })
        $arguments += @('--env','vs','--no-discover','--std',[string]$config.build.standard,'--release','--runtime','MT')
        foreach ($path in @($config.build.include_dirs)) { $arguments += @('-I',('cpp/' + $path)) }
        foreach ($arg in @($config.build.compiler_args)) { $arguments += @('--compiler-arg',[string]$arg) }
        $arguments += @('-D','MQB_VERSION="work-batch-probe"','--lib','shell32.lib','-o','msvc_work_batch_probe')
        Write-Json (Join-Path $OutputRoot 'build.argv.json') $arguments
        $output = @(& $MqbPath @arguments 2>&1); $code = $LASTEXITCODE
        $output | Set-Content -LiteralPath (Join-Path $OutputRoot 'build.output.txt') -Encoding utf8
        $output | ForEach-Object { Write-Host $_ }
        if ($code -ne 0) { throw "Probe build failed: $code" }
        $probe = Join-Path $RepoRoot '.mqb/bin/msvc_work_batch_probe.exe'
        Write-Json (Join-Path $OutputRoot 'probe.identity.json') @{
            schema=1; source_head=$head; version='5.5.0'; configuration='Release'
            probe_sha256=(Get-FileHash -LiteralPath $probe).Hash
            candidate_sha256=(Get-FileHash -LiteralPath $MqbPath).Hash
            build_run_id=$env:GITHUB_RUN_ID; build_run_attempt=$env:GITHUB_RUN_ATTEMPT
        }
        return
    }
    if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
        $env:MQB_BATCH_DISPOSABLE_HOST -ne '1') { throw 'Native batch cases require a separate disposable hosted VM.' }
    if ([string]::IsNullOrWhiteSpace($PrebuiltProbePath) -or [string]::IsNullOrWhiteSpace($ProbeIdentityPath)) { throw 'Prebuilt probe and identity required.' }
    $probe = [IO.Path]::GetFullPath($PrebuiltProbePath)
    $origin = Get-Content -LiteralPath $ProbeIdentityPath -Raw | ConvertFrom-Json
    if ($origin.source_head -cne $head -or $origin.version -cne '5.5.0' -or $origin.configuration -cne 'Release' -or
        $origin.probe_sha256 -cne (Get-FileHash -LiteralPath $probe).Hash) { throw 'Exact prebuilt source/binary identity mismatch.' }
    Write-Json (Join-Path $OutputRoot 'identity.json') @{
        source_head=$head; probe=$origin; image=$env:ImageVersion; run=$env:GITHUB_RUN_ID; attempt=$env:GITHUB_RUN_ATTEMPT
        os=[Environment]::OSVersion.VersionString; powershell=$PSVersionTable.PSVersion.ToString()
        production_pipeline_integrated=$false; safe_to_transfer_write_lease=$false
    }
    $plan = @()
    foreach ($config in @('Debug','Release')) {
        foreach ($mode in @('complete','cancel','failure-cancel')) { $plan += @{ configuration=$config; mode=$mode } }
    }
    Write-Json (Join-Path $OutputRoot 'plan.json') @{
        cases=$plan; expected_cases=6; workers=2; items=3; functions_per_active_source=12000
        compiler_failure='separately labelled static_assert, not historical PDB reproduction'; retries=0
        successor_gate='actual fixture link/run and commit sentinel only; not product cache/lease publication'
    }
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($case in $plan) {
        $name = $case.configuration + '-' + $case.mode
        $root = Join-Path $OutputRoot $name
        # The native probe owns creation of this fresh directory.
        $arguments = @($root,$case.configuration,$case.mode)
        $output = @(& $probe @arguments 2>&1); $code = $LASTEXITCODE
        if (-not (Test-Path -LiteralPath $root)) { New-Item -ItemType Directory -Path $root | Out-Null }
        $output | Set-Content -LiteralPath (Join-Path $root 'case.output.txt') -Encoding utf8
        Write-Json (Join-Path $root 'case.argv.json') $arguments
        $errors = @(); $report = $null
        try {
            $report = Get-Content -LiteralPath (Join-Path $root 'observation.json') -Raw | ConvertFrom-Json
            if ($code -ne 0 -or $report.gate_passed -isnot [bool] -or -not $report.gate_passed) { throw 'Native batch contract failed.' }
            if ($report.production_pipeline_integrated -isnot [bool] -or $report.production_pipeline_integrated -or
                $report.safe_to_transfer_write_lease -isnot [bool] -or $report.safe_to_transfer_write_lease) { throw 'Invalid authority claim.' }
            $expected = if ($case.mode -eq 'complete') { 'succeeded' } elseif ($case.mode -eq 'cancel') { 'cancelled' } else { 'failed' }
            if ($report.configuration -cne $case.configuration -or $report.mode -cne $case.mode -or $report.outcome -cne $expected) { throw 'Result identity/outcome mismatch.' }
            $count = if ($case.mode -eq 'complete') { 3 } else { 2 }
            if ($report.items.Count -ne 3 -or $report.compiler_invocations -ne $count -or $report.scheduling.worker_count -ne 2 -or
                $report.scheduling.started_count -ne $count -or $report.real_compiler_overlap -ne $true -or $report.retained_compilers_exited -ne $true) { throw 'Incomplete multiworker evidence.' }
            foreach ($index in 0..($count-1)) {
                $stem = 'work' + $index
                $result = Get-Content -LiteralPath (Join-Path $root "$stem.result.json") -Raw | ConvertFrom-Json
                $stdout = [IO.File]::ReadAllText((Join-Path $root "$stem.stdout.txt"))
                $stderr = [IO.File]::ReadAllText((Join-Path $root "$stem.stderr.txt"))
                if ($result.exit_code -ne $report.items[$index].exit_code -or $result.cancelled -ne $false) { throw 'Original result replaced or tool terminated.' }
                if ($case.mode -eq 'failure-cancel' -and $index -eq 1) {
                    if ($result.exit_code -eq 0 -or ($stdout+$stderr) -notmatch 'MQB_BATCH_EXPECTED_COMPILER_FAILURE') { throw 'Original injected compiler error missing.' }
                } elseif ($result.exit_code -ne 0) { throw 'Original positive compiler failed.' }
            }
            if ($count -eq 2) {
                if ($report.items[2].state -cne 'not_started' -or (Test-Path -LiteralPath (Join-Path $root 'work2.result.json')) -or
                    $report.link_calls -ne 0 -or $report.run_calls -ne 0 -or $report.fixture_commits -ne 0 -or
                    (Test-Path -LiteralPath (Join-Path $root 'fixture-commit.json')) -or (Test-Path -LiteralPath (Join-Path $root 'program.exe'))) {
                    throw 'Failed/cancelled batch advanced to a successor or counted missing work successful.'
                }
            } else {
                foreach ($stem in @('link','run')) {
                    $result = Get-Content -LiteralPath (Join-Path $root "$stem.result.json") -Raw | ConvertFrom-Json
                    if ($result.exit_code -ne 0) { throw 'Positive successor failed.' }
                }
                if ($report.link_calls -ne 1 -or $report.run_calls -ne 1 -or $report.fixture_commits -ne 1 -or
                    -not (Test-Path -LiteralPath (Join-Path $root 'fixture-commit.json'))) { throw 'Positive successor gate untested.' }
            }
        } catch { $errors += $_.Exception.Message }
        $rows.Add(@{ case=$name; native_exit=$code; accepted=($errors.Count -eq 0); errors=$errors; observation=$report })
        Write-Json (Join-Path $OutputRoot 'summary.json') @{
            expected_cases=6; completed_cases=$rows.Count; not_run_cases=(6-$rows.Count); cases=@($rows.ToArray())
            production_pipeline_integrated=$false; safe_to_transfer_write_lease=$false
        }
        if ($errors.Count) { throw 'Stopped at first failed evidence/control; remaining fixed cases not attempted.' }
    }
}
finally { Pop-Location }

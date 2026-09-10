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
        $arguments = @('cpp/tests/platform/windows/msvc_target_admission_probe.cpp')
        $arguments += @($sources | ForEach-Object { 'cpp/' + $_ })
        $arguments += @('--env','vs','--no-discover','--std',[string]$config.build.standard,'--release','--runtime','MT')
        foreach ($path in @($config.build.include_dirs)) { $arguments += @('-I',('cpp/' + $path)) }
        foreach ($arg in @($config.build.compiler_args)) { $arguments += @('--compiler-arg',[string]$arg) }
        $arguments += @('-D','MQB_VERSION="target-admission-probe"','--lib','shell32.lib','--lib','bcrypt.lib','-o','msvc_target_admission_probe')
        Write-Json (Join-Path $OutputRoot 'build.argv.json') $arguments
        $output = @(& $MqbPath @arguments 2>&1); $code = $LASTEXITCODE
        $output | Set-Content -LiteralPath (Join-Path $OutputRoot 'build.output.txt') -Encoding utf8
        $output | ForEach-Object { Write-Host $_ }
        if ($code -ne 0) { throw "Probe build failed: $code" }
        $probe = Join-Path $RepoRoot '.mqb/bin/msvc_target_admission_probe.exe'
        Write-Json (Join-Path $OutputRoot 'probe.identity.json') @{
            schema=1; source_head=$head; version='5.5.0'; configuration='Release'
            probe_sha256=(Get-FileHash -LiteralPath $probe).Hash
            candidate_sha256=(Get-FileHash -LiteralPath $MqbPath).Hash
            build_run_id=$env:GITHUB_RUN_ID; build_run_attempt=$env:GITHUB_RUN_ATTEMPT
        }
        return
    }
    if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
        $env:MQB_TARGET_DISPOSABLE_HOST -ne '1') { throw 'Native batch cases require a separate disposable hosted VM.' }
    if ([string]::IsNullOrWhiteSpace($PrebuiltProbePath) -or [string]::IsNullOrWhiteSpace($ProbeIdentityPath)) { throw 'Prebuilt probe and identity required.' }
    $probe = [IO.Path]::GetFullPath($PrebuiltProbePath)
    $origin = Get-Content -LiteralPath $ProbeIdentityPath -Raw | ConvertFrom-Json
    if ($origin.source_head -cne $head -or $origin.version -cne '5.5.0' -or $origin.configuration -cne 'Release' -or
        $origin.probe_sha256 -cne (Get-FileHash -LiteralPath $probe).Hash) { throw 'Exact prebuilt source/binary identity mismatch.' }
    Write-Json (Join-Path $OutputRoot 'identity.json') @{
        source_head=$head; probe=$origin; image=$env:ImageVersion; run=$env:GITHUB_RUN_ID; attempt=$env:GITHUB_RUN_ATTEMPT
        os=[Environment]::OSVersion.VersionString; powershell=$PSVersionTable.PSVersion.ToString()
        target_api_real_processes=$true; cli_integrated=$false; safe_to_transfer_write_lease=$false
    }
    $plan = @()
    foreach ($config in @('Debug','Release')) {
        foreach ($mode in @('complete','cancel','failure-cancel')) { $plan += @{ configuration=$config; mode=$mode } }
    }
    Write-Json (Join-Path $OutputRoot 'plan.json') @{
        cases=$plan; expected_cases=6; workers=2; items=3; functions_per_active_source=12000
        compiler_failure='separately labelled static_assert, not historical PDB reproduction'; retries=0
        target_api='run_with_compile_admission_stop'; phases=@('cold','warm','subject');
        cache_gate='actual product target link-cache/exe bytes, mtime and file identity; no project rollback'
    }
    $rows = [Collections.Generic.List[object]]::new()
    Write-Json (Join-Path $OutputRoot 'summary.json') @{
        expected_cases=6; completed_cases=0; not_run_cases=6; cases=@()
        cli_integrated=$false; safe_to_transfer_write_lease=$false
    }
    foreach ($case in $plan) {
        $name = $case.configuration + '-' + $case.mode
        $root = Join-Path $OutputRoot $name
        $arguments = @($root,$case.configuration,$case.mode)
        $output = @(& $probe @arguments 2>&1); $code = $LASTEXITCODE
        if (-not (Test-Path -LiteralPath $root)) { New-Item -ItemType Directory -Path $root | Out-Null }
        $output | Set-Content -LiteralPath (Join-Path $root 'case.output.txt') -Encoding utf8
        Write-Json (Join-Path $root 'case.argv.json') $arguments
        # Run even after a native failure, preserving missing/partial evidence as
        # rejection rather than inventing successful outcomes from the exit code.
        & python (Join-Path $PSScriptRoot 'verify_msvc_target_admission.py') --case $root `
            --output (Join-Path $root 'audit.json')
        $auditCode = $LASTEXITCODE
        $audit = Get-Content -LiteralPath (Join-Path $root 'audit.json') -Raw | ConvertFrom-Json
        $ok = $code -eq 0 -and $auditCode -eq 0 -and $audit.accepted -eq $true
        if ($ok -and ($audit.configuration -cne $case.configuration -or $audit.mode -cne $case.mode)) { $ok = $false }
        $rows.Add(@{ case=$name; native_exit=$code; audit_exit=$auditCode; accepted=$ok; audit=$audit })
        Write-Json (Join-Path $OutputRoot 'summary.json') @{
            expected_cases=6; completed_cases=$rows.Count; not_run_cases=(6-$rows.Count); cases=@($rows.ToArray())
            cli_integrated=$false; safe_to_transfer_write_lease=$false
        }
        if (-not $ok) { throw 'Original target/evidence control failed; stop fixed remaining slots without retry.' }
    }
    # Post-case inventories do not establish failure-time or all-writer identity.
    $binaries = @(Get-ChildItem -LiteralPath $OutputRoot -Recurse -File | Where-Object {
        $_.Extension -in @('.exe','.obj','.pdb')
    } | ForEach-Object { @{
        path=[IO.Path]::GetRelativePath($OutputRoot,$_.FullName); bytes=$_.Length
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    } })
    Write-Json (Join-Path $OutputRoot 'generated-binary-hashes.json') $binaries
}
finally { Pop-Location }

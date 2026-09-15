[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Build','Observe')][string]$Mode,
      [Parameter(Mandatory)][string]$OutputRoot,[string]$InputRoot='native-input')
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
Set-StrictMode -Version 2.0
if ($env:GITHUB_RUN_ATTEMPT -ne '1') { throw 'Budget is first-attempt only; do not rerun.' }
$repo=[IO.Path]::GetFullPath($PWD.Path)
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Never overwrite an earlier experiment.' }
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
function Write-Json($Path,$Value) { $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding utf8 }
function Invoke-Saved([string]$Exe,[string[]]$Argv,[string]$Stem) {
    Write-Json "$Stem.argv.json" @($Argv)
    # Separate streams and preserve nonzero results before interpreting them.
    & $Exe @Argv 1>"$Stem.stdout.txt" 2>"$Stem.stderr.txt"
    $code=$LASTEXITCODE
    Write-Json "$Stem.result.json" @{exit_code=$code}
    return $code
}
$head=(& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or @(& git status --porcelain --untracked-files=no).Count -ne 0) { throw 'Unknown/dirty tracked source.' }
if ((Get-Content VERSION -Raw).Trim() -cne '5.5.0') { throw 'Version drift.' }
$base='55f57a84ad938da10d0e28b4578cd1aef6d7f903'
& git merge-base --is-ancestor $base HEAD
if ($LASTEXITCODE -ne 0) { throw 'Wrong diagnostic base.' }
$allowed=@('tests/native/scheduler-query-plan.json','tests/native/scheduler_query_contrast.py','tests/native/collect_scheduler_query_contrast.ps1','.github/workflows/scheduler-query-contrast.yml')
$changed=@(& git diff --name-only $base HEAD)
if ($LASTEXITCODE -ne 0 -or @($changed | Where-Object { $_ -cnotin $allowed }).Count -ne 0 -or $changed.Count -ne 4) { throw 'Unexpected source changes.' }
Write-Json (Join-Path $OutputRoot 'identity.json') @{base=$base;head=$head;tree=(& git rev-parse 'HEAD^{tree}').Trim();version='5.5.0';run=$env:GITHUB_RUN_ID;attempt=$env:GITHUB_RUN_ATTEMPT;runner_image=$env:ImageVersion;os=[Environment]::OSVersion.VersionString;mode=$Mode;case_budget=2;historical_cause_resolved=$false;authorizes_merge=$false}
if ($Mode -eq 'Build') {
    & git archive -o (Join-Path $OutputRoot 'source.zip') HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Source archive failed.' }
    foreach ($script in @('verify_pdb_file_trace.py','verify_pdb_invocations.py','verify_pdb_observers.py','scheduler_query_contrast.py')) {
        $code=Invoke-Saved python @("tests/native/$script",$(if ($script -eq 'scheduler_query_contrast.py') {'self-test'} else {'--self-test'}),'--output',(Join-Path $OutputRoot "$script.json")) (Join-Path $OutputRoot "$script-test")
        if ($code -ne 0) { throw "Portable contract failed: $script" }
    }
    $derived=Join-Path $OutputRoot 'derived'
    $code=Invoke-Saved python @('tests/native/scheduler_query_contrast.py','prepare','--repo',$repo,'--plan','tests/native/scheduler-query-plan.json','--output',$derived) (Join-Path $OutputRoot 'prepare')
    if ($code -ne 0) { throw 'Preparation failed.' }
    $seed=& ./tests/native/acquire_seed.ps1 -RepoRoot $repo -OutputRoot (Join-Path $OutputRoot 'seed')
    if ($LASTEXITCODE -ne 0) { throw 'Pinned seed unavailable.' }
    Copy-Item -LiteralPath $seed -Destination (Join-Path $OutputRoot 'mqb-seed.exe')
    & ./tests/native/assert_cpp_layout.ps1 -CppRoot (Join-Path $repo 'cpp')
    if ($LASTEXITCODE -ne 0) { throw 'Layout gate failed.' }
    $cfg=Get-Content cpp/mqb.json -Raw | ConvertFrom-Json
    $sources=@($cfg.discovery.extra_sources | ForEach-Object { $_.Replace('\','/') } | Sort-Object -Unique)
    $actual=@(Get-ChildItem cpp/src -Recurse -File -Filter '*.cpp' | ForEach-Object { [IO.Path]::GetRelativePath((Join-Path $repo 'cpp'),$_.FullName).Replace('\','/') } | Where-Object { $_ -ne 'src/app/main.cpp' } | Sort-Object -Unique)
    if (@(Compare-Object $sources $actual).Count -ne 0) { throw 'Production inventory mismatch.' }
    foreach ($configuration in @('Debug','Release')) {
        $name='scheduler_probe_'+$configuration.ToLowerInvariant()
        $args=@((Join-Path $derived 'derived-probe.cpp'))+@($sources | ForEach-Object { 'cpp/'+$_ })
        $args+=@('--env','vs','--no-discover','--std','c++23',$(if($configuration -eq 'Debug'){'--debug'}else{'--release'}),'--runtime',$(if($configuration -eq 'Debug'){'MTd'}else{'MT'}))
        foreach($include in @($cfg.build.include_dirs)) {$args+=@('-I',('cpp/'+$include))}
        foreach($arg in @($cfg.build.compiler_args)) {$args+=@('--compiler-arg',[string]$arg)}
        $args+=@('-D','MQB_VERSION="scheduler-query-probe"','--lib','shell32.lib','--lib','Rstrtmgr.lib','-o',$name)
        $code=Invoke-Saved $seed $args (Join-Path $OutputRoot "build-$configuration")
        if($code -ne 0){throw "First $configuration probe build failed; no retry."}
        $exe=Join-Path $OutputRoot "$name.exe"
        Copy-Item -LiteralPath ".mqb/bin/$name.exe" -Destination $exe
        $code=Invoke-Saved $exe @('--evidence-contract-self-test') (Join-Path $OutputRoot "native-contract-$configuration")
        if($code -ne 0){throw 'Native contract failed.'}
    }
    $args=@('cpp/tests/platform/windows/pdb_file_event_probe.cpp','cpp/src/platform/windows/CommandLine.cpp','--env','vs','--no-discover','--std','c++23','--release','--runtime','MT','-I','cpp/include','--compiler-arg','/W4','--compiler-arg','/permissive-','--lib','advapi32.lib','--lib','tdh.lib','-o','scheduler_trace')
    $code=Invoke-Saved $seed $args (Join-Path $OutputRoot 'build-recorder')
    if($code -ne 0){throw 'First recorder build failed; no retry.'}
    Copy-Item -LiteralPath '.mqb/bin/scheduler_trace.exe' -Destination (Join-Path $OutputRoot 'scheduler_trace.exe')
    $files=@('mqb-seed.exe','scheduler_probe_debug.exe','scheduler_probe_release.exe','scheduler_trace.exe')
    Write-Json (Join-Path $OutputRoot 'binaries.json') @{head=$head;files=@($files | ForEach-Object { @{path=$_;sha256=(Get-FileHash (Join-Path $OutputRoot $_)).Hash} });build_attempts=3;new_windows_cases=0}
    return
}
# A separate fresh GitHub-hosted VM; no build or seed execution on this host.
if($env:MQB_OWNERSHIP_DISPOSABLE_HOST -ne '1' -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or (Test-Path Env:_MSPDBSRV_ENDPOINT_)){throw 'Disposable default-endpoint authorization required.'}
$InputRoot=[IO.Path]::GetFullPath($InputRoot)
$binary=Get-Content (Join-Path $InputRoot 'binaries.json') -Raw | ConvertFrom-Json
if($binary.head -cne $head){throw 'Binary source identity mismatch.'}
foreach($entry in $binary.files){if((Get-FileHash (Join-Path $InputRoot $entry.path)).Hash -cne $entry.sha256){throw 'Binary hash mismatch.'}}
Copy-Item -LiteralPath (Join-Path $InputRoot 'binaries.json') -Destination $OutputRoot
$tracer=Join-Path $InputRoot 'scheduler_trace.exe';$probe=Join-Path $InputRoot 'scheduler_probe_release.exe'
Write-Json (Join-Path $OutputRoot 'environment.json') @(@('PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_','VCToolsInstallDir','WindowsSdkDir','ImageOS','ImageVersion') | ForEach-Object { @{name=$_;value=[Environment]::GetEnvironmentVariable($_)} })
$rows=[Collections.Generic.List[object]]::new();$allComplete=$true
Write-Json (Join-Path $OutputRoot 'plan.json') @{modes=@('rm-on','rm-off');maximum_cases=2;order_fixed=$true;adaptive_retries=$false;only_variable='four coordinator Restart Manager queries';profile='modules-release';origin='A-started';ending='scheduler-drain';historical_cause_resolved=$false}
foreach($arm in @('rm-on','rm-off')){
    if(@(Get-Process mspdbsrv -ErrorAction SilentlyContinue).Count -ne 0){throw 'Preexisting service; no kill and no further case.'}
    $slot=Join-Path $OutputRoot $arm;New-Item -ItemType Directory -Path $slot | Out-Null
    $fixture=Join-Path $slot 'fixture';$trace=Join-Path $slot 'trace'
    Write-Json (Join-Path $slot 'case-start.json') @{fixture=$fixture;arm=$arm;utc=[DateTime]::UtcNow.ToString('o');run=$env:GITHUB_RUN_ID;attempt=1}
    $args=@($trace,$probe,'--scheduler-query-case',$fixture,'modules-release','A-started','scheduler-drain',('scheduler-query-'+$arm),$arm)
    $code=Invoke-Saved $tracer $args (Join-Path $slot 'tracer')
    $audit=Invoke-Saved python @('tests/native/scheduler_query_contrast.py','audit','--slot',$slot,'--mode',$arm,'--output',(Join-Path $slot 'audit.json')) (Join-Path $slot 'audit-invocation')
    $envelope=Get-Content (Join-Path $fixture 'default-envelope.json') -Raw | ConvertFrom-Json
    $cleanup=$envelope.cleanup_verified -eq $true -and $envelope.outer_lifecycle_verified -eq $true -and $envelope.endpoint_override_absent -eq $true -and @($envelope.remaining_servers).Count -eq 0 -and @(Get-Process mspdbsrv -ErrorAction SilentlyContinue).Count -eq 0
    $o=Get-Content (Join-Path $fixture 'observation.json') -Raw | ConvertFrom-Json
    $tool=(Get-Content (Join-Path $fixture 'toolchain.txt'))[0]
    $toolDir=Split-Path -Parent $tool
    Write-Json (Join-Path $slot 'actual-tools.json') @(@('cl.exe','link.exe','mspdbsrv.exe','c1xx.dll','c2.dll','mspdbcore.dll') | ForEach-Object { $p=Join-Path $toolDir $_;if(Test-Path -LiteralPath $p){@{path=$p;sha256=(Get-FileHash $p).Hash;version=(Get-Item $p).VersionInfo.FileVersion}}else{@{path=$p;missing=$true}} })
    $rows.Add(@{arm=$arm;native_exit=$code;audit_exit=$audit;cleanup=$cleanup;A_exit=$o.A_exit;B0_exit=$o.B0_exit;B1_exit=$o.B1_exit;link_exit=$o.B_link_exit;run_exit=$o.B_run_exit;recovery_exit=$o.recovery_compile_exit;drain_control_ok=$o.drain_control_ok})
    if($audit -ne 0){$allComplete=$false}
    Write-Json (Join-Path $OutputRoot 'summary.json') @{expected=2;attempted=$rows.Count;not_run=(2-$rows.Count);cases=@($rows.ToArray());evidence_complete=$allComplete;historical_cause_resolved=$false;authorizes_merge=$false}
    if(-not $cleanup){throw 'Cleanup unproven; remaining case not run.'}
}
# Outcome-dependent LINK/RUN cannot be forced just to make an inventory compare.
# Compare mandatory generated sources and original compile argv; preserve optional outputs separately.
$code=Invoke-Saved python @('tests/native/scheduler_query_contrast.py','compare','--left',(Join-Path $OutputRoot 'rm-on'),'--right',(Join-Path $OutputRoot 'rm-off'),'--output',(Join-Path $OutputRoot 'input-equivalence.json')) (Join-Path $OutputRoot 'compare-inputs')
if($code -ne 0){$allComplete=$false}
Write-Json (Join-Path $OutputRoot 'summary.json') @{expected=2;attempted=$rows.Count;not_run=(2-$rows.Count);cases=@($rows.ToArray());evidence_complete=$allComplete;historical_cause_resolved=$false;authorizes_merge=$false}
Write-Json (Join-Path $OutputRoot 'file-hashes.json') @(Get-ChildItem $OutputRoot -Recurse -File | ForEach-Object {@{path=[IO.Path]::GetRelativePath($OutputRoot,$_.FullName);bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}})
if(-not $allComplete){throw 'Raw samples retained; evidence audit incomplete.'}
# No B success/zero-overhead/release claim is derived from the collection exit.

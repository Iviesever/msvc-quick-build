# Actual coordinator + Python freeze/ready/audit + manifest/projection; fake builds/launcher/host.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputRoot,[Parameter(Mandatory)][string]$OriginalPreparation)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot);$OriginalPreparation=[IO.Path]::GetFullPath($OriginalPreparation)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Fresh synthetic evidence required.' }
$null=New-Item -ItemType Directory -Path $OutputRoot
# This read-only check accepts only the historical source archive/input; no programs run.
& python -B (Join-Path $PSScriptRoot 'v9_noop_rebound.py') check-preparation --inputs $OriginalPreparation
if ($LASTEXITCODE -ne 0) { throw 'Original fixed input refused for synthetic control.' }
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'run_v9_noop_samejob.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$definition=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq 'Import-SameJobDefinitions' })
if ($definition.Count -ne 1) { throw 'Missing importer.' }
. ([scriptblock]::Create($definition[0].Extent.Text))
Import-SameJobDefinitions (Join-Path $PSScriptRoot 'run_v9_noop_samejob.ps1') @('Invoke-SameJobPipeline','Invoke-SameJobCheck','Assert-SameJobEnvironment')
Import-SameJobDefinitions (Join-Path $PSScriptRoot 'collect_external_noop_boundary.ps1') @('Write-NewJson','Get-Digest','Get-FileManifest')
Import-SameJobDefinitions (Join-Path $PSScriptRoot 'run_v9_noop_validation.ps1') @('Invoke-V9Calls','Assert-V9Call','Get-V9CacheProjection')
$realCheck=${function:Invoke-SameJobCheck};$realWrite=${function:Write-NewJson}
$cases=[Collections.Generic.List[object]]::new();$failures=0
$commit=(& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Missing exact test source.' }
$sourceZip=Join-Path $OutputRoot 'test-source.zip'
& git -C $repo archive -o $sourceZip HEAD
if ($LASTEXITCODE -ne 0) { throw 'Test source export failed.' }
function Assert([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Refuses([scriptblock]$Action,[string]$Pattern) {
    $caught=$null;try { & $Action } catch { $caught=$_.ToString() }
    Assert ($null -ne $caught -and $caught -match $Pattern) "Expected $Pattern; got $caught"
}
function Write-NewJson($Path,$Value) {
    if ([IO.Path]::GetFileName($Path) -ceq $script:badJournal) { throw 'SYNTHETIC journal failure' }
    & $script:realWrite $Path $Value
}
function Assert-SameJobSources($Sources,$Repo) { if ($script:badSource) { throw 'SYNTHETIC source refusal' } }
function Assert-V9Space($Path,$Minimum) { if ($script:lowSpace) { throw 'SYNTHETIC low space' } }
function Get-V9Environment {
    $copy=$script:environment | ConvertTo-Json -Depth 12 | ConvertFrom-Json -AsHashtable
    if ($script:driftBefore -and $script:attempted -eq 0) { $copy.image='CHANGED' }
    if ($script:driftAfter -and $script:attempted -eq 16) { $copy.ambient_sha256='0'*64 }
    return $copy
}
function Invoke-SameJobCheck($Command,$Evidence,$Repo) {
    if ($Command -ceq $script:badCheck) { throw "SYNTHETIC $Command refusal" }
    & $script:realCheck $Command $Evidence $Repo
    if ($Command -ceq 'ready') { $script:readyObserved=$true }
}
function Invoke-V9Preparation($Repo,$Sources,$Evidence) {
    # Deliberately fake the build only. The REAL Python verifier sees the exact old
    # product-source ZIPs, newly written fake binary bytes and full synthetic ledger.
    Assert-V9Space $Evidence 8GB
    Write-NewJson (Join-Path $Evidence 'environment-before.json') $script:environment
    foreach ($phase in @('seed','baseline','candidate')) {
        $budget=if ($phase -ceq 'seed') {1} else {2}
        Write-NewJson (Join-Path $Evidence ($phase+'.started.json')) @{phase=$phase;mqb_ceiling=$budget}
        $script:admitted+=$budget
        if ($phase -ceq $script:failBuild) { throw 'SYNTHETIC build failure' }
        Write-NewJson (Join-Path $Evidence ($phase+'.finished.json')) @{phase=$phase;success=$true}
    }
    foreach ($side in @('baseline','candidate')) {
        Copy-Item -LiteralPath (Join-Path $OriginalPreparation "source-$side.zip") -Destination (Join-Path $Evidence "source-$side.zip")
        [IO.File]::WriteAllText((Join-Path $Evidence "bin/$side.exe"),('SYNTHETIC NOT EXECUTABLE '+$side))
        [IO.File]::WriteAllText((Join-Path $Evidence "$side-build.log"),'SYNTHETIC: no compiler/build/help invoked')
    }
    [IO.File]::WriteAllText((Join-Path $Evidence 'seed.log'),'SYNTHETIC no seed execution')
    Write-NewJson (Join-Path $Evidence 'seed-identity.json') @{sha256='f58d78066a2ec6c4d174e39fcb9c748776cdc4a6137a0d5a592a216b3eba6dd0'}
    $after=$script:environment | ConvertTo-Json -Depth 12 | ConvertFrom-Json -AsHashtable
    if ($script:driftPreparation) { $after.powershell='CHANGED' }
    Write-NewJson (Join-Path $Evidence 'environment-after.json') $after
}
function Invoke-LegacyBoundary($Executable,$Cwd,$Argv,$Prefix) {
    Assert $script:readyObserved 'Launcher reached before actual Python ready gate'
    $script:fakeCalls++
    $target=Join-Path $Cwd '.mqb/bin/timing_bench.exe';$prime=-not (Test-Path -LiteralPath $target)
    if ($prime) {
        $null=New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target))
        [IO.File]::WriteAllText($target,'SYNTHETIC NOT EXECUTABLE')
        if (-not $script:missingCache) {
            $cacheDir=Join-Path $Cwd '.mqb/cache/toolchain';$null=New-Item -ItemType Directory -Path $cacheDir
            $leaf=if ($script:fallbackOnly) {'msvc-auto-x64-x64.mqbcache'} else {'vs-x64.cache'}
            [IO.File]::WriteAllText((Join-Path $cacheDir $leaf),"MQB_TOOLCHAIN_CACHE_V9`nvc_tools_root `"C:/SYNTHETIC`"`n",[Text.UTF8Encoding]::new($false))
        }
    }
    $lines=if ($prime -or $script:badNoop) {@('[compile] main.cpp','[compile] helper.cpp','[link] timing_bench.exe')}
           else {@('[up-to-date] 2 translation units','[up-to-date] timing_bench.exe')}
    [long]$start=$script:attempted*1000000
    [long]$ticks=if ($Executable.EndsWith('candidate.exe')) {$script:candidateTicks} else {100000}
    return [ordered]@{clock=@{frequency=10000000;outer_start=$start;native_start=($start+1);native_end=($start+1+$ticks);outer_end=($start+2+$ticks)}
        exit_code=$(if ($script:fakeCalls -eq $script:failCall) {37} else {0});error=$null
        output_format='powershell_merged_lines';output_lines=$lines;root_times=$null;cleanup=$null}
}
function Case([string]$Name,[scriptblock]$Body) {
    $script:evidence=Join-Path $OutputRoot ('case-'+($cases.Count+1));$null=New-Item -ItemType Directory -Path $evidence
    Copy-Item $sourceZip (Join-Path $evidence 'source-execution.zip')
    & $script:realWrite (Join-Path $evidence 'request.json') @{
        GITHUB_REPOSITORY='Iviesever/msvc-quick-build';GITHUB_ACTIONS='true';GITHUB_EVENT_NAME='workflow_dispatch'
        GITHUB_REF='refs/heads/main';GITHUB_SHA=$commit;GITHUB_WORKFLOW_SHA=$commit
        GITHUB_WORKFLOW_REF='Iviesever/msvc-quick-build/.github/workflows/v9-noop-samejob.yml@refs/heads/main'
        GITHUB_RUN_ID='900';GITHUB_RUN_NUMBER='1';GITHUB_RUN_ATTEMPT='1';GITHUB_JOB='validation'
        RUNNER_ENVIRONMENT='github-hosted';RUNNER_OS='Windows';RUNNER_ARCH='X64';REVIEWED_COMMIT=$commit
        ALLOCATION='v9-reader-samejob-001';ACCEPT_AUTOMATIC_GATE='true'}
    $script:environment=[ordered]@{image='SYNTHETIC';powershell='SYNTHETIC';tools=@([ordered]@{root='C:/SYNTHETIC'
        files=[ordered]@{'cl.exe'=('f'*64);'link.exe'=('f'*64);'lib.exe'=('f'*64);'c1xx.dll'=('f'*64);'c2.dll'=('f'*64)}})
        sdk_versions=@('SYNTHETIC');ambient_sha256=('a'*64)}
    $script:badJournal='';$script:badCheck='';$script:failBuild='';$script:badSource=$false;$script:lowSpace=$false
    $script:driftPreparation=$false;$script:driftBefore=$false;$script:driftAfter=$false
    $script:missingCache=$false;$script:fallbackOnly=$false;$script:badNoop=$false;$script:readyObserved=$false
    $script:failCall=0;$script:fakeCalls=0;$script:candidateTicks=90000
    try { & $Body;$cases.Add(@{name=$Name;passed=$true;error=$null});Write-Host "PASS: $Name" }
    catch { ++$script:failures;$cases.Add(@{name=$Name;passed=$false;error=$_.ToString()});Write-Host "FAIL: $Name :: $_" }
}
function Run { Invoke-SameJobPipeline $repo 'SYNTHETIC SOURCE ROOT' $evidence }
function Audit([int]$Expected) {
    & python -B (Join-Path $PSScriptRoot 'v9_noop_samejob.py') audit --root $evidence --repo $repo --output (Join-Path $evidence 'decision.json')
    Assert ($LASTEXITCODE -eq $Expected) 'Wrong real offline gate exit'
}
Case 'full pipeline uses real Python freeze ready audit and actual file projection' {
    Run;Audit 0
    Assert ($script:admitted -eq 5 -and $script:fakeCalls -eq 16) 'Wrong separate call budgets'
    $m=Join-Path $evidence 'measurement'
    Assert (@(Get-ChildItem (Join-Path $m 'calls') -File).Count -eq 64) 'Missing call ledger'
    Assert (@(Get-ChildItem (Join-Path $m 'cache-projections') -File).Count -eq 8) 'Missing real projections'
}
Case 'strict equality boundary remains noncrossed' {$script:candidateTicks=110000;Run;Audit 0}
Case 'strictly crossed gate preserves decision and returns failure' {$script:candidateTicks=110001;Run;Audit 1}
Case 'wrong source refuses before all fake builds' {
    $script:badSource=$true;Refuses {Run} 'source refusal';Assert ($admitted -eq 0 -and $fakeCalls -eq 0) 'Source refusal continued'
}
Case 'low disk refuses before fake seed' {
    $script:lowSpace=$true;Refuses {Run} 'low space';Assert ($admitted -eq 0 -and $fakeCalls -eq 0) 'Space refusal continued'
}
Case 'candidate build failure forbids automatic gate or measurement' {
    $script:failBuild='candidate';Refuses {Run} 'build failure';Assert ($fakeCalls -eq 0 -and -not $readyObserved) 'Build failure continued'
}
Case 'actual Python rejects preparation environment drift' {
    $script:driftPreparation=$true;Refuses {Run} 'freeze refused';Assert ($fakeCalls -eq 0) 'Preparation drift continued'
}
Case 'lost preparation completion blocks freeze' {
    $script:badJournal='completion.json';Refuses {Run} 'journal failure';Assert ($fakeCalls -eq 0) 'Journal loss continued'
}
Case 'failed freeze blocks ready and first prime' {
    $script:badCheck='freeze';Refuses {Run} 'freeze refusal';Assert ($fakeCalls -eq 0 -and -not $readyObserved) 'Freeze failure continued'
}
Case 'failed ready blocks first prime' {
    $script:badCheck='ready';Refuses {Run} 'ready refusal';Assert ($fakeCalls -eq 0) 'Ready failure continued'
}
Case 'pre-measurement drift refuses after frozen manifest but before prime' {
    $script:driftBefore=$true;Refuses {Run} 'environment changed';Assert ($fakeCalls -eq 0) 'Environment refusal continued'
}
Case 'post-measurement drift keeps all records but no accepted result' {
    $script:driftAfter=$true;Refuses {Run} 'environment changed';Assert ($fakeCalls -eq 16) 'Wrong post-drift count';Audit 2
}
Case 'missing CLI cache preserves the first prime then stops' {
    $script:missingCache=$true;Refuses {Run} 'vs-x64.cache';Assert ($fakeCalls -eq 1) 'Missing cache continued';Audit 2
}
Case 'bare locator filename never replaces CLI destination' {
    $script:fallbackOnly=$true;Refuses {Run} 'vs-x64.cache';Assert ($fakeCalls -eq 1) 'Fallback cache continued'
}
Case 'final compilation is not silently accepted as a no-op' {
    $script:badNoop=$true;Refuses {Run} 'Not the fixed no-op';Assert ($fakeCalls -eq 2) 'Bad no-op continued'
}
Case 'native failure preserves prefix and releases binary pins' {
    $script:failCall=2;Refuses {Run} 'Original call failed';Assert ($fakeCalls -eq 2) 'Failed process continued'
    foreach ($side in @('baseline','candidate')) {
        $pin=[IO.File]::Open((Join-Path $evidence "preparation/bin/$side.exe"),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$pin.Dispose()
    }
}
Case 'result journal loss stops without another invocation' {
    $script:badJournal='01.result.json';Refuses {Run} 'journal failure';Assert ($fakeCalls -eq 1) 'Journal failure continued'
}
Case 'phase directory cannot be resumed' {
    $null=New-Item -ItemType Directory -Path (Join-Path $evidence 'preparation')
    Refuses {Run} 'Fresh phase';Assert ($fakeCalls -eq 0) 'Resumed old phase'
}
& $script:realWrite (Join-Path $OutputRoot 'summary.json') @{schema=1;tests=$cases.Count;failures=$failures;cases=@($cases.ToArray())
    real_mqb_calls=0;real_etw_sessions=0;scope='Real pipeline/Python/manifest/projection; synthetic preparation, launcher and environment'}
if ($failures) { throw "$failures same-job control tests failed." }
exit 0

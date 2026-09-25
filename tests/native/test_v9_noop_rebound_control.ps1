# Real rebound orchestrator, loop, manifest and projection; only launcher/host are fake.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PlanPath,[Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Fresh control directory required.' }
$null=New-Item -ItemType Directory -Path $OutputRoot
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'run_v9_noop_rebound.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$import=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq 'Import-V9ReboundDefinitions' })
if ($import.Count -ne 1) { throw 'Missing importer.' }
. ([scriptblock]::Create($import[0].Extent.Text))
Import-V9ReboundDefinitions (Join-Path $PSScriptRoot 'run_v9_noop_rebound.ps1') @('Invoke-V9ReboundCalls','Assert-V9ReboundEnvironment')
Import-V9ReboundDefinitions (Join-Path $PSScriptRoot 'run_v9_noop_validation.ps1') @('Invoke-V9Calls','Assert-V9Call','Get-V9CacheProjection')
Import-V9ReboundDefinitions (Join-Path $PSScriptRoot 'collect_external_noop_boundary.ps1') @('Write-NewJson','Get-Digest','Get-FileManifest')
$script:realWrite=${function:Write-NewJson}
$cases=[Collections.Generic.List[object]]::new(); $failures=0
function Assert([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Refuses([scriptblock]$Action,[string]$Pattern) {
    $caught=$null
    try { & $Action } catch { $caught=$_.ToString() }
    Assert ($null -ne $caught -and $caught -match $Pattern) "Expected $Pattern; got $caught"
}
function Write-NewJson($Path,$Value) {
    if ([IO.Path]::GetFileName($Path) -ceq $script:badJournal) { throw 'SYNTHETIC journal failure' }
    & $script:realWrite $Path $Value
}
function Get-V9Environment { return $script:environment }
function Assert-V9Space($Path,$Minimum) { if ($script:lowSpace) { throw 'SYNTHETIC low space' } }
function Write-SyntheticCache([string]$Fixture,[string]$ToolRoot) {
    $directory=Join-Path $Fixture '.mqb/cache/toolchain'
    $null=New-Item -ItemType Directory -Path $directory -Force
    $leaf=if ($script:fallbackOnly) {'msvc-auto-x64-x64.mqbcache'} else {'vs-x64.cache'}
    $escaped=$ToolRoot.Replace('\','\\').Replace('"','\"')
    $text="MQB_TOOLCHAIN_CACHE_V9`ntarget `"x64`"`nhost `"x64`"`npreference 0`nvc_tools_root `"$escaped`"`n" +
        "binary_stamp `"SYNTHETIC`"`nenvironment 1`nenv_name `"INCLUDE`"`nenv_value `"SYNTHETIC-PRIVATE`"`n" +
        "ambient_path `"SYNTHETIC`"`neffective_path `"SYNTHETIC`"`n"
    [IO.File]::WriteAllText((Join-Path $directory $leaf),$text,[Text.UTF8Encoding]::new($false))
}
function Invoke-LegacyBoundary($Executable,$Cwd,$Argv,$Prefix) {
    $script:fakeCalls.Add([object]@{executable=$Executable;cwd=$Cwd;argv=@($Argv)})
    $target=Join-Path $Cwd '.mqb/bin/timing_bench.exe'
    $prime=-not (Test-Path -LiteralPath $target)
    if ($prime) {
        $null=New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target))
        [IO.File]::WriteAllText($target,'SYNTHETIC NOT EXECUTABLE')
        if (-not $script:missingCache) { Write-SyntheticCache $Cwd 'C:/SYNTHETIC' }
    }
    $lines=if ($prime -or $script:finalCompile) { @('[compile] main.cpp','[compile] helper.cpp','[link] timing_bench.exe') }
           else { @('[up-to-date] 2 translation units','[up-to-date] timing_bench.exe') }
    [long]$start=$script:attempted*10000
    return [ordered]@{clock=@{frequency=10000000;outer_start=$start;native_start=($start+1);native_end=($start+100);outer_end=($start+101)}
        exit_code=$(if ($script:attempted -eq $script:failCall) {37} else {0});error=$null
        output_format='powershell_merged_lines';output_lines=$lines;root_times=$null;cleanup=$null}
}
function Case([string]$Name,[scriptblock]$Body) {
    $script:root=Join-Path $OutputRoot ('case-'+($cases.Count+1)); $script:inputs=Join-Path $root 'input'
    foreach ($directory in @($root,$inputs,(Join-Path $inputs 'bin'),(Join-Path $root 'calls'),(Join-Path $root 'fixtures'),(Join-Path $root 'cache-projections'))) {
        $null=New-Item -ItemType Directory -Path $directory
    }
    foreach ($side in @('baseline','candidate')) { [IO.File]::WriteAllText((Join-Path $inputs "bin/$side.exe"),'SYNTHETIC '+$side) }
    $script:environment=[ordered]@{image='SYNTHETIC';powershell='7';tools=@([ordered]@{root='C:/SYNTHETIC';files=@{}})
        sdk_versions=@('SYNTHETIC');ambient_sha256=('a'*64)}
    $script:manifest=[pscustomobject]@{environment=($environment | ConvertTo-Json -Depth 12 | ConvertFrom-Json)}
    $script:plan=@{protocol=(Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json)
        binaries=@{baseline=(Get-Digest (Join-Path $inputs 'bin/baseline.exe'));candidate=(Get-Digest (Join-Path $inputs 'bin/candidate.exe'))}}
    $script:badJournal='';$script:lowSpace=$false;$script:missingCache=$false;$script:fallbackOnly=$false
    $script:finalCompile=$false;$script:failCall=0;$script:fakeCalls=[Collections.Generic.List[object]]::new()
    try { & $Body; $cases.Add(@{name=$Name;passed=$true;error=$null});Write-Host "PASS: $Name" }
    catch { ++$script:failures; $cases.Add(@{name=$Name;passed=$false;error=$_.ToString()});Write-Host "FAIL: $Name :: $_" }
}
function Run-Rebound { Invoke-V9ReboundCalls $plan $root $inputs $manifest }
Case 'complete new sixteen calls use actual manifest and projection functions' {
    Run-Rebound
    Assert ($fakeCalls.Count -eq 16 -and $attempted -eq 16) 'Unexpected invocation count'
    Assert (@(Get-ChildItem (Join-Path $root 'calls') -File).Count -eq 64) 'Incomplete call ledger'
    Assert (@(Get-ChildItem (Join-Path $root 'cache-projections') -File).Count -eq 8) 'Incomplete projections'
    $done=Get-Content (Join-Path $root 'completion.json') -Raw | ConvertFrom-Json
    Assert ($done.status -ceq 'calls_complete_unreviewed' -and -not $done.clears_hold) 'False verdict'
}
Case 'image mismatch refuses before the first prime' {
    $script:environment.image='OTHER'
    Refuses { Run-Rebound } 'Unmatched host field'
    Assert ($fakeCalls.Count -eq 0 -and $attempted -eq 0) 'Invoked despite host mismatch'
}
Case 'key tool mismatch refuses before the first prime' {
    $script:environment.tools=@(@{root='OTHER';files=@{}})
    Refuses { Run-Rebound } 'Unmatched host field'
    Assert ($fakeCalls.Count -eq 0) 'Invoked despite tool mismatch'
}
Case 'free-space refusal records zero dispatch and does not request another host' {
    $script:lowSpace=$true
    Refuses { Run-Rebound } 'low space'
    Assert ($fakeCalls.Count -eq 0 -and $attempted -eq 0) 'Low-space call admitted'
}
Case 'wrong frozen binary refuses without launcher call' {
    $script:plan.binaries.baseline='0'*64
    Refuses { Run-Rebound } 'Frozen binary changed'
    Assert ($fakeCalls.Count -eq 0) 'Changed binary invoked'
}
Case 'original missing cache shape retains one prime and stops' {
    $script:missingCache=$true
    Refuses { Run-Rebound } 'vs-x64.cache'
    Assert ($fakeCalls.Count -eq 1 -and @(Get-ChildItem (Join-Path $root 'calls') -File).Count -eq 4) 'First prefix not retained'
    Assert (-not (Test-Path (Join-Path $root 'environment-after.json'))) 'Invented final environment'
}
Case 'locator fallback cannot replace the explicit CLI destination' {
    $script:fallbackOnly=$true
    Refuses { Run-Rebound } 'vs-x64.cache'
    Assert ($fakeCalls.Count -eq 1) 'Wrong cache continued'
}
Case 'final compilation is not accepted as a no-op' {
    $script:finalCompile=$true
    Refuses { Run-Rebound } 'Not the fixed no-op'
    Assert ($fakeCalls.Count -eq 2) 'Final compile was continued'
}
Case 'native failure preserves result and releases all binary pins' {
    $script:failCall=2
    Refuses { Run-Rebound } 'Original call failed'
    Assert ($fakeCalls.Count -eq 2) 'Original failure ignored'
    foreach ($side in @('baseline','candidate')) {
        $pin=[IO.File]::Open((Join-Path $inputs "bin/$side.exe"),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        $pin.Dispose()
    }
}
Case 'result journal failure does not dispatch the next call' {
    $script:badJournal='01.result.json'
    Refuses { Run-Rebound } 'journal failure'
    Assert ($fakeCalls.Count -eq 1) 'Missing journal continued'
}
Case 'existing fixture is never reused as a warmup' {
    $null=New-Item -ItemType Directory -Path (Join-Path $root 'fixtures/1-baseline')
    Refuses { Run-Rebound } 'Fresh fixture required'
    Assert ($fakeCalls.Count -eq 0) 'Existing fixture consumed'
}
Case 'seventeenth row is refused after exact sixteen calls' {
    $script:plan.protocol.rows+=@([pscustomobject]@{sequence=17;phase='prime';pair=5;side='baseline';fixture='5-baseline'})
    Refuses { Run-Rebound } 'Fixed 16-call/order ceiling'
    Assert ($fakeCalls.Count -eq 16) 'Seventeenth call dispatched'
}
Case 'actual projection handles UTF8 and quoted escaped root, exports no private values' {
    $fixture=Join-Path $root 'projection'
    $value='C:\Synthetic\日本\"quoted"'
    Write-SyntheticCache $fixture $value
    $projection=Get-V9CacheProjection $fixture
    $file=Join-Path $fixture '.mqb/cache/toolchain/vs-x64.cache'
    Assert ($projection.root -ceq $value.Replace('\','/')) 'Wrong escaped root'
    Assert ($projection.sha256 -ceq (Get-Digest $file) -and $projection.bytes -eq (Get-Item $file).Length) 'Wrong actual bytes/hash'
    Assert (@($projection.Keys).Count -eq 4 -and ($projection | ConvertTo-Json) -notmatch 'PRIVATE') 'Private value leaked'
}
Case 'CRLF cache is not represented as the binary LF writer output' {
    $fixture=Join-Path $root 'projection';Write-SyntheticCache $fixture 'C:/SYNTHETIC'
    $file=Join-Path $fixture '.mqb/cache/toolchain/vs-x64.cache'
    [IO.File]::WriteAllText($file,([IO.File]::ReadAllText($file)).Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
    Refuses { Get-V9CacheProjection $fixture } 'canonical V9 writer'
}
& $script:realWrite (Join-Path $OutputRoot 'summary.json') @{
    schema=1;tests=$cases.Count;failures=$failures;cases=@($cases.ToArray())
    real_mqb_calls=0;real_etw_sessions=0;scope='actual rebound and retained functions; synthetic launcher/host only'}
if ($failures) { throw "$failures rebound control failures." }
exit 0

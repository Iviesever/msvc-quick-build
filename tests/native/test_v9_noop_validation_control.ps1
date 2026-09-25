# Extract actual functions; all MQB invocations are in-process test doubles.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PlanPath,[Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Fresh contract output required.' }
$null=New-Item -ItemType Directory -Path $OutputRoot
function Import-Definition([string]$Path,[string[]]$Names) {
    $t=$null;$e=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$t,[ref]$e)
    if ($e.Count) { throw ($e | Out-String) }
    foreach ($name in $Names) {
        $d=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name })
        if ($d.Count -ne 1) { throw "Missing actual definition: $name" }
        $text=$d[0].Extent.Text -replace ('^function\s+'+[regex]::Escape($name)+'(?=[\s(])'),('function script:'+$name)
        . ([scriptblock]::Create($text))
    }
}
Import-Definition (Join-Path $PSScriptRoot 'run_v9_noop_validation.ps1') @('Invoke-V9Calls','Assert-V9Call','Invoke-V9Preparation','Get-V9CacheProjection')
$cases=[Collections.Generic.List[object]]::new();$failures=0
function Assert([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Refuses([scriptblock]$Action,[string]$Pattern) {
    $errorText=$null
    try { & $Action } catch { $errorText=$_.ToString() }
    Assert ($null -ne $errorText -and $errorText -match $Pattern) "Expected $Pattern; got $errorText"
}
function Write-NewJson($Path,$Value) {
    if ([IO.Path]::GetFileName($Path) -ceq $script:badJournal) { throw 'SYNTHETIC journal failure' }
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew)
    try {
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 30))
        $stream.Write($bytes,0,$bytes.Length)
    } finally {$stream.Dispose()}
}
function Get-Digest($Path) {
    if ($Path.EndsWith('.cache') -or $Path.EndsWith('.mqbcache')) {
        return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($Path))).ToLowerInvariant()
    }
    if ($script:badBinary) { return '0'*64 }
    return 'a'*64
}
function Get-FileManifest($Path) {
    $files=@([ordered]@{path='helper.cpp';size=1;mtime_ticks=1;sha256='x'},[ordered]@{path='main.cpp';size=1;mtime_ticks=1;sha256='y'})
    if ($script:built.ContainsKey($Path)) {
        $files+=@([ordered]@{path='.mqb/bin/timing_bench.exe';size=1;mtime_ticks=1;sha256='z'})
        $cache=Join-Path $Path '.mqb/cache/toolchain/vs-x64.cache'
        if (Test-Path -LiteralPath $cache -PathType Leaf) {
            $files+=@([ordered]@{path='.mqb/cache/toolchain/vs-x64.cache';size=[long](Get-Item -LiteralPath $cache).Length
                mtime_ticks=1;sha256=(Get-Digest $cache)})
        }
        if ($script:badFiles -and $script:attempted -eq 2) {$files[-1].mtime_ticks=2}
    }
    return ,$files
}
function Assert-V9Space($Path,$Minimum) { if ($script:lowSpace) { throw 'SYNTHETIC free-space failure' } }
# Only the process is simulated: create a real, synthetic V9 file at the CLI's
# independently specified destination, and use the actual projection on every prime.
function Write-SyntheticCache([string]$Fixture) {
    if ($script:missingCache) { return }
    $name=if ($script:fallbackOnly) {'msvc-auto-x64-x64.mqbcache'} else {'vs-x64.cache'}
    $dir=Join-Path $Fixture '.mqb/cache/toolchain'
    $null=New-Item -ItemType Directory -Path $dir -Force
    $tool=if ($script:badRoot) {'C:/OTHER'} else {'C:/SYNTHETIC'}
    $value=if ($script:badCache -and $script:attempted -ge 3) {'DIFFERENT-SYNTHETIC-VALUE'} else {'SYNTHETIC-PRIVATE-VALUE'}
    $text="MQB_TOOLCHAIN_CACHE_V9`ntarget `"x64`"`nhost `"x64`"`npreference 0`nvc_tools_root `"$tool`"`n" +
        "binary_stamp `"SYNTHETIC`"`nenvironment 1`nenv_name `"INCLUDE`"`nenv_value `"$value`"`n" +
        "ambient_path `"SYNTHETIC`"`neffective_path `"SYNTHETIC`"`n"
    [IO.File]::WriteAllText((Join-Path $dir $name),$text,[Text.UTF8Encoding]::new($false))
}
function Invoke-LegacyBoundary($Executable,$Cwd,$Argv,$Prefix) {
    $script:fakeCalls.Add(@{exe=$Executable;cwd=$Cwd;argv=@($Argv)})
    $prime=-not $script:built.ContainsKey($Cwd);$script:built[$Cwd]=$true
    if ($prime) { Write-SyntheticCache $Cwd }
    $lines=if ($prime -or ($script:badNoop -and $script:attempted -eq 2)) {
        @('[compile] main.cpp','[compile] helper.cpp','[link] timing_bench.exe')
    } else {@('[up-to-date] 2 translation units','[up-to-date] timing_bench.exe')}
    return [ordered]@{clock=@{frequency=10000;outer_start=1;native_start=2;native_end=3;outer_end=4}
        exit_code=$(if ($script:attempted -eq $script:failCall) {37} else {0});error=$null
        output_format='powershell_merged_lines';output_lines=$lines;root_times=$null;cleanup=$null}
}
function Get-V9Environment {
    return [ordered]@{image='SYNTHETIC';powershell='7';tools=@([ordered]@{root='C:/SYNTHETIC';files=@{}})
        sdk_versions=@('fake');ambient_sha256='a'*64}
}
function git {
    $i=[array]::IndexOf($args,'-o')
    Assert ($i -ge 0) 'Only fake git archive expected'
    [IO.File]::WriteAllText($args[$i+1],'SYNTHETIC SOURCE')
    $global:LASTEXITCODE=0
}
function Case([string]$Name,[scriptblock]$Action) {
    $script:root=Join-Path $OutputRoot ('case-{0:d2}' -f ($cases.Count+1))
    $null=New-Item -ItemType Directory -Path $root
    foreach ($folder in @('calls','cache-projections','fixtures')) {$null=New-Item -ItemType Directory -Path (Join-Path $root $folder)}
    $script:plan=@{protocol=(Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json);binaries=@{baseline='a'*64;candidate='a'*64}}
    $script:attempted=0;$script:admitted=0;$script:badJournal='';$script:badBinary=$false;$script:badCache=$false
    $script:badFiles=$false;$script:badNoop=$false;$script:badRoot=$false;$script:lowSpace=$false;$script:failCall=0
    $script:missingCache=$false;$script:fallbackOnly=$false
    $script:built=@{};$script:fakeCalls=[Collections.Generic.List[object]]::new()
    try { & $Action; $cases.Add(@{name=$Name;passed=$true;error=$null});Write-Host "PASS: $Name" }
    catch { ++$script:failures;$cases.Add(@{name=$Name;passed=$false;error=$_.ToString()});Write-Host "FAIL: $Name :: $_" }
}
function Collect { Invoke-V9Calls $plan $root (Join-Path $root 'SYNTHETIC-input') (Get-V9Environment) }
Case 'exact16 plan;8 primes8 noops;64 raw call JSONs;8 cache projections' {
    Collect
    Assert ($attempted -eq 16 -and $fakeCalls.Count -eq 16) 'Call budget'
    Assert (@(Get-ChildItem (Join-Path $root 'calls') -File).Count -eq 64) 'Journal count'
    Assert (@(Get-ChildItem (Join-Path $root 'cache-projections') -File).Count -eq 8) 'Cache count'
    Assert (@($fakeCalls | Where-Object { $_.argv -contains '--help' -or $_.argv -contains '--timings' }).Count -eq 0) 'Hidden probe'
}
Case 'hard ceiling rejects extra17 before dispatch' {
    $plan.protocol.rows+=@{sequence=17;phase='prime';fixture='extra';side='baseline'}
    Refuses {Collect} '16-call';Assert ($fakeCalls.Count -eq 16) 'Extra dispatch'
}
Case 'wrong row sequence refuses before first native call' {
    $plan.protocol.rows[0].sequence=2;Refuses {Collect} 'order';Assert ($attempted -eq 0) 'Wrong call'
}
Case 'first prime failure preserves result and stops' {
    $script:failCall=1;Refuses {Collect} 'Original call failed'
    Assert ($fakeCalls.Count -eq 1 -and (Test-Path (Join-Path $root 'calls/01.result.json'))) 'Lost failed result'
}
Case 'no-op failure never advances to another prime' {
    $script:failCall=2;Refuses {Collect} 'Original call failed';Assert ($fakeCalls.Count -eq 2) 'Refill'
}
Case 'started journal failure suppresses dispatch' {
    $script:badJournal='01.started.json';Refuses {Collect} 'journal';Assert ($fakeCalls.Count -eq 0) 'Unjournaled call'
}
Case 'result journal failure stops without retrying the call' {
    $script:badJournal='01.result.json';Refuses {Collect} 'journal';Assert ($fakeCalls.Count -eq 1) 'Retried'
}
Case 'cache mismatch stops after candidate prime' {
    $script:badCache=$true;Refuses {Collect} 'cache input differs';Assert ($fakeCalls.Count -eq 3) 'Unexpected continuation'
}
Case 'unknown tool root refuses before first final' {
    $script:badRoot=$true;Refuses {Collect} 'selected toolchain';Assert ($fakeCalls.Count -eq 1) 'Final admitted'
}
Case 'changed binary refuses before first dispatch' {
    $script:badBinary=$true;Refuses {Collect} 'binary changed';Assert ($fakeCalls.Count -eq 0) 'Bad EXE admitted'
}
Case 'unexpected rebuild is not counted as valid noop' {
    $script:badNoop=$true;Refuses {Collect} 'fixed no-op';Assert ($fakeCalls.Count -eq 2) 'Continued'
}
Case 'file mutation is retained as failure' {
    $script:badFiles=$true;Refuses {Collect} 'changed fixture';Assert ($fakeCalls.Count -eq 2) 'Continued'
}
Case 'low space refuses before prime' {
    $script:lowSpace=$true;Refuses {Collect} 'free-space';Assert ($fakeCalls.Count -eq 0) 'Low-space dispatch'
}
Case 'existing fixture cannot resume' {
    $null=New-Item -ItemType Directory -Path (Join-Path $root 'fixtures/1-baseline')
    Refuses {Collect} 'Fresh fixture';Assert ($fakeCalls.Count -eq 0) 'Resumed'
}
Case 'actual projection exports no private environment value' {
    $fixture=Join-Path $root 'private';$null=New-Item -ItemType Directory -Path (Join-Path $fixture '.mqb/cache/toolchain')
    $path=Join-Path $fixture '.mqb/cache/toolchain/vs-x64.cache'
    [IO.File]::WriteAllText($path,"MQB_TOOLCHAIN_CACHE_V9`nvc_tools_root `"C:/SYNTHETIC`"`nenv_value `"SECRET-SYNTHETIC-VALUE`"`n")
    $result=Get-V9CacheProjection $fixture
    Assert ($result.root -ceq 'C:/SYNTHETIC') 'Wrong root'
    Assert (($result | ConvertTo-Json) -notmatch 'SECRET') 'Private value disclosed'
}
Case 'missing CLI cache preserves first prime and blocks every final' {
    $script:missingCache=$true
    Refuses {Collect} 'vs-x64.cache'
    Assert ($attempted -eq 1 -and $fakeCalls.Count -eq 1) 'Continued after missing CLI cache'
    foreach ($suffix in @('before','started','result','after')) {
        Assert (Test-Path (Join-Path $root ('calls/01.'+$suffix+'.json'))) 'Lost first-prime evidence'
    }
    Assert (-not (Test-Path (Join-Path $root 'calls/02.started.json'))) 'Final started after failure'
}
Case 'bare locator fallback cache is not accepted in place of CLI override' {
    $script:fallbackOnly=$true
    Refuses {Collect} 'vs-x64.cache'
    Assert ($attempted -eq 1 -and $fakeCalls.Count -eq 1) 'Fallback silently accepted'
}
Case 'real cache projection hashes bytes and excludes environment values' {
    $fixture=Join-Path $root 'projection with spaces'
    Write-SyntheticCache $fixture
    $path=Join-Path $fixture '.mqb/cache/toolchain/vs-x64.cache'
    $result=Get-V9CacheProjection $fixture
    Assert ($result.bytes -eq [IO.File]::ReadAllBytes($path).Length) 'Not actual size'
    $expected=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($path))).ToLowerInvariant()
    Assert ($result.sha256 -ceq $expected -and $result.root -ceq 'C:/SYNTHETIC') 'Not actual digest/root'
    Assert ((@($result.Keys | Sort-Object) -join ',') -ceq 'bytes,root,schema,sha256') 'Projection leaked fields'
    Assert (($result | ConvertTo-Json) -notmatch 'PRIVATE-VALUE') 'Leaked cache value'
}
Case 'real projection refuses wrong magic duplicate root and invalid UTF8' {
    $fixture=Join-Path $root 'projection';Write-SyntheticCache $fixture
    $path=Join-Path $fixture '.mqb/cache/toolchain/vs-x64.cache'
    $original=[IO.File]::ReadAllText($path)
    [IO.File]::WriteAllText($path,$original.Replace('CACHE_V9','CACHE_V8'))
    Refuses {Get-V9CacheProjection $fixture} 'canonical V9'
    [IO.File]::WriteAllText($path,($original+"vc_tools_root `"C:/OTHER`"`n"))
    Refuses {Get-V9CacheProjection $fixture} 'unique tool root'
    [IO.File]::WriteAllBytes($path,[byte[]]@(0xff,0xfe,0xff))
    Refuses {Get-V9CacheProjection $fixture} '.'
}
Case 'actual size accounting accepts ordered keys and enforces exact64MiB' {
    $row=@{phase='prime'};$record=@{error=$null;exit_code=0;output_lines=@('[compile] main.cpp','[compile] helper.cpp','[link] timing_bench.exe')}
    foreach ($item in @([ordered]@{size=[long]64MB},@{size=[long]64MB},[pscustomobject]@{size=[long]64MB})) {
        Assert-V9Call $row $record @(1,2) @($item)
    }
    Refuses {Assert-V9Call $row $record @(1,2) @([ordered]@{size=[long]64MB},[ordered]@{size=1})} 'checkpoint exceeded'
    foreach ($value in @(-1,$true,'1')) {
        Refuses {Assert-V9Call $row $record @(1,2) @([ordered]@{size=$value})} 'Invalid fixture size'
    }
}
Case 'preparation uses only one seed admission and two fixed helper calls' {
    $repo=Join-Path $root 'fake-repo';$src=Join-Path $root 'fake-src';$evidence=Join-Path $root 'prep'
    $null=New-Item -ItemType Directory -Path (Join-Path $repo 'tests/native')
    foreach ($side in @('baseline','candidate')) {$null=New-Item -ItemType Directory -Path (Join-Path $src $side)}
    $null=New-Item -ItemType Directory -Path $evidence
    $oldTemp=$env:RUNNER_TEMP;$env:RUNNER_TEMP=$root
    [IO.File]::WriteAllText((Join-Path $repo 'tests/native/acquire_seed.ps1'),@'
param($RepoRoot,$OutputRoot)
$null=New-Item -ItemType Directory -Path $OutputRoot
[IO.File]::WriteAllText((Join-Path $OutputRoot 'mqb-seed.exe'),'FAKE NOT EXECUTABLE')
$global:LASTEXITCODE=0
'@)
    [IO.File]::WriteAllText((Join-Path $repo 'tests/native/build_mqb.ps1'),@'
param($BuilderMqbPath,$RepoRoot,$Version,$Configuration,$OutputPath)
if ($Version -cne '5.6.0' -or $Configuration -cne 'Release') {throw 'Changed build parameters'}
$null=New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($OutputPath)) -Force
[IO.File]::WriteAllText($OutputPath,'FAKE NOT EXECUTABLE')
$global:LASTEXITCODE=0
'@)
    try { Invoke-V9Preparation $repo $src $evidence } finally {$env:RUNNER_TEMP=$oldTemp}
    Assert ($admitted -eq 5 -and $fakeCalls.Count -eq 0) 'Preparation/measurement budgets mixed'
    Assert ((Test-Path (Join-Path $evidence 'candidate.finished.json'))) 'Missing preparation completion'
}
Write-NewJson (Join-Path $OutputRoot 'result.json') @{schema=1;tests=$cases.Count;failures=$failures;cases=@($cases.ToArray())
    real_mqb_calls=0;real_etw_sessions=0;scope='actual extracted control functions, in-process doubles only'}
if ($failures) {throw "$failures contracts failed"}
exit 0

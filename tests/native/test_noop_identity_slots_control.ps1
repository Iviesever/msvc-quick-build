# Definition-only module plus explicit synthetic callbacks. No native launcher is imported.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputRoot,[Parameter(Mandatory)][string]$PlanPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$root=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $root) { throw 'Fresh synthetic evidence root required.' }
$null=New-Item -ItemType Directory -Path $root
Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_runtime.psm1') -Scope Local -Force
$cases=[Collections.Generic.List[object]]::new();$failures=0
function Assert([bool]$Ok,[string]$Message) { if (-not $Ok) { throw $Message } }
function Refuses([scriptblock]$Action,[string]$Pattern) {
    $errorText=$null;try { & $Action } catch { $errorText=$_.ToString() }
    Assert ($null -ne $errorText -and $errorText -match $Pattern) "Expected $Pattern; got $errorText"
}
function Write-SyntheticSlotJson($State,$Path,$Value) {
    $State.context.events.Add('journal')
    if ([IO.Path]::GetFileName($Path) -ceq $State.context.failJournal) { throw 'SYNTHETIC journal failure' }
    Write-NewJson $Path $Value
}
function Get-SyntheticSlotDigest($State,$Path) { $State.context.events.Add('hash'); return (Get-Digest $Path) }
function Get-SyntheticSlotManifest($State,$Path) { $State.context.events.Add('manifest'); return ,(Get-FileManifest $Path) }
function Get-SyntheticSlotFreeBytes($State,$Root) { $State.context.events.Add('budget'); return $State.context.freeBytes }
function Invoke-SyntheticSlotLaunch($State,$Executable,$Cwd,$Argv,$Prefix) {
    ++$State.context.fakeCalls
    $State.context.events.Add('launch')
    if ($State.context.throwCall -eq $State.context.fakeCalls) { throw 'SYNTHETIC launcher did not return' }
    $expected=$State.context.rows[$State.context.fakeCalls-1]
    Assert ($Executable.EndsWith('/slots/'+$expected.slot+'.exe') -or
            $Executable.EndsWith('\slots\'+$expected.slot+'.exe')) 'wrong slot path'
    Assert (($Argv -join '|') -ceq 'main.cpp|helper.cpp|--output|timing_bench|-j|1') 'changed argv'
    $cold=$expected.phase -ceq 'prime' -and $expected.position -eq 0
    if ($cold) {
        $dir=Join-Path $Cwd '.mqb/bin';$null=New-Item -ItemType Directory -Path $dir
        [IO.File]::WriteAllText((Join-Path $dir 'timing_bench.exe'),'SYNTHETIC output, never executed')
        $lines=@('[compile] main.cpp','[compile] helper.cpp','[link] timing_bench.exe')
    } else { $lines=@('[up-to-date] 2 translation units','[up-to-date] timing_bench.exe') }
    if ($State.context.workCall -eq $State.context.fakeCalls) { $lines+=@('[compile] unplanned.cpp') }
    if ($State.context.mutateCall -eq $State.context.fakeCalls) { [IO.File]::AppendAllText((Join-Path $Cwd 'helper.cpp'),'CHANGED') }
    if ($State.context.timingCall -eq $State.context.fakeCalls) { $lines+=@('{"type":"mqb.timings"}') }
    $exit=if ($State.context.failCall -eq $State.context.fakeCalls) { 37 } else { 0 }
    $start=[long]$State.context.fakeCalls*100000; $end=$start+20
    if ($State.context.slowCall -eq $State.context.fakeCalls) { $end=$start+30001 }
    $outerStart=if ($State.context.negativeCall -eq $State.context.fakeCalls) { -1L } else { $start }
    return [ordered]@{clock=[ordered]@{frequency=1000L;outer_start=$outerStart;native_start=$start+2;native_end=$end-2;outer_end=$end}
        exit_code=$exit;error=$null;output_format='powershell_merged_lines';output_lines=$lines;root_times=$null;cleanup=$null}
}
function New-CaseState {
    $caseRoot=Join-Path $root ('case-{0:d2}' -f ($cases.Count+1));$null=New-Item -ItemType Directory -Path $caseRoot
    foreach ($folder in @('inputs','slots','retired','blocks','fixtures')) { $null=New-Item -ItemType Directory -Path (Join-Path $caseRoot $folder) }
    $context=@{events=[Collections.Generic.List[string]]::new();fakeCalls=0
        negativeCall=0;failCall=0;throwCall=0;workCall=0;slowCall=0;mutateCall=0;timingCall=0;failJournal='';freeBytes=8589934592L}
    $p=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
    foreach ($side in @('baseline','candidate')) {
        $file=Join-Path $caseRoot ('inputs/'+$side+'.exe');[IO.File]::WriteAllText($file,('SYNTHETIC '+$side))
        $p.images.$side=Get-Digest $file
    }
    $context.rows=@($p.blocks | ForEach-Object { $_.rows })
    foreach ($row in $context.rows) { $row.sha256=$p.images.($row.image) }
    return (New-SlotState -Root $caseRoot -Plan $p -Launcher ${function:Invoke-SyntheticSlotLaunch} -Context $context -Overrides @{
        Write=${function:Write-SyntheticSlotJson};Digest=${function:Get-SyntheticSlotDigest}
        Manifest=${function:Get-SyntheticSlotManifest};FreeBytes=${function:Get-SyntheticSlotFreeBytes}
    })
}
function Case([string]$Name,[scriptblock]$Action) {
    $state=New-CaseState
    try {
        & $Action $state
        $cases.Add(@{name=$Name;passed=$true;error=$null});Write-Host "PASS: $Name"
    } catch {
        ++$script:failures;$cases.Add(@{name=$Name;passed=$false;error=$_.ToString()});Write-Host "FAIL: $Name :: $_"
    }
}
Case 'all 48 fake requests and 8 blocks; four measured calls have no intervening probes' {
    param($s)
    Invoke-SlotStudy $s
    Assert ($s.attempted -eq 48 -and $s.context.fakeCalls -eq 48) 'call count'
    $finish=Get-Content (Join-Path $s.root 'completion.json') -Raw | ConvertFrom-Json
    Assert ($finish.status -ceq 'completed_diagnostic_unreviewed' -and -not $finish.may_clear_hold) 'false result'
    $runs=0;$streak=0
    foreach ($event in $s.context.events) {
        if ($event -ceq 'launch') { ++$streak }
        else { if ($streak -eq 4) { ++$runs };$streak=0 }
    }
    Assert ($runs -eq 8) 'not exactly eight uninterrupted four-call windows'
    Assert (@(Get-ChildItem (Join-Path $s.root 'retired') -File -Recurse).Count -eq 14) 'retired bytes missing'
}
Case 'first measured nonzero exit stops at three, preserves result, no next block' {
    param($s) $s.context.failCall=3
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.attempted -eq 3 -and $s.context.fakeCalls -eq 3) 'continued after failure'
    $r=Get-Content (Join-Path $s.root 'blocks/r1-AA.result.json') -Raw | ConvertFrom-Json
    Assert ($r.calls.Count -eq 3 -and $r.calls[-1].exit_code -eq 37) 'failed call erased'
}
Case 'launcher throw retains pending requested row without invented process result' {
    param($s) $s.context.throwCall=4
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    $r=Get-Content (Join-Path $s.root 'blocks/r1-AA.result.json') -Raw | ConvertFrom-Json
    Assert ($s.attempted -eq 4 -and $r.calls.Count -eq 3 -and $r.pending.row.sequence -eq 4) 'unknown request erased'
}
Case 'second prime must not rebuild' {
    param($s) $s.context.workCall=2
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 2) 'second prime failure continued'
}
Case 'timings or measured work refused immediately' {
    param($s) $s.context.timingCall=5
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 5) 'timing failure continued'
}
Case 'returned call over thirty seconds retained and stops' {
    param($s) $s.context.slowCall=3
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 3) 'time bound failure continued'
}
Case 'post-window fixture mutation stops before next block and retains changed bytes' {
    param($s) $s.context.mutateCall=6
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 6) 'mutation continued'
    Assert ((Get-Content (Join-Path $s.root 'fixtures/r1-AA/helper.cpp') -Raw).Contains('CHANGED')) 'evidence removed'
}
Case 'window journal failure prevents all measured calls' {
    param($s) $s.context.failJournal='r1-AA.window.json'
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 2) 'continued without intent'
}
Case 'result journal failure stops and is recorded by completion' {
    param($s) $s.context.failJournal='r1-AA.result.json'
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    $r=Get-Content (Join-Path $s.root 'completion.json') -Raw | ConvertFrom-Json
    Assert ($s.context.fakeCalls -eq 6 -and $r.error.Contains('SYNTHETIC journal failure')) 'write error not propagated'
}
Case 'pre-existing block intent is never overwritten' {
    param($s)
    $file=Join-Path $s.root 'blocks/r1-AA.started.json';[IO.File]::WriteAllText($file,'SENTINEL')
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 0 -and [IO.File]::ReadAllText($file) -ceq 'SENTINEL') 'overwritten intent'
}
Case 'unowned existing executable is not overwritten' {
    param($s)
    $file=Join-Path $s.root 'slots/L.exe';[IO.File]::WriteAllText($file,'SENTINEL')
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 0 -and [IO.File]::ReadAllText($file) -ceq 'SENTINEL') 'foreign slot altered'
}
Case 'input identity mutation precedes every launch' {
    param($s)
    [IO.File]::AppendAllText((Join-Path $s.root 'inputs/baseline.exe'),'BAD')
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 0) 'changed input executed'
}
Case 'owned slot tampering prevents rotation and preserves offending bytes' {
    param($s)
    Invoke-SlotBlock $s $s.plan.blocks[0]
    $file=Join-Path $s.root 'slots/L.exe';[IO.File]::AppendAllText($file,'BAD')
    Refuses { Invoke-SlotBlock $s $s.plan.blocks[1] } 'Owned slot changed'
    Assert ($s.context.fakeCalls -eq 6 -and [IO.File]::ReadAllText($file).EndsWith('BAD')) 'changed slot moved or launched'
}
Case 'fixed sequence and ceiling cannot be extended' {
    param($s) $s.attempted=48
    Refuses { Invoke-SlotBlock $s $s.plan.blocks[0] } 'Fixed sequence/budget'
    Assert ($s.context.fakeCalls -eq 0) '49th dispatch'
}
Case 'exact resource bounds accepted, excess or deficit rejected' {
    param($s)
    Assert-SlotLimits 268435456 4294967296
    Refuses { Assert-SlotLimits 268435457 4294967296 } '256 MiB'
    Refuses { Assert-SlotLimits 1 4294967295 } '4 GiB'
    $s.context.freeBytes=4294967295L
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    Assert ($s.context.fakeCalls -eq 0) 'insufficient space launched'
}
Case 'admission excludes automatic events, retries, wrong source labels and user desktops' {
    param($s)
    $valid=@{actions='true';repository='Iviesever/msvc-quick-build';event='workflow_dispatch';attempt='1';disposable='1';allocation='pr232-slot-001'}
    Assert-SlotAdmission $true $true 7 $true ('a'*40) 'pr232-slot-001' $valid
    foreach ($key in @('actions','repository','event','attempt','disposable','allocation')) {
        $bad=$valid.Clone();$bad[$key]='wrong'
        Refuses { Assert-SlotAdmission $true $true 7 $true ('a'*40) 'pr232-slot-001' $bad } 'allocation required'
    }
    Refuses { Assert-SlotAdmission $false $true 7 $true ('a'*40) 'pr232-slot-001' $valid } 'Explicit Windows'
    Refuses { Assert-SlotAdmission $true $false 7 $true ('a'*40) 'pr232-slot-001' $valid } 'Explicit Windows'
    Refuses { Assert-SlotAdmission $true $true 7 $true 'HEAD' 'pr232-slot-001' $valid } 'Exact review'
    Assert ($s.context.fakeCalls -eq 0) 'admission testing launched a process'
}
Case 'negative returned clock stops immediately and remains recorded' {
    param($s) $s.context.negativeCall=3
    Refuses { Invoke-SlotStudy $s } 'Diagnostic stopped'
    $record=Get-Content (Join-Path $s.root 'blocks/r1-AA.result.json') -Raw | ConvertFrom-Json
    Assert ($s.context.fakeCalls -eq 3 -and $record.calls[-1].clock.outer_start -eq -1) 'negative clock lost or continued'
}
Case 'runtime provides no implicit native launcher' {
    param($s)
    Refuses { New-SlotState -Root $s.root -Plan $s.plan } 'explicit launcher dependency'
    Assert ($s.context.fakeCalls -eq 0) 'missing callback caused launch'
    Assert ($null -eq (Get-Command Invoke-LegacyBoundary -ErrorAction SilentlyContinue)) 'test imported native capture'
}
Case 'unknown or non-callable IO dependencies are refused' {
    param($s)
    Refuses { New-SlotState -Root $s.root -Plan $s.plan -Launcher ${function:Invoke-SyntheticSlotLaunch} -Overrides @{Unknown={}} } 'IO callback'
    Refuses { New-SlotState -Root $s.root -Plan $s.plan -Launcher ${function:Invoke-SyntheticSlotLaunch} -Overrides @{Write='not code'} } 'IO callback'
    Assert ($s.context.fakeCalls -eq 0) 'invalid dependency caused launch'
}
Case 'callback state is owned by each case rather than global function replacement' {
    param($s)
    $otherContext=@{fakeCalls=123}
    $other=New-SlotState -Root $s.root -Plan $s.plan -Launcher ${function:Invoke-SyntheticSlotLaunch} -Context $otherContext
    $other.context.fakeCalls=124
    Assert ($s.context.fakeCalls -eq 0 -and $other.context.fakeCalls -eq 124) 'case state leaked'
    Assert ((Get-Command Get-Digest).ModuleName -ceq 'noop_identity_slots_runtime') 'production function shadowed'
}
$result=@{schema=1;tests=$cases.Count;failures=$failures;cases=@($cases.ToArray());study_mqb_calls=0;native_launches=0;etw_sessions=0
    scope='Imported definition-only orchestrator with explicit callbacks, real file IO with tiny synthetic images, in-process fake launcher; not live diagnostic.'}
Write-NewJson (Join-Path $root 'result.json') $result
if ($failures) { throw "$failures synthetic control cases failed." }
exit 0

# Real recording functions and comparator; synthetic MQB output only, no compiler or study.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Fresh control output required.' }
$null = New-Item -ItemType Directory -Path $OutputRoot
. (Join-Path $PSScriptRoot 'performance_evidence_files.ps1')
$cases = [Collections.Generic.List[object]]::new()
function Assert($Condition, $Message) { if (-not $Condition) { throw $Message } }
function Refuses([scriptblock]$Action, [string]$Pattern) {
    $caught = $null
    try { & $Action | Out-Null } catch { $caught = $_.ToString() }
    Assert ($null -ne $caught -and $caught -match $Pattern) "Expected $Pattern; got $caught"
}
function Case([string]$Name, [scriptblock]$Action) {
    try { & $Action; $cases.Add(@{name=$Name; passed=$true}) }
    catch { $cases.Add(@{name=$Name; passed=$false; error=$_.ToString()}) }
}
function Fresh([string]$Name) { New-PerformanceEvidenceDirectory (Join-Path $OutputRoot $Name) }
function ReadRecord([string]$Root, [string]$Name) { Get-Content (Join-Path $Root $Name) -Raw | ConvertFrom-Json }
# Import only the actual functions; never execute benchmark fixture creation or an MQB binary.
foreach ($item in @(
    @{file='benchmark_mqb.ps1'; names=@('Invoke-ObservedMqb','Invoke-TimedMqb','Invoke-UntimedMqb')},
    @{file='verify_performance_instrumentation.ps1'; names=@('Invoke-MqbCapture')}
)) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $item.file),[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
    foreach ($name in $item.names) {
        $definitions=@($ast.EndBlock.Statements | Where-Object {
            $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name
        })
        Assert ($definitions.Count -eq 1) "Missing function $name"
        . ([scriptblock]::Create($definitions[0].Extent.Text))
    }
}
$script:fakeMode='ok'; $script:fakeCalls=0
function Invoke-FakeMqb {
    ++$script:fakeCalls
    $global:LASTEXITCODE=0
    if ($script:fakeMode -ceq 'throw') { throw 'SYNTHETIC launch failure' }
    if ($script:fakeMode -ceq 'exit') { $global:LASTEXITCODE=7; 'SYNTHETIC native diagnostic'; return }
    'SYNTHETIC stdout'
    if ('--timings=json' -in $args) {
        if ($script:fakeMode -ceq 'missing') { return }
        if ($script:fakeMode -ceq 'bad-json') { '{"type":"mqb.timings",BROKEN'; return }
        [ordered]@{
            type='mqb.timings'; schema_version=2
            phases=@{total=10;discovery=1;dependency_scan=0;compile_queue=0;compile=0;link=0;archive=0;run_startup=0}
            cache=@{compile=@{hits=2;misses=0};link=@{hits=1;misses=0};archive=@{hits=0;misses=0}}
            attribution=$null; counters=$null;counter_breakdown=$null
        } | ConvertTo-Json -Depth 8 -Compress
    }
}
$MqbPath='Invoke-FakeMqb'; $InvocationEvidenceDirectory=$null
$cwd=Fresh 'cwd'
Case 'create-new record roundtrip' {
    $r=Fresh 'writer'; Write-PerformanceEvidence $r 'one.json' @{value=42}
    Assert ((ReadRecord $r 'one.json').value -eq 42) 'record mismatch'
}
Case 'existing record is not overwritten' {
    $r=Fresh 'duplicate'; Write-PerformanceEvidence $r 'one.json' @{value=42}
    Refuses {Write-PerformanceEvidence $r 'one.json' @{value=7}} 'exist'
    Assert ((ReadRecord $r 'one.json').value -eq 42) 'old record changed'
}
Case 'path escape rejected' { Refuses {Write-PerformanceEvidence $OutputRoot '../outside.json' @{x=1}} 'one JSON leaf' }
Case 'directory reuse refused' { Refuses {New-PerformanceEvidenceDirectory $cwd} 'Fresh' }
Case 'timed output recorded before parsing' {
    $RawEvidenceDirectory=Fresh 'timed';$script:fakeMode='ok'
    $r=Invoke-TimedMqb -Scenario cold -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')
    Assert ($r.total_ms -eq 10 -and $r.measurement_source -ceq 'mqb.timings') 'timing changed'
    $v=ReadRecord $RawEvidenceDirectory '1-cold.result.json'
    Assert ($v.exit_code -eq 0 -and $v.output_lines.Count -eq 2) 'raw output missing'
}
Case 'external elapsed remains original returned stopwatch value' {
    $RawEvidenceDirectory=Fresh 'untimed';$script:fakeMode='ok'
    $r=Invoke-UntimedMqb -Scenario no-op -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')
    $v=ReadRecord $RawEvidenceDirectory '1-no-op.result.json'
    Assert ($r.measurement_source -ceq 'external_stopwatch' -and $r.total_ms -eq $v.elapsed_ms) 'clock substituted'
}
Case 'nonzero exit retained before rejection' {
    $RawEvidenceDirectory=Fresh 'nonzero';$script:fakeMode='exit'
    Refuses {Invoke-TimedMqb -Scenario cold -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')} 'exit code 7'
    $v=ReadRecord $RawEvidenceDirectory '1-cold.result.json'
    Assert ($v.exit_code -eq 7 -and $v.output_lines[0] -ceq 'SYNTHETIC native diagnostic') 'failure lost'
}
Case 'missing timing retains output' {
    $RawEvidenceDirectory=Fresh 'missing';$script:fakeMode='missing'
    Refuses {Invoke-TimedMqb -Scenario cold -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')} 'no MQB timing'
    Assert ((ReadRecord $RawEvidenceDirectory '1-cold.result.json').output_lines.Count -eq 1) 'output lost'
}
Case 'malformed timing retains original text' {
    $RawEvidenceDirectory=Fresh 'bad-json';$script:fakeMode='bad-json'
    Refuses {Invoke-TimedMqb -Scenario cold -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')} 'JSON|Unexpected|Invalid'
    Assert ((ReadRecord $RawEvidenceDirectory '1-cold.result.json').output_lines[1] -match 'BROKEN') 'bad JSON lost'
}
Case 'launch exception keeps started and result records' {
    $RawEvidenceDirectory=Fresh 'launch';$script:fakeMode='throw'
    Refuses {Invoke-UntimedMqb -Scenario no-op -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')} 'SYNTHETIC launch'
    Assert ((ReadRecord $RawEvidenceDirectory '1-no-op.result.json').capture_error -match 'SYNTHETIC launch') 'exception lost'
}
Case 'cannot overwrite start before launch' {
    $RawEvidenceDirectory=Fresh 'start-collision';$script:fakeMode='ok';$n=$script:fakeCalls
    Write-PerformanceEvidence $RawEvidenceDirectory '1-cold.started.json' @{original=$true}
    Refuses {Invoke-TimedMqb -Scenario cold -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')} 'exist'
    Assert ($script:fakeCalls -eq $n) 'called before evidence admission'
}
Case 'result collision fails without replacing older evidence' {
    $RawEvidenceDirectory=Fresh 'result-collision';$script:fakeMode='ok'
    Write-PerformanceEvidence $RawEvidenceDirectory '1-cold.result.json' @{original=$true}
    Refuses {Invoke-TimedMqb -Scenario cold -Iteration 1 -WorkingDirectory $cwd -Arguments @('main.cpp')} 'exist'
    Assert ((ReadRecord $RawEvidenceDirectory '1-cold.result.json').original) 'overwritten'
}
Case 'instrumentation success retains argv and raw timing' {
    $EvidenceDirectory=Fresh 'instrumentation';$script:instrumentationSequence=0;$script:fakeMode='ok'
    $v=Invoke-MqbCapture -WorkingDirectory $cwd -Arguments @('main.cpp','--timings=json') -ExpectTimings
    Assert ($v.timing.schema_version -eq 2) 'instrumentation changed'
    Assert ((ReadRecord $EvidenceDirectory 'call-01.started.json').expect_timings) 'request missing'
}
Case 'instrumentation failure is retained' {
    $EvidenceDirectory=Fresh 'instrumentation-fail';$script:instrumentationSequence=0;$script:fakeMode='exit'
    Refuses {Invoke-MqbCapture -WorkingDirectory $cwd -Arguments @('main.cpp')} 'exited with code 7'
    Assert ((ReadRecord $EvidenceDirectory 'call-01.result.json').exit_code -eq 7) 'lost instrumentation failure'
}
Case 'instrumentation malformed contract preserves successful native output' {
    $EvidenceDirectory=Fresh 'instrumentation-missing';$script:instrumentationSequence=0;$script:fakeMode='missing'
    Refuses {Invoke-MqbCapture -WorkingDirectory $cwd -Arguments @('main.cpp','--timings=json') -ExpectTimings} 'exactly one timing'
    Assert ((ReadRecord $EvidenceDirectory 'call-01.result.json').exit_code -eq 0) 'lost valid exit'
}
# Exercise the real comparator end to end with synthetic per-suite reports.
$harness=Fresh 'comparator-harness'
foreach($name in @('compare_mqb_benchmarks.ps1','performance_evidence_files.ps1')) {
    Copy-Item (Join-Path $PSScriptRoot $name) (Join-Path $harness $name)
}
$stub=@'
param($MqbPath,$Iterations,$OutputPath,$RawEvidenceDirectory)
$ErrorActionPreference='Stop';$global:LASTEXITCODE=0
$id=[IO.Path]::GetFileNameWithoutExtension($OutputPath)
if ($RawEvidenceDirectory) {
    $r=New-PerformanceEvidenceDirectory $RawEvidenceDirectory
    Write-PerformanceEvidence $r 'synthetic.result.json' @{only_synthetic=$true;label=$id}
}
$fail=Get-Content (Join-Path $PSScriptRoot 'fail-on.txt') -Raw
if ($id -ceq $fail.Trim()) {throw 'SYNTHETIC suite failure after retained output'}
$names=@('cold','no-op','single-tu','public-header','build-run','link-only','target-scale-cold','target-scale-no-op','target-scale-no-op-auto','target-scale-no-op-j1','target-scale-common-header-no-op','target-scale-single-tu','discovery-cold','discovery-no-op','discovery-header','modules-cold','modules-no-op','timings-enabled-no-op','timings-disabled-no-op')
$value=10.0
if ([IO.Path]::GetFileName($MqbPath) -ceq 'B.fake') {
    $value=[double]::Parse((Get-Content (Join-Path $PSScriptRoot 'candidate-ms.txt') -Raw),[Globalization.CultureInfo]::InvariantCulture)
}
$samples=@(foreach($name in $names) {
    @{scenario=$name;iteration=1;total_ms=$value;discovery_ms=0;compile_queue_ms=0
      measurement_source=$(if($name -ceq 'timings-disabled-no-op'){'external_stopwatch'}else{'mqb.timings'})
      timing_schema_version=$(if($name -ceq 'timings-disabled-no-op'){0}else{2})
      attribution=$null;counters=$null;counter_breakdown=$null
      compile_hits=-1;compile_misses=-1;link_hits=-1;link_misses=-1;archive_hits=-1;archive_misses=-1}
})
@{schema_version=3;iterations=1;samples=$samples;summary=@($names|ForEach-Object{@{scenario=$_}})} |
    ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $OutputPath -Encoding utf8
'@
Set-Content (Join-Path $harness 'benchmark_mqb.ps1') $stub -Encoding utf8
$a=Join-Path $harness 'A.fake';$b=Join-Path $harness 'B.fake'
Set-Content $a 'NOT EXECUTABLE';Set-Content $b 'NOT EXECUTABLE'
$comparator=Join-Path $harness 'compare_mqb_benchmarks.ps1'
$gate=Join-Path $PSScriptRoot 'check_external_noop_gate.py'
foreach($definition in @(
    @{id='complete';fail='none';candidate='10.2';gate=0},
    @{id='equality';fail='none';candidate='11';gate=0},
    @{id='hold';fail='none';candidate='11.1';gate=1},
    @{id='partial';fail='pair-2-baseline';candidate='10.2';gate=2}
)) {
    Case ('comparator '+$definition.id) {
        $dir=Fresh $definition.id;$ev=Join-Path $dir 'records';$report=Join-Path $dir 'comparison.json'
        Set-Content (Join-Path $harness 'fail-on.txt') $definition.fail
        Set-Content (Join-Path $harness 'candidate-ms.txt') $definition.candidate
        if ($definition.id -ceq 'partial') {
            Refuses { & $comparator -BaselineMqbPath $a -CandidateMqbPath $b -Iterations 4 -OutputPath $report -EvidenceDirectory $ev *> (Join-Path $dir 'comparator.log') } 'SYNTHETIC suite failure'
            Assert (-not (Test-Path $report)) 'partial became final report'
            Assert (Test-Path (Join-Path $ev 'pair-1.completed.json')) 'earlier complete pair removed'
            Assert (Test-Path (Join-Path $ev 'pair-2-candidate.json')) 'completed half-pair removed'
            Assert ((ReadRecord $ev 'failure.json').status -ceq 'INVALID') 'failure not explicit'
        } else {
            & $comparator -BaselineMqbPath $a -CandidateMqbPath $b -Iterations 4 -OutputPath $report -EvidenceDirectory $ev *> (Join-Path $dir 'comparator.log')
            Assert ((ReadRecord $dir 'comparison.json').paired_samples.Count -eq 76) 'grid incomplete'
        }
        & python -B $gate $report --output (Join-Path $dir 'gate.json') *> (Join-Path $dir 'gate.log')
        Assert ($LASTEXITCODE -eq $definition.gate) "wrong gate result $LASTEXITCODE"
    }
}
Case 'existing comparator output is refused before suites' {
    $dir=Fresh 'old-output';$report=Join-Path $dir 'comparison.json';Set-Content $report 'ORIGINAL'
    Refuses {& $comparator -BaselineMqbPath $a -CandidateMqbPath $b -OutputPath $report -EvidenceDirectory (Join-Path $dir 'records')} 'replace an existing'
    Assert ((Get-Content $report -Raw).Trim() -ceq 'ORIGINAL') 'old report altered'
}
Case 'existing comparator evidence is refused' {
    $dir=Fresh 'old-evidence';$ev=Fresh 'old-evidence-records'
    Refuses {& $comparator -BaselineMqbPath $a -CandidateMqbPath $b -OutputPath (Join-Path $dir 'comparison.json') -EvidenceDirectory $ev} 'Fresh performance'
}
$summary=[ordered]@{schema=1;only_synthetic=$true;cases=@($cases.ToArray());passed=@($cases|Where-Object {$_.passed}).Count;failed=@($cases|Where-Object {-not $_.passed}).Count;new_mqb_calls=0}
Write-PerformanceEvidence $OutputRoot 'summary.json' $summary
$summary | ConvertTo-Json -Depth 8
if ($summary.failed) { exit 1 }
exit 0

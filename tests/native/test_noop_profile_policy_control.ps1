# Actual extracted cell/control functions, fake WPR and fake MQB only.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputPath, [Parameter(Mandatory)][string]$PlanPath, [Parameter(Mandatory)][string]$InlinePath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
function Import-TestDefinition([string]$Path, [string[]]$Names) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
    foreach ($name in $Names) {
        $found=@($ast.EndBlock.Statements | Where-Object {
            $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name
        })
        if ($found.Count -ne 1) { throw "Missing actual function $name" }
        # Define in caller script scope, never run the enclosing entry.
        $definition=$found[0].Extent.Text -replace ('^function\s+'+[regex]::Escape($name)+'(?=[\s(])'), ('function script:'+ $name)
        . ([scriptblock]::Create($definition))
    }
}
Import-TestDefinition (Join-Path $PSScriptRoot 'trace_noop_causal.ps1') @('Invoke-OwnedWpr')
Import-TestDefinition (Join-Path $PSScriptRoot 'trace_noop_causal_windows.ps1') @('Assert-CausalWindowBudget')
Import-TestDefinition (Join-Path $PSScriptRoot 'trace_noop_profile_policy.ps1') @('Invoke-PolicyCell','Invoke-PolicyRow','Save-ProfileInterval')
Import-TestDefinition $InlinePath @('Assert-PolicyJournal')
$spec=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
$outRoot=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) 'policy-fake-evidence'
if (Test-Path -LiteralPath $outRoot) { throw 'New fake evidence directory required.' }
$null=New-Item -ItemType Directory -Path $outRoot
$cases=[Collections.Generic.List[object]]::new(); $failures=0
function Assert-True([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    $caught=$null
    try { & $Action } catch { $caught=$_.ToString() }
    Assert-True ($null -ne $caught -and $caught -match $Pattern) "Expected $Pattern, got $caught"
}
function Write-NewJson($Path, $Value) {
    $script:events.Add('journal')
    if ([IO.Path]::GetFileName($Path) -ceq $script:failLeaf -or
        ($script:failPrimeWrite -and $Path -match '[\\/]primes[\\/]') -or
        ($script:failCellWrite -and $Path -match '[\\/]cells[\\/]')) { throw 'SYNTHETIC journal failure' }
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew)
    try {
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 32))
        $stream.Write($bytes,0,$bytes.Length)
    } finally { $stream.Dispose() }
}
function Get-FileManifest($Path) {
    $script:events.Add('manifest')
    return @(@{path='main.cpp';size=1;sha256='SYNTHETIC'},@{path='helper.cpp';size=1;sha256='SYNTHETIC'})
}
function Get-CausalFreeSpace { return $script:freeBytes }
function Invoke-CausalWindowRow($cell,$row,$cellRecord,$fixture) {
    Assert-True ($row.sequence -eq ($script:attempted+1)) 'Changed call order'
    if ($row.phase -eq 'prime') { Assert-True (-not $script:owned) 'Prime was traced' }
    else { Assert-True ($script:owned -eq ($cell.policy -ceq 'P')) 'Wrong N/P recording state' }
    ++$script:attempted
    $script:events.Add('call-'+$row.phase)
    $cellRecord.calls.Add(@{row=$row;fake=$true})
    if ($row.phase -ceq $script:failPhase) { throw 'SYNTHETIC call failure' }
}
function Reset-Case {
    $script:root=Join-Path $outRoot ('case-{0:d2}' -f ($cases.Count+1))
    $null=New-Item -ItemType Directory -Path $root
    foreach ($dir in @('cells','primes','traces','fixtures')) { $null=New-Item -ItemType Directory -Path (Join-Path $root $dir) }
    $script:profile=Join-Path $root 'SYNTHETIC.wprp'
    $script:owned=$false; $script:attempted=0; $script:controlSequence=0; $script:stopError=$null
    $script:failLeaf=''; $script:failPrimeWrite=$false; $script:failCellWrite=$false; $script:failPhase=''; $script:failCommand=''
    $script:freeBytes=8589934592
    $script:events=[Collections.Generic.List[string]]::new()
    $script:nativeCalls=[Collections.Generic.List[object]]::new()
    $script:wpr={
        $script:nativeCalls.Add([object]@($args))
        $script:events.Add('wpr'+$args[0])
        if ($args[0] -ceq '-profint') { Assert-True (-not $script:owned -and $args.Count -eq 1) 'Query overlaps window' }
        else { Assert-True ($args[-2] -ceq '-instancename' -and $args[-1] -ceq $script:instance) 'Wrong instance' }
        $global:LASTEXITCODE=$(if ($args[0] -ceq $script:failCommand) { 37 } else { 0 })
        if ($args[0] -ceq '-stop' -and $LASTEXITCODE -eq 0) {
            [IO.File]::WriteAllText($args[1],'SYNTHETIC NOT AN ETL')
        }
        'SYNTHETIC control output; no native process'
    }
}
function Invoke-Case([string]$Name,[scriptblock]$Body) {
    Reset-Case
    try {
        & $Body
        $cases.Add(@{name=$Name;passed=$true;error=$null})
        Write-Host "PASS: $Name"
    } catch {
        ++$script:failures
        $cases.Add(@{name=$Name;passed=$false;error=$_.ToString()})
        Write-Host "FAIL: $Name :: $_"
    }
}
function Invoke-SingleCell {
    $cell=$spec.cells[2] # first P, original sequence5/6
    $script:attempted=$cell.rows[0].sequence-1
    Invoke-PolicyCell $cell
}
Invoke-Case 'complete exact plan:16 fake calls,4 P sessions,20 controls,2 read-only queries' {
    Save-ProfileInterval 'before'
    foreach ($cell in $spec.cells) { Invoke-PolicyCell $cell }
    Save-ProfileInterval 'after'
    Assert-True ($attempted -eq 16 -and $controlSequence -eq 20 -and $nativeCalls.Count -eq 22 -and -not $owned) 'Budget mismatch'
    Assert-True (@(Get-ChildItem (Join-Path $root 'traces') -Directory).Count -eq 4) 'N created a trace directory'
    Assert-True ($nativeCalls[0][0] -ceq '-profint' -and $nativeCalls[21][0] -ceq '-profint') 'Query timing/order'
    Assert-True (@($nativeCalls | Where-Object { $_[0] -ceq '-start' } | ForEach-Object { $_[-1] } | Select-Object -Unique).Count -eq 4) 'Instance reused'
}
Invoke-Case 'N final error never touches WPR' {
    $script:failPhase='final'
    Assert-Throws { Invoke-PolicyCell $spec.cells[0] } 'SYNTHETIC call failure'
    Assert-True ($nativeCalls.Count -eq 0 -and $attempted -eq 2) 'N error started trace or additional call'
}
Invoke-Case 'P prime error cannot start tracing' {
    $script:failPhase='prime'; Assert-Throws { Invoke-SingleCell } 'SYNTHETIC call failure'
    Assert-True ($nativeCalls.Count -eq 0 -and -not $owned) 'Prime failure captured'
}
Invoke-Case 'P prime checkpoint error cannot start tracing' {
    $script:failPrimeWrite=$true; Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($nativeCalls.Count -eq 0) 'Lost checkpoint captured'
}
Invoke-Case 'failed start gives no ownership or stop permission' {
    $script:failCommand='-start'; Assert-Throws { Invoke-SingleCell } '37'
    Assert-True ($nativeCalls.Count -eq 1 -and -not $owned -and $attempted -eq 5) 'Unowned stop/final'
}
Invoke-Case 'successful start with failed journal still stopped exactly once' {
    $script:failLeaf='wpr-01.json'; Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($nativeCalls.Count -eq 2 -and $nativeCalls[1][0] -ceq '-stop' -and -not $owned) 'Cleanup lost/retried'
}
Invoke-Case 'begin marker journal failure suppresses final and stops once' {
    $script:failLeaf='wpr-02.json'; Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($attempted -eq 5 -and $nativeCalls.Count -eq 3 -and -not $owned) 'Continued after missing marker journal'
}
Invoke-Case 'P final error preserves prefix with one owned stop' {
    $script:failPhase='final'; Assert-Throws { Invoke-SingleCell } 'SYNTHETIC call failure'
    Assert-True ($attempted -eq 6 -and $nativeCalls.Count -eq 3 -and -not $owned) 'Final error continued'
    $saved=Get-Content (Join-Path $root ('cells/'+$spec.cells[2].id+'.json')) -Raw | ConvertFrom-Json
    Assert-True ($saved.calls.Count -eq 2 -and $null -ne $saved.error) 'Prefix lost'
}
Invoke-Case 'stop pre-journal error cannot prevent sole native stop' {
    $script:failLeaf='wpr-05.started.json'; Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($nativeCalls.Count -eq 5 -and -not $owned) 'Cleanup blocked or retried'
}
Invoke-Case 'nonzero stop blocks next N prime; never retry' {
    $script:failCommand='-stop'; Assert-Throws { Invoke-SingleCell } '37'; $before=$attempted
    Assert-Throws { Invoke-PolicyCell $spec.cells[6] } 'Prior stop uncertain'
    Assert-True ($attempted -eq $before -and $nativeCalls.Count -eq 5 -and $owned) 'Retried/continued'
}
Invoke-Case 'successful stop but lost result journal still blocks next N prime' {
    $script:failLeaf='wpr-05.json'; Assert-Throws { Invoke-SingleCell } 'journal'; $before=$attempted
    Assert-Throws { Invoke-PolicyCell $spec.cells[6] } 'Prior stop uncertain'
    Assert-True ($attempted -eq $before -and $nativeCalls.Count -eq 5 -and -not $owned) 'Journal error silently cleared'
}
Invoke-Case 'cell journal error never becomes success' {
    $script:failCellWrite=$true; Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($nativeCalls.Count -eq 5 -and -not $owned) 'Extra cleanup'
}
Invoke-Case 'sixteenth call is hard ceiling independent of legacy32 ceiling' {
    $script:attempted=16
    Assert-Throws { Invoke-PolicyRow $spec.cells[0] $spec.cells[0].rows[0] @{} '' } '16-call ceiling'
    Assert-True ($attempted -eq 16) 'Seventeenth invocation dispatched'
}
Invoke-Case 'query failure is recorded and not reset/retried' {
    $script:failCommand='-profint'; Assert-Throws { Save-ProfileInterval 'before' } 'Interval query failed'
    Assert-True ($attempted -eq 0 -and $nativeCalls.Count -eq 1 -and -not $owned) 'Query failure continued'
}
Invoke-Case 'interval query refuses an owned window' {
    $script:owned=$true; Assert-Throws { Save-ProfileInterval 'before' } 'outside every study window'
    Assert-True ($nativeCalls.Count -eq 0) 'Query entered active trace'
}
Invoke-Case 'low space fails before prime or WPR' {
    $script:freeBytes=1; Assert-Throws { Invoke-SingleCell } '4 GiB'
    Assert-True ($attempted -eq 4 -and $nativeCalls.Count -eq 0) 'Low-space work admitted'
}
Invoke-Case 'actual wrapper verdict accepts only exact counts and unreviewed flags' {
    $ok=@{status='policy_journal_complete_trace_unreviewed';calls=16;traced_calls=4;windows=4;interval_queries=2
        trace_files=@(1,2,3,4);trace_health_verified=$false;clears_hold=$false;cause=$null}
    Assert-PolicyJournal $ok
    foreach ($k in @('calls','traced_calls','windows','interval_queries')) {
        foreach ($value in @($true,'4',0,99)) {
            $bad=$ok.Clone();$bad[$k]=$value; Assert-Throws { Assert-PolicyJournal $bad } 'Wrong'
        }
    }
    foreach ($k in @('trace_health_verified','clears_hold')) {
        foreach ($value in @($true,'false',$null)) {
            $bad=$ok.Clone();$bad[$k]=$value;Assert-Throws { Assert-PolicyJournal $bad } 'inconsistent'
        }
    }
    $bad=$ok.Clone();$bad.cause='noise';Assert-Throws { Assert-PolicyJournal $bad } 'inconsistent'
    $bad=$ok.Clone();$bad.trace_files=@();Assert-Throws { Assert-PolicyJournal $bad } 'inconsistent'
    $bad=$ok.Clone();$bad.status='passed';Assert-Throws { Assert-PolicyJournal $bad } 'Not a complete'
}
$result=@{schema=1;scope='actual extracted policy, budget, owned-control and wrapper verdict; in-process fake WPR/MQB'
    tests=$cases.Count;failures=$failures;cases=@($cases.ToArray());native_wpr_invocations=0;etw_sessions_started=0;study_mqb_calls=0}
$stream=[IO.File]::Open([IO.Path]::GetFullPath($OutputPath),[IO.FileMode]::CreateNew)
try {
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 12));$stream.Write($bytes,0,$bytes.Length)
} finally { $stream.Dispose() }
if ($failures) { throw "$failures policy-control tests failed." }
exit 0

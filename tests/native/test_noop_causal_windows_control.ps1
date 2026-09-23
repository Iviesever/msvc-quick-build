# Actual extracted cell/control functions, fake WPR and fake MQB only.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputPath, [Parameter(Mandatory)][string]$PlanPath)
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
        $definition=$found[0].Extent.Text -replace ('^function '+[regex]::Escape($name)+' '), ('function script:'+ $name+' ')
        . ([scriptblock]::Create($definition))
    }
}
Import-TestDefinition (Join-Path $PSScriptRoot 'trace_noop_causal.ps1') @('Invoke-OwnedWpr')
Import-TestDefinition (Join-Path $PSScriptRoot 'trace_noop_causal_windows.ps1') @('Invoke-PostPrimeCell','Assert-CausalWindowBudget')
$spec=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
$outRoot=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) 'window-fake-evidence'
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
    else { Assert-True $script:owned 'Measured call lacked owned trace' }
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
        Assert-True ($args[-2] -ceq '-instancename' -and $args[-1] -ceq $script:instance) 'Wrong instance'
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
    $cell=$spec.cells[4] # first middle-on cell, original rows/order
    $script:attempted=$cell.rows[0].sequence-1
    Invoke-PostPrimeCell $cell
}
Invoke-Case 'all 32 fake calls: primes untraced, measured pairs contiguous, 12 owned windows' {
    foreach ($cell in $spec.cells) { Invoke-PostPrimeCell $cell }
    Assert-True ($attempted -eq 32 -and $controlSequence -eq 60 -and -not $owned) 'Budget or release mismatch'
    $starts=@($nativeCalls | Where-Object { $_[0] -eq '-start' })
    Assert-True (@($starts | ForEach-Object { $_[-1] } | Select-Object -Unique).Count -eq 12) 'Instance reused'
    for ($i=0; $i -lt $events.Count; ++$i) {
        if ($events[$i] -eq 'call-middle') { Assert-True ($events[$i+1] -eq 'call-final') 'Work inserted between measured calls' }
    }
}
Invoke-Case 'prime failure starts no WPR' {
    $script:failPhase='prime'
    Assert-Throws { Invoke-SingleCell } 'SYNTHETIC call failure'
    Assert-True ($nativeCalls.Count -eq 0 -and -not $owned) 'Trace after prime failure'
}
Invoke-Case 'prime checkpoint failure starts no WPR' {
    $script:failPrimeWrite=$true
    Assert-Throws { Invoke-SingleCell } 'SYNTHETIC journal failure'
    Assert-True ($nativeCalls.Count -eq 0) 'Trace after lost prime checkpoint'
}
Invoke-Case 'unknown or nonzero start cannot stop another session' {
    $script:failCommand='-start'
    Assert-Throws { Invoke-SingleCell } '37'
    Assert-True ($nativeCalls.Count -eq 1 -and -not $owned) 'Foreign stop or measured call'
}
Invoke-Case 'start result journal failure still reaches sole owned stop' {
    $script:failLeaf='wpr-01.json'
    Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($nativeCalls.Count -eq 2 -and $nativeCalls[1][0] -eq '-stop' -and -not $owned) 'Lost owned cleanup'
}
Invoke-Case 'middle failure suppresses final and preserves the prefix' {
    $script:failPhase='middle'
    Assert-Throws { Invoke-SingleCell } 'SYNTHETIC call failure'
    Assert-True (-not ($events -contains 'call-final') -and -not $owned) 'Continued after middle failure'
    $saved=Get-Content (Join-Path $root ('cells/'+$spec.cells[4].id+'.json')) -Raw | ConvertFrom-Json
    Assert-True ($saved.calls.Count -eq 2 -and $null -ne $saved.error) 'Lost stopped prefix'
}
Invoke-Case 'begin marker failure still stops without measured calls or retry' {
    # First marker result lost: critical calls must not begin.
    $script:failLeaf='wpr-02.json'
    Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True (-not ($events -contains 'call-middle') -and $nativeCalls.Count -eq 3 -and -not $owned) 'Marker failure continued'
}
Invoke-Case 'stop pre-journal failure cannot block native cleanup' {
    $script:failLeaf='wpr-05.started.json'
    Assert-Throws { Invoke-SingleCell } 'journal'
    Assert-True ($nativeCalls.Count -eq 5 -and -not $owned) 'Cleanup blocked or duplicated'
}
Invoke-Case 'nonzero stop is not retried and blocks next prime' {
    $script:failCommand='-stop'
    Assert-Throws { Invoke-SingleCell } '37'
    Assert-True ($nativeCalls.Count -eq 5 -and $owned) 'Stop retried or false release'
    $before=$attempted
    Assert-Throws { Invoke-PostPrimeCell $spec.cells[5] } 'Prior window still owned'
    Assert-True ($attempted -eq $before -and $nativeCalls.Count -eq 5) 'Next window admitted'
}
Invoke-Case 'cell journal failure is not capture success' {
    $script:failCellWrite=$true
    Assert-Throws { Invoke-SingleCell } 'SYNTHETIC journal failure'
    Assert-True (-not $owned -and @($nativeCalls | Where-Object { $_[0] -eq '-stop' }).Count -eq 1) 'Cell write leaked/retried'
}
Invoke-Case 'final failure retains its original error and one stop' {
    $script:failPhase='final'
    Assert-Throws { Invoke-SingleCell } 'SYNTHETIC call failure'
    Assert-True (-not $owned -and @($nativeCalls | Where-Object { $_[0] -eq '-stop' }).Count -eq 1) 'Final failure leaked/retried'
}
# Exercise actual production checkpoint code with synthetic size metadata, not huge files.
function Invoke-BudgetCase([long]$Total,[long]$Segment,[long]$Kernel,[long]$Free) {
    $script:freeBytes=$Free
    $script:syntheticBudget=@{total=$Total;segment=$Segment;kernel=$Kernel}
    function Get-ChildItem {
        param($LiteralPath,[switch]$Recurse,[switch]$Force,[switch]$File)
        $length=$(if ($LiteralPath -match '[\\/]temp$') { $script:syntheticBudget.kernel } else { $script:syntheticBudget.total })
        return [pscustomobject]@{Attributes=0;PSIsContainer=$false;Length=$length}
    }
    function Test-Path { param($LiteralPath); return $true }
    function Get-Item { param($LiteralPath); return [pscustomobject]@{Length=$script:syntheticBudget.segment} }
    $null=Assert-CausalWindowBudget (Join-Path $root 'SYNTHETIC-segment')
}
Invoke-Case 'exact segment/total/free boundaries accepted' {
    Invoke-BudgetCase 536870912 268435456 268435455 4294967296
}
Invoke-Case 'aggregate byte ceiling refused' {
    Assert-Throws { Invoke-BudgetCase 536870913 1 1 4294967296 } '512 MiB'
}
Invoke-Case 'segment byte ceiling refused' {
    Assert-Throws { Invoke-BudgetCase 1 268435457 1 4294967296 } '256 MiB'
}
Invoke-Case 'kernel ceiling equality refused, not overwritten' {
    Assert-Throws { Invoke-BudgetCase 1 1 268435456 4294967296 } 'kernel file limit'
}
Invoke-Case 'free-space deficit refused' {
    Assert-Throws { Invoke-BudgetCase 1 1 1 4294967295 } '4 GiB'
}
$result=@{schema=1;scope='actual cell and owned-control functions; in-process fake WPR/MQB and synthetic budgets'
    tests=$cases.Count;failures=$failures;cases=@($cases.ToArray())
    native_wpr_invocations=0;etw_sessions_started=0;study_mqb_calls=0}
$stream=[IO.File]::Open([IO.Path]::GetFullPath($OutputPath),[IO.FileMode]::CreateNew)
try {
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 12))
    $stream.Write($bytes,0,$bytes.Length)
} finally { $stream.Dispose() }
if ($failures) { throw "$failures window-control tests failed." }
exit 0

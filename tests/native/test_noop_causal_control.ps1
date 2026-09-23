# Execute only the extracted production function. In-process fakes: no WPR/MQB/ETW.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$RunnerPath = (Join-Path $PSScriptRoot 'trace_noop_causal.ps1')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    [IO.Path]::GetFullPath($RunnerPath), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$functions = @($ast.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq 'Invoke-OwnedWpr'
})
if ($functions.Count -ne 1) { throw 'Exactly one production control function is required.' }
. ([scriptblock]::Create($functions[0].Extent.Text)) # Never invoke the enclosing entry.
$cases = [Collections.Generic.List[object]]::new()
$failures = 0
function Assert-True([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    $caught = $null
    try { & $Action } catch { $caught = $_.ToString() }
    Assert-True ($null -ne $caught -and $caught -match $Pattern) "Expected refusal matching '$Pattern', got '$caught'"
}
function Write-NewJson($Path, $Value) {
    $leaf = [IO.Path]::GetFileName($Path)
    if ($script:failPaths -contains $leaf) { throw "SYNTHETIC write failure $leaf" }
    Assert-True (-not $script:journal.ContainsKey($leaf)) 'Synthetic journal overwrite'
    $script:journal[$leaf] = $Value
}
function Reset-Control {
    $script:owned = $false; $script:controlSequence = 0
    $script:root = [IO.Path]::GetTempPath(); $script:instance = 'SYNTHETIC-owned-instance'
    $script:failPaths = @(); $script:journal = @{}
    $script:nativeCalls = [Collections.Generic.List[object]]::new()
    $script:fakeExit = 0; $script:fakeThrow = $false
    $script:wpr = {
        $script:nativeCalls.Add([object]@($args))
        Assert-True ($args[-2] -ceq '-instancename' -and $args[-1] -ceq $script:instance) 'Wrong named instance'
        if ($script:fakeThrow) { throw 'SYNTHETIC native exception' }
        $global:LASTEXITCODE = $script:fakeExit
        'SYNTHETIC WPR output; no native command executed'
    }
}
function Invoke-Case([string]$Name, [scriptblock]$Body) {
    Reset-Control
    try {
        & $Body
        $cases.Add(@{name=$Name; passed=$true; error=$null})
        Write-Host "PASS: $Name"
    } catch {
        ++$script:failures
        $cases.Add(@{name=$Name; passed=$false; error=$_.ToString()})
        Write-Host "FAIL: $Name :: $_"
    }
}
Invoke-Case 'normal owned start and stop' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    Assert-True $script:owned 'Successful start must claim ownership'
    Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl')
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 2) 'Successful stop must release ownership'
}
Invoke-Case 'start pre-journal failure dispatches nothing' {
    $script:failPaths = @('wpr-01.started.json')
    Assert-Throws { Invoke-OwnedWpr @('-start', 'SYNTHETIC') } 'SYNTHETIC write'
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 0) 'Unrecorded start was dispatched'
}
Invoke-Case 'nonzero start never grants ownership' {
    $script:fakeExit = 37
    Assert-Throws { Invoke-OwnedWpr @('-start', 'SYNTHETIC') } '37'
    Assert-True (-not $script:owned -and $script:journal['wpr-01.json'].exit_code -eq 37) 'Start failure lost'
}
Invoke-Case 'post-start write failure still reaches owned cleanup' {
    $script:failPaths = @('wpr-01.json')
    Assert-Throws { Invoke-OwnedWpr @('-start', 'SYNTHETIC') } 'journal failed'
    Assert-True $script:owned 'Successful native start lost ownership on journal failure'
    Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl')
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 2) 'Owned stop was not attempted'
}
Invoke-Case 'stop pre-journal failure cannot block owned stop' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:failPaths = @('wpr-02.started.json')
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } 'journal failed'
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 2) 'Stop blocked by journal failure'
    Assert-True ($script:journal['wpr-02.json'].journal_errors.Count -eq 1) 'Original pre-write error missing'
}
Invoke-Case 'stop post-journal failure retains confirmed release and error' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:failPaths = @('wpr-02.json')
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } 'journal failed'
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 2) 'Confirmed stop should not be retried'
}
Invoke-Case 'both stop journal writes fail but command still runs once' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:failPaths = @('wpr-02.started.json', 'wpr-02.json')
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } 'before:.*after:'
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 2) 'Owned stop skipped or duplicated'
}
Invoke-Case 'nonzero stop keeps unresolved ownership without retry' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:fakeExit = 29
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } '29'
    Assert-True ($script:owned -and $script:nativeCalls.Count -eq 2) 'Unsuccessful stop misreported or retried'
}
Invoke-Case 'unowned stop refused before logging or native call' {
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } 'does not own'
    Assert-True ($script:nativeCalls.Count -eq 0 -and $script:journal.Count -eq 0) 'Foreign session control'
}
Invoke-Case 'unknown start outcome never grants ownership' {
    $script:fakeThrow = $true
    Assert-Throws { Invoke-OwnedWpr @('-start', 'SYNTHETIC') } 'SYNTHETIC native exception'
    Assert-True (-not $script:owned -and $null -eq $script:journal['wpr-01.json'].exit_code) 'Unknown exit fabricated'
}
Invoke-Case 'marker log failure preserves ownership for finally' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:failPaths = @('wpr-02.json')
    Assert-Throws { Invoke-OwnedWpr @('-marker', 'SYNTHETIC') } 'journal failed'
    Assert-True $script:owned 'Marker result write lost ownership'
    Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl')
    Assert-True (-not $script:owned -and $script:nativeCalls.Count -eq 3) 'Marker failure cleanup failed'
}
Invoke-Case 'already owned start cannot restart the session' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    Assert-Throws { Invoke-OwnedWpr @('-start', 'SYNTHETIC') } 'does not own'
    Assert-True ($script:owned -and $script:nativeCalls.Count -eq 1) 'Existing session restarted'
}
Invoke-Case 'stop exception keeps unknown release and original error' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:fakeThrow = $true
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } 'SYNTHETIC native exception'
    Assert-True ($script:owned -and $script:nativeCalls.Count -eq 2) 'Unknown stop outcome treated as released'
}
Invoke-Case 'nonzero stop plus lost journals preserve native failure' {
    Invoke-OwnedWpr @('-start', 'SYNTHETIC')
    $script:fakeExit = 29; $script:failPaths = @('wpr-02.started.json', 'wpr-02.json')
    Assert-Throws { Invoke-OwnedWpr @('-stop', 'SYNTHETIC.etl') } '29.*before:.*after:'
    Assert-True ($script:owned -and $script:nativeCalls.Count -eq 2) 'Failed native stop lost or retried'
}
$result = @{schema=1; scope='extracted production function with in-process fakes only'
    tests=$cases.Count; failures=$failures; cases=@($cases.ToArray())
    etw_sessions_started=0; native_wpr_invocations=0; study_mqb_calls=0}
$stream = [IO.File]::Open([IO.Path]::GetFullPath($OutputPath), [IO.FileMode]::CreateNew)
try {
    $data = [Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 10) + "`n")
    $stream.Write($data, 0, $data.Length)
} finally { $stream.Dispose() }
if ($failures) { throw "$failures owned-control contracts failed; original outcomes preserved." }
exit 0

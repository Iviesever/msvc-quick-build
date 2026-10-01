# Tests a private IO boundary with harmless PowerShell children; never original A/B.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputRoot,[Parameter(Mandatory)][string]$PlanPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_runtime.psm1') -Force
$module=Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_capture.psm1') -Force -PassThru
$root=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $root) { throw 'New test root required.' }
$null=New-Item -ItemType Directory -Path $root
$cwd=Join-Path $root 'cwd with spaces'; $null=New-Item -ItemType Directory -Path $cwd
$pwsh=Join-Path $PSHOME $(if ($IsWindows) {'pwsh.exe'} else {'pwsh'})
$child=Join-Path $PSScriptRoot 'fixtures/slot_capture_child.ps1'
$plan=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
$rows=@($plan.blocks | ForEach-Object { $_.rows })
$cases=[Collections.Generic.List[object]]::new(); $failures=0; $children=0
function Check([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
function Refused([scriptblock]$Body,[string]$Message) {
    $found=$false
    try { & $Body } catch { $found=$true; Check ($_.ToString().Contains($Message)) $_.ToString() }
    Check $found 'Expected rejection was not raised.'
}
function Capture([string]$Mode) {
    $argv=@('-NoProfile','-NonInteractive','-File',$child,'-Mode',$Mode,'-Value','literal space & ; ( )')
    & $module { param($exe,$dir,$a) Invoke-SlotNativeEnvelope $exe $dir $a } $pwsh $cwd $argv
}
function Spec($State,$Exe,$Dir,$ArgsValue,$Prefix='') {
    & $module { param($s,$e,$d,$a,$p) Get-PinnedSlotRequest $s $e $d $a $p } $State $Exe $Dir $ArgsValue $Prefix
}
function CheckClock($Value) {
    $q=$Value.clock; $previous=-1L
    Check ($q.frequency -gt 0) 'Missing QPC frequency.'
    foreach ($key in @('outer_start','native_start','native_end','outer_end')) {
        Check ($null -ne $q[$key] -and $q[$key] -ge $previous) ('Invalid clock '+$key)
        $previous=$q[$key]
    }
    Check ($Value.output_format -ceq 'powershell_merged_lines') 'Wrong capture mode.'
    Check ($null -eq $Value.root_times -and $null -eq $Value.cleanup) 'Invented process telemetry.'
}
function Case([string]$Name,[scriptblock]$Body) {
    $errorText=$null
    try { & $Body } catch { $errorText=$_.ToString(); ++$script:failures }
    $cases.Add(@{name=$Name;passed=$null -eq $errorText;error=$errorText})
}
Case 'real success preserves both streams and literal arguments' {
    $before=(Get-Location).Path; ++$script:children; $v=Capture 'success'
    Write-NewJson (Join-Path $root 'success.json') $v
    CheckClock $v
    Check ($v.exit_code -eq 0 -and $null -eq $v.error) 'Success not retained.'
    Check ($v.output_lines.Count -eq 4 -and 'prefix-out' -cin $v.output_lines -and
        'prefix-err' -cin $v.output_lines -and 'value=literal space & ; ( )' -cin $v.output_lines -and
        ('cwd='+$cwd) -cin $v.output_lines) 'Output or literal argument lost.'
    Check ((Get-Location).Path -ceq $before) 'Caller location changed.'
}
Case 'real nonzero exit and diagnostic prefix remain data' {
    ++$script:children; $v=Capture 'nonzero'; Write-NewJson (Join-Path $root 'nonzero.json') $v
    CheckClock $v
    Check ($v.exit_code -eq 37 -and $null -eq $v.error) 'Nonzero exit was masked.'
    Check ('prefix-out' -cin $v.output_lines -and 'prefix-err' -cin $v.output_lines) 'Failure prefix lost.'
}
Case 'empty output remains an empty array with exit zero' {
    ++$script:children; $v=Capture 'empty'; Write-NewJson (Join-Path $root 'empty.json') $v
    CheckClock $v; Check ($v.exit_code -eq 0 -and $v.output_lines.Count -eq 0) 'Empty output changed.'
}
Case 'unicode line survives the explicitly textual boundary' {
    ++$script:children; $v=Capture 'unicode'; Write-NewJson (Join-Path $root 'unicode.json') $v
    CheckClock $v
    Check ($v.exit_code -eq 0 -and $v.output_lines.Count -eq 1 -and
        $v.output_lines[0] -ceq '日本語 / 中文 / café') 'Unicode changed.'
}
Case 'both output streams drain without dropping lines' {
    ++$script:children; $v=Capture 'many'; Write-NewJson (Join-Path $root 'many.json') $v
    CheckClock $v; Check ($v.exit_code -eq 0 -and $v.output_lines.Count -eq 128) 'Line count changed.'
    for ($i=0;$i -lt 64;++$i) {
        Check (('out-'+$i) -cin $v.output_lines -and ('err-'+$i) -cin $v.output_lines) 'Stream line lost.'
    }
}
Case 'missing program keeps unknown exit rather than stale success' {
    $before=(Get-Location).Path; $LASTEXITCODE=0
    $v=& $module {param($e,$d) Invoke-SlotNativeEnvelope $e $d @()} (Join-Path $root 'absent.exe') $cwd
    Write-NewJson (Join-Path $root 'missing.json') $v
    Check ($null -eq $v.exit_code -and $null -ne $v.error) 'Missing program appeared successful.'
    Check ((Get-Location).Path -ceq $before) 'Missing program changed caller location.'
}
Case 'missing working directory cannot dispatch and restores location' {
    $before=(Get-Location).Path
    $v=& $module {param($e,$d) Invoke-SlotNativeEnvelope $e $d @()} $pwsh (Join-Path $root 'absent-dir')
    Write-NewJson (Join-Path $root 'missing-cwd.json') $v
    Check ($null -eq $v.exit_code -and $null -ne $v.error -and $null -eq $v.clock.native_start) 'Bad cwd dispatched.'
    Check ((Get-Location).Path -ceq $before) 'Bad cwd changed caller location.'
}
Case 'only the restricted adapter is exported; import has no native fallback' {
    $exports=@($module.ExportedFunctions.Keys)
    Check ($exports.Count -eq 1 -and $exports[0] -ceq 'Invoke-PinnedSlotCapture') 'Unexpected exports.'
    Refused { Invoke-PinnedSlotCapture @{context=$null} '' '' @() '' } 'Validated native session required'
}
Case 'independently derived 48 requests agree with fixed planner without launching' {
    foreach ($row in $rows) {
        $s=@{root=$root;attempted=[int]$row.sequence;owned=@{};plan=$plan}
        $s.owned[$row.slot]=@{sha256=$row.sha256;size=1933824}
        $v=Spec $s (Join-Path $root ('slots/'+$row.slot+'.exe')) `
            (Join-Path $root ('fixtures/'+$row.block)) ([string[]]$row.argv)
        Check ($v.sha256 -ceq $row.sha256 -and ($v.argv -join '|') -ceq ($row.argv -join '|')) 'Schedule mismatch.'
    }
}
Case 'wrong path argv identity size prefix and budget are refused without dispatch' {
    $row=$rows[0]; $s=@{root=$root;attempted=1;owned=@{L=@{sha256=$row.sha256;size=1933824}};plan=$plan}
    $exe=Join-Path $root 'slots/L.exe'; $dir=Join-Path $root 'fixtures/r1-AA'
    Refused { Spec $s 'foreign.exe' $dir $row.argv } 'path or argument'
    Refused { Spec $s $exe 'foreign-cwd' $row.argv } 'path or argument'
    Refused { Spec $s $exe $dir @('--help') } 'path or argument'
    $bad=@($row.argv);$bad[0]='another.cpp'
    Refused { Spec $s $exe $dir $bad } 'argument contract'
    Refused { Spec $s $exe $dir $row.argv 'extra-output' } 'path or argument'
    $s.owned.L.sha256='0'*64; Refused { Spec $s $exe $dir $row.argv } 'Pinned slot identity'
    $s.owned.L.sha256=$row.sha256;$s.owned.L.size=1
    Refused { Spec $s $exe $dir $row.argv } 'Pinned slot identity'
    foreach ($n in @(0,49,'1')) { $s.attempted=$n; Refused { Spec $s $exe $dir $row.argv } 'fixed budget' }
}
Case 'entry defaults to refusal before preparation or dispatch' {
    $entry=Join-Path $PSScriptRoot 'run_noop_identity_slots.ps1'
    Refused { & $entry } 'Explicit Windows'
    Check (-not (Test-Path (Join-Path $root 'completion.json'))) 'Entry created a study.'
}
Write-NewJson (Join-Path $root 'result.json') @{schema=1;tests=$cases.Count;failures=$failures
    cases=@($cases.ToArray());harmless_child_launches=$children;study_mqb_calls=0;msvc_calls=0;etw_sessions=0
    scope='Private capture IO uses five harmless PowerShell child processes. Public pinned A/B adapter never dispatched.'}
if ($failures) { throw "$failures native capture contract cases failed." }

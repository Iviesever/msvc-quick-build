# Explicit manual entry. No workflow is added and no measurement is allocated here.
[CmdletBinding()]
param(
    [string]$ArtifactPath,
    [string]$OutputRoot,
    [string]$ReviewedCommit,
    [string]$AllocationLabel,
    [switch]$ExecuteReviewedDiagnostic
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_runtime.psm1') -Force
$environment=@{actions=$env:GITHUB_ACTIONS;repository=$env:GITHUB_REPOSITORY
    event=$env:GITHUB_EVENT_NAME;attempt=$env:GITHUB_RUN_ATTEMPT
    disposable=$env:MQB_SLOT_DISPOSABLE;allocation=$env:MQB_SLOT_ALLOCATION}
Assert-SlotAdmission ([bool]$ExecuteReviewedDiagnostic) $IsWindows $PSVersionTable.PSVersion.Major `
    ([Environment]::Is64BitProcess) $ReviewedCommit $AllocationLabel $environment
# No output directory, source preparation or native launch precedes admission.
if ($env:GITHUB_RUN_ID -cnotmatch '^[1-9][0-9]*$' -or -not $env:RUNNER_TEMP) {
    throw 'Exact hosted run and temporary root required.'
}
Assert-SlotPath $env:RUNNER_TEMP
$expectedRoot=Join-Path $env:RUNNER_TEMP ($AllocationLabel+'-'+$env:GITHUB_RUN_ID+'-1')
if ($OutputRoot -cne $expectedRoot -or (Test-Path -LiteralPath $OutputRoot)) {
    throw 'New exact allocation-owned temporary directory required; no resume.'
}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$git=(Get-Command git -CommandType Application -ErrorAction Stop).Source
$python=(Get-Command python -CommandType Application -ErrorAction Stop).Source
$head=@(& $git -C $repo rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1 -or $head[0] -cne $ReviewedCommit) {
    throw 'Checked-out source differs from reviewed commit.'
}
$dirty=@(& $git -C $repo status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Tracked collector source is dirty.' }
$tool=Join-Path $PSScriptRoot 'noop_identity_slots.py'
& $python -B $tool prepare --archive $ArtifactPath --root $OutputRoot --repo $repo `
    --reviewed-commit $ReviewedCommit --allocation $AllocationLabel --output (Join-Path $OutputRoot 'prepared.json')
if ($LASTEXITCODE -ne 0) { throw 'Preparation failed; preserve any existing prefix.' }
& $python -B $tool native-preflight --root $OutputRoot --repo $repo --reviewed-commit $ReviewedCommit `
    --allocation $AllocationLabel --output (Join-Path $OutputRoot 'native-preflight.json')
if ($LASTEXITCODE -ne 0) { throw 'Native preflight refused; no measurement started.' }
$plan=Get-Content -LiteralPath (Join-Path $OutputRoot 'plan.json') -Raw | ConvertFrom-Json
$hostRecord=@{plan_sha256=Get-Digest (Join-Path $OutputRoot 'plan.json');reviewed_commit=$ReviewedCommit
    allocation_label=$AllocationLabel;qpc_frequency=[Diagnostics.Stopwatch]::Frequency
    native_entry='run_noop_identity_slots.ps1';protocol='pinned_819_merged_lines_v1'
    run_id=$env:GITHUB_RUN_ID;run_attempt=$env:GITHUB_RUN_ATTEMPT;event=$env:GITHUB_EVENT_NAME
    repository=$env:GITHUB_REPOSITORY;image=$env:ImageVersion;powershell=$PSVersionTable.PSVersion.ToString()
    execution_allocated_by_plan=$false;may_clear_hold=$false}
Write-NewJson (Join-Path $OutputRoot 'host.json') $hostRecord
Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_capture.psm1') -Force
$context=@{kind='reviewed_819_native_session';root=$OutputRoot;reviewed=$ReviewedCommit;allocation=$AllocationLabel}
$launcher={param($state,$executable,$cwd,$argv,$prefix)
    noop_identity_slots_capture\Invoke-PinnedSlotCapture $state $executable $cwd $argv $prefix}
$state=New-SlotState -Root $OutputRoot -Plan $plan -Launcher $launcher -Context $context
# The existing coordinator preserves first failure/pending requests and performs
# zero extra hash/manifest/journal/resource operations inside measurement windows.
Invoke-SlotStudy $state
& $python -B $tool audit-native --root $OutputRoot --output (Join-Path $OutputRoot 'audit.json')
if ($LASTEXITCODE -ne 0) { throw 'Native audit refused; retain all evidence and do not retry.' }
Write-Host 'Diagnostic recorded; original #819 remains HOLD. No qualification decision was produced.'

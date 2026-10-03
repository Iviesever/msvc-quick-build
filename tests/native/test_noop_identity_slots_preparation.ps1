[CmdletBinding()]
param([string]$ArtifactPath,[string]$OutputRoot,[string]$RepoRoot,[string]$ReviewedCommit)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_runtime.psm1') -Force
$result=Invoke-SlotPreparation -ArtifactPath $ArtifactPath -OutputRoot $OutputRoot -RepoRoot $RepoRoot `
    -ReviewedCommit $ReviewedCommit -AllocationLabel 'pr232-slot-001'
if ($result.mqb_calls -ne 0 -or $result.execution_authority) { throw 'Preparation must not authorize execution.' }
Write-NewJson (Join-Path $OutputRoot 'preparation-only.json') $result

# One manually allocated study. PR/push contracts must never invoke this entrypoint.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactPath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$ReviewedCommit,
    [Parameter(Mandatory)][string]$ManifestSha256,
    [Parameter(Mandatory)][string]$Allocation
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'Windows and PowerShell 7 required.'
}
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $root) { throw 'New root required; no resume or overwrite.' }
$checkout = @(& git -C $sourceRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $checkout.Count -ne 1) { throw 'Cannot identify checkout.' }
$pythonCommand = Get-Command python -CommandType Application | Select-Object -First 1
$pythonExe = [IO.Path]::GetFullPath($pythonCommand.Source)
$checker = Join-Path $PSScriptRoot 'retained_noop_executor.py'
$pins = [Collections.Generic.List[IO.FileStream]]::new()
$prepared = $false; $collectorInvoked = $false; $complete = $false; $failure = $null
try {
    # Resolve once and deny write/delete of this interpreter during the invocation.
    # This does not pin its stdlib/DLLs or prove a hermetic environment.
    $pins.Add([IO.File]::Open($pythonExe, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
    & $pythonExe -B $checker prepare ([IO.Path]::GetFullPath($ArtifactPath)) `
        --source-root $sourceRoot --output $root --approved-commit $ReviewedCommit `
        --checkout-commit $checkout[0] --manifest-sha $ManifestSha256 --allocation $Allocation
    if ($LASTEXITCODE -ne 0) { throw 'Admission/input/source validation refused; no study dispatched.' }
    $prepared = $true
    $snapshot = Join-Path $root 'source'
    foreach ($f in @(Get-ChildItem -LiteralPath $snapshot -File -Recurse)) {
        $pins.Add([IO.File]::Open($f.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
    }
    $pins.Add([IO.File]::Open((Join-Path $root 'original-701.zip'), [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read))
    # The unchanged collector's `python` checks inherit this local alias, with no fallback.
    Set-Alias -Name python -Value $pythonExe -Scope Local -Force
    $collector = Join-Path $snapshot 'tests/native/collect_external_noop_boundary.ps1'
    $collectorInvoked = $true
    & $collector -ArtifactPath (Join-Path $root 'original-701.zip') `
        -OutputRoot (Join-Path $root 'study') -ExecuteRegisteredStudy
    if ($LASTEXITCODE -ne 0) { throw 'Collector returned failure; retain prefix without retry.' }
    & $pythonExe -B (Join-Path $snapshot 'tests/native/retained_noop_executor.py') finish $root `
        --output (Join-Path $root 'execution-audit.json')
    if ($LASTEXITCODE -ne 0) { throw 'Execution closure refused; do not refill or clear HOLD.' }
    $complete = $true
} catch { $failure = $_.ToString() }
finally {
    foreach ($pin in $pins) { $pin.Dispose() }
    if ($prepared) {
        $summary = [ordered]@{ schema = 1; status = $(if ($complete) { 'completed_diagnostic_only' } else { 'stopped' })
            collector_invoked = $collectorInvoked; error = $failure; clears_hold = $false; cause = $null }
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($summary | ConvertTo-Json -Depth 10) + "`n")
        $s = [IO.File]::Open((Join-Path $root 'execution-completion.json'), [IO.FileMode]::CreateNew)
        try { $s.Write($bytes, 0, $bytes.Length) } finally { $s.Dispose() }
    }
}
if ($null -ne $failure) { throw $failure }
Write-Host 'Diagnostic collection complete. HOLD remains; no performance clearance or causal classification.'

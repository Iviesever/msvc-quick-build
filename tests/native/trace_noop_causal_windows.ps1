# Explicit post-prime windows. Existing consumed workflow never invokes this entry.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactPath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$ReviewedCommit,
    [Parameter(Mandatory)][string]$ProfileSha256,
    [switch]$ExecuteReviewedWindows
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if (-not $ExecuteReviewedWindows) { throw 'Separate exact-source trace allocation required.' }
if ($env:MQB_CAUSAL_DISPOSABLE_HOST -ne '1') { throw 'An explicitly approved disposable host is required.' }
if (-not $IsWindows -or $PSVersionTable.PSVersion.Major -lt 7) { throw 'Windows / PowerShell 7 required.' }
if ($env:GITHUB_ACTIONS -eq 'true' -and ($env:GITHUB_EVENT_NAME -ne 'workflow_dispatch' -or $env:GITHUB_RUN_ATTEMPT -ne '1')) {
    throw 'No automatic CI study or repeated attempt.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = [IO.Path]::GetFullPath($OutputRoot)
$head = @(& git -C $repo rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1 -or $head[0] -cne $ReviewedCommit) { throw 'Wrong reviewed checkout.' }
$dirty = @(& git -C $repo status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) { throw 'Tracked checkout is not clean.' }
if (Test-Path -LiteralPath $root) { throw 'New output directory required, never resume.' }
if ([IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).AvailableFreeSpace -lt 4GB) { throw 'At least 4 GiB free required.' }
$python = (Get-Command python -CommandType Application | Select-Object -First 1).Source
$wpr = (Get-Command wpr.exe -CommandType Application | Select-Object -First 1).Source
$checker = Join-Path $repo 'tests/native/noop_causal_windows.py'
& $python -B $checker prepare --archive ([IO.Path]::GetFullPath($ArtifactPath)) --root $root `
    --repo $repo --reviewed-commit $ReviewedCommit --profile-sha $ProfileSha256 `
    --output (Join-Path $root 'preparation.json')
if ($LASTEXITCODE -ne 0) { throw 'Input/plan preparation failed; zero study calls.' }
$plan = Get-Content -LiteralPath (Join-Path $root 'plan.json') -Raw | ConvertFrom-Json

# Reuse ONLY the original four functions, not the old script's 40-call entry.
$tokens = $null; $parseErrors = $null
$source = Join-Path $root 'source/tests/native/collect_external_noop_boundary.ps1'
$ast = [Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Original launcher parse failed.' }
$names = @('Write-NewJson', 'Get-Digest', 'Get-FileManifest', 'Invoke-LegacyBoundary')
foreach ($name in $names) {
    $matches = @($ast.EndBlock.Statements | Where-Object {
        $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name
    })
    if ($matches.Count -ne 1) { throw "Non-unique original function $name" }
    . ([scriptblock]::Create($matches[0].Extent.Text))
}
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class MqbNoopCausalThread {
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}
'@

$controlTokens=$null; $controlErrors=$null
$controlAst=[Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'source/tests/native/trace_noop_causal.ps1'),[ref]$controlTokens,[ref]$controlErrors)
if ($controlErrors.Count) { throw 'Reviewed owned-control source failed parsing.' }
$controls=@($controlAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq 'Invoke-OwnedWpr'
})
if ($controls.Count -ne 1) { throw 'Expected exactly one original owned-control function.' }
. ([scriptblock]::Create($controls[0].Extent.Text))
$profile=Join-Path $root 'source/tests/native/noop_causal_trace.wprp'
$controlSequence=0; $attempted=0; $owned=$false; $stopError=$null; $failure=$null

function Invoke-CausalWindowRow {
    param($cell, $row, $cellRecord, [string]$fixture)
    if ($attempted -ge 32 -or $row.sequence -ne ($attempted+1)) { throw 'Fixed call budget/order changed.' }
    ++$script:attempted
    $executable = Join-Path $root ('inputs/'+$cell.side+'.exe')
    $tidBefore = [MqbNoopCausalThread]::GetCurrentThreadId()
    $record = Invoke-LegacyBoundary $executable $fixture ([string[]]$row.argv) ''
    $record.host_tid_before = $tidBefore
    $record.host_tid_after = [MqbNoopCausalThread]::GetCurrentThreadId()
    $record.row = $row; $record.argv = @($row.argv)
    $record.executable_sha256 = $plan.original.binaries.($cell.side)
    $record.dispatch_attempted = $true; $record.clears_hold = $false
    $cellRecord.calls.Add($record) # Retain original result before any validation.
    if ($null -ne $record.error -or $record.exit_code -ne 0) { throw 'Original call failed; no continuation.' }
    $lines = @($record.output_lines)
    if ($row.phase -eq 'prime') {
        if (@($lines | Where-Object { $_.StartsWith('[compile] ') }).Count -ne 2 -or
            @($lines | Where-Object { $_.StartsWith('[link] ') }).Count -ne 1) { throw 'Invalid fresh prime.' }
    } else {
        $progress = @($lines | Where-Object { $_.StartsWith('[up-to-date] ') })
        if (($progress -join "`n") -cne "[up-to-date] 2 translation units`n[up-to-date] timing_bench.exe" -or
            @($lines | Where-Object { $_.StartsWith('[compile] ') -or $_.StartsWith('[link] ') }).Count) {
            throw 'No-op performed work or has wrong compact status.'
        }
    }
    $timings = @($lines | Where-Object { $_.StartsWith('{"type":"mqb.timings"') })
    $enabled = $row.argv[-1] -ceq '--timings=json'
    if ($timings.Count -ne [int]$enabled) { throw 'Wrong timing mode.' }
    if ($enabled) {
        $t = $timings[0] | ConvertFrom-Json
        if ($t.cache.compile.hits -ne 2 -or $t.cache.compile.misses -ne 0 -or
            $t.cache.link.hits -ne 1 -or $t.cache.link.misses -ne 0) { throw 'Timed middle was not a no-op.' }
    }
}
function Get-CausalFreeSpace {
    return [IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).AvailableFreeSpace
}
function Assert-CausalWindowBudget {
    param([string]$Segment)
    [long]$total=0; [long]$segmentBytes=0
    $traceRoot=Join-Path $root 'traces'
    foreach ($item in @(Get-ChildItem -LiteralPath $traceRoot -Recurse -Force)) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Trace reparse path refused.' }
        if (-not $item.PSIsContainer) { $total += $item.Length }
    }
    if ($total -gt 536870912) { throw 'Total trace checkpoint exceeded 512 MiB; retain all files.' }
    $etl=Join-Path $Segment 'trace.etl'
    if (Test-Path -LiteralPath $etl) {
        $segmentBytes=(Get-Item -LiteralPath $etl).Length
        if ($segmentBytes -gt 268435456) { throw 'Segment ETL checkpoint exceeded 256 MiB.' }
    }
    $temp=Join-Path $Segment 'temp'
    if (Test-Path -LiteralPath $temp) {
        foreach ($item in @(Get-ChildItem -LiteralPath $temp -File -Recurse -Force)) {
            if ($item.Length -ge 268435456) { throw 'Trace reached registered kernel file limit.' }
        }
    }
    [long]$free=Get-CausalFreeSpace
    if ($free -lt 4294967296) { throw 'At least 4 GiB free required at each window boundary.' }
    return @{observed_trace_bytes=$total; segment_etl_bytes=$segmentBytes; free_bytes=$free}
}
function Invoke-PostPrimeCell {
    param($cell)
    if ($script:owned) { throw 'Prior window still owned; no next prime or start.' }
    if ($script:controlSequence -ge 60) { throw 'Fixed window control budget exhausted.' }
    $fixture=Join-Path $root ('fixtures/'+$cell.id)
    $segment=Join-Path $root ('traces/'+$cell.id)
    if (Test-Path -LiteralPath $segment) { throw 'New window directory required; never resume.' }
    $null=New-Item -ItemType Directory -Path $segment
    $null=New-Item -ItemType Directory -Path (Join-Path $segment 'temp')
    $script:instance='MQB-NoopWindow-'+[guid]::NewGuid().ToString('N')
    $window=[ordered]@{instance=$instance; first_control=($script:controlSequence+1); last_control=$null
        start_qpc=$null; ready_qpc=$null; calls_end_qpc=$null; stop_qpc=$null; stopped_qpc=$null
        stop_attempted=$false; stop_error=$null; owned_after_stop=$null; budget_before=$null; budget_after=$null}
    $cellRecord=[ordered]@{cell=$cell; before=$null; after_prime=$null; after_final=$null
        calls=[Collections.Generic.List[object]]::new(); error=$null; window=$window}
    $primary=$null; $saveError=$null
    try {
        $window.budget_before=Assert-CausalWindowBudget $segment
        $cellRecord.before=Get-FileManifest $fixture
        # Prime remains one real planned call, outside every WPR session.
        Invoke-CausalWindowRow $cell $cell.rows[0] $cellRecord $fixture
        $cellRecord.after_prime=Get-FileManifest $fixture
        Write-NewJson (Join-Path $root ('primes/'+$cell.id+'.json')) @{
            cell=$cell; before=$cellRecord.before; after_prime=$cellRecord.after_prime; call=$cellRecord.calls[0]}
        $window.budget_before=Assert-CausalWindowBudget $segment
        $window.start_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        Invoke-OwnedWpr -WprArguments @('-start', ($profile+'!MqbNoopCausal.Verbose'), '-filemode', '-recordtempto', (Join-Path $segment 'temp'))
        Invoke-OwnedWpr -WprArguments @('-marker', ('MQB_NOOP_WINDOW|'+$cell.id+'|begin'))
        $window.ready_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        # Only the original launcher and in-memory validation in this loop.
        foreach ($row in @($cell.rows | Select-Object -Skip 1)) {
            Invoke-CausalWindowRow $cell $row $cellRecord $fixture
        }
        $window.calls_end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        Invoke-OwnedWpr -WprArguments @('-marker', ('MQB_NOOP_WINDOW|'+$cell.id+'|end'))
        Invoke-OwnedWpr -WprArguments @('-status', 'collectors', '-details')
        $null=Assert-CausalWindowBudget $segment
    } catch { $primary=$_.ToString(); $cellRecord.error=$primary }
    finally {
        # One stop attempt only, even if native stop or its journals fail.
        if ($script:owned) {
            $window.stop_attempted=$true
            $window.stop_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
            try {
                Invoke-OwnedWpr -WprArguments @('-stop', (Join-Path $segment 'trace.etl'), 'MQB post-prime causal window; not a score', '-skipPdbGen')
            } catch { $window.stop_error=$_.ToString(); $script:stopError=$window.stop_error }
            $window.stopped_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        }
        $window.owned_after_stop=[bool]$script:owned
        $window.last_control=$script:controlSequence
        try {
            # Post-stop work is outside the next window; a stopped prefix is never resumed.
            if ($null -eq $primary -and $null -eq $window.stop_error) {
                $window.budget_after=Assert-CausalWindowBudget $segment
                if (-not (Test-Path -LiteralPath (Join-Path $segment 'trace.etl')) -or
                    $window.budget_after.segment_etl_bytes -le 0) { throw 'Missing segment ETL.' }
                $cellRecord.after_final=Get-FileManifest $fixture
                if (($cellRecord.after_prime | ConvertTo-Json -Depth 8 -Compress) -cne
                    ($cellRecord.after_final | ConvertTo-Json -Depth 8 -Compress)) { throw 'No-op sequence changed fixture.' }
            }
        } catch { $primary=$_.ToString(); $cellRecord.error=$primary }
        try { Write-NewJson (Join-Path $root ('cells/'+$cell.id+'.json')) $cellRecord }
        catch { $saveError=$_.ToString() }
    }
    if ($null -ne $primary -or $null -ne $window.stop_error -or $null -ne $saveError) {
        throw "Window stopped; original: $primary; stop: $($window.stop_error); save: $saveError"
    }
}

$pins=[Collections.Generic.List[IO.FileStream]]::new()
try {
    foreach ($side in @('baseline','candidate')) {
        $file=Join-Path $root "inputs/$side.exe"
        if ((Get-Digest $file) -cne $plan.original.binaries.$side) { throw 'Binary changed after preparation.' }
        $pins.Add([IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
    }
    Write-NewJson (Join-Path $root 'host.json') @{pid=$PID; powershell=$PSVersionTable.PSVersion.ToString()
        qpc_frequency=[Diagnostics.Stopwatch]::Frequency; utc=[DateTime]::UtcNow.ToString('o')
        wpr_path=$wpr; wpr_sha256=(Get-Digest $wpr); image_version=$env:ImageVersion
        reviewed_commit=$ReviewedCommit; clears_hold=$false}
    foreach ($cell in $plan.cells) { Invoke-PostPrimeCell $cell }
} catch { $failure=$_.ToString() }
finally {
    # Each cell owns its sole cleanup. Never retry a failed or uncertain stop here.
    foreach ($pin in $pins) { $pin.Dispose() }
    Write-NewJson (Join-Path $root 'completion.json') @{status=$(if ($null -eq $failure -and $null -eq $stopError -and $attempted -eq 32) {
        'captured_windows_unreviewed' } else { 'stopped' }); attempted=$attempted
        error=$failure; trace_stop_error=$stopError; clears_hold=$false; cause=$null}
}
if ($null -ne $failure -or $null -ne $stopError) { throw 'Windows/calls stopped; retain all files, do not retry.' }
& $python -B $checker audit --root $root --output (Join-Path $root 'journal-audit.json')
if ($LASTEXITCODE -ne 0) { throw 'Window journal audit failed; no event-health or cause verdict.' }

# Narrow, manually reviewed diagnostic. No pipeline automatically invokes this entry.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactPath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$ReviewedCommit,
    [Parameter(Mandatory)][string]$ProfileSha256,
    [switch]$ExecuteReviewedTrace
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if (-not $ExecuteReviewedTrace) { throw 'Separate exact-source trace allocation required.' }
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
$checker = Join-Path $repo 'tests/native/noop_causal_trace.py'
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
$instance = 'MQB-NoopCausal-' + [guid]::NewGuid().ToString('N')
$profile = Join-Path $root 'source/tests/native/noop_causal_trace.wprp'
$etl = Join-Path $root 'trace.etl'
$controlSequence = 0
function Invoke-OwnedWpr {
    param([string[]]$WprArguments)
    $isStart = $WprArguments[0] -ceq '-start'
    $isStop = $WprArguments[0] -ceq '-stop'
    if (($isStart -and $script:owned) -or (-not $isStart -and -not $script:owned)) {
        throw 'WPR control refused: this invocation does not own the required instance state.'
    }
    ++$script:controlSequence
    $prefix = Join-Path $root ('wpr-{0:d2}' -f $script:controlSequence)
    $journalErrors = [Collections.Generic.List[string]]::new()
    try { Write-NewJson ($prefix + '.started.json') @{ argv=$WprArguments; instance=$instance } }
    catch {
        # Logging gates new work, but must not prevent releasing our own session.
        if (-not $isStop) { throw }
        $journalErrors.Add('before: ' + $_.ToString())
    }
    $lines = @(); $code = $null; $commandError = $null
    try {
        $PSNativeCommandUseErrorActionPreference = $false
        $lines = @(& $wpr @WprArguments -instancename $instance 2>&1)
        $code = $LASTEXITCODE
        # Ownership follows confirmed native completion, BEFORE fallible journal I/O.
        if ($code -eq 0) {
            if ($isStart) { $script:owned = $true }
            if ($isStop) { $script:owned = $false }
        }
    } catch { $commandError = $_.ToString() }
    try {
        Write-NewJson ($prefix + '.json') @{ argv=$WprArguments; instance=$instance; exit_code=$code
            command_error=$commandError; journal_errors=@($journalErrors.ToArray())
            lines=@($lines | ForEach-Object { [string]$_ }) }
    } catch { $journalErrors.Add('after: ' + $_.ToString()) }
    $evidenceError = $journalErrors -join '; '
    if ($null -ne $commandError) { throw "WPR invocation failed: $commandError; journal: $evidenceError" }
    if ($null -eq $code -or $code -ne 0) { throw "WPR failed ($code); journal: $evidenceError" }
    if ($journalErrors.Count -ne 0) { throw "WPR command completed but journal failed: $evidenceError" }
}
$attempted = 0; $failure = $null; $stopError = $null; $owned = $false
$pins = [Collections.Generic.List[IO.FileStream]]::new()
try {
    foreach ($side in @('baseline','candidate')) {
        $file = Join-Path $root "inputs/$side.exe"
        if ((Get-Digest $file) -cne $plan.original.binaries.$side) { throw 'Binary changed after preparation.' }
        $pins.Add([IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
    }
    Write-NewJson (Join-Path $root 'host.json') @{ pid=$PID; powershell=$PSVersionTable.PSVersion.ToString()
        qpc_frequency=[Diagnostics.Stopwatch]::Frequency; utc=[DateTime]::UtcNow.ToString('o')
        wpr_path=$wpr; wpr_sha256=(Get-Digest $wpr); image_version=$env:ImageVersion
        instance=$instance; reviewed_commit=$ReviewedCommit; clears_hold=$false }
    # A unique instance is never cancelled/reused. A start failure does not grant ownership.
    Invoke-OwnedWpr -WprArguments @('-start', ($profile+'!MqbNoopCausal.Verbose'), '-filemode', '-recordtempto', (Join-Path $root 'wpr-temp'))
    foreach ($cell in $plan.cells) {
        $fixture = Join-Path $root ('fixtures/'+$cell.id)
        $cellRecord = [ordered]@{cell=$cell; before=(Get-FileManifest $fixture); after_prime=$null
            after_final=$null; calls=[Collections.Generic.List[object]]::new(); error=$null}
        try {
            Invoke-OwnedWpr -WprArguments @('-marker', ('MQB_NOOP_CAUSAL|'+$cell.id+'|begin'))
            foreach ($row in $cell.rows) {
                if ($attempted -ge 32 -or $row.sequence -ne ($attempted+1)) { throw 'Fixed call budget/order changed.' }
                ++$attempted
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
                    $cellRecord.after_prime = Get-FileManifest $fixture
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
                # No Python process, file hash, WPR utility or journal write between middle and final.
            }
            $cellRecord.after_final = Get-FileManifest $fixture
            if (($cellRecord.after_prime | ConvertTo-Json -Depth 8 -Compress) -cne
                ($cellRecord.after_final | ConvertTo-Json -Depth 8 -Compress)) { throw 'No-op sequence changed fixture.' }
            Invoke-OwnedWpr -WprArguments @('-marker', ('MQB_NOOP_CAUSAL|'+$cell.id+'|end'))
            foreach ($file in @(Get-ChildItem (Join-Path $root 'wpr-temp') -File -Recurse -Force)) {
                if ($file.Length -ge 268435456) { throw 'Trace reached registered kernel file limit.' }
            }
        } catch { $cellRecord.error = $_.ToString(); throw }
        finally { Write-NewJson (Join-Path $root ('cells/'+$cell.id+'.json')) $cellRecord }
    }
    Invoke-OwnedWpr -WprArguments @('-status', 'collectors', '-details')
} catch { $failure = $_.ToString() }
finally {
    if ($owned) {
        try { Invoke-OwnedWpr -WprArguments @('-stop', $etl, 'MQB three-arm causal diagnostic; not a score', '-skipPdbGen') }
        catch { $stopError = $_.ToString() }
    }
    foreach ($pin in $pins) { $pin.Dispose() }
    Write-NewJson (Join-Path $root 'completion.json') @{status=$(if ($null -eq $failure -and $null -eq $stopError -and $attempted -eq 32) {
        'captured_trace_unreviewed' } else { 'stopped' }); attempted=$attempted
        error=$failure; trace_stop_error=$stopError; clears_hold=$false; cause=$null}
}
if ($null -ne $failure -or $null -ne $stopError) { throw 'Trace/calls stopped; preserve all files. Do not retry.' }
& $python -B $checker audit --root $root --output (Join-Path $root 'journal-audit.json')
if ($LASTEXITCODE -ne 0) { throw 'Journal audit failed. ETL completeness/causality remain unreviewed.' }

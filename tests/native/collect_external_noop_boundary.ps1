# Diagnostic only. Never called by opened/synchronize/Ready CI.
[CmdletBinding()]
param(
    [string]$ArtifactPath,
    [string]$OutputRoot,
    [switch]$ExecuteRegisteredStudy
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Write-NewJson {
    param([string]$Path, $Value)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $text = ($Value | ConvertTo-Json -Depth 30) + "`n"
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}

function Get-Digest {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-FileManifest {
    param([string]$Root)
    $files = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    $directories = 0
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        if (++$directories -gt 256) { throw 'Directory budget exceeded.' }
        $d = Get-Item -LiteralPath $directory -Force
        if ($d.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse directory refused.' }
        foreach ($f in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if ($f.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse entry refused.' }
            if ($f.PSIsContainer) { $pending.Push($f.FullName); continue }
            if ($files.Count -ge 2048 -or $f.Length -gt 268435456) { throw 'File budget exceeded.' }
            $files.Add([ordered]@{
                path = [IO.Path]::GetRelativePath($Root, $f.FullName).Replace('\', '/')
                size = [long]$f.Length; mtime_ticks = [long]$f.LastWriteTimeUtc.Ticks
                sha256 = Get-Digest $f.FullName
            })
        }
    }
    return ,@($files | Sort-Object { $_.path })
}

function Initialize-RootTimes {
    if ('MqbNoopRootTimes' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class MqbNoopRootTimes {
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool GetProcessTimes(IntPtr process, out ulong created,
        out ulong exited, out ulong kernel, out ulong user);
    public ulong Created, Exited, Kernel, User;
    public static MqbNoopRootTimes Read(IntPtr process) {
        var r = new MqbNoopRootTimes();
        if (!GetProcessTimes(process, out r.Created, out r.Exited, out r.Kernel, out r.User))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return r;
    }
}
'@
}

function Invoke-LegacyBoundary {
    param([string]$Executable, [string]$Cwd, [string[]]$Argv, [string]$Prefix)
    $PSNativeCommandUseErrorActionPreference = $false
    $output = @(); $exitCode = $null; $errorText = $null; $pushed = $false
    $c = [ordered]@{ frequency = [Diagnostics.Stopwatch]::Frequency
        outer_start = $null; native_start = $null; native_end = $null; outer_end = $null }
    $c.outer_start = [Diagnostics.Stopwatch]::GetTimestamp()
    try {
        Push-Location -LiteralPath $Cwd; $pushed = $true
        $c.native_start = [Diagnostics.Stopwatch]::GetTimestamp()
        $output = @(& $Executable @Argv 2>&1)
        $exitCode = $LASTEXITCODE
    } catch { $errorText = $_.ToString() }
    finally {
        $c.native_end = [Diagnostics.Stopwatch]::GetTimestamp()
        if ($pushed) {
            try { Pop-Location } catch { $errorText = "$errorText`nPop-Location: $_" }
        }
        $c.outer_end = [Diagnostics.Stopwatch]::GetTimestamp()
    }
    # Conversion/storage is outside the measured interval. Legacy merged lines
    # are not per-stream bytes; the native envelope is NOT a root lifetime.
    return [ordered]@{ clock = $c; exit_code = $exitCode; error = $errorText
        output_format = 'powershell_merged_lines'; output_lines = @($output | ForEach-Object { [string]$_ })
        root_times = $null; cleanup = $null }
}

function Invoke-ProcessBoundary {
    param([string]$Executable, [string]$Cwd, [string[]]$Argv, [string]$Prefix)
    $p = [Diagnostics.Process]::new()
    $p.StartInfo.FileName = $Executable; $p.StartInfo.WorkingDirectory = $Cwd
    $p.StartInfo.UseShellExecute = $false
    $p.StartInfo.RedirectStandardOutput = $true; $p.StartInfo.RedirectStandardError = $true
    foreach ($a in $Argv) { $p.StartInfo.ArgumentList.Add($a) }
    $out = $null; $err = $null; $outTask = $null; $errTask = $null
    $started = $false; $exitCode = $null; $times = $null; $errorText = $null; $cleanup = $null
    $c = [ordered]@{ frequency = [Diagnostics.Stopwatch]::Frequency
        outer_start = $null; native_start = $null; start_return = $null
        wait_return = $null; native_end = $null; outer_end = $null }
    try {
        $out = [IO.File]::Open("$Prefix.stdout.bin", [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $err = [IO.File]::Open("$Prefix.stderr.bin", [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $c.outer_start = [Diagnostics.Stopwatch]::GetTimestamp()
        $c.native_start = [Diagnostics.Stopwatch]::GetTimestamp()
        $started = $p.Start()
        $c.start_return = [Diagnostics.Stopwatch]::GetTimestamp()
        if (-not $started) { throw 'Process.Start returned false.' }
        $handle = $p.Handle; $rootPid = $p.Id
        # Both pipes drain concurrently; never read stdout then stderr serially.
        $outTask = $p.StandardOutput.BaseStream.CopyToAsync($out)
        $errTask = $p.StandardError.BaseStream.CopyToAsync($err)
        if (-not $p.WaitForExit(180000)) { throw 'Root wait exceeded 180 seconds.' }
        $c.wait_return = [Diagnostics.Stopwatch]::GetTimestamp()
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outTask, $errTask), 10000)) {
            throw 'Pipe drain exceeded 10 seconds.'
        }
        $c.native_end = [Diagnostics.Stopwatch]::GetTimestamp()
        $exitCode = $p.ExitCode
        $c.outer_end = [Diagnostics.Stopwatch]::GetTimestamp()
        # Separate FILETIME clock; query only after exit using the held handle.
        # CPU sums ROOT threads, not child processes and not wall elapsed.
        $t = [MqbNoopRootTimes]::Read($handle)
        $times = [ordered]@{ pid = $rootPid; created_100ns = $t.Created; exited_100ns = $t.Exited
            kernel_100ns = $t.Kernel; user_100ns = $t.User }
    } catch {
        $errorText = $_.ToString()
        if ($started) {
            try {
                if (-not $p.HasExited) { $p.Kill($true) }
                $cleanup = [ordered]@{ root_exited = $p.WaitForExit(5000); descendants_verified = $false }
            } catch { $cleanup = [ordered]@{ error = $_.ToString(); descendants_verified = $false } }
        }
    } finally {
        if ($null -eq $c.native_end) { $c.native_end = [Diagnostics.Stopwatch]::GetTimestamp() }
        if ($null -eq $c.outer_end) { $c.outer_end = [Diagnostics.Stopwatch]::GetTimestamp() }
        # On collection failure, closing owned streams may leave partial bytes;
        # error remains non-null and the entire study stops. No successful refill.
        $p.Dispose()
        if ($null -ne $out) { $out.Dispose() }
        if ($null -ne $err) { $err.Dispose() }
    }
    return [ordered]@{ clock = $c; exit_code = $exitCode; error = $errorText
        output_format = 'separate_bytes'; output_lines = $null; root_times = $times; cleanup = $cleanup }
}

function Invoke-RegisteredStudy {
    param([string]$Archive, [string]$Root)
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or $PSVersionTable.PSVersion.Major -lt 7) {
        throw 'This collector requires Windows and PowerShell 7.'
    }
    $helper = Join-Path $PSScriptRoot 'external_noop_boundary.py'
    & python $helper prepare $Archive --output $Root
    if ($LASTEXITCODE -ne 0) { throw 'Pinned input preparation failed. No MQB dispatched.' }
    $planPath = Join-Path $Root 'plan.json'
    $planHash = Get-Digest $planPath
    $plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
    $pins = [Collections.Generic.List[IO.FileStream]]::new()
    $attempted = 0; $validated = 0; $failure = $null
    try {
        Initialize-RootTimes
        foreach ($side in @('baseline', 'candidate')) {
            $path = Join-Path $Root "inputs/$side/mqb.exe"
            $pins.Add([IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
        }
        Write-NewJson (Join-Path $Root 'host.json') ([ordered]@{
            pid = $PID; powershell = $PSVersionTable.PSVersion.ToString()
            dotnet = [Environment]::Version.ToString(); os = [Environment]::OSVersion.VersionString
            qpc_frequency = [Diagnostics.Stopwatch]::Frequency; original_plan_sha256 = $planHash
            image_version = $env:ImageVersion; utc = [DateTime]::UtcNow.ToString('o')
        })
        foreach ($row in $plan.rows) {
            if ($attempted -ge 40 -or $row.sequence -ne ($attempted+1)) { throw 'Fixed call budget/order mismatch.' }
            if ((Get-Digest $planPath) -ne $planHash) { throw 'Plan changed during study.' }
            foreach ($n in @('external_noop_boundary.py', 'collect_external_noop_boundary.ps1')) {
                if ((Get-Digest (Join-Path $PSScriptRoot $n)) -ne $plan.collector_hashes.$n) {
                    throw 'Collector bytes changed.'
                }
            }
            $fixture = Join-Path $Root "fixtures/$($row.fixture)"
            if ($row.phase -eq 'prime') {
                if (Test-Path -LiteralPath $fixture) { throw 'Fixture already exists; no resume.' }
                $null = New-Item -ItemType Directory -Path $fixture
                [IO.File]::WriteAllBytes((Join-Path $fixture 'helper.cpp'),
                    [Text.UTF8Encoding]::new($false).GetBytes("int timing_helper() { return 42; }`r`n"))
                [IO.File]::WriteAllBytes((Join-Path $fixture 'main.cpp'),
                    [Text.UTF8Encoding]::new($false).GetBytes("int timing_helper(); int main() { return timing_helper() == 42 ? 0 : 1; }`r`n"))
            }
            $exe = Join-Path $Root "inputs/$($row.side)/mqb.exe"
            $hash = Get-Digest $exe
            if ($hash -ne $plan.binaries.($row.side)) { throw 'Input binary changed.' }
            $prefix = Join-Path $Root ('calls/{0:D2}' -f [int]$row.sequence)
            Write-NewJson "$prefix.before.json" (Get-FileManifest $fixture)
            foreach ($sourceName in @('main.cpp', 'helper.cpp')) {
                if ((Get-Digest (Join-Path $fixture $sourceName)) -ne $plan.source_hashes.$sourceName) {
                    throw 'Fixture source changed before dispatch.'
                }
            }
            if ($row.phase -eq 'noop') {
                $previous = Join-Path $Root ('calls/{0:D2}.after.json' -f ([int]$row.sequence-1))
                if ((Get-Digest "$prefix.before.json") -ne (Get-Digest $previous)) {
                    throw 'Fixture changed between prime and no-op; no dispatch.'
                }
            }
            Write-NewJson "$prefix.started.json" ([ordered]@{
                row = $row; argv = @($plan.argv); executable = $exe; cwd = $fixture
                executable_sha256 = $hash; host_pid = $PID; utc = [DateTime]::UtcNow.ToString('o')
            })
            ++$attempted
            if ($row.mode -eq 'legacy') { $record = Invoke-LegacyBoundary $exe $fixture $plan.argv $prefix }
            elseif ($row.mode -eq 'process') { $record = Invoke-ProcessBoundary $exe $fixture $plan.argv $prefix }
            else { throw 'Unexpected launch method.' }
            $record.row = $row; $record.argv = @($plan.argv); $record.executable_sha256 = $hash
            $record.dispatch_attempted = $true; $record.clears_hold = $false
            # Preserve result BEFORE manifest/semantic/time validation.
            Write-NewJson "$prefix.result.json" $record
            Write-NewJson "$prefix.after.json" (Get-FileManifest $fixture)
            & python $helper check-call $Root --sequence $row.sequence --output "$prefix.validated.json"
            if ($LASTEXITCODE -ne 0) { throw "Call $($row.sequence) invalid; first failure retained, no refill." }
            ++$validated
        }
    } catch { $failure = $_.ToString() }
    finally {
        foreach ($pin in $pins) { $pin.Dispose() }
        Write-NewJson (Join-Path $Root 'completion.json') ([ordered]@{
            status = $(if ($null -eq $failure -and $validated -eq 40) { 'completed' } else { 'stopped' })
            attempted = $attempted; validated = $validated; error = $failure; clears_hold = $false
        })
    }
    if ($null -ne $failure) { throw $failure }
    & python $helper audit $Root --output (Join-Path $Root 'audit.json')
    if ($LASTEXITCODE -ne 0) { throw 'Final audit invalid; no performance clearance.' }
}

# Explicit execution requires a separate reviewed allocation. Syntax/helper CI
# extracts function definitions only and never crosses this guard.
if (-not $ExecuteRegisteredStudy) { throw 'Study not authorized. Use only after exact-source execution review.' }
if ([string]::IsNullOrWhiteSpace($ArtifactPath) -or [string]::IsNullOrWhiteSpace($OutputRoot)) {
    throw 'An original artifact and entirely new output root are required.'
}
Invoke-RegisteredStudy ([IO.Path]::GetFullPath($ArtifactPath)) ([IO.Path]::GetFullPath($OutputRoot))

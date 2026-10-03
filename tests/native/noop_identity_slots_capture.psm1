# Restricted #819 adapter. Importing this module never starts a process.
# No arbitrary command string, shell-evaluation, network, OS tracing or cleanup API.
Set-StrictMode -Version 2.0

function Invoke-SlotNativeEnvelope {
    # Private IO boundary. Tests call it in module scope with a harmless child;
    # the public adapter below never accepts caller-selected commands/arguments.
    param([string]$Executable,[string]$Cwd,[string[]]$Argv)
    $ErrorActionPreference='Stop'
    $PSNativeCommandUseErrorActionPreference=$false
    # NativeCommandProcessor writes global:LASTEXITCODE. A local shadow would
    # hide the real exit (as the first Windows/Linux IO controls demonstrated).
    $output=@(); $exitCode=$null; $errorText=$null; $pushed=$false
    $clock=[ordered]@{frequency=[Diagnostics.Stopwatch]::Frequency
        outer_start=$null;native_start=$null;native_end=$null;outer_end=$null}
    $clock.outer_start=[Diagnostics.Stopwatch]::GetTimestamp()
    try {
        Push-Location -LiteralPath $Cwd; $pushed=$true
        $clock.native_start=[Diagnostics.Stopwatch]::GetTimestamp()
        # Catch INSIDE the array expression: an invocation error must not discard
        # an already captured prefix. Native nonzero exits remain data, not success.
        $output=@(try {
            & $Executable @Argv 2>&1
            $exitCode=$global:LASTEXITCODE
        } catch { $errorText=$_.ToString() })
    } catch { $errorText=$_.ToString() }
    finally {
        $clock.native_end=[Diagnostics.Stopwatch]::GetTimestamp()
        if ($pushed) {
            try { Pop-Location } catch { $errorText="$errorText`nPop-Location: $_" }
        }
        $clock.outer_end=[Diagnostics.Stopwatch]::GetTimestamp()
    }
    # Conversion is outside the envelope. Merged PowerShell lines are NOT raw
    # stdout/stderr bytes, child census, root CPU time, or root process lifetime.
    return [ordered]@{clock=$clock;exit_code=$exitCode;error=$errorText
        output_format='powershell_merged_lines'
        output_lines=@($output | ForEach-Object { [string]$_ })
        root_times=$null;cleanup=$null}
}

function Invoke-PinnedSlotCapture {
    param($State,[string]$Executable,[string]$Cwd,[string[]]$Argv,[string]$Prefix)
    if ($null -eq $State.context -or $State.context.kind -cne 'reviewed_819_native_session' -or
        $State.context.root -cne $State.root) { throw 'Validated native session required.' }
    $environment=@{actions=$env:GITHUB_ACTIONS;repository=$env:GITHUB_REPOSITORY
        event=$env:GITHUB_EVENT_NAME;attempt=$env:GITHUB_RUN_ATTEMPT
        disposable=$env:MQB_SLOT_DISPOSABLE;allocation=$env:MQB_SLOT_ALLOCATION}
    noop_identity_slots_runtime\Assert-SlotAdmission $true $IsWindows $PSVersionTable.PSVersion.Major `
        ([Environment]::Is64BitProcess) $State.context.reviewed $State.context.allocation $environment
    $request=Get-PinnedSlotRequest $State $Executable $Cwd $Argv $Prefix
    # Invoke-SlotStudy/Block owns read handles that deny writes/deletion throughout
    # each block and hashes before/after it. This is not a malicious-writer boundary.
    Invoke-SlotNativeEnvelope -Executable $request.executable -Cwd $request.cwd -Argv $request.argv
}

function Get-PinnedSlotRequest {
    # Pure validation seam. It does not authorize or execute a process.
    param($State,[string]$Executable,[string]$Cwd,[string[]]$Argv,[string]$Prefix)
    if ($State.attempted -isnot [int] -or $State.attempted -lt 1 -or $State.attempted -gt 48) {
        throw 'Native request outside fixed budget.'
    }
    # Derive the schedule independently of mutable plan/row fields. All checks
    # here are in memory; no hashes, probes or journal writes within a window.
    $index=$State.attempted-1
    $blockIndex=[int][Math]::Floor($index/6)
    $position=$index%6
    $labels=@('AA','AB','BB','BA','BA','BB','AB','AA')
    $label=$labels[$blockIndex]
    $round=if ($blockIndex -lt 4) {1} else {2}
    $order=if ($round -eq 1) {'LRLRRL'} else {'LRRLLR'}
    $slot=[string]$order[$position]
    $side=if ($label[([int]($slot -ceq 'R'))] -ceq 'A') {'baseline'} else {'candidate'}
    $hashes=@{baseline='48fc85fe1777b142599e24673928bc719dc77ef635a19e757f033945337b0bea'
        candidate='55aff284fcd1cf09a7446c689754a40b3ad51a249f64fe709709e53ec5033af0'}
    $expectedExe=Join-Path $State.root ('slots/'+$slot+'.exe')
    $expectedCwd=Join-Path $State.root ('fixtures/r'+$round+'-'+$label)
    $fixed=@('main.cpp','helper.cpp','--output','timing_bench','-j','1')
    if ($Prefix -cne '' -or $Executable -cne $expectedExe -or $Cwd -cne $expectedCwd -or
        $Argv.Count -ne $fixed.Count) { throw 'Native path or argument contract changed.' }
    for ($i=0;$i -lt $fixed.Count;++$i) {
        if ($Argv[$i] -cne $fixed[$i]) { throw 'Native argument contract changed.' }
    }
    if (-not $State.owned.ContainsKey($slot) -or
        $State.owned[$slot].sha256 -cne $hashes[$side] -or $State.owned[$slot].size -ne 1933824 -or
        $State.plan.images.$side -cne $hashes[$side]) { throw 'Pinned slot identity required.' }
    return @{executable=$expectedExe;cwd=$expectedCwd;argv=$fixed;sha256=$hashes[$side]}
}

Export-ModuleMember -Function @('Invoke-PinnedSlotCapture')

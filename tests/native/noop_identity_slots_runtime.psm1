# Definition-only runtime: explicit dependencies, no native process-launch default.
Set-StrictMode -Version 2.0

function Resolve-SlotApplication {
    param([Parameter(Mandatory)][ValidateSet('git','python')][string]$Name)
    # Application lookup may return several PATH matches. Resolve one command
    # before reading Source; never stringify an array into an executable name.
    $command = Get-Command -Name $Name -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
    if ($null -eq $command -or $command.Source -isnot [string] -or
        [string]::IsNullOrWhiteSpace($command.Source) -or
        -not [IO.Path]::IsPathFullyQualified($command.Source)) {
        throw 'One absolute prerequisite application path is required.'
    }
    return $command.Source
}

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

function New-SlotState {
    param([string]$Root,$Plan,[scriptblock]$Launcher,[hashtable]$Overrides=@{},$Context=$null)
    # There is deliberately no default native launcher in this module.
    if ($null -eq $Launcher) { throw 'An explicit launcher dependency is required.' }
    $services=@{
        Launch=$Launcher
        Write={ param($State,$Path,$Value) Write-NewJson $Path $Value }
        Digest={ param($State,$Path) Get-Digest $Path }
        Manifest={ param($State,$Path) return ,(Get-FileManifest $Path) }
        FreeBytes={ param($State,$Path) Get-SlotFreeBytes $Path }
    }
    foreach ($name in $Overrides.Keys) {
        if ($name -cnotin @('Write','Digest','Manifest','FreeBytes') -or $Overrides[$name] -isnot [scriptblock]) {
            throw 'Only explicit IO callback dependencies may be supplied.'
        }
        $services[$name]=$Overrides[$name]
    }
    return @{root=$Root;plan=$Plan;owned=@{};previous=$null;attempted=0;services=$services;context=$Context}
}

function Assert-SlotPath {
    param([string]$Path)
    $item=Get-Item -LiteralPath $Path -Force
    while ($null -ne $item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse path refused.' }
        $parent=[IO.Path]::GetDirectoryName($item.FullName)
        if (-not $parent -or $parent -ceq $item.FullName) { break }
        $item=Get-Item -LiteralPath $parent -Force
    }
}
function Get-SlotMetadata {
    param($State,[string]$Path)
    Assert-SlotPath $Path
    $item=Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer) { throw 'Ordinary slot file required.' }
    return [ordered]@{size=[long]$item.Length;mtime_ticks=[long]$item.LastWriteTimeUtc.Ticks
        creation_ticks=[long]$item.CreationTimeUtc.Ticks;sha256=(& ($State.services.Digest) $State $Path)}
}
function Assert-SlotSame {
    param($Actual,$Expected)
    foreach ($key in @('size','mtime_ticks','creation_ticks','sha256')) {
        if ($Actual[$key] -cne $Expected[$key]) { throw "Owned slot changed: $key" }
    }
}
function Get-SlotFreeBytes {
    param([string]$Root)
    return [IO.DriveInfo]::new([IO.Path]::GetPathRoot($Root)).AvailableFreeSpace
}
function Assert-SlotLimits {
    param([long]$EvidenceBytes,[long]$FreeBytes)
    if ($EvidenceBytes -lt 0 -or $EvidenceBytes -gt 268435456) { throw 'Evidence checkpoint exceeds 256 MiB.' }
    if ($FreeBytes -lt 4294967296) { throw 'At least 4 GiB free required.' }
}
function Get-SlotBudget {
    param($State)
    $Root=$State.root
    $pending=[Collections.Generic.Stack[string]]::new(); $pending.Push($Root)
    [long]$bytes=0; $files=0; $dirs=0
    while ($pending.Count) {
        $dir=$pending.Pop(); Assert-SlotPath $dir
        if (++$dirs -gt 256) { throw 'Directory checkpoint exceeded.' }
        foreach ($item in @(Get-ChildItem -LiteralPath $dir -Force)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse evidence refused.' }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            else {
                if (++$files -gt 2048) { throw 'File checkpoint exceeded.' }
                $bytes += $item.Length
                if ($bytes -gt 268435456) { throw 'Evidence checkpoint exceeds 256 MiB.' }
            }
        }
    }
    [long]$free=& ($State.services.FreeBytes) $State $Root
    Assert-SlotLimits $bytes $free
    return @{evidence_bytes=$bytes;free_bytes=$free}
}
function Set-SlotMapping {
    param($State,$Block,$Record)
    foreach ($slot in @('L','R')) {
        $path=Join-Path $State.root ('slots/'+$slot+'.exe')
        if ($State.owned.ContainsKey($slot)) {
            Assert-SlotSame (Get-SlotMetadata $State $path) $State.owned[$slot]
            $Record.retired[$slot]=$State.owned[$slot]
            $retired=Join-Path $State.root ('retired/'+$State.previous)
            if (-not (Test-Path -LiteralPath $retired)) { $null=New-Item -ItemType Directory -Path $retired }
            Assert-SlotPath $retired
            # Never delete/overwrite a prior image; preserve it before replacing this owned slot.
            [IO.File]::Move($path,(Join-Path $retired ($slot+'.exe')))
        } elseif (Test-Path -LiteralPath $path) { throw 'Unowned slot already exists.' }
        $image=$Block.mapping.$slot
        $source=Join-Path $State.root ('inputs/'+$image+'.exe')
        $meta=Get-SlotMetadata $State $source
        if ($meta.sha256 -cne $State.plan.images.$image) { throw 'Input image changed.' }
        [IO.File]::Copy($source,$path,$false)
        $new=Get-SlotMetadata $State $path
        if ($new.sha256 -cne $State.plan.images.$image) { throw 'Copied slot identity mismatch.' }
        $State.owned[$slot]=$new; $Record.slots_before[$slot]=$new
    }
    $State.previous=$Block.id
}
function Assert-SlotCall {
    param($Row,$Record)
    if ($null -ne $Record.error -or $null -eq $Record.exit_code -or
        ($Record.exit_code -isnot [int] -and $Record.exit_code -isnot [long]) -or $Record.exit_code -ne 0) { throw 'Call failed or exit unknown.' }
    if ($Record.output_format -cne 'powershell_merged_lines' -or $null -ne $Record.root_times -or
        $null -ne $Record.cleanup) { throw 'Wrong capture or invented process times.' }
    $clock=$Record.clock; $last=-1L
    foreach ($key in @('outer_start','native_start','native_end','outer_end')) {
        $tick=$clock[$key]
        if (($tick -isnot [long] -and $tick -isnot [int]) -or $tick -lt 0 -or $tick -lt $last) { throw 'Invalid call clock.' }
        $last=$tick
    }
    if (($clock.frequency -isnot [long] -and $clock.frequency -isnot [int]) -or $clock.frequency -le 0 -or
        ($clock.outer_end-$clock.outer_start) -gt 30*$clock.frequency) { throw 'Returned call exceeded 30 seconds or clock invalid.' }
    $lines=@($Record.output_lines)
    foreach ($line in $lines) {
        if ($line -isnot [string]) { throw 'Invalid output lines.' }
        if ($line.StartsWith('{"type":"mqb.timings')) { throw 'Unexpected internal timings.' }
    }
    $compiles=@($lines | Where-Object { $_.StartsWith('[compile] ') }).Count
    $links=@($lines | Where-Object { $_.StartsWith('[link] ') }).Count
    if ($Row.phase -ceq 'prime' -and $Row.position -eq 0) {
        if ($compiles -ne 2 -or $links -ne 1) { throw 'First prime is not fresh.' }
    } else {
        $progress=@($lines | Where-Object { $_.StartsWith('[up-to-date] ') })
        if ($compiles -or $links -or ($progress -join "`n") -cne
            "[up-to-date] 2 translation units`n[up-to-date] timing_bench.exe") { throw 'Not the fixed no-op.' }
    }
}
function Invoke-SlotRow {
    param($State,$Row,$Record,[string]$Fixture)
    if ($State.attempted -ge 48 -or $Row.sequence -ne $State.attempted+1) { throw 'Fixed sequence/budget changed.' }
    $path=Join-Path $State.root ('slots/'+$Row.slot+'.exe')
    $Record.pending=@{row=$Row;dispatch_requested=$true}
    ++$State.attempted # A request count, not proof of an OS process successfully created.
    $call=& ($State.services.Launch) $State $path $Fixture ([string[]]$Row.argv) ''
    $call.row=$Row; $call.argv=@($Row.argv); $call.executable=$path; $call.cwd=$Fixture
    $call.executable_sha256=$Row.sha256; $call.dispatch_requested=$true; $call.may_clear_hold=$false
    $Record.calls.Add($call); $Record.pending=$null # Retain before validating any outcome.
    Assert-SlotCall $Row $call
}
function Invoke-SlotBlock {
    param($State,$Block)
    $prefix=Join-Path $State.root ('blocks/'+$Block.id)
    $fixture=Join-Path $State.root ('fixtures/'+$Block.id)
    $record=[ordered]@{block=$Block;calls=[Collections.Generic.List[object]]::new();pending=$null;error=$null
        close_errors=[Collections.Generic.List[string]]::new();before=$null;after_prime=$null;after_final=$null
        slots_before=@{};slots_after=@{};retired=@{L=$null;R=$null};budget_before=$null;budget_after=$null}
    $pins=[Collections.Generic.List[IO.FileStream]]::new(); $saveError=$null
    try {
        & ($State.services.Write) $State ($prefix+'.started.json') @{block=$Block;attempted_before=$State.attempted;intent_only=$true}
        $record.budget_before=Get-SlotBudget $State
        Set-SlotMapping $State $Block $record
        foreach ($slot in @('L','R')) {
            $path=Join-Path $State.root ('slots/'+$slot+'.exe')
            $pins.Add([IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
            Assert-SlotSame (Get-SlotMetadata $State $path) $record.slots_before[$slot]
        }
        if (Test-Path -LiteralPath $fixture) { throw 'Fresh block fixture required.' }
        $null=New-Item -ItemType Directory -Path $fixture
        foreach ($prop in $State.plan.sources.PSObject.Properties) {
            [IO.File]::WriteAllBytes((Join-Path $fixture $prop.Name),[Text.Encoding]::ASCII.GetBytes($prop.Value))
        }
        $record.before=& ($State.services.Manifest) $State $fixture
        foreach ($row in @($Block.rows | Select-Object -First 2)) { Invoke-SlotRow $State $row $record $fixture }
        $record.after_prime=& ($State.services.Manifest) $State $fixture
        $null=Get-SlotBudget $State
        # Save intent plus completed primes, NOT invented started records for future calls.
        & ($State.services.Write) $State ($prefix+'.window.json') @{rows=@($Block.rows | Select-Object -Skip 2);intent_only=$true
            after_prime=$record.after_prime;slots=$record.slots_before;primes=@($record.calls.ToArray())}
        # No hash/manifest/journal/resource probe between the four measurement requests.
        foreach ($row in @($Block.rows | Select-Object -Skip 2)) {
            Invoke-SlotRow $State $row $record $fixture
        }
        $record.after_final=& ($State.services.Manifest) $State $fixture
        if (($record.after_prime | ConvertTo-Json -Depth 8 -Compress) -cne
            ($record.after_final | ConvertTo-Json -Depth 8 -Compress)) { throw 'Measured window changed fixture.' }
        foreach ($slot in @('L','R')) {
            $record.slots_after[$slot]=Get-SlotMetadata $State (Join-Path $State.root ('slots/'+$slot+'.exe'))
            Assert-SlotSame $record.slots_after[$slot] $record.slots_before[$slot]
        }
        $record.budget_after=Get-SlotBudget $State
    } catch { $record.error=$_.ToString() }
    finally {
        foreach ($pin in $pins) { try { $pin.Dispose() } catch { $record.close_errors.Add($_.ToString()) } }
        try { & ($State.services.Write) $State ($prefix+'.result.json') $record } catch { $saveError=$_.ToString() }
    }
    if ($record.error -or $record.close_errors.Count -or $saveError) {
        throw "Block stopped; original: $($record.error); close: $($record.close_errors -join ';'); save: $saveError"
    }
}
function Invoke-SlotStudy {
    param($State)
    $failure=$null; $close=[Collections.Generic.List[string]]::new()
    $pins=[Collections.Generic.List[IO.FileStream]]::new()
    try {
        foreach ($side in @('baseline','candidate')) {
            $path=Join-Path $State.root ('inputs/'+$side+'.exe'); Assert-SlotPath $path
            $pins.Add([IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
            if ((& ($State.services.Digest) $State $path) -cne $State.plan.images.$side) { throw 'Input image changed.' }
        }
        foreach ($block in $State.plan.blocks) { Invoke-SlotBlock $State $block }
    } catch { $failure=$_.ToString() }
    finally {
        foreach ($pin in $pins) { try { $pin.Dispose() } catch { $close.Add($_.ToString()) } }
        $status=if (-not $failure -and -not $close.Count -and $State.attempted -eq 48) {
            'completed_diagnostic_unreviewed' } else { 'stopped' }
        & ($State.services.Write) $State (Join-Path $State.root 'completion.json') @{status=$status;attempted=$State.attempted
            error=$failure;close_errors=@($close.ToArray());may_clear_hold=$false}
    }
    if ($failure -or $close.Count -or $State.attempted -ne 48) { throw 'Diagnostic stopped; retain prefix, do not retry.' }
}
function Assert-SlotAdmission {
    param([bool]$Execute,[bool]$Windows,[int]$PsMajor,[bool]$X64,[string]$Reviewed,[string]$Label,$Environment)
    if (-not $Execute -or -not $Windows -or $PsMajor -lt 7 -or -not $X64) { throw 'Explicit Windows x64 PowerShell 7 execution required.' }
    if ($Reviewed -cnotmatch '^[0-9a-f]{40}$' -or $Label -cnotmatch '^pr232-slot-[0-9]{3}$') { throw 'Exact review/allocation label required.' }
    if ($Environment.actions -cne 'true' -or $Environment.repository -cne 'Iviesever/msvc-quick-build' -or
        $Environment.event -cne 'workflow_dispatch' -or $Environment.attempt -cne '1' -or
        $Environment.disposable -cne '1' -or $Environment.allocation -cne $Label) {
        throw 'Separate first-attempt disposable hosted allocation required; no automatic CI run.'
    }
}

Export-ModuleMember -Function @('Resolve-SlotApplication','New-SlotState','Invoke-SlotStudy','Invoke-SlotBlock','Assert-SlotAdmission','Assert-SlotLimits','Assert-SlotPath','Get-SlotFreeBytes','Write-NewJson','Get-Digest','Get-FileManifest')

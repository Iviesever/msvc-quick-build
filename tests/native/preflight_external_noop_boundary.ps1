# Runs ONLY the registered deterministic helper preflight, never the MQB study.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'Windows and PowerShell 7 are required.'
}
$root = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $root) { throw 'New output root required; no resume/overwrite.' }
$null = New-Item -ItemType Directory -Path $root
foreach ($n in @('source', 'calls', 'fixture space')) {
    $null = New-Item -ItemType Directory -Path (Join-Path $root $n)
}
$names = @('collect_external_noop_boundary.ps1', 'external_noop_boundary.py',
    'preflight_external_noop_boundary.ps1', 'external_noop_preflight.py', 'external_noop_preflight_child.py')
foreach ($n in $names) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $n))
    $s = [IO.File]::Open((Join-Path $root "source/$n"), [IO.FileMode]::CreateNew)
    try { $s.Write($bytes, 0, $bytes.Length) } finally { $s.Dispose() }
}
# Resolve one Application in lookup order; never join multiple Source paths.
$pythonCommand = Get-Command python -CommandType Application | Select-Object -First 1
$exe = [IO.Path]::GetFullPath($pythonCommand.Source)
$checker = Join-Path $root 'source/external_noop_preflight.py'
& $exe -B $checker plan $root --executable $exe --output (Join-Path $root 'plan.json')
if ($LASTEXITCODE -ne 0) { throw 'Preflight identity/plan refused; no helper dispatched.' }
$plan = Get-Content -LiteralPath (Join-Path $root 'plan.json') -Raw | ConvertFrom-Json
# Extract verbatim functions only; never evaluate the original study entrypoint.
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'source/collect_external_noop_boundary.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw ($errors | Out-String) }
$functions = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] })
$expected = @('Write-NewJson', 'Get-Digest', 'Get-FileManifest', 'Initialize-RootTimes',
    'Invoke-LegacyBoundary', 'Invoke-ProcessBoundary', 'Invoke-RegisteredStudy')
if (@(Compare-Object ($expected | Sort-Object) ($functions.Name | Sort-Object)).Count -ne 0) {
    throw 'Unexpected collector function inventory.'
}
$allowed = @('Write-NewJson', 'Get-Digest', 'Initialize-RootTimes', 'Invoke-LegacyBoundary', 'Invoke-ProcessBoundary')
foreach ($f in $functions) {
    if ($f.Name -in $allowed) { . ([scriptblock]::Create($f.Extent.Text)) }
}
$hostLocation = (Get-Location).Path
$hostEnvironmentCwd = [Environment]::CurrentDirectory
Write-NewJson (Join-Path $root 'host.json') ([ordered]@{
    original_plan_sha256 = Get-Digest (Join-Path $root 'plan.json')
    location = $hostLocation; environment_cwd = $hostEnvironmentCwd
    pid = $PID; powershell = $PSVersionTable.PSVersion.ToString()
    dotnet = [Environment]::Version.ToString(); os = [Environment]::OSVersion.VersionString
    image_version = $env:ImageVersion; utc = [DateTime]::UtcNow.ToString('o')
})
$attempted = 0; $validated = 0; $failure = $null
try {
    Initialize-RootTimes
    foreach ($row in $plan.rows) {
        if ($attempted -ge 9 -or $row.sequence -ne ($attempted+1)) { throw 'Preflight budget/order mismatch.' }
        if ((Get-Digest $exe) -ne $plan.executable_sha256) { throw 'Interpreter changed before dispatch.' }
        foreach ($n in $names) {
            if ((Get-Digest (Join-Path $root "source/$n")) -ne $plan.sources.$n -or
                (Get-Digest (Join-Path $PSScriptRoot $n)) -ne $plan.sources.$n) { throw "Changed source: $n" }
        }
        $fixture = Join-Path $root 'fixture space'
        $prefix = Join-Path $root ('calls/{0:D2}' -f [int]$row.sequence)
        $argv = @('-I', '-u', (Join-Path $root 'source/external_noop_preflight_child.py'),
            [string]$row.scenario, "$prefix.child.json") + @($plan.payload)
        $launchExe = $(if ($row.scenario -eq 'missing') { Join-Path $root 'missing-helper.exe' } else { $exe })
        if ($row.scenario -eq 'missing' -and (Test-Path -LiteralPath $launchExe)) { throw 'Missing target exists.' }
        Write-NewJson "$prefix.started.json" ([ordered]@{
            row = $row; executable = $launchExe; cwd = $fixture; argv = $argv
            interpreter_sha256 = Get-Digest $exe
            host_location = (Get-Location).Path; host_environment_cwd = [Environment]::CurrentDirectory
        })
        ++$attempted
        if ($row.mode -eq 'legacy') { $record = Invoke-LegacyBoundary $launchExe $fixture $argv $prefix }
        elseif ($row.mode -eq 'process') { $record = Invoke-ProcessBoundary $launchExe $fixture $argv $prefix }
        else { throw 'Unknown preflight mode.' }
        $record.row = $row; $record.clears_hold = $false
        # Keep the unaltered launch observation BEFORE interpreting expected faults.
        Write-NewJson "$prefix.result.json" $record
        Write-NewJson "$prefix.host-after.json" ([ordered]@{
            location = (Get-Location).Path; environment_cwd = [Environment]::CurrentDirectory
            interpreter_sha256 = Get-Digest $exe
        })
        & $exe -B $checker check-call $root --live-interpreter --sequence $row.sequence --output "$prefix.validated.json"
        if ($LASTEXITCODE -ne 0) { throw "Unexpected preflight result at $($row.sequence); stop without refill." }
        ++$validated
    }
} catch { $failure = $_.ToString() }
finally {
    Write-NewJson (Join-Path $root 'completion.json') ([ordered]@{
        status = $(if ($null -eq $failure -and $validated -eq 9) { 'completed' } else { 'stopped' })
        attempted = $attempted; validated = $validated; error = $failure; clears_hold = $false; study_mqb_calls = 0
    })
}
if ($null -ne $failure) { throw $failure }
& $exe -B $checker audit $root --live-interpreter --output (Join-Path $root 'audit.json')
if ($LASTEXITCODE -ne 0) { throw 'Preflight evidence audit refused.' }
Write-Host 'PASS: 9 launch attempts / 7 deterministic helpers. Study MQB=0; HOLD unchanged.'

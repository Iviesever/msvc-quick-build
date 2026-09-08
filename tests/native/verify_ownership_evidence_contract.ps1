[CmdletBinding()]
param(
    [string]$CollectorPath = (Join-Path $PSScriptRoot 'collect_msvc_service_ownership.ps1'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '../../.mqb/ownership-contract-tests')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite earlier contract evidence.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
# Import only the actual collector's validation functions. Never execute its
# main body, discovery or compiler while testing damaged synthetic records.
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($CollectorPath, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -ne 0) { throw "Collector parse errors: $parseErrors" }
foreach ($name in @('Assert-OwnershipObservation', 'Test-OwnershipCleanupEnvelope', 'Get-OwnershipDiagnosticErrors')) {
    $definitions = @($ast.FindAll({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true))
    if ($definitions.Count -ne 1) { throw "Expected exactly one collector function: $name" }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$rows = [System.Collections.Generic.List[object]]::new()
function Write-Json($Path, $Value) {
    $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8
}
function Write-Tool($Root, $Stem, [int]$Code = 0, [bool]$Cancelled = $false) {
    $path = Join-Path $Root $Stem
    New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($path)) -Force | Out-Null
    Write-Json "$path.result.json" @{ exit_code = $Code; cancelled = $Cancelled }
    [System.IO.File]::WriteAllText("$path.stdout.txt", '')
    [System.IO.File]::WriteAllText("$path.stderr.txt", '')
    if ($Stem -ne 'A') { [System.IO.File]::WriteAllText("$path.argv.txt", "synthetic-tool`n") }
}
function New-Fixture([string]$Name, [string]$Mode, [string]$Profile, [string]$Origin, [string]$Ending) {
    $root = Join-Path $OutputRoot $Name
    New-Item -ItemType Directory -Path $root | Out-Null
    $observation = [pscustomobject]@{
        schema = 2; endpoint_mode = $Mode; profile = $Profile; origin = $Origin; ending = $Ending
        A_exit = $(if ($Ending -eq 'cancel') { 1223 } else { 0 })
        B0_exit = 0; B1_exit = 0; B_link_exit = 0; B_run_exit = 0; recovery_compile_exit = 0
        lifecycle_ok = $true; unmanaged_control_ok = $true; drain_control_ok = $true
        safe_to_integrate_cancellation = $false; safe_to_transfer_write_lease = $false
        server_survived_A = $true; B_compiler_overlap_observed = $true
        drain_requested = ($Ending -eq 'drain'); A_compiler_overlap_observed = ($Ending -eq 'drain')
        A_observed_compilers_signaled = $(if ($Ending -eq 'drain') { $true } else { $null })
        A_pending_compile_dispatched = $(if ($Ending -eq 'drain') { $false } else { $null })
    }
    Write-Tool $root 'A' $observation.A_exit ($Ending -eq 'cancel')
    foreach ($stem in @('A/warm', 'B/warm', 'B/work0', 'B/work1', 'B/link', 'B/run', 'B/recovery')) { Write-Tool $root $stem }
    $prepared = @('A', 'B')
    if ($Origin -eq 'preexisting') { $prepared += 'seed'; Write-Tool $root 'seed/warm' }
    foreach ($directory in $prepared) {
        if ($Profile.StartsWith('pch-')) { Write-Tool $root "$directory/prefix" }
        if ($Profile.StartsWith('modules-')) { Write-Tool $root "$directory/provider" }
    }
    if ($Ending -eq 'drain') {
        Write-Tool $root 'A/work0'
        Write-Json (Join-Path $root 'A/drain.json') @{
            stop_observed = $true; work_compiles_dispatched = 1; first_compile_exit = 0
            pending_compile_exit = -2; safe_to_transfer_write_lease = $false
        }
    }
    return [pscustomobject]@{
        root = $root; observation = $observation
        envelope = [pscustomobject]@{ cleanup_verified = $true; endpoint_override_absent = $true
            outer_lifecycle_verified = $true; remaining_servers = @() }
    }
}
function Run-Case {
    param([string]$Name, [scriptblock]$Mutate = {}, [bool]$ExpectAccepted = $true,
        [string]$Mode = 'default', [string]$Profile = 'zi-debug', [string]$Origin = 'A-started', [string]$Ending = 'normal')
    $fixture = New-Fixture $Name $Mode $Profile $Origin $Ending
    & $Mutate $fixture
    Write-Json (Join-Path $fixture.root 'observation.json') $fixture.observation
    Write-Json (Join-Path $fixture.root 'default-envelope.json') $fixture.envelope
    # Use deserialized values, just as the collector does (including Int64 JSON numbers).
    $observation = Get-Content -LiteralPath (Join-Path $fixture.root 'observation.json') -Raw | ConvertFrom-Json
    $envelope = Get-Content -LiteralPath (Join-Path $fixture.root 'default-envelope.json') -Raw | ConvertFrom-Json
    $errors = @()
    try { Assert-OwnershipObservation $observation $Mode $Profile $Origin $Ending }
    catch { $errors += $_.Exception.Message }
    $errors += @(Get-OwnershipDiagnosticErrors $fixture.root $Profile $Origin $Ending $observation)
    if ($Mode -eq 'default' -and -not (Test-OwnershipCleanupEnvelope $envelope)) { $errors += 'Cleanup unproven.' }
    $accepted = $errors.Count -eq 0
    $rows.Add([pscustomobject]@{ name = $Name; expected_accepted = $ExpectAccepted; accepted = $accepted
        passed = ($accepted -eq $ExpectAccepted); errors = @($errors) })
    Write-Json (Join-Path $OutputRoot 'summary.json') @{
        schema = 1; synthetic_only = $true; collector_sha256 = (Get-FileHash $CollectorPath -Algorithm SHA256).Hash
        powershell = $PSVersionTable.PSVersion.ToString(); cases = @($rows.ToArray())
    }
}
# Each real matrix shape gets a synthetic intact-record control. This does not
# claim any MSVC run, process termination or write-quiescence observation.
foreach ($mode in @('private', 'default')) {
    foreach ($profile in @('zi-debug', 'ZI-debug', 'zi-release', 'pch-debug', 'pch-release', 'modules-debug', 'modules-release')) {
        foreach ($origin in @('preexisting', 'A-started')) {
            $endings = @('unmanaged-normal', 'normal', 'cancel')
            if ($mode -eq 'default') { $endings += 'drain' }
            foreach ($ending in $endings) {
                $label = if ($profile -ceq 'ZI-debug') { 'edit-continue-debug' } else { $profile }
                Run-Case -Name "$mode-$label-$origin-$ending" -Mode $mode -Profile $profile -Origin $origin -Ending $ending
            }
        }
    }
}
foreach ($suffix in @('result.json', 'stdout.txt', 'stderr.txt', 'argv.txt')) {
    Run-Case -Name "missing-$suffix" -ExpectAccepted $false -Mutate {
        param($f) Remove-Item -LiteralPath (Join-Path $f.root "B/work0.$suffix")
    }
}
Run-Case 'malformed-result' { param($f) [IO.File]::WriteAllText((Join-Path $f.root 'B/work0.result.json'), '{broken') } $false
Run-Case 'infrastructure-result' { param($f) Write-Json (Join-Path $f.root 'B/work0.result.json') @{ infrastructure_error = $true; native_code = 5 } } $false
Run-Case 'string-result-code' { param($f) Write-Json (Join-Path $f.root 'B/work0.result.json') @{ exit_code = '0'; cancelled = $false } } $false
Run-Case 'unexpected-tool-cancellation' { param($f) Write-Tool $f.root 'B/work0' 0 $true } $false
Run-Case 'original-result-mismatch' { param($f) Write-Tool $f.root 'B/work0' 1 } $false
Run-Case 'unlisted-attempt-without-result' { param($f) [IO.File]::WriteAllText((Join-Path $f.root 'B/extra.argv.txt'), 'attempted') } $false
Run-Case 'wrong-profile-case' { param($f) $f.observation.profile = 'ZI-debug' } $false
Run-Case 'wrong-mode' { param($f) $f.observation.endpoint_mode = 'private' } $false
Run-Case 'missing-schema' { param($f) $f.observation.PSObject.Properties.Remove('schema') } $false
Run-Case 'string-schema' { param($f) $f.observation.schema = '2' } $false
Run-Case 'floating-exit' { param($f) $f.observation.A_exit = 0.25 } $false
Run-Case 'out-of-range-exit' { param($f) $f.observation.A_exit = [long]4294967296 } $false
Run-Case 'string-safety-flag' { param($f) $f.observation.safe_to_transfer_write_lease = 'false' } $false
Run-Case 'authorized-safety-flag' { param($f) $f.observation.safe_to_integrate_cancellation = $true } $false
Run-Case 'false-control' { param($f) $f.observation.lifecycle_ok = $false } $false
Run-Case 'string-cleanup-flag' { param($f) $f.envelope.cleanup_verified = 'true' } $false
Run-Case 'null-server-list' { param($f) $f.envelope.remaining_servers = $null } $false
Run-Case 'remaining-server' { param($f) $f.envelope.remaining_servers = @(@{ pid = 42 }) } $false
Run-Case 'missing-envelope-field' { param($f) $f.envelope.PSObject.Properties.Remove('outer_lifecycle_verified') } $false
Run-Case -Name 'drain-no-overlap' -Ending drain -ExpectAccepted $false -Mutate { param($f) $f.observation.A_compiler_overlap_observed = $false }
Run-Case -Name 'drain-record-missing' -Ending drain -ExpectAccepted $false -Mutate { param($f) Remove-Item -LiteralPath (Join-Path $f.root 'A/drain.json') }
Run-Case -Name 'drain-pending-attempted' -Ending drain -ExpectAccepted $false -Mutate { param($f) Write-Tool $f.root 'A/work1' }
Run-Case -Name 'preparation-failed' -Origin preexisting -Profile pch-debug -ExpectAccepted $false -Mutate { param($f) Write-Tool $f.root 'seed/prefix' 1 }
# A failed managed B compile is valuable adverse evidence, not a failed collector;
# the successful recovery MUST NOT replace the original nonzero tool result.
Run-Case 'adverse-managed-compile-retained' {
    param($f)
    $f.observation.B0_exit = 1; $f.observation.B_link_exit = -2; $f.observation.B_run_exit = -2
    $f.observation.server_survived_A = $false
    Write-Tool $f.root 'B/work0' 1
    foreach ($stem in @('B/link', 'B/run')) {
        foreach ($suffix in @('result.json', 'stdout.txt', 'stderr.txt', 'argv.txt')) {
            Remove-Item -LiteralPath (Join-Path $f.root "$stem.$suffix")
        }
    }
} $true
$failed = @($rows | Where-Object { -not $_.passed })
Write-Host "OWNERSHIP_COLLECTOR_CONTRACT $($rows.Count) cases; $($failed.Count) failures (synthetic; no MSVC run)"
if ($failed.Count -ne 0) { $failed | Format-List | Out-String | Write-Host; exit 1 }

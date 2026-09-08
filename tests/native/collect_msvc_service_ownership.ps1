[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [string]$OutputRoot,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$BuildOnly,
    [string]$PrebuiltProbePath,
    [string]$ProbeIdentityPath,
    [ValidateSet('private', 'default')][string]$EndpointMode = 'private'
)

$ErrorActionPreference = 'Stop'
# Nonzero native exits remain data until their diagnostics and case result are
# persisted. Every native invocation below still checks its actual exit code.
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0
$prebuilt = -not [string]::IsNullOrWhiteSpace($PrebuiltProbePath)
if ($BuildOnly -and ($prebuilt -or $EndpointMode -eq 'default')) { throw 'BuildOnly requires a private build-only invocation.' }
if ($prebuilt -ne (-not [string]::IsNullOrWhiteSpace($ProbeIdentityPath))) { throw 'Prebuilt probe and identity must be supplied together.' }
if ($EndpointMode -eq 'default') {
    # Default endpoint experiments must never compile their own tooling on the
    # measurement host, or terminate pre-existing developer services.
    if (-not $prebuilt -or $env:MQB_OWNERSHIP_DISPOSABLE_HOST -ne '1' -or
        $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
        throw 'Default endpoints require a prebuilt probe and an explicitly disposable GitHub-hosted runner.'
    }
    if (Test-Path Env:_MSPDBSRV_ENDPOINT_) { throw 'Default endpoint requires no ambient endpoint override.' }
}
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) { throw "Missing MQB: $MqbPath" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot ('.mqb/msvc-ownership-evidence/' + [guid]::NewGuid().ToString('N'))
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
# Never erase an earlier run, failure, or unfavorable observation.
if (Test-Path -LiteralPath $OutputRoot) { throw "Evidence directory already exists: $OutputRoot" }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

function Assert-OwnershipObservation {
    param($Observation, [string]$EndpointMode, [string]$Profile, [string]$Origin, [string]$Ending)
    if ($Observation -isnot [pscustomobject] -or
        ($Observation.schema -isnot [int] -and $Observation.schema -isnot [long]) -or
        $Observation.schema -ne 2) { throw 'Unexpected observation schema.' }
    $identity = @{ endpoint_mode = $EndpointMode; profile = $Profile; origin = $Origin; ending = $Ending }
    foreach ($field in $identity.Keys) {
        if ($Observation.$field -isnot [string] -or $Observation.$field -cne $identity[$field]) {
            throw "Unexpected observation identity: $field"
        }
    }
    foreach ($field in @('A_exit', 'B0_exit', 'B1_exit', 'B_link_exit', 'B_run_exit', 'recovery_compile_exit')) {
        if (($Observation.$field -isnot [int] -and $Observation.$field -isnot [long]) -or
            $Observation.$field -lt [int]::MinValue -or $Observation.$field -gt [int]::MaxValue) {
            throw "Missing or mistyped outcome: $field"
        }
    }
    foreach ($field in @('safe_to_integrate_cancellation', 'safe_to_transfer_write_lease')) {
        if ($Observation.$field -isnot [bool] -or $Observation.$field) { throw "Evidence must not authorize safety: $field" }
    }
    foreach ($field in @('lifecycle_ok', 'unmanaged_control_ok', 'drain_control_ok')) {
        if ($Observation.$field -isnot [bool] -or -not $Observation.$field) { throw "Fixture control failed: $field" }
    }
    if ($Observation.server_survived_A -isnot [bool] -or $Observation.B_compiler_overlap_observed -isnot [bool]) {
        throw 'Missing or mistyped liveness observation.'
    }
    if ($Observation.scheduler_api_used -isnot [bool] -or
        $Observation.scheduler_api_used -ne ($Ending -eq 'scheduler-drain')) { throw 'Wrong scheduler API identity.' }
    if ($Ending -eq 'cancel') {
        if ($Observation.A_exit -eq 0) { throw 'Cancelled A cannot report successful exit.' }
    } elseif ($Observation.A_exit -ne 0) { throw 'Original A control failed.' }
    if ($Observation.B0_exit -ne 0 -or $Observation.B1_exit -ne 0) {
        if ($Observation.B_link_exit -ne -2 -or $Observation.B_run_exit -ne -2) { throw 'Unattempted link/run must remain unattempted.' }
    } elseif ($Observation.B_link_exit -ne 0 -and $Observation.B_run_exit -ne -2) { throw 'Failed link cannot run an artifact.' }
    if ($Ending -in @('unmanaged-normal', 'drain', 'scheduler-drain')) {
        if (-not $Observation.server_survived_A -or $Observation.B0_exit -ne 0 -or $Observation.B1_exit -ne 0 -or
            $Observation.B_link_exit -ne 0 -or $Observation.B_run_exit -ne 0) { throw 'Original B control failed.' }
    }
    if ($Ending -in @('drain', 'scheduler-drain')) {
        foreach ($field in @('drain_requested', 'A_compiler_overlap_observed', 'B_compiler_overlap_observed', 'A_observed_compilers_signaled')) {
            if ($Observation.$field -isnot [bool] -or -not $Observation.$field) { throw "Unproven drain boundary: $field" }
        }
        if ($Observation.A_pending_compile_dispatched -isnot [bool] -or $Observation.A_pending_compile_dispatched) {
            throw 'Pending A work was not suppressed.'
        }
    }
}

function Test-OwnershipCleanupEnvelope {
    param($Envelope)
    try {
        return $Envelope -is [pscustomobject] -and
            $Envelope.cleanup_verified -is [bool] -and $Envelope.cleanup_verified -and
            $Envelope.endpoint_override_absent -is [bool] -and $Envelope.endpoint_override_absent -and
            $Envelope.outer_lifecycle_verified -is [bool] -and $Envelope.outer_lifecycle_verified -and
            $Envelope.remaining_servers -is [array] -and $Envelope.remaining_servers.Count -eq 0
    } catch { return $false }
}

function Get-OwnershipDiagnosticErrors {
    param([string]$CaseRoot, [string]$Profile, [string]$Origin, [string]$Ending, $Observation)
    # Check both expected and observed tools: a present-file inventory alone
    # cannot detect an entirely missing invocation record.
    $stems = @('A', 'A/warm', 'B/warm', 'B/work0', 'B/work1', 'B/recovery')
    $prepared = @('A', 'B')
    if ($Origin -eq 'preexisting') { $prepared += 'seed'; $stems += 'seed/warm' }
    foreach ($directory in $prepared) {
        if ($Profile.StartsWith('pch-')) { $stems += "$directory/prefix" }
        if ($Profile.StartsWith('modules-')) { $stems += "$directory/provider" }
    }
    if ($Ending -in @('drain', 'scheduler-drain')) { $stems += 'A/work0' }
    if ($null -ne $Observation -and $Observation.B0_exit -eq 0 -and $Observation.B1_exit -eq 0) {
        $stems += 'B/link'
        if ($Observation.B_link_exit -eq 0) { $stems += 'B/run' }
    }
    $stems += @(Get-ChildItem -LiteralPath $CaseRoot -Recurse -File |
        Where-Object { $_.Name -match '\.(result\.json|argv\.txt)$' -and $_.Name -notin @('case.result.json', 'case.argv.txt') } |
        ForEach-Object { [System.IO.Path]::GetRelativePath($CaseRoot, $_.FullName).Replace('\', '/') -replace '\.(result\.json|argv\.txt)$', '' })
    foreach ($stem in @($stems | Sort-Object -Unique)) {
        try {
            $result = Get-Content -LiteralPath (Join-Path $CaseRoot "$stem.result.json") -Raw | ConvertFrom-Json
            if ($result -isnot [pscustomobject] -or $null -ne $result.PSObject.Properties['infrastructure_error']) {
                throw 'Process infrastructure error or invalid result schema.'
            }
            if (($result.exit_code -isnot [int] -and $result.exit_code -isnot [long]) -or
                $result.cancelled -isnot [bool]) { throw 'Missing or mistyped tool outcome.' }
            if ($result.cancelled -ne ($stem -eq 'A' -and $Ending -eq 'cancel')) { throw 'Unexpected cancellation outcome.' }
            $outcomeFields = @{ A = 'A_exit'; 'B/work0' = 'B0_exit'; 'B/work1' = 'B1_exit';
                'B/link' = 'B_link_exit'; 'B/run' = 'B_run_exit'; 'B/recovery' = 'recovery_compile_exit' }
            if ($null -ne $Observation -and $outcomeFields.ContainsKey($stem)) {
                $field = $outcomeFields[$stem]
                if ($result.exit_code -ne $Observation.$field) { throw 'Original tool outcome disagrees with observation.' }
            }
            $suffixes = @('stdout.txt', 'stderr.txt')
            if ($stem -ne 'A') { $suffixes += 'argv.txt' }
            foreach ($suffix in $suffixes) {
                if (-not (Test-Path -LiteralPath (Join-Path $CaseRoot "$stem.$suffix") -PathType Leaf)) {
                    throw "Missing $suffix; original diagnostics are incomplete."
                }
            }
            if ($stem -match '/(warm|prefix|provider)$' -and $result.exit_code -ne 0) { throw 'Preparation control failed.' }
        } catch { "${stem}.result.json: $($_.Exception.Message)" }
    }
    if ($Ending -in @('drain', 'scheduler-drain')) {
        try {
            $drain = Get-Content -LiteralPath (Join-Path $CaseRoot 'A/drain.json') -Raw | ConvertFrom-Json
            if ($drain.stop_observed -isnot [bool] -or -not $drain.stop_observed -or
                $drain.safe_to_transfer_write_lease -isnot [bool] -or $drain.safe_to_transfer_write_lease) { throw 'Invalid drain flags.' }
            foreach ($entry in @{ work_compiles_dispatched = 1; first_compile_exit = 0; pending_compile_exit = -2 }.GetEnumerator()) {
                if (($drain.($entry.Key) -isnot [int] -and $drain.($entry.Key) -isnot [long]) -or
                    $drain.($entry.Key) -ne $entry.Value) { throw 'Invalid original drain outcome.' }
            }
            $first = Get-Content -LiteralPath (Join-Path $CaseRoot 'A/work0.result.json') -Raw | ConvertFrom-Json
            if ($first.exit_code -ne $drain.first_compile_exit) { throw 'Original A compile disagrees with drain outcome.' }
            if (Test-Path -LiteralPath (Join-Path $CaseRoot 'A/work1.argv.txt')) { throw 'Pending A work was dispatched.' }
        } catch { "A/drain.json: $($_.Exception.Message)" }
    }
    if ($Ending -eq 'scheduler-drain') {
        try {
            $scheduler = Get-Content -LiteralPath (Join-Path $CaseRoot 'A/scheduler.json') -Raw | ConvertFrom-Json
            if ($scheduler -isnot [pscustomobject] -or $scheduler.api -isnot [string] -or $scheduler.api -cne 'BoundedWorkScheduler::run_with_admission_stop') {
                throw 'Wrong scheduler API record.'
            }
            foreach ($entry in @{ schema = 1; worker_count = 1; started_count = 1; bridge_wait_error = 0 }.GetEnumerator()) {
                if (($scheduler.($entry.Key) -isnot [int] -and $scheduler.($entry.Key) -isnot [long]) -or
                    $scheduler.($entry.Key) -ne $entry.Value) { throw 'Invalid scheduler or bridge outcome.' }
            }
            foreach ($field in @('scheduler_succeeded', 'stop_requested', 'admission_stop_observed',
                'stopped_before_all_items', 'event_observed', 'forwarded_during_callback')) {
                if ($scheduler.$field -isnot [bool] -or -not $scheduler.$field) { throw "Unproven scheduler boundary: $field" }
            }
            if ($scheduler.callback_exception -isnot [string] -or $scheduler.callback_exception.Length -ne 0 -or
                $null -ne $scheduler.scheduler_error_code -or $scheduler.safe_to_transfer_write_lease -isnot [bool] -or
                $scheduler.safe_to_transfer_write_lease) { throw 'Scheduler failure or invalid safety authorization.' }
        } catch { "A/scheduler.json: $($_.Exception.Message)" }
    }
}

Push-Location $RepoRoot
try {
    $head = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source HEAD.' }
    $status = @(& git status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) { throw 'Tracked source must be clean.' }
    $version = (Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim()
    if ($version -ne '5.5.0') { throw "This M1b experiment requires unchanged VERSION 5.5.0, got $version" }
    $identity = [ordered]@{
        schema = 2
        endpoint_mode = $EndpointMode
        build_only = [bool]$BuildOnly
        prebuilt_probe = $prebuilt
        source_head = $head
        version = $version
        candidate_path = $MqbPath
        candidate_sha256 = (Get-FileHash -LiteralPath $MqbPath -Algorithm SHA256).Hash
        configuration = $Configuration
        os = [System.Environment]::OSVersion.VersionString
        powershell = $PSVersionTable.PSVersion.ToString()
        processor_count = [System.Environment]::ProcessorCount
        github_run_id = $env:GITHUB_RUN_ID
        github_run_attempt = $env:GITHUB_RUN_ATTEMPT
        runner_image = $env:ImageVersion
        utc_started = [DateTime]::UtcNow.ToString('o')
        isolation = $(if ($EndpointMode -eq 'default') {
            'Unmodified default endpoint on a separate clean disposable runner; no bootstrap compiler; reject any pre-existing server; outer Job bounds A+B.'
        } else { 'Unique fixture-only _MSPDBSRV_ENDPOINT_ shared by A+B, bounded by outer Job.' })
        limitation = 'Retained process/resource snapshots are not RPC-in-flight, owner-crash recovery, or write-lease release proof.'
    }
    $identity | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'identity.json') -Encoding utf8

    & (Join-Path $RepoRoot 'tests/native/assert_cpp_layout.ps1') -CppRoot (Join-Path $RepoRoot 'cpp')
    $config = Get-Content -LiteralPath (Join-Path $RepoRoot 'cpp/mqb.json') -Raw | ConvertFrom-Json
    $sharedSources = @($config.discovery.extra_sources | ForEach-Object { $_.Replace('\', '/') } | Sort-Object -Unique)
    $actualSources = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'cpp/src') -File -Recurse -Filter '*.cpp' |
        ForEach-Object { [System.IO.Path]::GetRelativePath((Join-Path $RepoRoot 'cpp'), $_.FullName).Replace('\', '/') } |
        Where-Object { $_ -ne 'src/app/main.cpp' } | Sort-Object -Unique)
    if (@(Compare-Object $actualSources $sharedSources).Count -ne 0) { throw 'Production source manifest drift.' }
    if (-not $prebuilt) {
        $arguments = [System.Collections.Generic.List[string]]::new()
        $arguments.Add('cpp/tests/platform/windows/msvc_service_ownership_probe.cpp')
        foreach ($source in $sharedSources) { $arguments.Add('cpp/' + $source) }
        $arguments.AddRange([string[]]@('--env', 'vs', '--no-discover', '--std', [string]$config.build.standard))
        $arguments.Add($(if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }))
        $arguments.Add('--runtime')
        $arguments.Add($(if ($Configuration -eq 'Debug') { 'MTd' } else { 'MT' }))
        foreach ($include in @($config.build.include_dirs)) { $arguments.Add('-I'); $arguments.Add('cpp/' + $include) }
        foreach ($arg in @($config.build.compiler_args)) { $arguments.Add('--compiler-arg'); $arguments.Add([string]$arg) }
        $arguments.AddRange([string[]]@('-D', 'MQB_VERSION="ownership-probe"', '--lib', 'shell32.lib', '--lib', 'Rstrtmgr.lib', '-o', 'msvc_service_ownership_probe'))
        $arguments | Set-Content -LiteralPath (Join-Path $OutputRoot 'probe-build.argv.txt') -Encoding utf8
        $output = @(& $MqbPath @arguments 2>&1)
        $buildExit = $LASTEXITCODE
        $output | Set-Content -LiteralPath (Join-Path $OutputRoot 'probe-build.output.txt') -Encoding utf8
        foreach ($line in $output) { Write-Host $line }
        if ($buildExit -ne 0) { throw "Probe build failed with $buildExit" }
        $probe = Join-Path $RepoRoot '.mqb/bin/msvc_service_ownership_probe.exe'
        if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw 'Probe executable missing.' }
    } else {
        $probe = [System.IO.Path]::GetFullPath($PrebuiltProbePath)
        $provenance = Get-Content -LiteralPath $ProbeIdentityPath -Raw | ConvertFrom-Json
        if ($provenance.source_head -ne $head -or $provenance.version -ne $version -or
            $provenance.configuration -ne $Configuration -or
            $provenance.candidate_sha256 -ne $identity.candidate_sha256 -or
            $provenance.probe_sha256 -ne (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash) {
            throw 'Prebuilt probe source/configuration/binary identity mismatch.'
        }
        Copy-Item -LiteralPath $ProbeIdentityPath -Destination (Join-Path $OutputRoot 'origin-probe.identity.json')
    }
    [ordered]@{
        schema = 1; source_head = $head; version = $version; configuration = $Configuration
        candidate_sha256 = $identity.candidate_sha256
        probe_sha256 = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash
        github_run_id = $env:GITHUB_RUN_ID; github_run_attempt = $env:GITHUB_RUN_ATTEMPT
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'probe.identity.json') -Encoding utf8
    Get-FileHash -LiteralPath $probe -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'probe-binary.json') -Encoding utf8

    if ($BuildOnly) { Write-Host 'Exact-source probe built; no ownership experiment ran on the build host.'; return }

    # This is a fixed diagnostic matrix, not a repeat-until-green policy.
    $profiles = @('zi-debug', 'ZI-debug', 'zi-release', 'pch-debug', 'pch-release', 'modules-debug', 'modules-release')
    $origins = @('preexisting', 'A-started')
    $endings = @('unmanaged-normal', 'normal', 'cancel')
    if ($EndpointMode -eq 'default') { $endings += @('drain', 'scheduler-drain') }
    $expectedCases = $profiles.Count * $origins.Count * $endings.Count
    $aborted = $false
    $rows = [System.Collections.Generic.List[object]]::new()
    $failed = 0
    :caseMatrix foreach ($profile in $profiles) {
        foreach ($origin in $origins) {
            foreach ($ending in $endings) {
                # /Zi and /ZI are different compiler modes, but their short
                # names alone collide on a case-insensitive output filesystem.
                $directoryProfile = if ($profile -ceq 'ZI-debug') { 'zi-edit-and-continue-debug' } else { $profile }
                $name = "$directoryProfile-$origin-$ending"
                $caseRoot = Join-Path $OutputRoot $name
                New-Item -ItemType Directory -Path $caseRoot | Out-Null
                $fixtureId = 'mqb-' + [guid]::NewGuid().ToString('N')
                $mode = if ($EndpointMode -eq 'default') { '--default-case' } else { '--case' }
                $caseArgs = @($mode, $caseRoot, $profile, $origin, $ending, $fixtureId)
                $caseArgs | Set-Content -LiteralPath (Join-Path $caseRoot 'case.argv.txt') -Encoding utf8
                Write-Host "=== MSVC ownership: $name ==="
                $timer = [System.Diagnostics.Stopwatch]::StartNew()
                $caseOutput = @(& $probe @caseArgs 2>&1)
                $caseExit = $LASTEXITCODE
                $timer.Stop()
                $caseOutput | Set-Content -LiteralPath (Join-Path $caseRoot 'case.output.txt') -Encoding utf8
                foreach ($line in $caseOutput) { Write-Host $line }
                $observationPath = Join-Path $caseRoot 'observation.json'
                $observation = $null
                $parseError = $null
                if (Test-Path -LiteralPath $observationPath -PathType Leaf) {
                    try {
                        $observation = Get-Content -LiteralPath $observationPath -Raw | ConvertFrom-Json
                        Assert-OwnershipObservation -Observation $observation -EndpointMode $EndpointMode `
                            -Profile $profile -Origin $origin -Ending $ending
                    } catch { $parseError = $_.Exception.Message }
                }
                # B failures are the result of the policy under test, not a reason
                # to suppress their data. Missing evidence / failed unmanaged
                # controls / process-infrastructure failures do fail collection.
                try {
                    $toolErrors = @(Get-OwnershipDiagnosticErrors -CaseRoot $caseRoot -Profile $profile `
                        -Origin $origin -Ending $ending -Observation $observation)
                } catch { $toolErrors = @("Diagnostic inventory failed: $($_.Exception.Message)") }
                $cleanupVerified = $null
                $envelopeError = $null
                if ($EndpointMode -eq 'default') {
                    try {
                        $envelope = Get-Content -LiteralPath (Join-Path $caseRoot 'default-envelope.json') -Raw | ConvertFrom-Json
                        $cleanupVerified = Test-OwnershipCleanupEnvelope -Envelope $envelope
                    } catch { $envelopeError = $_.Exception.Message }
                }
                $collectionOk = $caseExit -eq 0 -and $null -ne $observation -and
                    $null -eq $parseError -and $toolErrors.Count -eq 0 -and ($EndpointMode -ne 'default' -or $cleanupVerified -eq $true)
                if ($EndpointMode -eq 'default' -and $cleanupVerified -ne $true) { $aborted = $true }
                if (-not $collectionOk) { $failed++ }
                $row = [ordered]@{
                    case = $name; exit_code = $caseExit; elapsed_ms = $timer.Elapsed.TotalMilliseconds
                    collection_ok = $collectionOk; tool_infrastructure_errors = $toolErrors
                    observation = $observation; parse_error = $parseError
                    cleanup_verified = $cleanupVerified; envelope_error = $envelopeError
                }
                $rows.Add($row)
                $row | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $caseRoot 'case.result.json') -Encoding utf8
                # Checkpoint the complete prefix, including adverse/inconclusive cases.
                [ordered]@{
                    schema = 2; endpoint_mode = $EndpointMode; expected_cases = $expectedCases
                    completed_cases = $rows.Count; failed_cases = $failed
                    aborted_after_unproven_cleanup = $aborted; cases = $rows
                } | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $OutputRoot 'summary.json') -Encoding utf8
                # A failed outer lifecycle is not permission to test/kill another
                # request on the same default endpoint. Keep the incomplete prefix.
                if ($aborted) { Write-Warning 'Default endpoint cleanup unproven; remaining cases not attempted.'; break caseMatrix }
            }
        }
    }
    if ($aborted) {
        # Do not read/hash outputs possibly still being written after uncertain
        # cleanup. Retain the captured prefix without inventing stable snapshots.
        [ordered]@{ status = 'not_attempted'; reason = 'cleanup_unproven' } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $OutputRoot 'binary-inventory-status.json') -Encoding utf8
        exit 1
    }
    # Raw diagnostics/inputs are uploaded; large generated binaries are identified
    # by SHA-256 rather than being silently mistaken for retained artifact bytes.
    $binaries = @(Get-ChildItem -LiteralPath $OutputRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.obj', '.pdb', '.pch', '.ifc', '.exe') } |
        ForEach-Object { [ordered]@{
            path = [System.IO.Path]::GetRelativePath($OutputRoot, $_.FullName)
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        } })
    $binaries | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputRoot 'generated-binary-hashes.json') -Encoding utf8
    Write-Host "Collected $($rows.Count)/$expectedCases cases; $failed collection/control failures. This never authorizes CLI cancellation or lease transfer."
    if ($rows.Count -ne $expectedCases -or $failed -ne 0 -or $aborted) { exit 1 }
}
finally { Pop-Location }

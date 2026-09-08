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
    if ($EndpointMode -eq 'default') { $endings += 'drain' }
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
                    try { $observation = Get-Content -LiteralPath $observationPath -Raw | ConvertFrom-Json }
                    catch { $parseError = $_.Exception.Message }
                }
                # B failures are the result of the policy under test, not a reason
                # to suppress their data. Missing evidence / failed unmanaged
                # controls / process-infrastructure failures do fail collection.
                $toolErrors = @(Get-ChildItem -LiteralPath $caseRoot -Recurse -File -Filter '*.result.json' |
                    ForEach-Object {
                        $result = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
                        $property = $result.PSObject.Properties['infrastructure_error']
                        if ($null -ne $property -and $property.Value -eq $true) {
                            [System.IO.Path]::GetRelativePath($caseRoot, $_.FullName)
                        }
                    })
                $cleanupVerified = $null
                $envelopeError = $null
                if ($EndpointMode -eq 'default') {
                    try {
                        $envelope = Get-Content -LiteralPath (Join-Path $caseRoot 'default-envelope.json') -Raw | ConvertFrom-Json
                        $cleanupVerified = $envelope.cleanup_verified -eq $true -and
                            $envelope.endpoint_override_absent -eq $true -and
                            $envelope.outer_lifecycle_verified -eq $true -and @($envelope.remaining_servers).Count -eq 0
                    } catch { $envelopeError = $_.Exception.Message }
                }
                $collectionOk = $caseExit -eq 0 -and $null -ne $observation -and
                    $null -eq $parseError -and $toolErrors.Count -eq 0 -and ($EndpointMode -ne 'default' -or $cleanupVerified -eq $true)
                if ($null -ne $observation -and ($observation.schema -ne 2 -or $observation.endpoint_mode -ne $EndpointMode)) {
                    $collectionOk = $false
                    $parseError = 'Unexpected observation schema or endpoint mode.'
                }
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

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..'),
    [ValidateSet('calibration', 'invocations', 'preexisting', 'query-contrast')][string]$Study = 'calibration'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0
$InputRoot = [IO.Path]::GetFullPath($InputRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if ($env:MQB_OWNERSHIP_DISPOSABLE_HOST -ne '1' -or $env:GITHUB_ACTIONS -ne 'true' -or
    $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or (Test-Path Env:_MSPDBSRV_ENDPOINT_)) {
    throw 'File-event tracing requires an authorized pristine disposable hosted VM.'
}
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite earlier trace evidence.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
function Write-Json($Path, $Value) {
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding utf8
}
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source.' }
if (@(& git -C $RepoRoot status --porcelain --untracked-files=no).Count -ne 0 -or $LASTEXITCODE -ne 0) { throw 'Tracked source is not clean.' }
if ((Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim() -cne '5.5.0') { throw 'VERSION changed.' }
$origin = Get-Content -LiteralPath (Join-Path $InputRoot 'probe.identity.json') -Raw | ConvertFrom-Json
$traceOrigin = Get-Content -LiteralPath (Join-Path $InputRoot 'file-trace.identity.json') -Raw | ConvertFrom-Json
if ($origin.source_head -cne $head -or $traceOrigin.source_head -cne $head -or
    $origin.version -cne '5.5.0' -or $origin.configuration -cne 'Release') { throw 'Prebuilt source identity mismatch.' }
$probe = Join-Path $InputRoot 'msvc_service_ownership_probe.exe'
$tracer = Join-Path $InputRoot 'pdb_file_event_probe.exe'
if ($origin.probe_sha256 -cne (Get-FileHash -LiteralPath $probe).Hash -or
    $origin.candidate_sha256 -cne (Get-FileHash -LiteralPath (Join-Path $InputRoot 'mqb.exe')).Hash -or
    $traceOrigin.trace_sha256 -cne (Get-FileHash -LiteralPath $tracer).Hash) { throw 'Prebuilt binary identity mismatch.' }
Write-Json (Join-Path $OutputRoot 'identity.json') @{
    source_head=$head; probe=$origin; tracer=$traceOrigin; image=$env:ImageVersion
    os=[Environment]::OSVersion.VersionString; powershell=$PSVersionTable.PSVersion.ToString()
    run=$env:GITHUB_RUN_ID; attempt=$env:GITHUB_RUN_ATTEMPT
    historical_cause_resolved=$false; all_writer_coverage_proven=$false
}
# Complete plan before the first native capture. Neither outcomes nor runtime
# affect this budget. Boundary calibration files are not compiler PDB failures.
$plan = @('rm-on','rm-off','rm-off','rm-on')
if ($Study -eq 'invocations') {
    $plan = @('pch-release','modules-debug','modules-debug','pch-release',
              'pch-release','modules-debug','modules-debug','pch-release')
}
if ($Study -eq 'preexisting') {
    # T/U, U/T, T/U, U/T; the original untraced mode gets no ETW or sidecars.
    $plan = @('traced','untraced','untraced','traced','traced','untraced','untraced','traced')
}
if ($Study -eq 'query-contrast') {
    # Both arms trace identically; only the four coordinator RM queries differ.
    $plan = @('rm-on','rm-off','rm-off','rm-on','rm-on','rm-off','rm-off','rm-on')
}
$expected = $plan.Count
Write-Json (Join-Path $OutputRoot 'plan.json') @{
    study=$Study; modes=$plan; cases=$expected; pairs=($expected/2); controlled_conflicts_per_trace=2; adaptive_retries=$false
    preexisting_profile=$(if ($Study -in @('preexisting','query-contrast')) { 'zi-debug' } else { $null })
    traced_slots=$(if ($Study -eq 'preexisting') { 4 } else { $expected })
    readiness_wait_limit_ms=10000; negative_readiness_cases=1
    note='Positive native sharing conflicts are separate from original MSVC outcomes; no forced C1041.'
}
# One separately labeled refusal control: omit the file-provider enable request.
# It must wait to its fixed deadline without issuing calibration or launching the
# child. An accidentally admitted child writes a sentinel and fails this control.
$negative = Join-Path $OutputRoot 'negative-readiness'
New-Item -ItemType Directory -Path $negative | Out-Null
$negativeTrace = Join-Path $negative 'trace'
$sentinel = Join-Path $negative 'child-was-admitted.txt'
Write-Json (Join-Path $OutputRoot 'summary.json') @{
    expected=$expected; completed=0; cases=@(); not_run=$expected; negative_readiness_verified=$false
    historical_cause_resolved=$false; authorizes_held_pr_merge=$false; safe_to_transfer_write_lease=$false
}
$negativeArguments = @('--test-withhold-file-provider',$negativeTrace,$tracer,'--test-child-sentinel',$sentinel)
Write-Json (Join-Path $negative 'arguments.json') $negativeArguments
$negativeOutput = @(& $tracer @negativeArguments 2>&1); $negativeExit = $LASTEXITCODE
$negativeOutput | Set-Content -LiteralPath (Join-Path $negative 'output.txt') -Encoding utf8
& python (Join-Path $PSScriptRoot 'verify_pdb_file_trace.py') --trace $negativeTrace --case-root $negative `
    --expect-no-readiness --sentinel $sentinel --output (Join-Path $negative 'audit.json')
$negativeAuditExit = $LASTEXITCODE
$negativeOk = $negativeExit -eq 1 -and $negativeAuditExit -eq 0 -and -not (Test-Path -LiteralPath $sentinel)
Write-Json (Join-Path $negative 'result.json') @{
    expected_native_exit=1; actual_native_exit=$negativeExit; auditor_exit=$negativeAuditExit
    sentinel_exists=(Test-Path -LiteralPath $sentinel); negative_readiness_verified=$negativeOk
}
if (-not $negativeOk) { throw 'Missing-readiness refusal control failed; no compiler cases attempted.' }
# Reuse actual prior validators without executing their discovery/build bodies.
foreach ($definition in @(
    @{ file='collect_msvc_service_ownership.ps1'; names=@('Get-OwnershipDiagnosticErrors','Test-OwnershipCleanupEnvelope','Assert-OwnershipObservation') },
    @{ file='collect_pdb_open_investigation.ps1'; names=@('Assert-PdbQuery','Assert-PdbPair') }
)) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $definition.file),[ref]$tokens,[ref]$errors)
    if (@($errors).Count) { throw 'Existing validator parse failed.' }
    foreach ($name in $definition.names) {
        $functions=@($ast.FindAll({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
        }, $true))
        if ($functions.Count -ne 1) { throw "Ambiguous validation helper: $name" }
        . ([scriptblock]::Create($functions[0].Extent.Text))
    }
}
$rows = [Collections.Generic.List[object]]::new()
$fixtureRoots = [Collections.Generic.List[string]]::new()
foreach ($index in 0..($expected-1)) {
    $mode = $plan[$index]; $name = ('{0:D2}-{1}' -f ($index+1),$mode)
    $slot = Join-Path $OutputRoot $name
    $fixture = Join-Path $slot 'fixture'; $trace = Join-Path $slot 'trace'
    New-Item -ItemType Directory -Path $slot | Out-Null
    $isQueryContrast = $Study -eq 'query-contrast'
    $isPreexisting = $Study -in @('preexisting','query-contrast')
    $isTraced = -not ($isPreexisting -and $mode -eq 'untraced')
    $hasInvocations = $Study -eq 'invocations' -or ($isPreexisting -and $isTraced)
    $profile = if ($isPreexisting) { 'zi-debug' } elseif ($Study -eq 'invocations') { $mode } else { 'pch-release' }
    $caseOrigin = if ($isPreexisting) { 'preexisting' } else { 'A-started' }
    $probeMode = if ($isQueryContrast) { '--query-contrast-case' } elseif (-not $isTraced) { '--default-case' } elseif ($hasInvocations) { '--invocation-case' } else { '--pdb-case' }
    $arguments = @($probeMode,$fixture,$profile,$caseOrigin,'drain',('trace-'+[guid]::NewGuid().ToString('N')))
    if ($Study -eq 'calibration' -or $isQueryContrast) { $arguments += $mode }
    $program = $probe
    if ($isTraced) { $arguments = @($trace,$probe) + $arguments; $program = $tracer }
    Write-Json (Join-Path $slot 'arguments.json') $arguments
    $fixtureRoots.Add($fixture)
    $output = @(& $program @arguments 2>&1); $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $slot 'output.txt') -Encoding utf8
    $errors = [Collections.Generic.List[string]]::new(); $originalOk=$null; $cleanup=$null
    $outcomeExit=$null
    if ($hasInvocations) {
        # Always preserve original outcome diagnostics, even when the recorder
        # failed. An independently complete trace is not compiler success.
        & python (Join-Path $PSScriptRoot 'verify_pdb_invocations.py') --trace $trace --fixture $fixture `
            --native-root $fixture --profile $profile --origin $caseOrigin --output (Join-Path $slot 'invocation-audit.json')
        $outcomeExit=$LASTEXITCODE
        $queryOptions = @()
        if ($isQueryContrast) { $queryOptions = @('--query-mode',$mode) }
        & python (Join-Path $PSScriptRoot 'verify_pdb_observers.py') --trace $trace --fixture $fixture `
            --native-root $fixture --profile $profile --origin $caseOrigin @queryOptions --output (Join-Path $slot 'observer-audit.json')
        $observerExit=$LASTEXITCODE
        if ($observerExit -ne 0) { $errors.Add('Observer API boundary evidence or original control failed.') }
    }
    try {
        if ($code -ne 0) { throw "Native recorder/fixture failed with $code; no retry." }
        if ($hasInvocations -and $outcomeExit -ne 0) { throw 'Invocation evidence or original compiler control failed.' }
        if ($isTraced) {
            & python (Join-Path $PSScriptRoot 'verify_pdb_file_trace.py') --trace $trace --case-root $fixture --require-readiness --output (Join-Path $slot 'trace-audit.json')
            if ($LASTEXITCODE -ne 0) { throw 'Trace integrity/calibration gate failed.' }
        } else {
            if (Test-Path -LiteralPath $trace) { throw 'Untraced control created a trace directory.' }
            $unexpected = @(Get-ChildItem -LiteralPath $fixture -Recurse -File | Where-Object {
                $_.Name -match '\.(invocation-begin|invocation-end)\.json$' -or
                $_.Name -like 'observer-query-*' -or $_.Name -like 'pdb-before-*' -or
                $_.Name -like 'pdb-after-*' -or $_.Name -eq 'events.etl'
            })
            if ($unexpected.Count) { throw 'Untraced control contains investigation sidecars.' }
        }
        $envelope=Get-Content -LiteralPath (Join-Path $fixture 'default-envelope.json') -Raw | ConvertFrom-Json
        $cleanup=Test-OwnershipCleanupEnvelope $envelope
        if (-not $cleanup) { throw 'Original fixture cleanup unproven.' }
        $observation=Get-Content -LiteralPath (Join-Path $fixture 'observation.json') -Raw | ConvertFrom-Json
        if ($observation.profile -cne $profile -or $observation.origin -cne $caseOrigin -or $observation.ending -cne 'drain' -or
            $observation.endpoint_mode -cne 'default' -or $observation.safe_to_transfer_write_lease -isnot [bool] -or
            $observation.safe_to_transfer_write_lease -or $observation.safe_to_integrate_cancellation -isnot [bool] -or
            $observation.safe_to_integrate_cancellation) { throw 'Wrong original fixture identity/safety fields.' }
        $diagnostics=@(Get-OwnershipDiagnosticErrors $fixture $profile $caseOrigin 'drain' $observation)
        # This prior helper is strict about successful drain controls. Preserve
        # failures as failures; do not relabel an A compiler failure as calibration.
        if ($diagnostics.Count) { throw ($diagnostics -join '; ') }
        if ($Study -eq 'calibration') {
            foreach ($i in 0..3) {
                $query=Get-Content -LiteralPath (Join-Path $fixture "rm-query-$i.json") -Raw | ConvertFrom-Json
                Assert-PdbQuery $query ($mode -eq 'rm-on')
            }
            foreach ($phase in @('before-A','after-A','after-B')) {
                $snapshot=Get-Content -LiteralPath (Join-Path $fixture "pdb-$phase.json") -Raw | ConvertFrom-Json
                Assert-PdbPair $snapshot.pdb; Assert-PdbPair $snapshot.pch
            }
        }
        if ($isTraced) {
            $capture=Get-Content -LiteralPath (Join-Path $trace 'capture.json') -Raw | ConvertFrom-Json
            $originalOk=$capture.child.exit_code -eq 0 -and $observation.lifecycle_ok -eq $true -and $observation.drain_control_ok -eq $true
        } else {
            # Reuse the original matrix's assertion, with raw first-A and seed
            # outcomes checked separately. Recovery never supplies these values.
            Assert-OwnershipObservation $observation 'default' $profile $caseOrigin 'drain'
            foreach ($stem in @('A/work0','seed/warm')) {
                $first=Get-Content -LiteralPath (Join-Path $fixture "$stem.result.json") -Raw | ConvertFrom-Json
                if (($first.exit_code -isnot [long] -and $first.exit_code -isnot [int]) -or
                    $first.exit_code -ne 0 -or $first.cancelled -isnot [bool] -or $first.cancelled) {
                    throw "Untraced original compiler control failed: $stem"
                }
            }
            $originalOk=$true
        }
        if (-not $originalOk) { throw 'Original natural-drain control failed; traced failure is not a passing control.' }
    } catch { $errors.Add($_.Exception.Message) }
    $rows.Add([pscustomobject]@{ name=$name; mode=$mode; recorder_used=$isTraced; profile=$profile; origin=$caseOrigin
        process_exit=$code; capture_exit=$(if ($isTraced) { $code } else { $null }); invocation_audit_exit=$outcomeExit; cleanup_verified=$cleanup
        original_control_ok=$originalOk; accepted=($errors.Count -eq 0); errors=@($errors.ToArray()) })
    Write-Json (Join-Path $OutputRoot 'summary.json') @{
        study=$Study; expected=$expected; completed=$rows.Count; cases=@($rows.ToArray()); not_run=($expected-$rows.Count)
        negative_readiness_verified=$negativeOk; input_equivalence_verified=$null
        historical_cause_resolved=$false; authorizes_held_pr_merge=$false; safe_to_transfer_write_lease=$false
    }
    if ($errors.Count) { throw 'Stopped at failed evidence/control gate; remaining slots not attempted, no adaptive retry.' }
}
if ($Study -in @('preexisting','query-contrast')) {
    $pairs = [Collections.Generic.List[object]]::new()
    foreach ($pair in 0..3) {
        $left=$fixtureRoots[$pair*2]; $right=$fixtureRoots[$pair*2+1]
        $auditPath=Join-Path $OutputRoot ("input-pair-$pair.json")
        & python (Join-Path $PSScriptRoot 'verify_pdb_invocations.py') --compare-pair $left $right `
            --left-native-root $left --right-native-root $right --output $auditPath
        $pairOk=$LASTEXITCODE -eq 0
        $pairs.Add([pscustomobject]@{ pair=$pair+1; accepted=$pairOk; report=$auditPath })
        Write-Json (Join-Path $OutputRoot 'input-equivalence.json') @{
            expected_pairs=4; completed_pairs=$pairs.Count; pairs=@($pairs.ToArray())
            all_pairs_verified=($pairOk -and $pairs.Count -eq 4)
        }
        if (-not $pairOk) { throw 'Paired original inputs differ; evidence not accepted.' }
    }
    Write-Json (Join-Path $OutputRoot 'summary.json') @{
        study=$Study; expected=8; completed=$rows.Count; cases=@($rows.ToArray()); not_run=0
        negative_readiness_verified=$negativeOk; input_equivalence_verified=$true
        historical_cause_resolved=$false; authorizes_held_pr_merge=$false; safe_to_transfer_write_lease=$false
    }
}
# This executable inventory is kept outside every timed/native capture and is
# not an attestation of in-flight loaded modules or the separate ABBA pair.
Get-ChildItem -LiteralPath $InputRoot -File | Get-FileHash | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $OutputRoot 'input-hashes.json') -Encoding utf8

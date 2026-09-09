[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..'),
    [ValidateSet('calibration', 'invocations')][string]$Study = 'calibration'
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
$expected = $plan.Count
Write-Json (Join-Path $OutputRoot 'plan.json') @{
    study=$Study; modes=$plan; cases=$expected; pairs=($expected/2); controlled_conflicts_per_trace=2; adaptive_retries=$false
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
    @{ file='collect_msvc_service_ownership.ps1'; names=@('Get-OwnershipDiagnosticErrors','Test-OwnershipCleanupEnvelope') },
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
foreach ($index in 0..($expected-1)) {
    $mode = $plan[$index]; $name = ('{0:D2}-{1}' -f ($index+1),$mode)
    $slot = Join-Path $OutputRoot $name
    $fixture = Join-Path $slot 'fixture'; $trace = Join-Path $slot 'trace'
    New-Item -ItemType Directory -Path $slot | Out-Null
    $profile = if ($Study -eq 'invocations') { $mode } else { 'pch-release' }
    $probeMode = if ($Study -eq 'invocations') { '--invocation-case' } else { '--pdb-case' }
    $arguments = @($trace,$probe,$probeMode,$fixture,$profile,'A-started','drain',('trace-'+[guid]::NewGuid().ToString('N')))
    if ($Study -eq 'calibration') { $arguments += $mode }
    Write-Json (Join-Path $slot 'arguments.json') $arguments
    $output = @(& $tracer @arguments 2>&1); $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $slot 'output.txt') -Encoding utf8
    $errors = [Collections.Generic.List[string]]::new(); $originalOk=$null; $cleanup=$null
    $outcomeExit=$null
    if ($Study -eq 'invocations') {
        # Always preserve original outcome diagnostics, even when the recorder
        # failed. An independently complete trace is not compiler success.
        & python (Join-Path $PSScriptRoot 'verify_pdb_invocations.py') --trace $trace --fixture $fixture `
            --native-root $fixture --profile $profile --output (Join-Path $slot 'invocation-audit.json')
        $outcomeExit=$LASTEXITCODE
    }
    try {
        if ($code -ne 0) { throw "Native trace capture failed with $code; no retry." }
        if ($Study -eq 'invocations' -and $outcomeExit -ne 0) { throw 'Invocation evidence or original compiler control failed.' }
        & python (Join-Path $PSScriptRoot 'verify_pdb_file_trace.py') --trace $trace --case-root $fixture --require-readiness --output (Join-Path $slot 'trace-audit.json')
        if ($LASTEXITCODE -ne 0) { throw 'Trace integrity/calibration gate failed.' }
        $envelope=Get-Content -LiteralPath (Join-Path $fixture 'default-envelope.json') -Raw | ConvertFrom-Json
        $cleanup=Test-OwnershipCleanupEnvelope $envelope
        if (-not $cleanup) { throw 'Original fixture cleanup unproven.' }
        $observation=Get-Content -LiteralPath (Join-Path $fixture 'observation.json') -Raw | ConvertFrom-Json
        if ($observation.profile -cne $profile -or $observation.origin -cne 'A-started' -or $observation.ending -cne 'drain' -or
            $observation.endpoint_mode -cne 'default' -or $observation.safe_to_transfer_write_lease -isnot [bool] -or
            $observation.safe_to_transfer_write_lease -or $observation.safe_to_integrate_cancellation -isnot [bool] -or
            $observation.safe_to_integrate_cancellation) { throw 'Wrong original fixture identity/safety fields.' }
        $diagnostics=@(Get-OwnershipDiagnosticErrors $fixture $profile 'A-started' 'drain' $observation)
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
        $capture=Get-Content -LiteralPath (Join-Path $trace 'capture.json') -Raw | ConvertFrom-Json
        $originalOk=$capture.child.exit_code -eq 0 -and $observation.lifecycle_ok -eq $true -and $observation.drain_control_ok -eq $true
        if (-not $originalOk) { throw 'Original natural-drain control failed; traced failure is not a passing control.' }
    } catch { $errors.Add($_.Exception.Message) }
    $rows.Add([pscustomobject]@{ name=$name; mode=$mode; capture_exit=$code; invocation_audit_exit=$outcomeExit; cleanup_verified=$cleanup
        original_control_ok=$originalOk; accepted=($errors.Count -eq 0); errors=@($errors.ToArray()) })
    Write-Json (Join-Path $OutputRoot 'summary.json') @{
        study=$Study; expected=$expected; completed=$rows.Count; cases=@($rows.ToArray()); not_run=($expected-$rows.Count)
        negative_readiness_verified=$negativeOk
        historical_cause_resolved=$false; authorizes_held_pr_merge=$false; safe_to_transfer_write_lease=$false
    }
    if ($errors.Count) { throw 'Stopped at failed evidence/control gate; remaining slots not attempted, no adaptive retry.' }
}
# This executable inventory is kept outside every timed/native capture and is
# not an attestation of in-flight loaded modules or the separate ABBA pair.
Get-ChildItem -LiteralPath $InputRoot -File | Get-FileHash | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $OutputRoot 'input-hashes.json') -Encoding utf8

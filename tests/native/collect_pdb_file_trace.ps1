[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputRoot,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..')
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
Write-Json (Join-Path $OutputRoot 'plan.json') @{
    modes=$plan; cases=4; pairs=2; controlled_conflicts_per_trace=2; adaptive_retries=$false
    note='Positive native sharing conflicts are separate from original MSVC outcomes; no forced C1041.'
}
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
foreach ($index in 0..3) {
    $mode = $plan[$index]; $name = ('{0:D2}-{1}' -f ($index+1),$mode)
    $slot = Join-Path $OutputRoot $name
    $fixture = Join-Path $slot 'fixture'; $trace = Join-Path $slot 'trace'
    New-Item -ItemType Directory -Path $slot | Out-Null
    $arguments = @($trace,$probe,'--pdb-case',$fixture,'pch-release','A-started','drain',('trace-'+[guid]::NewGuid().ToString('N')),$mode)
    Write-Json (Join-Path $slot 'arguments.json') $arguments
    $output = @(& $tracer @arguments 2>&1); $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $slot 'output.txt') -Encoding utf8
    $errors = [Collections.Generic.List[string]]::new(); $originalOk=$null; $cleanup=$null
    try {
        if ($code -ne 0) { throw "Native trace capture failed with $code; no retry." }
        & python (Join-Path $PSScriptRoot 'verify_pdb_file_trace.py') --trace $trace --case-root $fixture --output (Join-Path $slot 'trace-audit.json')
        if ($LASTEXITCODE -ne 0) { throw 'Trace integrity/calibration gate failed.' }
        $envelope=Get-Content -LiteralPath (Join-Path $fixture 'default-envelope.json') -Raw | ConvertFrom-Json
        $cleanup=Test-OwnershipCleanupEnvelope $envelope
        if (-not $cleanup) { throw 'Original fixture cleanup unproven.' }
        $observation=Get-Content -LiteralPath (Join-Path $fixture 'observation.json') -Raw | ConvertFrom-Json
        if ($observation.profile -cne 'pch-release' -or $observation.origin -cne 'A-started' -or $observation.ending -cne 'drain' -or
            $observation.endpoint_mode -cne 'default' -or $observation.safe_to_transfer_write_lease -isnot [bool] -or
            $observation.safe_to_transfer_write_lease -or $observation.safe_to_integrate_cancellation -isnot [bool] -or
            $observation.safe_to_integrate_cancellation) { throw 'Wrong original fixture identity/safety fields.' }
        $diagnostics=@(Get-OwnershipDiagnosticErrors $fixture 'pch-release' 'A-started' 'drain' $observation)
        # This prior helper is strict about successful drain controls. Preserve
        # failures as failures; do not relabel an A compiler failure as calibration.
        if ($diagnostics.Count) { throw ($diagnostics -join '; ') }
        foreach ($i in 0..3) {
            $query=Get-Content -LiteralPath (Join-Path $fixture "rm-query-$i.json") -Raw | ConvertFrom-Json
            Assert-PdbQuery $query ($mode -eq 'rm-on')
        }
        foreach ($phase in @('before-A','after-A','after-B')) {
            $snapshot=Get-Content -LiteralPath (Join-Path $fixture "pdb-$phase.json") -Raw | ConvertFrom-Json
            Assert-PdbPair $snapshot.pdb; Assert-PdbPair $snapshot.pch
        }
        $capture=Get-Content -LiteralPath (Join-Path $trace 'capture.json') -Raw | ConvertFrom-Json
        $originalOk=$capture.child.exit_code -eq 0 -and $observation.lifecycle_ok -eq $true -and $observation.drain_control_ok -eq $true
        if (-not $originalOk) { throw 'Original natural-drain control failed; traced failure is not a passing control.' }
    } catch { $errors.Add($_.Exception.Message) }
    $rows.Add([pscustomobject]@{ name=$name; mode=$mode; capture_exit=$code; cleanup_verified=$cleanup
        original_control_ok=$originalOk; accepted=($errors.Count -eq 0); errors=@($errors.ToArray()) })
    Write-Json (Join-Path $OutputRoot 'summary.json') @{
        expected=4; completed=$rows.Count; cases=@($rows.ToArray()); not_run=(4-$rows.Count)
        historical_cause_resolved=$false; authorizes_held_pr_merge=$false; safe_to_transfer_write_lease=$false
    }
    if ($errors.Count) { throw 'Stopped at failed evidence/control gate; remaining slots not attempted, no adaptive retry.' }
}
# This executable inventory is kept outside every timed/native capture and is
# not an attestation of in-flight loaded modules or the separate ABBA pair.
Get-ChildItem -LiteralPath $InputRoot -File | Get-FileHash | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $OutputRoot 'input-hashes.json') -Encoding utf8

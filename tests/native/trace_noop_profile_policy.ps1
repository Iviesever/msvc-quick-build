# Explicit O-only N/P study. Import definitions, never execute consumed entries.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactPath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$ReviewedCommit,
    [Parameter(Mandatory)][string]$ProfileSha256,
    [switch]$ExecuteReviewedPolicy
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
if (-not $ExecuteReviewedPolicy -or $env:MQB_CAUSAL_DISPOSABLE_HOST -cne '1') { throw 'Separate reviewed allocation required.' }
if (-not $IsWindows -or $PSVersionTable.PSVersion.Major -lt 7 -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 / PS7 required.' }
if ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted' -or
    $env:GITHUB_EVENT_NAME -cne 'workflow_dispatch' -or $env:GITHUB_RUN_NUMBER -cne '1' -or
    $env:GITHUB_RUN_ATTEMPT -cne '1' -or $env:GITHUB_REF -cne 'refs/heads/main' -or
    $env:GITHUB_SHA -cne $ReviewedCommit -or $env:GITHUB_WORKFLOW_SHA -cne $ReviewedCommit -or
    $env:ALLOCATION -cne '701-noop-profile-policy-001' -or
    $env:GITHUB_WORKFLOW_REF -cne 'Iviesever/msvc-quick-build/.github/workflows/noop-profile-policy-study.yml@refs/heads/main') {
    throw 'Only separately allocated first manual hosted execution; no retry.'
}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root=[IO.Path]::GetFullPath($OutputRoot)
$head=@(& git -C $repo rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1 -or $head[0] -cne $ReviewedCommit) { throw 'Wrong checkout.' }
$dirty=@(& git -C $repo status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Dirty tracked checkout.' }
if (Test-Path -LiteralPath $root) { throw 'Fresh output required, never resume.' }
if ([IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).AvailableFreeSpace -lt 4GB) { throw 'At least 4 GiB free required.' }
$python=(Get-Command python -CommandType Application | Select-Object -First 1).Source
$wpr=(Get-Command wpr.exe -CommandType Application | Select-Object -First 1).Source
$checker=Join-Path $repo 'tests/native/noop_profile_policy.py'
& $python -B $checker prepare --archive ([IO.Path]::GetFullPath($ArtifactPath)) --root $root --repo $repo `
    --reviewed-commit $ReviewedCommit --profile-sha $ProfileSha256 --output (Join-Path $root 'preparation.json')
if ($LASTEXITCODE -ne 0) { throw 'Preparation failed; zero study calls.' }
$plan=Get-Content (Join-Path $root 'plan.json') -Raw | ConvertFrom-Json
# prepare pins these sources; only these original definitions enter script scope.
foreach ($entry in @(
    @{path='collect_external_noop_boundary.ps1'; names=@('Write-NewJson','Get-Digest','Get-FileManifest','Invoke-LegacyBoundary')},
    @{path='trace_noop_causal.ps1'; names=@('Invoke-OwnedWpr')},
    @{path='trace_noop_causal_windows.ps1'; names=@('Invoke-CausalWindowRow','Get-CausalFreeSpace','Assert-CausalWindowBudget')}
)) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root ('source/tests/native/'+$entry.path)),[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Pinned helper parse failure.' }
    foreach ($name in $entry.names) {
        $defs=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name })
        if ($defs.Count -ne 1) { throw 'Non-unique pinned helper.' }
        . ([scriptblock]::Create($defs[0].Extent.Text))
    }
}
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class MqbNoopCausalThread {
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}
'@
$profile=Join-Path $root 'source/tests/native/noop_profile_policy.wprp'
$attempted=0; $controlSequence=0; $owned=$false; $stopError=$null; $failure=$null

function Invoke-PolicyRow($cell,$row,$rec,$fixture) {
    if ($script:attempted -ge 16) { throw 'Fixed 16-call ceiling.' }
    # Same original launcher/result checks; its looser ceiling cannot enlarge this plan.
    Invoke-CausalWindowRow $cell $row $rec $fixture
}
function Save-ProfileInterval([string]$Phase) {
    if ($script:owned) { throw 'Interval query must be outside every study window.' }
    $start=[Diagnostics.Stopwatch]::GetTimestamp()
    Write-NewJson (Join-Path $root ('profile-interval-'+$Phase+'.started.json')) @{phase=$Phase;argv=@('-profint')}
    $code=$null; $err=$null; $lines=@()
    try {
        $PSNativeCommandUseErrorActionPreference=$false
        $lines=@(& $wpr -profint 2>&1 | ForEach-Object { [string]$_ }); $code=$LASTEXITCODE
    } catch { $err=$_.ToString() }
    $end=[Diagnostics.Stopwatch]::GetTimestamp()
    Write-NewJson (Join-Path $root ('profile-interval-'+$Phase+'.json')) @{
        phase=$Phase;argv=@('-profint');exit_code=$code;error=$err;lines=$lines
        start_qpc=$start;end_qpc=$end;changes_interval=$false}
    if ($null -ne $err -or $null -eq $code -or $code -ne 0 -or $lines.Count -eq 0) { throw 'Interval query failed; retain prefix.' }
}
function Invoke-PolicyCell($cell) {
    if ($script:owned -or $null -ne $script:stopError) { throw 'Prior stop uncertain; no next prime.' }
    if ($script:attempted -ge 16 -or $cell.rows.Count -ne 2 -or
        $cell.rows[0].sequence -ne ($script:attempted+1) -or $cell.policy -cnotin @('N','P')) { throw 'Plan/order/budget changed.' }
    if ($cell.policy -ceq 'P' -and $script:controlSequence -gt 15) { throw '20-control ceiling.' }
    $fixture=Join-Path $root ('fixtures/'+$cell.id); $segment=Join-Path $root ('traces/'+$cell.id)
    $rec=[ordered]@{cell=$cell;before=$null;after_prime=$null;after_final=$null
        calls=[Collections.Generic.List[object]]::new();error=$null;window=$null;budget_before=$null;budget_after=$null}
    $primary=$null; $saveError=$null
    try {
        $rec.budget_before=Assert-CausalWindowBudget $segment
        $rec.before=Get-FileManifest $fixture
        Invoke-PolicyRow $cell $cell.rows[0] $rec $fixture
        $rec.after_prime=Get-FileManifest $fixture
        Write-NewJson (Join-Path $root ('primes/'+$cell.id+'.json')) @{
            cell=$cell;before=$rec.before;after_prime=$rec.after_prime;call=$rec.calls[0]}
        if ($cell.policy -ceq 'P') {
            if (Test-Path -LiteralPath $segment) { throw 'Fresh segment required.' }
            $null=New-Item -ItemType Directory -Path (Join-Path $segment 'temp')
            $script:instance='MQB-NoopPolicy-'+[guid]::NewGuid().ToString('N')
            $rec.window=[ordered]@{instance=$instance;first_control=($script:controlSequence+1);last_control=$null
                start_qpc=$null;ready_qpc=$null;calls_end_qpc=$null;stop_qpc=$null;stopped_qpc=$null
                stop_attempted=$false;stop_error=$null;owned_after_stop=$null}
            $null=Assert-CausalWindowBudget $segment
            $rec.window.start_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
            Invoke-OwnedWpr -WprArguments @('-start',($profile+'!MqbNoopPolicy.Verbose'),'-filemode','-recordtempto',(Join-Path $segment 'temp'))
            Invoke-OwnedWpr -WprArguments @('-marker',('MQB_NOOP_POLICY|'+$cell.id+'|begin'))
            $rec.window.ready_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        }
        # One final-off call, unchanged console/output semantics; no compensating sleep.
        Invoke-PolicyRow $cell $cell.rows[1] $rec $fixture
        if ($cell.policy -ceq 'P') {
            $rec.window.calls_end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
            Invoke-OwnedWpr -WprArguments @('-marker',('MQB_NOOP_POLICY|'+$cell.id+'|end'))
            Invoke-OwnedWpr -WprArguments @('-status','collectors','-details')
            $null=Assert-CausalWindowBudget $segment
        }
    } catch { $primary=$_.ToString(); $rec.error=$primary }
    finally {
        if ($script:owned) {
            $rec.window.stop_attempted=$true; $rec.window.stop_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
            try { Invoke-OwnedWpr -WprArguments @('-stop',(Join-Path $segment 'trace.etl'),'MQB N/P policy; not a score','-skipPdbGen') }
            catch { $rec.window.stop_error=$_.ToString(); $script:stopError=$rec.window.stop_error }
            $rec.window.stopped_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        }
        if ($null -ne $rec.window) {
            $rec.window.owned_after_stop=[bool]$script:owned; $rec.window.last_control=$script:controlSequence
        }
        try {
            if ($null -eq $primary -and $null -eq $script:stopError) {
                $rec.budget_after=Assert-CausalWindowBudget $segment
                if ($cell.policy -ceq 'P' -and $rec.budget_after.segment_etl_bytes -le 0) { throw 'Missing P trace.' }
                $rec.after_final=Get-FileManifest $fixture
                if (($rec.after_prime | ConvertTo-Json -Depth 8 -Compress) -cne ($rec.after_final | ConvertTo-Json -Depth 8 -Compress)) { throw 'No-op changed fixture.' }
            }
        } catch { $primary=$_.ToString(); $rec.error=$primary }
        try { Write-NewJson (Join-Path $root ('cells/'+$cell.id+'.json')) $rec } catch { $saveError=$_.ToString() }
    }
    if ($null -ne $primary -or $null -ne $script:stopError -or $null -ne $saveError) {
        throw "Policy stopped; original: $primary; stop: $script:stopError; save: $saveError"
    }
}
$pins=[Collections.Generic.List[IO.FileStream]]::new()
try {
    foreach ($side in @('baseline','candidate')) {
        $file=Join-Path $root "inputs/$side.exe"
        if ((Get-Digest $file) -cne $plan.original.binaries.$side) { throw 'Binary changed.' }
        $pins.Add([IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
    }
    Write-NewJson (Join-Path $root 'host.json') @{pid=$PID;powershell=$PSVersionTable.PSVersion.ToString()
        qpc_frequency=[Diagnostics.Stopwatch]::Frequency;utc=[DateTime]::UtcNow.ToString('o')
        wpr_path=$wpr;wpr_sha256=(Get-Digest $wpr);image_version=$env:ImageVersion
        reviewed_commit=$ReviewedCommit;clears_hold=$false}
    Save-ProfileInterval 'before'
    foreach ($cell in $plan.cells) { Invoke-PolicyCell $cell }
    Save-ProfileInterval 'after'
} catch { $failure=$_.ToString() }
finally {
    # Cells own their only stop. Never retry it, including after a journal error.
    foreach ($pin in $pins) { $pin.Dispose() }
    Write-NewJson (Join-Path $root 'completion.json') @{
        status=$(if ($null -eq $failure -and $null -eq $stopError -and $attempted -eq 16) { 'policy_calls_complete_trace_unreviewed' } else { 'stopped' })
        attempted=$attempted;error=$failure;trace_stop_error=$stopError;cause=$null;clears_hold=$false}
}
if ($null -ne $failure -or $null -ne $stopError) { throw 'Stopped policy; no continuation or retry.' }
& $python -B $checker audit --root $root --output (Join-Path $root 'journal-audit.json')
if ($LASTEXITCODE -ne 0) { throw 'Policy journal refused; not an event/causal verdict.' }

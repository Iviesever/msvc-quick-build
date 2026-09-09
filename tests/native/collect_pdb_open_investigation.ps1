[CmdletBinding(DefaultParameterSetName = 'Study')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Study')][string]$MqbPath,
    [Parameter(Mandatory, ParameterSetName = 'Study')][string]$ProbePath,
    [Parameter(Mandatory, ParameterSetName = 'Study')][string]$ProbeIdentityPath,
    [Parameter(Mandatory, ParameterSetName = 'Contract')][switch]$SelfTest,
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..')
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version 2.0
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Refusing to overwrite an earlier investigation.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
function Write-Json($Path, $Value) {
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding utf8
}
function Assert-PdbQuery($Record, [bool]$Enabled) {
    if ($Record.attempted -isnot [bool] -or $Record.attempted -ne $Enabled) { throw 'Wrong query mode.' }
    if ($Enabled) {
        if (($Record.error -isnot [long] -and $Record.error -isnot [int]) -or
            $Record.error -lt 0 -or $Record.owners -isnot [array]) { throw 'Invalid query outcome.' }
    } elseif ($null -ne $Record.error -or $null -ne $Record.owners) { throw 'Disabled query must remain unavailable.' }
}
function Assert-PdbPair($Pair) {
    foreach ($side in @('A', 'B')) {
        $r = $Pair.$side
        if (($r.open_error -isnot [int] -and $r.open_error -isnot [long]) -or $r.open_error -lt 0) { throw 'Missing native open error.' }
        if ($r.open_error -ne 0) {
            if ($null -ne $r.identity_error -or $null -ne $r.file_id -or $null -ne $r.volume_serial) { throw 'Failed open cannot invent identity.' }
        } else {
            if (($r.identity_error -isnot [int] -and $r.identity_error -isnot [long]) -or $r.identity_error -lt 0) { throw 'Missing identity query error.' }
            if ($r.identity_error -eq 0) {
                if ($r.file_id -isnot [string] -or $r.file_id -cnotmatch '^[0-9a-f]{32}$' -or
                    $r.volume_serial -isnot [string] -or $r.volume_serial -notmatch '^\d+$') { throw 'Malformed file identity.' }
            } elseif ($null -ne $r.file_id -or $null -ne $r.volume_serial) { throw 'Failed query cannot invent identity.' }
        }
    }
    $known = $Pair.A.open_error -eq 0 -and $Pair.B.open_error -eq 0 -and
        $Pair.A.identity_error -eq 0 -and $Pair.B.identity_error -eq 0
    if (-not $known) {
        if ($null -ne $Pair.same_file) { throw 'Unavailable identity cannot prove distinctness.' }
    } else {
        $same = $Pair.A.volume_serial -ceq $Pair.B.volume_serial -and $Pair.A.file_id -ceq $Pair.B.file_id
        if ($Pair.same_file -isnot [bool] -or $Pair.same_file -ne $same) { throw 'Identity comparison disagreement.' }
    }
}
if ($SelfTest) {
    $rows = @(foreach ($case in 0..9) {
        $pair = [pscustomobject]@{
            A = [pscustomobject]@{ open_error=0; identity_error=0; volume_serial='17'; file_id=('a'*32) }
            B = [pscustomobject]@{ open_error=0; identity_error=0; volume_serial='17'; file_id=('b'*32) }
            same_file=$false
        }
        $query = [pscustomobject]@{ attempted=$false; error=$null; owners=$null }
        $expected = $case -in @(0,1,2)
        switch ($case) {
            1 { $pair.B.file_id=$pair.A.file_id; $pair.same_file=$true }
            2 { $pair.A.open_error=2; $pair.A.identity_error=$null; $pair.A.file_id=$null; $pair.A.volume_serial=$null; $pair.same_file=$null }
            3 { $pair.A.open_error='0' }
            4 { $pair.B.file_id='bad' }
            5 { $pair.same_file='false' }
            6 { $pair.B.file_id=$pair.A.file_id }
            7 { $pair.A.identity_error=5 }
            8 { $query.error=0; $query.owners=@() }
            9 { $query.attempted='false' }
        }
        $errorText=$null
        try { Assert-PdbPair $pair; Assert-PdbQuery $query $false } catch { $errorText=$_.Exception.Message }
        [pscustomobject]@{ case=$case; expected_accepted=$expected; accepted=($null -eq $errorText)
            passed=($expected -eq ($null -eq $errorText)); error=$errorText }
    })
    Write-Json (Join-Path $OutputRoot 'contracts.json') @{ synthetic_only=$true; cases=$rows }
    if (@($rows | Where-Object { -not $_.passed }).Count) { throw 'PDB evidence contract failed.' }
    Write-Host 'PDB_RECORD_CONTRACT 10 checks passed (synthetic)'
    return
}
if ($env:MQB_OWNERSHIP_DISPOSABLE_HOST -ne '1' -or $env:GITHUB_ACTIONS -ne 'true' -or
    $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or (Test-Path Env:_MSPDBSRV_ENDPOINT_)) {
    throw 'Requires an authorized pristine default endpoint on a disposable hosted VM.'
}
$ProbePath=[IO.Path]::GetFullPath($ProbePath); $MqbPath=[IO.Path]::GetFullPath($MqbPath)
$head=(& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Unknown source head.' }
if (@(& git -C $RepoRoot status --porcelain --untracked-files=no).Count -ne 0 -or $LASTEXITCODE -ne 0) { throw 'Dirty tracked source.' }
if ((Get-Content (Join-Path $RepoRoot 'VERSION') -Raw).Trim() -cne '5.5.0') { throw 'VERSION changed.' }
$origin=Get-Content -LiteralPath $ProbeIdentityPath -Raw | ConvertFrom-Json
if ($origin.source_head -cne $head -or $origin.version -cne '5.5.0' -or $origin.configuration -cne 'Release' -or
    $origin.probe_sha256 -cne (Get-FileHash -LiteralPath $ProbePath).Hash -or
    $origin.candidate_sha256 -cne (Get-FileHash -LiteralPath $MqbPath).Hash) { throw 'Prebuilt identity mismatch.' }
Copy-Item -LiteralPath $ProbePath -Destination (Join-Path $OutputRoot 'observed-probe.exe')
Copy-Item -LiteralPath $MqbPath -Destination (Join-Path $OutputRoot 'builder-mqb.exe')
Write-Json (Join-Path $OutputRoot 'identity.json') @{ head=$head; origin=$origin; os=[Environment]::OSVersion.VersionString
    powershell=$PSVersionTable.PSVersion.ToString(); image=$env:ImageVersion; run_id=$env:GITHUB_RUN_ID
    attempt=$env:GITHUB_RUN_ATTEMPT; historical_cause_resolved=$false }
$plan=@(foreach ($pair in 1..4) {
    $modes=if ($pair % 2) { @('rm-on','rm-off') } else { @('rm-off','rm-on') }
    foreach ($mode in $modes) { [pscustomobject]@{ pair=$pair; mode=$mode; name=('{0:D2}-{1}' -f $pair,$mode) } }
})
Write-Json (Join-Path $OutputRoot 'plan.json') @{ cases=$plan; fixed_cases=8; repetitions_per_mode=4
    profile='pch-release'; service_origin='A-started'; ending='drain'; adaptive_retries=$false }
$rows=[Collections.Generic.List[object]]::new()
$allTools=@{}
foreach ($slot in $plan) {
    $dir=Join-Path $OutputRoot $slot.name
    New-Item -ItemType Directory -Path $dir | Out-Null
    $arguments=@('--pdb-case',$dir,'pch-release','A-started','drain',('pdb-'+[guid]::NewGuid().ToString('N')),$slot.mode)
    Write-Json (Join-Path $dir 'case-arguments.json') $arguments
    $output=@(& $ProbePath @arguments 2>&1); $code=$LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $dir 'outer-output.txt') -Encoding utf8
    $errors=[Collections.Generic.List[string]]::new(); $cleanup=$false; $observation=$null; $aCode=$null
    try {
        $envelope=Get-Content -LiteralPath (Join-Path $dir 'default-envelope.json') -Raw | ConvertFrom-Json
        $cleanup=$envelope.cleanup_verified -is [bool] -and $envelope.cleanup_verified -and
            $envelope.outer_lifecycle_verified -is [bool] -and $envelope.outer_lifecycle_verified -and
            $envelope.endpoint_override_absent -is [bool] -and $envelope.endpoint_override_absent -and
            $envelope.remaining_servers -is [array] -and $envelope.remaining_servers.Count -eq 0
        if (-not $cleanup) { throw 'Unproven outer cleanup.' }
        $study=Get-Content -LiteralPath (Join-Path $dir 'pdb-study.json') -Raw | ConvertFrom-Json
        $enabled=$slot.mode -eq 'rm-on'
        if ($study.schema -ne 1 -or $study.query_slots -ne 4 -or $study.rm_queries_enabled -isnot [bool] -or
            $study.rm_queries_enabled -ne $enabled -or $study.historical_cause_resolved -isnot [bool] -or
            $study.historical_cause_resolved -or $study.safe_to_transfer_write_lease -isnot [bool] -or
            $study.safe_to_transfer_write_lease) { throw 'Wrong study identity/safety record.' }
        foreach ($i in 0..3) {
            $record=Get-Content -LiteralPath (Join-Path $dir "rm-query-$i.json") -Raw | ConvertFrom-Json
            Assert-PdbQuery $record $enabled
        }
        foreach ($phase in @('before-A','after-A','after-B')) {
            $snapshot=Get-Content -LiteralPath (Join-Path $dir "pdb-$phase.json") -Raw | ConvertFrom-Json
            if ($snapshot.phase -cne $phase -or $snapshot.desired_access -ne 128 -or $snapshot.share_mode -ne 7 -or
                $snapshot.safe_to_transfer_write_lease -isnot [bool] -or $snapshot.safe_to_transfer_write_lease) { throw 'Wrong snapshot contract.' }
            Assert-PdbPair $snapshot.pdb; Assert-PdbPair $snapshot.pch
            if ($phase -eq 'before-A' -and ($snapshot.pdb.same_file -cne $false -or $snapshot.pch.same_file -cne $false)) {
                throw 'A/B file identities not proven distinct before workload.'
            }
        }
        $observation=Get-Content -LiteralPath (Join-Path $dir 'observation.json') -Raw | ConvertFrom-Json
        if ($observation.profile -cne 'pch-release' -or $observation.origin -cne 'A-started' -or $observation.ending -cne 'drain' -or
            $observation.endpoint_mode -cne 'default' -or $observation.safe_to_integrate_cancellation -isnot [bool] -or
            $observation.safe_to_integrate_cancellation -or $observation.safe_to_transfer_write_lease -isnot [bool] -or
            $observation.safe_to_transfer_write_lease) { throw 'Wrong original observation identity or safety flags.' }
        $stems=@('A','A/prefix','A/warm','A/work0','B/prefix','B/warm','B/work0','B/work1','B/recovery')
        if ($observation.B0_exit -eq 0 -and $observation.B1_exit -eq 0) {
            $stems+='B/link'; if ($observation.B_link_exit -eq 0) { $stems+='B/run' }
        }
        $mapping=@{ A='A_exit'; 'B/work0'='B0_exit'; 'B/work1'='B1_exit'; 'B/link'='B_link_exit'; 'B/run'='B_run_exit'; 'B/recovery'='recovery_compile_exit' }
        foreach ($stem in $stems) {
            $r=Get-Content -LiteralPath (Join-Path $dir "$stem.result.json") -Raw | ConvertFrom-Json
            if ($null -ne $r.PSObject.Properties['infrastructure_error'] -or
                ($r.exit_code -isnot [int] -and $r.exit_code -isnot [long]) -or $r.cancelled -isnot [bool] -or $r.cancelled) { throw "Invalid tool result: $stem" }
            if ($mapping.ContainsKey($stem) -and $r.exit_code -ne $observation.($mapping[$stem])) { throw "Original outcome mismatch: $stem" }
            if ($stem -eq 'A/work0') { $aCode=$r.exit_code }
            foreach ($suffix in @('stdout.txt','stderr.txt')) {
                if (-not (Test-Path -LiteralPath (Join-Path $dir "$stem.$suffix") -PathType Leaf)) { throw "Missing diagnostic: $stem.$suffix" }
            }
            if ($stem -ne 'A' -and -not (Test-Path -LiteralPath (Join-Path $dir "$stem.argv.txt") -PathType Leaf)) { throw "Missing argv: $stem" }
        }
        $drain=Get-Content -LiteralPath (Join-Path $dir 'A/drain.json') -Raw | ConvertFrom-Json
        if ($drain.first_compile_exit -ne $aCode -or $drain.work_compiles_dispatched -ne 1 -or $drain.pending_compile_exit -ne -2 -or
            $drain.stop_observed -isnot [bool] -or -not $drain.stop_observed -or
            $drain.safe_to_transfer_write_lease -isnot [bool] -or $drain.safe_to_transfer_write_lease -or
            (Test-Path -LiteralPath (Join-Path $dir 'A/work1.argv.txt'))) { throw 'Original drain outcome mismatch.' }
        if ($code -ne 0 -and $code -ne 1) { throw 'Unexpected outer exit.' }
        # A nonzero original compiler result remains a recorded failure, not a
        # rejected record or a passing drain control. Never replace it by B recovery.
        $compiler=(Get-Content -LiteralPath (Join-Path $dir 'toolchain.txt'))[0]
        $toolDir=[IO.Path]::GetDirectoryName($compiler)
        foreach ($name in @('cl.exe','c1xx.dll','c2.dll','mspdbcore.dll','mspdbsrv.exe','link.exe')) {
            $path=Join-Path $toolDir $name
            if (-not $allTools.ContainsKey($path)) {
                $allTools[$path]=[ordered]@{ path=$path; available=(Test-Path -LiteralPath $path -PathType Leaf)
                    phase='post-case tool-file inventory, not an in-flight module attestation' }
                if ($allTools[$path].available) {
                    $allTools[$path].sha256=(Get-FileHash -LiteralPath $path).Hash
                    $allTools[$path].version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion
                }
            }
        }
    } catch { $errors.Add($_.Exception.Message) }
    $originalOk=$code -eq 0 -and $aCode -eq 0 -and $null -ne $observation -and
        $observation.drain_control_ok -eq $true -and $observation.lifecycle_ok -eq $true
    $rows.Add([pscustomobject]@{ name=$slot.name; mode=$slot.mode; exit_code=$code; cleanup_verified=$cleanup
        evidence_complete=($errors.Count -eq 0); errors=@($errors.ToArray()); original_control_ok=$originalOk
        original_A_compile_exit=$aCode; observation=$observation })
    Write-Json (Join-Path $OutputRoot 'summary.json') @{ expected=8; completed=$rows.Count; cases=@($rows.ToArray())
        original_failures=@($rows | Where-Object { -not $_.original_control_ok }).Count
        historical_cause_resolved=$false; authorizes_held_pr_merge=$false
        not_run=@($plan.name | Where-Object { $_ -notin $rows.name }) }
    if (-not $cleanup -or $errors.Count) { throw 'Investigation stopped at incomplete evidence; no retry or further file inventory.' }
    # Retain PDB/PCH bytes only for a failed case after confirmed outer cleanup.
    # Successful case files and all source/argv/diagnostic files also remain local;
    # the workflow upload excludes generated binaries except this failure snapshot.
    if (-not $originalOk) {
        $failure=Join-Path $OutputRoot ('failure-files/'+$slot.name)
        foreach ($project in @('A','B')) {
            New-Item -ItemType Directory -Path (Join-Path $failure $project) -Force | Out-Null
            foreach ($name in @('compiler.pdb','common.pch')) {
                $file=Join-Path $dir "$project/$name"
                if (Test-Path -LiteralPath $file -PathType Leaf) { Copy-Item -LiteralPath $file -Destination (Join-Path $failure $project) }
            }
        }
    }
}
Write-Json (Join-Path $OutputRoot 'tool-files.json') @($allTools.Values)
if ($rows.Count -ne 8) { throw 'Incomplete fixed budget.' }
Write-Host "PDB_OPEN_INVESTIGATION 8/8 records; $(@($rows | Where-Object { -not $_.original_control_ok }).Count) original control failures; historical cause unresolved."

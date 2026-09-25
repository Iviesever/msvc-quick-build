# New A2/B2 in one disposable job; reserved name is not execution authorization.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Root,
      [Parameter(Mandatory)][string]$SourceRoot,
      [switch]$ExecuteReviewedSameJob)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0

function Import-SameJobDefinitions([string]$Path,[string[]]$Names) {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Pinned definitions do not parse.' }
    foreach ($name in $Names) {
        $defs=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name })
        if ($defs.Count -ne 1) { throw "Missing unique definition: $name" }
        $text=$defs[0].Extent.Text -replace ('^function\s+'+[regex]::Escape($name)+'(?=[\s(])'),('function script:'+$name)
        . ([scriptblock]::Create($text))
    }
}
function Invoke-SameJobCheck([string]$Command,[string]$Evidence,[string]$Repo) {
    & python -B (Join-Path $Repo 'tests/native/v9_noop_samejob.py') $Command --root $Evidence --repo $Repo
    if ($LASTEXITCODE -ne 0) { throw "Same-job $Command refused." }
}
function Assert-SameJobSources([string]$Sources,[string]$Repo) {
    & python -B (Join-Path $Repo 'tests/native/v9_noop_validation.py') check-sources --root $Sources
    if ($LASTEXITCODE -ne 0) { throw 'Exact A2/B2 source check failed.' }
}
function Assert-SameJobEnvironment($Before,$Expected) {
    # Includes PATH digest: unlike the old cross-VM case all phases share this host.
    foreach ($key in @('image','powershell','tools','sdk_versions','ambient_sha256')) {
        if (($Before[$key] | ConvertTo-Json -Depth 12 -Compress) -cne
            ($Expected.$key | ConvertTo-Json -Depth 12 -Compress)) { throw "Same-job environment changed: $key" }
    }
}
function Invoke-SameJobPipeline([string]$Repo,[string]$Sources,[string]$Evidence) {
    $script:admitted=0;$script:attempted=0
    $failure=$null;$preparationFailure=$null
    $pins=[Collections.Generic.List[IO.FileStream]]::new()
    $prepared=Join-Path $Evidence 'preparation';$measure=Join-Path $Evidence 'measurement'
    if ((Test-Path -LiteralPath $prepared) -or (Test-Path -LiteralPath $measure)) { throw 'Fresh phase directories required; no resume.' }
    $null=New-Item -ItemType Directory -Path (Join-Path $prepared 'bin')
    try {
        Assert-SameJobSources $Sources $Repo
        try {
            # Original helper: seed help(1), A2 build/help(2), B2 build/help(2).
            # Downloads finish here, before the immutable intermediate manifest.
            Invoke-V9Preparation $Repo $Sources $prepared
            Assert-SameJobSources $Sources $Repo
        } catch { $preparationFailure=$_.ToString();throw }
        finally {
            Write-NewJson (Join-Path $prepared 'completion.json') @{
                status=$(if ($null -eq $preparationFailure -and $script:admitted -eq 5) {'prepared_unmeasured'} else {'stopped'})
                mqb_ceiling_admitted=$script:admitted;error=$preparationFailure;clears_hold=$false}
        }
        foreach ($side in @('baseline','candidate')) {
            $pins.Add([IO.File]::Open((Join-Path $prepared "bin/$side.exe"),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
        }
        # Both must succeed BEFORE any prime. Neither changes the old request/manifest.
        Invoke-SameJobCheck 'freeze' $Evidence $Repo
        Invoke-SameJobCheck 'ready' $Evidence $Repo
        $manifest=Get-Content (Join-Path $prepared 'manifest.json') -Raw | ConvertFrom-Json
        $plan=Get-Content (Join-Path $measure 'plan.json') -Raw | ConvertFrom-Json
        $before=Get-V9Environment
        Write-NewJson (Join-Path $measure 'environment-before.json') $before
        Assert-SameJobEnvironment $before $manifest.environment
        # Same fixed launcher, real CLI cache projection, and 16-call hard ceiling.
        Invoke-V9Calls $plan $measure $prepared $before
        $after=Get-V9Environment
        Write-NewJson (Join-Path $measure 'environment-after.json') $after
        Assert-SameJobEnvironment $after $manifest.environment
    } catch { $failure=$_.ToString() }
    finally {
        foreach ($pin in $pins) { $pin.Dispose() }
        # Missing results stay missing. No retry/cleanup invocation of a measured program.
        if (Test-Path -LiteralPath $measure) {
            Write-NewJson (Join-Path $measure 'completion.json') @{
                status=$(if ($null -eq $failure -and $script:attempted -eq 16) {'calls_complete_unreviewed'} else {'stopped'})
                attempted=$script:attempted;error=$failure;clears_hold=$false}
        }
        Write-NewJson (Join-Path $Evidence 'completion.json') @{
            status=$(if ($null -eq $failure -and $script:admitted -eq 5 -and $script:attempted -eq 16) {'same_job_complete_unreviewed'} else {'stopped'})
            preparation_mqb_ceiling_admitted=$script:admitted;study_attempted=$script:attempted;error=$failure;clears_hold=$false}
    }
    if ($null -ne $failure) { throw $failure }
}

# Contract tests import definitions only, never this entry or the old guarded entries.
if (-not $ExecuteReviewedSameJob -or -not $IsWindows -or -not [Environment]::Is64BitProcess -or
    $PSVersionTable.PSVersion.Major -lt 7) { throw 'Reviewed same-job Windows x64/PS7 required.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Root=[IO.Path]::GetFullPath($Root);$SourceRoot=[IO.Path]::GetFullPath($SourceRoot)
Invoke-SameJobCheck 'admit' $Root $repo
Import-SameJobDefinitions (Join-Path $PSScriptRoot 'collect_external_noop_boundary.ps1') @(
    'Write-NewJson','Get-Digest','Get-FileManifest','Invoke-LegacyBoundary')
Import-SameJobDefinitions (Join-Path $PSScriptRoot 'run_v9_noop_validation.ps1') @(
    'Get-V9Environment','Assert-V9Space','Get-V9CacheProjection','Assert-V9Call','Invoke-V9Calls','Invoke-V9Preparation')
if (-not [Diagnostics.Stopwatch]::IsHighResolution) { throw 'High-resolution QPC required.' }
& python -B -c "import sys;sys.path.insert(0,sys.argv[1]);import v9_noop_validation as v;v.milliseconds(1,int(sys.argv[2]))" $PSScriptRoot ([Diagnostics.Stopwatch]::Frequency)
if ($LASTEXITCODE -ne 0) { throw 'Unsupported exact QPC conversion.' }
Invoke-SameJobPipeline $repo $SourceRoot $Root
& python -B (Join-Path $PSScriptRoot 'v9_noop_samejob.py') audit --root $Root --repo $repo --output (Join-Path $Root 'decision.json')
if ($LASTEXITCODE -ne 0) { throw 'Invalid evidence or regression HOLD; retain results and do not retry.' }

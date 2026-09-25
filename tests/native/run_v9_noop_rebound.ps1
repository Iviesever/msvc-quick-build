# Independent, unallocated V9 rebound. No preparation, old-entry execution or retry.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Root,
      [Parameter(Mandatory)][string]$InputsRoot,
      [switch]$ExecuteReviewedRebound)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0

function Import-V9ReboundDefinitions([string]$Path,[string[]]$Names) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Pinned definitions do not parse.' }
    foreach ($name in $Names) {
        $defs=@($ast.EndBlock.Statements | Where-Object {
            $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name
        })
        if ($defs.Count -ne 1) { throw "Missing unique definition: $name" }
        $text=$defs[0].Extent.Text -replace ('^function\s+'+[regex]::Escape($name)+'(?=[\s(])'),('function script:'+$name)
        . ([scriptblock]::Create($text))
    }
}

function Assert-V9ReboundEnvironment($Before,$Prepared) {
    # Same key-inventory rule as the original entry; PATH is host-local.
    foreach ($key in @('image','powershell','tools','sdk_versions')) {
        if (($Before[$key] | ConvertTo-Json -Depth 12 -Compress) -cne
            ($Prepared.$key | ConvertTo-Json -Depth 12 -Compress)) { throw "Unmatched host field: $key" }
    }
}

function Invoke-V9ReboundCalls($Plan,[string]$Evidence,[string]$Inputs,$Manifest) {
    $script:attempted=0
    $failure=$null
    $pins=[Collections.Generic.List[IO.FileStream]]::new()
    try {
        Assert-V9Space $Evidence 8GB
        $before=Get-V9Environment
        Write-NewJson (Join-Path $Evidence 'environment-before.json') $before
        Assert-V9ReboundEnvironment $before $Manifest.environment
        foreach ($side in @('baseline','candidate')) {
            $pins.Add([IO.File]::Open((Join-Path $Inputs "bin/$side.exe"),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
        }
        Invoke-V9Calls $Plan $Evidence $Inputs $before
        $after=Get-V9Environment
        Write-NewJson (Join-Path $Evidence 'environment-after.json') $after
    } catch { $failure=$_.ToString() }
    finally {
        foreach ($pin in $pins) { $pin.Dispose() }
        Write-NewJson (Join-Path $Evidence 'completion.json') @{
            status=$(if ($null -eq $failure -and $attempted -eq 16) {'calls_complete_unreviewed'} else {'stopped'})
            attempted=$attempted;error=$failure;clears_hold=$false}
    }
    if ($null -ne $failure) { throw $failure }
}

# Tests extract definitions only; the guarded entry is never dot-sourced.
if (-not $ExecuteReviewedRebound -or -not $IsWindows -or -not [Environment]::Is64BitProcess -or
    $PSVersionTable.PSVersion.Major -lt 7) { throw 'Reviewed hosted Windows x64/PS7 rebound required.' }
$Root=[IO.Path]::GetFullPath($Root); $InputsRoot=[IO.Path]::GetFullPath($InputsRoot)
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$checker=Join-Path $PSScriptRoot 'v9_noop_rebound.py'
& python -B $checker admit --root $Root --repo $repo
if ($LASTEXITCODE -ne 0) { throw 'Execution request refused before MQB.' }
$head=@(& git -C $repo rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1 -or $head[0] -cne $env:REVIEWED_COMMIT) { throw 'Execution checkout mismatch.' }
$dirty=@(& git -C $repo status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Tracked execution harness changed.' }
Import-V9ReboundDefinitions (Join-Path $PSScriptRoot 'collect_external_noop_boundary.ps1') @(
    'Write-NewJson','Get-Digest','Get-FileManifest','Invoke-LegacyBoundary')
Import-V9ReboundDefinitions (Join-Path $PSScriptRoot 'run_v9_noop_validation.ps1') @(
    'Get-V9Environment','Assert-V9Space','Get-V9CacheProjection','Assert-V9Call','Invoke-V9Calls')
& python -B $checker prepare-measure --root $Root --inputs $InputsRoot --repo $repo
if ($LASTEXITCODE -ne 0) { throw 'Frozen preparation/execution binding refused.' }
$plan=Get-Content (Join-Path $Root 'plan.json') -Raw | ConvertFrom-Json
$manifest=Get-Content (Join-Path $InputsRoot 'manifest.json') -Raw | ConvertFrom-Json
if (-not [Diagnostics.Stopwatch]::IsHighResolution) { throw 'High-resolution QPC required.' }
& python -B -c "import sys;sys.path.insert(0,sys.argv[1]);import v9_noop_validation as v;v.milliseconds(1,int(sys.argv[2]))" $PSScriptRoot ([Diagnostics.Stopwatch]::Frequency)
if ($LASTEXITCODE -ne 0) { throw 'Unsupported exact clock conversion.' }
Invoke-V9ReboundCalls $plan $Root $InputsRoot $manifest
& python -B $checker audit --root $Root --inputs $InputsRoot --output (Join-Path $Root 'decision.json')
if ($LASTEXITCODE -ne 0) { throw 'Invalid evidence or regression HOLD; preserve result, never retry.' }

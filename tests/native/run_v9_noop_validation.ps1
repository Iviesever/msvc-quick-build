# Fixed V9 before/after study. Manual only; neither phase is allocated by this file.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Root,
      [Parameter(Mandatory)][string]$InputsRoot,
      [switch]$ExecuteReviewedPhase)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0

function Get-V9Environment {
    # Do not import a developer shell or silently remove ambient compiler options.
    foreach ($name in @('CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','VCToolsInstallDir','VCINSTALLDIR')) {
        if (-not [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($name))) {
            throw "Ambient toolchain variable present: $name"
        }
    }
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installations=@(& $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) { throw 'No installed MSVC inventory.' }
    $tools=@()
    foreach ($installation in ($installations | Sort-Object -Unique)) {
        $parent=Join-Path $installation 'VC/Tools/MSVC'
        foreach ($version in @(Get-ChildItem -LiteralPath $parent -Directory | Sort-Object Name)) {
            $directory=Join-Path $version.FullName 'bin/Hostx64/x64'
            $files=[ordered]@{}
            foreach ($leaf in @('cl.exe','link.exe','lib.exe','c1xx.dll','c2.dll')) {
                $files[$leaf]=Get-Digest (Join-Path $directory $leaf)
            }
            $tools+=[ordered]@{root=$version.FullName.Replace('\','/');files=$files}
            if ($tools.Count -gt 32) { throw 'Tool inventory budget.' }
        }
    }
    $sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include'
    $versions=@(Get-ChildItem -LiteralPath $sdk -Directory | Sort-Object Name | ForEach-Object { $_.Name })
    if ($tools.Count -eq 0 -or $versions.Count -eq 0 -or [string]::IsNullOrEmpty($env:ImageVersion)) {
        throw 'Incomplete host identity.'
    }
    $ambient=([Text.UTF8Encoding]::new($false)).GetBytes([string]$env:PATH)
    $ambientHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($ambient)).ToLowerInvariant()
    return [ordered]@{image=$env:ImageVersion;powershell=$PSVersionTable.PSVersion.ToString()
        tools=$tools;sdk_versions=$versions;ambient_sha256=$ambientHash}
}

function Assert-V9Space([string]$Path,[long]$Minimum) {
    if ([IO.DriveInfo]::new([IO.Path]::GetPathRoot($Path)).AvailableFreeSpace -lt $Minimum) {
        throw 'Free-space admission failed.'
    }
}

function Get-V9CacheProjection([string]$Fixture) {
    $path=Join-Path $Fixture '.mqb/cache/toolchain/msvc-auto-x64-x64.mqbcache'
    $file=Get-Item -LiteralPath $path -Force
    if ($file.Length -gt 1MB -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Invalid V9 cache file.'
    }
    $text=[IO.File]::ReadAllText($path,[Text.UTF8Encoding]::new($false,$true))
    if (-not $text.StartsWith("MQB_TOOLCHAIN_CACHE_V9`n")) { throw 'Expected canonical V9 writer.' }
    $matches=[regex]::Matches($text,'(?m)^vc_tools_root "((?:[^"\\\r\n]|\\[\\"])*)"$')
    if ($matches.Count -ne 1) { throw 'Missing unique tool root.' }
    $toolRoot=[regex]::Replace($matches[0].Groups[1].Value,'\\([\\"])','$1').Replace('\','/')
    # No PATH/environment values or raw cache bytes leave this function.
    return @{schema='MQB_TOOLCHAIN_CACHE_V9';root=$toolRoot;sha256=(Get-Digest $path);bytes=[long]$file.Length}
}

function Assert-V9Call($Row,$Record,$Before,$After) {
    if ($null -ne $Record.error -or $null -eq $Record.exit_code -or $Record.exit_code -ne 0) {
        throw 'Original call failed; no refill.'
    }
    if (($After | Measure-Object -Property size -Sum).Sum -gt 64MB) { throw 'Fixture byte checkpoint exceeded.' }
    $lines=@($Record.output_lines)
    if (@($lines | Where-Object { $_ -match 'mqb\.timings' }).Count) { throw 'Timings were enabled.' }
    $compiles=@($lines | Where-Object { $_.StartsWith('[compile] ') }).Count
    $links=@($lines | Where-Object { $_.StartsWith('[link] ') }).Count
    if ($Row.phase -ceq 'prime') {
        if ($Before.Count -ne 2 -or $compiles -ne 2 -or $links -ne 1) { throw 'Not a fresh prime.' }
    } else {
        $progress=@($lines | Where-Object { $_.StartsWith('[up-to-date] ') })
        if ($compiles -ne 0 -or $links -ne 0 -or $progress.Count -ne 2 -or
            $progress[0] -cne '[up-to-date] 2 translation units' -or
            $progress[1] -cne '[up-to-date] timing_bench.exe') { throw 'Not the fixed no-op.' }
        if (($Before | ConvertTo-Json -Depth 8 -Compress) -cne ($After | ConvertTo-Json -Depth 8 -Compress)) {
            throw 'No-op changed fixture.'
        }
    }
}

function Invoke-V9Calls($Plan,[string]$Evidence,[string]$Inputs,$Environment) {
    $previous=$null; $cacheHash=$null
    foreach ($row in $Plan.protocol.rows) {
        if ($script:attempted -ge 16 -or $row.sequence -ne $script:attempted+1) { throw 'Fixed 16-call/order ceiling.' }
        Assert-V9Space $Evidence 4GB
        $fixture=Join-Path $Evidence ('fixtures/'+$row.fixture)
        if ($row.phase -ceq 'prime') {
            if (Test-Path -LiteralPath $fixture) { throw 'Fresh fixture required.' }
            $null=New-Item -ItemType Directory -Path $fixture
            [IO.File]::WriteAllText((Join-Path $fixture 'helper.cpp'),"int timing_helper() { return 42; }`r`n",[Text.UTF8Encoding]::new($false))
            [IO.File]::WriteAllText((Join-Path $fixture 'main.cpp'),"int timing_helper(); int main() { return timing_helper() == 42 ? 0 : 1; }`r`n",[Text.UTF8Encoding]::new($false))
        }
        $before=Get-FileManifest $fixture
        if ($row.phase -ceq 'noop' -and ($before | ConvertTo-Json -Depth 8 -Compress) -cne
            ($previous | ConvertTo-Json -Depth 8 -Compress)) { throw 'Fixture changed after prime.' }
        $exe=Join-Path $Inputs ('bin/'+$row.side+'.exe')
        $digest=Get-Digest $exe
        if ($digest -cne $Plan.binaries.($row.side)) { throw 'Frozen binary changed.' }
        $prefix=Join-Path $Evidence ('calls/{0:d2}' -f [int]$row.sequence)
        Write-NewJson ($prefix+'.before.json') $before
        Write-NewJson ($prefix+'.started.json') @{row=$row;argv=@($Plan.protocol.argv)
            executable=$exe;cwd=$fixture;executable_sha256=$digest}
        ++$script:attempted
        $record=Invoke-LegacyBoundary $exe $fixture $Plan.protocol.argv $prefix
        $record.row=$row; $record.argv=@($Plan.protocol.argv);$record.executable_sha256=$digest
        $record.dispatch_attempted=$true;$record.clears_hold=$false
        # Preserve raw result BEFORE semantic/manifest/cache checks.
        Write-NewJson ($prefix+'.result.json') $record
        $after=Get-FileManifest $fixture
        Write-NewJson ($prefix+'.after.json') $after
        Assert-V9Call $row $record $before $after
        if ($row.phase -ceq 'prime') {
            $projection=Get-V9CacheProjection $fixture
            Write-NewJson (Join-Path $Evidence ('cache-projections/'+$row.fixture+'.json')) $projection
            if ($projection.root -cnotin @($Environment.tools | ForEach-Object { $_.root })) { throw 'Unexpected selected toolchain.' }
            if ($null -eq $cacheHash) { $cacheHash=$projection.sha256 }
            if ($projection.sha256 -cne $cacheHash) { throw 'Paired cache input differs.' }
        }
        $previous=$after
    }
}

function Invoke-V9Preparation([string]$Repo,[string]$SourceRoot,[string]$Evidence) {
    $before=Get-V9Environment
    Write-NewJson (Join-Path $Evidence 'environment-before.json') $before
    Assert-V9Space $Evidence 8GB
    foreach ($side in @('baseline','candidate')) {
        if (Test-Path -LiteralPath (Join-Path $SourceRoot "$side/.mqb")) { throw 'Source build directory is not fresh.' }
        & git -C (Join-Path $SourceRoot $side) archive -o (Join-Path $Evidence "source-$side.zip") HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Source snapshot failed.' }
    }
    $seedRoot=Join-Path $env:RUNNER_TEMP 'v9-fixed-seed'
    if (Test-Path -LiteralPath $seedRoot) { throw 'Seed directory already exists.' }
    Write-NewJson (Join-Path $Evidence 'seed.started.json') @{phase='seed';mqb_ceiling=1}
    $script:admitted=1
    & (Join-Path $Repo 'tests/native/acquire_seed.ps1') -RepoRoot $Repo -OutputRoot $seedRoot *> (Join-Path $Evidence 'seed.log')
    if ($LASTEXITCODE -ne 0) { throw 'Seed acquisition failed.' }
    $seed=Join-Path $seedRoot 'mqb-seed.exe'
    Write-NewJson (Join-Path $Evidence 'seed-identity.json') @{sha256=(Get-Digest $seed)}
    Write-NewJson (Join-Path $Evidence 'seed.finished.json') @{phase='seed';success=$true}
    foreach ($side in @('baseline','candidate')) {
        Assert-V9Space $Evidence 8GB
        Write-NewJson (Join-Path $Evidence "$side.started.json") @{phase=$side;mqb_ceiling=2}
        $script:admitted+=2
        if ($script:admitted -gt 5) { throw 'Preparation budget.' }
        & (Join-Path $Repo 'tests/native/build_mqb.ps1') -BuilderMqbPath $seed -RepoRoot (Join-Path $SourceRoot $side) `
            -Version '5.6.0' -Configuration Release -OutputPath (Join-Path $Evidence "bin/$side.exe") *> (Join-Path $Evidence "$side-build.log")
        if ($LASTEXITCODE -ne 0) { throw "Matched build failed: $side" }
        Write-NewJson (Join-Path $Evidence "$side.finished.json") @{phase=$side;success=$true}
    }
    $after=Get-V9Environment
    Write-NewJson (Join-Path $Evidence 'environment-after.json') $after
    if (($before | ConvertTo-Json -Depth 12 -Compress) -cne ($after | ConvertTo-Json -Depth 12 -Compress)) {
        throw 'Build host identity changed.'
    }
}

# Top-level entry is never dot-sourced by CI. Tests import only the above functions.
if (-not $ExecuteReviewedPhase -or -not $IsWindows -or -not [Environment]::Is64BitProcess -or
    $PSVersionTable.PSVersion.Major -lt 7) { throw 'Reviewed hosted Windows x64/PS7 phase required.' }
$Root=[IO.Path]::GetFullPath($Root);$InputsRoot=[IO.Path]::GetFullPath($InputsRoot)
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$checker=Join-Path $PSScriptRoot 'v9_noop_validation.py'
& python -B $checker admit --root $Root --repo $repo
if ($LASTEXITCODE -ne 0) { throw 'Request refused before MQB.' }
$head=@(& git -C $repo rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1 -or $head[0] -cne $env:REVIEWED_COMMIT) { throw 'Checkout mismatch.' }
$dirty=@(& git -C $repo status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Tracked harness changed.' }
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'collect_external_noop_boundary.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Pinned definitions do not parse.' }
foreach ($name in @('Write-NewJson','Get-Digest','Get-FileManifest','Invoke-LegacyBoundary')) {
    $defs=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name })
    if ($defs.Count -ne 1) { throw 'Missing unique pinned definition.' }
    . ([scriptblock]::Create($defs[0].Extent.Text))
}
$script:attempted=0;$script:admitted=0;$failure=$null
$pins=[Collections.Generic.List[IO.FileStream]]::new()
try {
    if ($env:PHASE -ceq 'prepare') {
        & python -B $checker check-sources --root $InputsRoot
        if ($LASTEXITCODE -ne 0) { throw 'Source admission failed.' }
        Invoke-V9Preparation $repo $InputsRoot $Root
        & python -B $checker check-sources --root $InputsRoot
        if ($LASTEXITCODE -ne 0) { throw 'Build source changed.' }
    } else {
        & python -B $checker prepare-measure --root $Root --inputs $InputsRoot --repo $repo
        if ($LASTEXITCODE -ne 0) { throw 'Frozen inputs refused.' }
        $plan=Get-Content (Join-Path $Root 'plan.json') -Raw | ConvertFrom-Json
        $manifest=Get-Content (Join-Path $InputsRoot 'manifest.json') -Raw | ConvertFrom-Json
        $before=Get-V9Environment
        Write-NewJson (Join-Path $Root 'environment-before.json') $before
        # Compare public key inventory/image before the first prime. PATH is host-local.
        foreach ($key in @('image','powershell','tools','sdk_versions')) {
            if (($before[$key] | ConvertTo-Json -Depth 12 -Compress) -cne
                ($manifest.environment.$key | ConvertTo-Json -Depth 12 -Compress)) { throw "Unmatched host field: $key" }
        }
        foreach ($side in @('baseline','candidate')) {
            $pins.Add([IO.File]::Open((Join-Path $InputsRoot "bin/$side.exe"),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
        }
        if (-not [Diagnostics.Stopwatch]::IsHighResolution) { throw 'High-resolution QPC required.' }
        # This validates exact QPC conversion without an additional program probe.
        & python -B -c "import sys;sys.path.insert(0,sys.argv[1]);import v9_noop_validation as v;v.milliseconds(1,int(sys.argv[2]))" $PSScriptRoot ([Diagnostics.Stopwatch]::Frequency)
        if ($LASTEXITCODE -ne 0) { throw 'Unsupported exact clock conversion.' }
        Invoke-V9Calls $plan $Root $InputsRoot $before
        $after=Get-V9Environment
        Write-NewJson (Join-Path $Root 'environment-after.json') $after
    }
} catch { $failure=$_.ToString() }
finally {
    foreach ($pin in $pins) { $pin.Dispose() }
    if ($env:PHASE -ceq 'prepare') {
        Write-NewJson (Join-Path $Root 'completion.json') @{status=$(if ($null -eq $failure -and $admitted -eq 5) {'prepared_unmeasured'} else {'stopped'})
            mqb_ceiling_admitted=$admitted;error=$failure;clears_hold=$false}
    } else {
        Write-NewJson (Join-Path $Root 'completion.json') @{status=$(if ($null -eq $failure -and $attempted -eq 16) {'calls_complete_unreviewed'} else {'stopped'})
            attempted=$attempted;error=$failure;clears_hold=$false}
    }
}
if ($null -ne $failure) { throw $failure }
if ($env:PHASE -ceq 'prepare') {
    & python -B $checker freeze --root $Root --repo $repo
} else {
    & python -B $checker audit --root $Root --inputs $InputsRoot --output (Join-Path $Root 'decision.json')
}
if ($LASTEXITCODE -ne 0) { throw 'Invalid evidence or regression HOLD; preserve result, never retry.' }

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BootstrapMqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$ReleaseVersion,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-MqbVersion {
    param(
        [Parameter(Mandatory = $true)][string]$MqbPath,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) {
        throw "$Label executable not found: $MqbPath"
    }

    $help = @(& $MqbPath --help 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $help.Count -eq 0) {
        throw "$Label --help failed with exit code $exitCode"
    }

    $expected = "MQB $ExpectedVersion - MSVC Quick Build (C++ refactor)"
    if ($help[0] -ne $expected) {
        throw "$Label version mismatch. Expected '$expected', got '$($help[0])'"
    }
    Write-Host "$Label: $($help[0])"
}

function Assert-SelfHostSourceManifest {
    param([Parameter(Mandatory = $true)][string]$CppRoot)

    $configPath = Join-Path $CppRoot 'mqb.json'
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json

    $declared = @('apps/mqb/main.cpp') + @($config.discovery.extra_sources)
    $declared = @($declared | ForEach-Object { $_.Replace('\', '/') } | Sort-Object -Unique)

    $sourceRoots = @(
        'apps/mqb',
        'backends/msvc/src',
        'config/src',
        'core/src',
        'discovery/src',
        'json/src',
        'modules/src',
        'orchestration/src',
        'platform/windows/src'
    )

    $actual = @()
    foreach ($relativeRoot in $sourceRoots) {
        $root = Join-Path $CppRoot $relativeRoot
        $actual += Get-ChildItem -LiteralPath $root -File -Filter '*.cpp' | ForEach-Object {
            [System.IO.Path]::GetRelativePath($CppRoot, $_.FullName).Replace('\', '/')
        }
    }
    $actual = @($actual | Sort-Object -Unique)

    $diff = @(Compare-Object -ReferenceObject $actual -DifferenceObject $declared)
    if ($diff.Count -ne 0) {
        $diff | Format-Table | Out-String | Write-Host
        throw 'cpp/mqb.json does not exactly cover the production MQB translation-unit set.'
    }

    Write-Host "Self-host source manifest covers all $($actual.Count) production translation units."
}

function Invoke-SelfBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Builder,
        [Parameter(Mandatory = $true)][string]$CppRoot,
        [Parameter(Mandatory = $true)][string]$VersionDefine,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Push-Location $CppRoot
    try {
        Write-Host "[$Label] builder: $Builder"
        & $Builder 'apps/mqb/main.cpp' '--env' 'vs' '-D' $VersionDefine
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            throw "$Label self-build failed with exit code $exitCode"
        }
    }
    finally {
        Pop-Location
    }

    $result = Join-Path $CppRoot '.mqb/bin/mqb.exe'
    if (-not (Test-Path -LiteralPath $result -PathType Leaf)) {
        throw "$Label did not produce $result"
    }
    return $result
}

$RepoRoot = Get-FullPath $RepoRoot
$BootstrapMqbPath = Get-FullPath $BootstrapMqbPath
$cppRoot = Join-Path $RepoRoot 'cpp'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot 'selfhost-out'
}
$OutputRoot = Get-FullPath $OutputRoot

if (-not (Test-Path -LiteralPath $BootstrapMqbPath -PathType Leaf)) {
    throw "Bootstrap MQB not found: $BootstrapMqbPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $cppRoot 'mqb.json') -PathType Leaf)) {
    throw 'cpp/mqb.json is required for self-hosting.'
}

Assert-SelfHostSourceManifest -CppRoot $cppRoot
Assert-MqbVersion -MqbPath $BootstrapMqbPath -ExpectedVersion $ReleaseVersion -Label 'Stage 0 bootstrap'

if (Test-Path -LiteralPath $OutputRoot) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$mqbState = Join-Path $cppRoot '.mqb'
if (Test-Path -LiteralPath $mqbState) {
    Remove-Item -LiteralPath $mqbState -Recurse -Force
}

# The compiler receives this as /DMQB_VERSION=\"X.Y.Z\".
$versionDefine = 'MQB_VERSION=\"' + $ReleaseVersion + '\"'

# Stage 0 may come from the bootstrap build system. It is never packaged.
$stage1Built = Invoke-SelfBuild `
    -Builder $BootstrapMqbPath `
    -CppRoot $cppRoot `
    -VersionDefine $versionDefine `
    -Label 'Stage 0 -> Stage 1'

$stage1Dir = Join-Path $OutputRoot 'stage1'
New-Item -ItemType Directory -Force -Path $stage1Dir | Out-Null
$stage1 = Join-Path $stage1Dir 'mqb.exe'
Copy-Item -LiteralPath $stage1Built -Destination $stage1 -Force
Assert-MqbVersion -MqbPath $stage1 -ExpectedVersion $ReleaseVersion -Label 'Stage 1 self-hosted release candidate'

$stage1Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $stage1).Hash.ToLowerInvariant()
Write-Host "Stage 1 SHA256: $stage1Hash"

# Prove closure: discard every Stage-0-produced MQB cache/object/output and use the
# self-hosted Stage 1 executable to rebuild the same source/config from a clean state.
if (Test-Path -LiteralPath $mqbState) {
    Remove-Item -LiteralPath $mqbState -Recurse -Force
}

$stage2Built = Invoke-SelfBuild `
    -Builder $stage1 `
    -CppRoot $cppRoot `
    -VersionDefine $versionDefine `
    -Label 'Stage 1 -> Stage 2'

$stage2Dir = Join-Path $OutputRoot 'stage2'
New-Item -ItemType Directory -Force -Path $stage2Dir | Out-Null
$stage2 = Join-Path $stage2Dir 'mqb.exe'
Copy-Item -LiteralPath $stage2Built -Destination $stage2 -Force
Assert-MqbVersion -MqbPath $stage2 -ExpectedVersion $ReleaseVersion -Label 'Stage 2 closure proof'

$stage2Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $stage2).Hash.ToLowerInvariant()
Write-Host "Stage 2 SHA256: $stage2Hash"
Write-Host 'MQB self-host closure passed: the self-hosted release candidate can build MQB again from a clean state.'
exit 0

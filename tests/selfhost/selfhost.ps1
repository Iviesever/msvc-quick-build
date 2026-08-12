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
    Write-Host "${Label}: $($help[0])"
}

$RepoRoot = Get-FullPath $RepoRoot
$BootstrapMqbPath = Get-FullPath $BootstrapMqbPath
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot 'selfhost-out'
}
$OutputRoot = Get-FullPath $OutputRoot

if (-not (Test-Path -LiteralPath $BootstrapMqbPath -PathType Leaf)) {
    throw "Stage 0 MQB not found: $BootstrapMqbPath"
}

$nativeBuild = Join-Path $RepoRoot 'tests/native/build_mqb.ps1'
if (-not (Test-Path -LiteralPath $nativeBuild -PathType Leaf)) {
    throw "MQB-native build driver not found: $nativeBuild"
}

Assert-MqbVersion -MqbPath $BootstrapMqbPath -ExpectedVersion $ReleaseVersion -Label 'Stage 0 MQB-built bootstrap'

if (Test-Path -LiteralPath $OutputRoot) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$stage1 = Join-Path $OutputRoot 'stage1/mqb.exe'
$stage1Result = & $nativeBuild `
    -BuilderMqbPath $BootstrapMqbPath `
    -RepoRoot $RepoRoot `
    -Version $ReleaseVersion `
    -Configuration Release `
    -Clean `
    -OutputPath $stage1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ([System.IO.Path]::GetFullPath($stage1Result) -ne [System.IO.Path]::GetFullPath($stage1)) {
    throw "Stage 1 build returned an unexpected path: $stage1Result"
}
Assert-MqbVersion -MqbPath $stage1 -ExpectedVersion $ReleaseVersion -Label 'Stage 1 self-hosted release candidate'
$stage1Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $stage1).Hash.ToLowerInvariant()
Write-Host "Stage 1 SHA256: $stage1Hash"

# Closure proof: Stage 1 must rebuild the exact same 42-TU MQB project from a
# clean MQB state. No CMake configure/build/test step exists in this chain.
$stage2 = Join-Path $OutputRoot 'stage2/mqb.exe'
$stage2Result = & $nativeBuild `
    -BuilderMqbPath $stage1 `
    -RepoRoot $RepoRoot `
    -Version $ReleaseVersion `
    -Configuration Release `
    -Clean `
    -OutputPath $stage2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ([System.IO.Path]::GetFullPath($stage2Result) -ne [System.IO.Path]::GetFullPath($stage2)) {
    throw "Stage 2 build returned an unexpected path: $stage2Result"
}
Assert-MqbVersion -MqbPath $stage2 -ExpectedVersion $ReleaseVersion -Label 'Stage 2 closure proof'
$stage2Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $stage2).Hash.ToLowerInvariant()
Write-Host "Stage 2 SHA256: $stage2Hash"

Write-Host 'MQB-native self-host closure passed: Stage 0 -> Stage 1 -> Stage 2 used MQB for every build generation.'
exit 0

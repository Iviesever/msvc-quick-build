[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuilderMqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$Version,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$OutputPath,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

$RepoRoot = Get-FullPath $RepoRoot
$BuilderMqbPath = Get-FullPath $BuilderMqbPath
$cppRoot = Join-Path $RepoRoot 'cpp'
$configPath = Join-Path $cppRoot 'mqb.json'

if (-not (Test-Path -LiteralPath $BuilderMqbPath -PathType Leaf)) {
    throw "Builder MQB not found: $BuilderMqbPath"
}
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "MQB self-build config not found: $configPath"
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$sources = @('apps/mqb/main.cpp') + @($config.discovery.extra_sources)
$sources = @($sources | ForEach-Object { $_.Replace('\', '/') } | Sort-Object -Unique)
if ($sources.Count -ne 42) {
    throw "MQB self-build manifest must contain exactly 42 production translation units; found $($sources.Count)."
}

foreach ($source in $sources) {
    if (-not (Test-Path -LiteralPath (Join-Path $cppRoot $source) -PathType Leaf)) {
        throw "MQB self-build source is missing: $source"
    }
}

$mqbState = Join-Path $cppRoot '.mqb'
if ($Clean -and (Test-Path -LiteralPath $mqbState)) {
    Remove-Item -LiteralPath $mqbState -Recurse -Force
}

$quote = [char]34
$versionDefine = 'MQB_VERSION=' + $quote + $Version + $quote
$configArg = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
$runtime = if ($Configuration -eq 'Debug') { 'MTd' } else { 'MT' }

$arguments = @()
$arguments += $sources
$arguments += @(
    '--env', 'vs',
    $configArg,
    '--runtime', $runtime,
    '-D', $versionDefine,
    '-o', 'mqb'
)

Push-Location $cppRoot
try {
    Write-Host "Building MQB with MQB: $BuilderMqbPath"
    Write-Host "Configuration: $Configuration"
    Write-Host "Version: $Version"
    Write-Host "Production translation units: $($sources.Count)"

    $buildOutput = @(& $BuilderMqbPath @arguments 2>&1)
    $exitCode = $LASTEXITCODE
    foreach ($line in $buildOutput) { Write-Host $line }
    if ($exitCode -ne 0) {
        exit $exitCode
    }
}
finally {
    Pop-Location
}

$built = Join-Path $cppRoot '.mqb/bin/mqb.exe'
if (-not (Test-Path -LiteralPath $built -PathType Leaf)) {
    throw "MQB-native build did not produce: $built"
}

$help = @(& $built --help 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0 -or $help.Count -eq 0) {
    throw 'MQB-native build produced an executable that cannot run --help.'
}
$expected = "MQB $Version - MSVC Quick Build (C++ refactor)"
if ($help[0] -ne $expected) {
    throw "Built MQB identity mismatch. Expected '$expected', got '$($help[0])'"
}

$result = $built
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Get-FullPath $OutputPath
    $parent = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $built -Destination $OutputPath -Force
    $result = $OutputPath
}

Write-Host "MQB-native build passed: $($help[0])"
Write-Output $result

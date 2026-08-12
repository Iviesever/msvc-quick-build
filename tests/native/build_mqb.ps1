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

# cpp/mqb.json remains the source of truth for the production manifest and
# native build policy. The historical seed is intentionally invoked from the
# repository root, where no mqb.json is auto-loaded, so an old seed never has
# to understand a newer project-config schema.
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$relativeSources = @('src/app/main.cpp') + @($config.discovery.extra_sources)
$relativeSources = @($relativeSources | ForEach-Object { $_.Replace('\', '/') } | Sort-Object -Unique)

# Keep the manifest exact without coupling architecture work to a magic TU
# count. Every production .cpp under cpp/src must appear exactly once in the
# normalized manifest, and the manifest must not name anything outside that
# production source set.
$productionSources = @(
    Get-ChildItem -LiteralPath (Join-Path $cppRoot 'src') -Recurse -File -Filter '*.cpp' |
        ForEach-Object {
            [System.IO.Path]::GetRelativePath($cppRoot, $_.FullName).Replace('\', '/')
        } |
        Sort-Object -Unique
)
$manifestDiff = @(Compare-Object -ReferenceObject $productionSources -DifferenceObject $relativeSources)
if ($manifestDiff.Count -ne 0) {
    $details = @(
        $manifestDiff | ForEach-Object {
            $kind = if ($_.SideIndicator -eq '<=') { 'missing from manifest' } else { 'not a production source' }
            "  $kind: $($_.InputObject)"
        }
    ) -join [Environment]::NewLine
    throw "MQB self-build manifest does not exactly match cpp/src production translation units:`n$details"
}

foreach ($source in $relativeSources) {
    if (-not (Test-Path -LiteralPath (Join-Path $cppRoot $source) -PathType Leaf)) {
        throw "MQB self-build source is missing: $source"
    }
}

$includeDirs = @($config.build.include_dirs)
if ($includeDirs.Count -eq 0) { throw 'MQB self-build config has no include_dirs.' }
foreach ($include in $includeDirs) {
    if (-not (Test-Path -LiteralPath (Join-Path $cppRoot $include) -PathType Container)) {
        throw "MQB self-build include directory is missing: $include"
    }
}

$stateRoot = Join-Path $RepoRoot '.mqb'
if ($Clean -and (Test-Path -LiteralPath $stateRoot)) {
    Remove-Item -LiteralPath $stateRoot -Recurse -Force
}

$quote = [char]34
$versionDefine = 'MQB_VERSION=' + $quote + $Version + $quote
$configArg = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
$runtime = if ($Configuration -eq 'Debug') { 'MTd' } else { 'MT' }
$standard = [string]$config.build.standard
$subsystem = [string]$config.build.subsystem

$arguments = @()
$arguments += @($relativeSources | ForEach-Object { 'cpp/' + $_ })
$arguments += @('--env', 'vs', $configArg, '--std', $standard, '--runtime', $runtime)
if (-not [string]::IsNullOrWhiteSpace($subsystem)) {
    $arguments += @('--subsystem', $subsystem)
}
foreach ($include in $includeDirs) {
    $arguments += @('-I', ('cpp/' + ([string]$include).Replace('\', '/')))
}
foreach ($compilerArg in @($config.build.compiler_args)) {
    $arguments += @('--compiler-arg', [string]$compilerArg)
}
$arguments += @('-D', $versionDefine, '-o', 'mqb')

Push-Location $RepoRoot
try {
    Write-Host "Building MQB with MQB: $BuilderMqbPath"
    Write-Host "Configuration: $Configuration"
    Write-Host "Version: $Version"
    Write-Host "Verified project manifest: $($relativeSources.Count) production translation units"
    Write-Host 'Invocation deliberately bypasses automatic config loading for seed-schema independence.'

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

$built = Join-Path $RepoRoot '.mqb/bin/mqb.exe'
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

Write-Output $result

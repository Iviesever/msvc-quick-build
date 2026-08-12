[CmdletBinding()]
param(
    [string]$SeedMqbPath,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$Version = '5.0.0-dev',
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($SeedMqbPath)) {
    $command = Get-Command mqb -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw 'No seed MQB was supplied and `mqb` was not found on PATH. Install a working MQB or pass -SeedMqbPath.'
    }
    $SeedMqbPath = $command.Source
}
$SeedMqbPath = [System.IO.Path]::GetFullPath($SeedMqbPath)

$outDir = Join-Path $RepoRoot "native-dev/$($Configuration.ToLowerInvariant())"
$currentMqb = Join-Path $outDir 'mqb.exe'

& (Join-Path $PSScriptRoot 'build_mqb.ps1') `
    -BuilderMqbPath $SeedMqbPath `
    -RepoRoot $RepoRoot `
    -Version $Version `
    -Configuration $Configuration `
    -OutputPath $currentMqb
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $PSScriptRoot 'run_native_tests.ps1') `
    -BuilderMqbPath $currentMqb `
    -TestMqbPath $currentMqb `
    -RepoRoot $RepoRoot `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "MQB-native development cycle passed: $currentMqb"
exit 0

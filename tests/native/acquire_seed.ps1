[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)

$seedTag = 'v5.0.0-rc.2'
$seedAsset = 'vscode-msvc-quick-build-v5.0.0-rc.2-windows-x64.zip'
$seedSha256 = 'a094261e2c1cc2fb90e6e8c8ed2933ffa88a0ccbd376dfd7c6d16c405d8d6eeb'
$repository = if ([string]::IsNullOrWhiteSpace($env:GITHUB_REPOSITORY)) { 'Iviesever/msvc-quick-build' } else { $env:GITHUB_REPOSITORY }

$gh = Get-Command gh -ErrorAction SilentlyContinue
if ($null -eq $gh) {
    throw 'gh is required to acquire the pinned historical MQB seed in CI.'
}

if (Test-Path -LiteralPath $OutputRoot) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$downloadDir = Join-Path $OutputRoot 'download'
$extractDir = Join-Path $OutputRoot 'extracted'
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

Write-Host "Acquiring pinned MQB seed: $seedTag / $seedAsset"
$downloadOutput = @(& gh release download $seedTag `
    --repo $repository `
    --pattern $seedAsset `
    --dir $downloadDir 2>&1)
$exitCode = $LASTEXITCODE
foreach ($line in $downloadOutput) { Write-Host $line }
if ($exitCode -ne 0) {
    exit $exitCode
}

$zip = Join-Path $downloadDir $seedAsset
if (-not (Test-Path -LiteralPath $zip -PathType Leaf)) {
    throw "Seed archive not found after download: $zip"
}

$actualSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
if ($actualSha -ne $seedSha256) {
    throw "Pinned seed SHA-256 mismatch. Expected $seedSha256, got $actualSha"
}

Expand-Archive -LiteralPath $zip -DestinationPath $extractDir
$seedMqb = Join-Path $extractDir 'mqb.exe'
if (-not (Test-Path -LiteralPath $seedMqb -PathType Leaf)) {
    throw "Pinned seed archive does not contain mqb.exe at its root: $seedMqb"
}

$help = @(& $seedMqb --help 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0 -or $help.Count -eq 0) {
    throw 'Pinned MQB seed failed to execute.'
}
if ($help[0] -ne 'MQB 5.0.0-rc.2 - MSVC Quick Build (C++ refactor)') {
    throw "Unexpected pinned seed identity: '$($help[0])'"
}

$stablePath = Join-Path $OutputRoot 'mqb-seed.exe'
Copy-Item -LiteralPath $seedMqb -Destination $stablePath -Force
Write-Host "Pinned seed verified: $($help[0])"
Write-Output $stablePath

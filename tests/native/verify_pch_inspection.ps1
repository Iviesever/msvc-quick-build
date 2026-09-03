[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuilderMqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$BuilderMqbPath = [System.IO.Path]::GetFullPath($BuilderMqbPath)
if (-not (Test-Path -LiteralPath $BuilderMqbPath -PathType Leaf)) {
    throw "Builder MQB not found: $BuilderMqbPath"
}

$cppRoot = Join-Path $RepoRoot 'cpp'
$configPath = Join-Path $cppRoot 'mqb.json'
$contractSource = Join-Path $RepoRoot 'tests/native/pch_inspection_contract.cpp'
foreach ($path in @($configPath, $contractSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PCH inspection evidence input missing: $path"
    }
}

& (Join-Path $RepoRoot 'tests/native/assert_cpp_layout.ps1') -CppRoot $cppRoot

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$productionSources = @($config.discovery.extra_sources | ForEach-Object {
    'cpp/' + ([string]$_).Replace('\', '/')
})
if ($productionSources.Count -eq 0) {
    throw 'cpp/mqb.json has no production sources for PCH inspection evidence.'
}

$arguments = [System.Collections.ArrayList]::new()
[void]$arguments.Add('tests/native/pch_inspection_contract.cpp')
foreach ($source in $productionSources) {
    [void]$arguments.Add($source)
}
[void]$arguments.Add('--no-discover')
[void]$arguments.Add('--env')
[void]$arguments.Add('vs')
[void]$arguments.Add('--debug')
[void]$arguments.Add('--std')
[void]$arguments.Add([string]$config.build.standard)
[void]$arguments.Add('--runtime')
[void]$arguments.Add('MTd')
foreach ($include in @($config.build.include_dirs)) {
    [void]$arguments.Add('-I')
    [void]$arguments.Add('cpp/' + ([string]$include).Replace('\', '/'))
}
foreach ($compilerArg in @($config.build.compiler_args)) {
    [void]$arguments.Add('--compiler-arg')
    [void]$arguments.Add([string]$compilerArg)
}
[void]$arguments.Add('--lib')
[void]$arguments.Add('shell32.lib')
[void]$arguments.Add('-o')
[void]$arguments.Add('pch_inspection_contract')

$stateRoot = Join-Path $RepoRoot '.mqb'
if (Test-Path -LiteralPath $stateRoot) {
    Remove-Item -LiteralPath $stateRoot -Recurse -Force
}

Push-Location $RepoRoot
try {
    $buildOutput = @(& $BuilderMqbPath @arguments 2>&1)
    $buildExit = $LASTEXITCODE
    foreach ($line in $buildOutput) { Write-Host $line }
    if ($buildExit -ne 0) {
        throw "Failed to build PCH inspection contract (exit $buildExit)."
    }

    $contractExe = Join-Path $RepoRoot '.mqb/bin/pch_inspection_contract.exe'
    if (-not (Test-Path -LiteralPath $contractExe -PathType Leaf)) {
        throw "PCH inspection contract executable missing: $contractExe"
    }

    $testOutput = @(& $contractExe 2>&1)
    $testExit = $LASTEXITCODE
    foreach ($line in $testOutput) { Write-Host $line }
    if ($testExit -ne 0) {
        throw "PCH inspection contract failed (exit $testExit)."
    }
}
finally {
    Pop-Location
}

Write-Host 'PCH inspection evidence passed.'

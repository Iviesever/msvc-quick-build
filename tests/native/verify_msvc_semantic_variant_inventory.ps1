[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [Parameter(Mandatory = $true)][string]$SharedProductLibraryPath,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Full([string]$Path) { [System.IO.Path]::GetFullPath($Path) }
$MqbPath = Full $MqbPath
$RepoRoot = Full $RepoRoot
$SharedProductLibraryPath = Full $SharedProductLibraryPath
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $RepoRoot 'native-out/msvc-semantic-variant-inventory.tsv'
} else {
    $OutputPath = Full $OutputPath
}

foreach ($path in @($MqbPath, $SharedProductLibraryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required semantic inventory input not found: $path"
    }
}

$sourcePath = Join-Path $RepoRoot 'tests/native/msvc_semantic_variant_inventory.cpp'
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Semantic inventory source not found: $sourcePath"
}

$config = Get-Content -LiteralPath (Join-Path $RepoRoot 'cpp/mqb.json') -Raw | ConvertFrom-Json
$args = [System.Collections.ArrayList]::new()
[void]$args.Add([System.IO.Path]::GetRelativePath($RepoRoot, $sourcePath).Replace('\','/'))
[void]$args.Add('--env'); [void]$args.Add('vs')
[void]$args.Add($(if ($Configuration -eq 'Debug') {'--debug'} else {'--release'}))
[void]$args.Add('--std'); [void]$args.Add([string]$config.build.standard)
[void]$args.Add('--runtime'); [void]$args.Add($(if ($Configuration -eq 'Debug') {'MTd'} else {'MT'}))
foreach ($include in @($config.build.include_dirs)) {
    [void]$args.Add('-I')
    [void]$args.Add('cpp/' + ([string]$include).Replace('\','/'))
}
foreach ($compilerArg in @($config.build.compiler_args)) {
    [void]$args.Add('--compiler-arg')
    [void]$args.Add([string]$compilerArg)
}
[void]$args.Add('-D'); [void]$args.Add('MQB_VERSION="native-tests"')
[void]$args.Add('--no-discover')
[void]$args.Add('--lib'); [void]$args.Add($SharedProductLibraryPath)
[void]$args.Add('--linker-arg'); [void]$args.Add("/WHOLEARCHIVE:$SharedProductLibraryPath")
[void]$args.Add('--lib'); [void]$args.Add('shell32.lib')
[void]$args.Add('-o'); [void]$args.Add('native_msvc_semantic_variant_inventory')

Push-Location $RepoRoot
try {
    $build = @(& $MqbPath @args 2>&1)
    $buildExit = $LASTEXITCODE
    foreach ($line in $build) { Write-Host $line }
} finally {
    Pop-Location
}
if ($buildExit -ne 0) {
    throw "Failed to build MSVC semantic variant inventory probe (exit $buildExit)"
}

$exe = Join-Path $RepoRoot '.mqb/bin/native_msvc_semantic_variant_inventory.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Semantic inventory probe executable missing: $exe"
}

$work = Join-Path $RepoRoot 'native-test-work/msvc-semantic-variant-inventory'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$stdoutPath = Join-Path $work 'stdout.tsv'
$stderrPath = Join-Path $work 'stderr.txt'
$process = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
if (Test-Path -LiteralPath $stderrPath -PathType Leaf) {
    foreach ($line in @(Get-Content -LiteralPath $stderrPath)) { Write-Host $line }
}
if ($process.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) {
        foreach ($line in @(Get-Content -LiteralPath $stdoutPath)) { Write-Host $line }
    }
    throw "MSVC semantic variant inventory probe failed (exit $($process.ExitCode))"
}

$matrix = @(Get-Content -LiteralPath $stdoutPath)
if ($matrix.Count -lt 2 -or $matrix[0] -ne "tool`tfamily`tvariant`trisk`texpected_ownership`tactual_ownership`tbehavioral_gate`trationale") {
    throw 'Semantic inventory probe did not emit the expected TSV schema'
}

$parent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$matrix | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "MSVC semantic variant inventory: $($matrix.Count - 1) concrete high-risk variants"
Write-Host "Coverage matrix: $OutputPath"

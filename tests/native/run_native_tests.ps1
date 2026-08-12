[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuilderMqbPath,
    [Parameter(Mandatory = $true)][string]$TestMqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

$RepoRoot = Get-FullPath $RepoRoot
$BuilderMqbPath = Get-FullPath $BuilderMqbPath
$TestMqbPath = Get-FullPath $TestMqbPath
$cppRoot = Join-Path $RepoRoot 'cpp'
$configPath = Join-Path $cppRoot 'mqb.json'

foreach ($path in @($BuilderMqbPath, $TestMqbPath, $configPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required native-test input not found: $path"
    }
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$productionSources = @($config.discovery.extra_sources | ForEach-Object { $_.Replace('\', '/') } | Sort-Object -Unique)
if ($productionSources.Count -ne 41) {
    throw "Native test runner requires exactly 41 non-main production translation units; found $($productionSources.Count)."
}

$testFiles = @(Get-ChildItem -LiteralPath $cppRoot -Recurse -File -Filter '*_tests.cpp' |
    Where-Object { $_.FullName -notmatch '[\\/]\.mqb[\\/]' } |
    Sort-Object FullName)
if ($testFiles.Count -ne 67) {
    throw "Native test manifest drift: expected 67 *_tests.cpp files, found $($testFiles.Count)."
}

$quote = [char]34
$versionDefine = 'MQB_VERSION=' + $quote + 'native-tests' + $quote
$configArg = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
$runtime = if ($Configuration -eq 'Debug') { 'MTd' } else { 'MT' }

function Get-TestOutputName {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $stem = [System.IO.Path]::ChangeExtension($RelativePath, $null)
    return ($stem -replace '[^0-9A-Za-z_]+', '_').Trim('_')
}

function Invoke-MqbTestBuild {
    param(
        [Parameter(Mandatory = $true)][string]$EntrySource,
        [Parameter(Mandatory = $true)][string]$OutputName
    )

    $sources = @($EntrySource) + $productionSources
    $arguments = @()
    $arguments += $sources
    $arguments += @(
        '--env', 'vs',
        $configArg,
        '--runtime', $runtime,
        '-D', $versionDefine,
        '--linker-arg', 'shell32.lib',
        '-o', $OutputName
    )

    Push-Location $cppRoot
    try {
        $buildOutput = @(& $BuilderMqbPath @arguments 2>&1)
        $exitCode = $LASTEXITCODE
        foreach ($line in $buildOutput) { Write-Host $line }
        if ($exitCode -ne 0) {
            throw "MQB failed to build native test '$EntrySource' with exit code $exitCode"
        }
    }
    finally {
        Pop-Location
    }

    $exe = Join-Path $cppRoot ".mqb/bin/$OutputName.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Native test build did not produce: $exe"
    }
    return $exe
}

Write-Host "MQB-native test graph: $($testFiles.Count) tests / configuration $Configuration"
Write-Host "Builder MQB: $BuilderMqbPath"
Write-Host "Tested MQB:  $TestMqbPath"

$helperSource = 'tests/process_echo_helper.cpp'
$helperExe = Invoke-MqbTestBuild -EntrySource $helperSource -OutputName 'native_test_process_echo_helper'

$workRoot = Join-Path $RepoRoot "native-test-work/$($Configuration.ToLowerInvariant())"
if (Test-Path -LiteralPath $workRoot) {
    Remove-Item -LiteralPath $workRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null

$passed = 0
$failures = @()
foreach ($testFile in $testFiles) {
    $relative = [System.IO.Path]::GetRelativePath($cppRoot, $testFile.FullName).Replace('\', '/')
    $outputName = Get-TestOutputName -RelativePath $relative
    Write-Host ""
    Write-Host "=== [$($passed + $failures.Count + 1)/$($testFiles.Count)] $relative ==="

    $exe = Invoke-MqbTestBuild -EntrySource $relative -OutputName $outputName
    $testArgs = @()
    if ($testFile.Name -match '^mqb_.*_e2e_tests\.cpp$') {
        $testArgs += $TestMqbPath
    }
    elseif ($testFile.Name -eq 'windows_process_runner_tests.cpp') {
        $testArgs += $helperExe
    }

    $testWork = Join-Path $workRoot $outputName
    New-Item -ItemType Directory -Force -Path $testWork | Out-Null

    Push-Location $testWork
    try {
        $testOutput = @(& $exe @testArgs 2>&1)
        $exitCode = $LASTEXITCODE
        foreach ($line in $testOutput) { Write-Host $line }
    }
    finally {
        Pop-Location
    }

    if ($exitCode -eq 0) {
        $passed++
        Write-Host "PASS: $relative"
    }
    else {
        $failures += [PSCustomObject]@{
            Test = $relative
            ExitCode = $exitCode
        }
        Write-Host "FAIL: $relative (exit $exitCode)"
    }
}

Write-Host ""
Write-Host "MQB-native tests: $passed/$($testFiles.Count) passed."
if ($failures.Count -ne 0) {
    $failures | Format-Table -AutoSize | Out-String | Write-Host
    exit 1
}

Write-Host 'All MQB-native tests passed without CMake or CTest.'
exit 0

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

# Native tests provide their own entry-point main, so their shared product
# graph is exactly cpp/src/**/*.cpp except src/app/main.cpp. Validate the
# manifest by source-set identity instead of coupling refactors to a TU count.
$expectedProductionSources = @(
    Get-ChildItem -LiteralPath (Join-Path $cppRoot 'src') -Recurse -File -Filter '*.cpp' |
        ForEach-Object {
            [System.IO.Path]::GetRelativePath($cppRoot, $_.FullName).Replace('\', '/')
        } |
        Where-Object { $_ -ne 'src/app/main.cpp' } |
        Sort-Object -Unique
)
$manifestDiff = @(Compare-Object -ReferenceObject $expectedProductionSources -DifferenceObject $productionSources)
if ($manifestDiff.Count -ne 0) {
    $details = @(
        $manifestDiff | ForEach-Object {
            $kind = if ($_.SideIndicator -eq '<=') { 'missing from manifest' } else { 'not a shared production source' }
            "  ${kind}: $($_.InputObject)"
        }
    ) -join [Environment]::NewLine
    throw "Native test production manifest does not exactly match cpp/src excluding app main:`n$details"
}
foreach ($source in $productionSources) {
    if (-not (Test-Path -LiteralPath (Join-Path $cppRoot $source) -PathType Leaf)) {
        throw "Native test production source is missing: $source"
    }
}

$includeDirs = @($config.build.include_dirs)
if ($includeDirs.Count -eq 0) { throw 'Native test config has no include_dirs.' }

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
$standard = [string]$config.build.standard

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

    # Invoke from repository root, where no mqb.json is auto-loaded. The
    # current cpp/mqb.json is still the source of truth for the shared
    # production manifest and policy, but old/new builders see only native CLI.
    $arguments = @('cpp/' + $EntrySource)
    $arguments += @($productionSources | ForEach-Object { 'cpp/' + $_ })
    $arguments += @('--env', 'vs', $configArg, '--std', $standard, '--runtime', $runtime)
    foreach ($include in $includeDirs) {
        $arguments += @('-I', ('cpp/' + ([string]$include).Replace('\', '/')))
    }
    foreach ($compilerArg in @($config.build.compiler_args)) {
        $arguments += @('--compiler-arg', [string]$compilerArg)
    }
    $arguments += @(
        '-D', $versionDefine,
        '--linker-arg', 'shell32.lib',
        '-o', $OutputName
    )

    Push-Location $RepoRoot
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

    $exe = Join-Path $RepoRoot ".mqb/bin/$OutputName.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Native test build did not produce: $exe"
    }
    return $exe
}

Write-Host "MQB-native test graph: $($testFiles.Count) tests / configuration $Configuration"
Write-Host "Verified shared production manifest: $($productionSources.Count) non-main translation units"
Write-Host "Builder MQB: $BuilderMqbPath"
Write-Host "Tested MQB:  $TestMqbPath"

$helperSource = 'tests/process/process_echo_helper.cpp'
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

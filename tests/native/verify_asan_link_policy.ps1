[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) {
    throw "MQB executable not found: $MqbPath"
}

$fixture = Join-Path $RepoRoot 'native-test-work/asan-link-policy'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8 -Value @(
    '#include <cstdlib>',
    'int main() {',
    '    int* value = new int[1];',
    '    value[0] = 42;',
    '    const int result = value[0] == 42 ? 0 : 1;',
    '    delete[] value;',
    '    return result;',
    '}'
)

function Invoke-AsanBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Target,
        [switch]$RawIncremental
    )

    Push-Location $fixture
    try {
        $arguments = @(
            'build', 'main.cpp', '--debug', '--no-discover', '-o', $Target,
            '/fsanitize=address'
        )
        if ($RawIncremental) {
            $arguments += @('/link', '/INCREMENTAL')
        }
        $output = @(& $MqbPath @arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    [PSCustomObject]@{
        ExitCode = $exitCode
        Text = ($output -join [Environment]::NewLine)
    }
}

function Invoke-WithMinimalParentPath {
    param([Parameter(Mandatory = $true)][scriptblock]$Action)

    $previousPath = $env:PATH
    $env:PATH = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    try {
        & $Action
    }
    finally {
        $env:PATH = $previousPath
    }
}

$cold = Invoke-AsanBuild -Target 'asan_probe'
if ($cold.ExitCode -ne 0) {
    throw "Cold AddressSanitizer build failed:`n$($cold.Text)"
}
$exe = Join-Path $fixture '.mqb/bin/asan_probe.exe'
$ilk = Join-Path $fixture '.mqb/bin/asan_probe.ilk'
$linkCache = Join-Path $fixture '.mqb/cache/link/asan_probe.linkcache'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "AddressSanitizer build did not produce expected executable: $exe"
}
if (Test-Path -LiteralPath $ilk) {
    throw "AddressSanitizer Debug link produced an .ilk even though incremental linking is unsupported: $ilk"
}
if (-not (Test-Path -LiteralPath $linkCache -PathType Leaf)) {
    throw "AddressSanitizer link cache missing: $linkCache"
}

$cacheText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($linkCache))
foreach ($library in @(
    'clang_rt.asan_dynamic-x86_64.lib',
    'clang_rt.asan_dynamic_runtime_thunk-x86_64.lib'
)) {
    if ($cacheText -notmatch [Regex]::Escape($library)) {
        throw "AddressSanitizer link cache did not seal inferred runtime input '$library'"
    }
}

$warm = Invoke-AsanBuild -Target 'asan_probe'
if ($warm.ExitCode -ne 0) {
    throw "Warm AddressSanitizer build failed:`n$($warm.Text)"
}
if ($warm.Text -notmatch '\[up-to-date\]\s+asan_probe\.exe') {
    throw "Warm AddressSanitizer build did not reuse sealed link inputs:`n$($warm.Text)"
}

$rawIncremental = Invoke-AsanBuild -Target 'asan_raw_incremental' -RawIncremental
if ($rawIncremental.ExitCode -ne 0) {
    throw "AddressSanitizer build with raw /INCREMENTAL failed:`n$($rawIncremental.Text)"
}
$rawIncrementalIlk = Join-Path $fixture '.mqb/bin/asan_raw_incremental.ilk'
if (Test-Path -LiteralPath $rawIncrementalIlk) {
    throw "Raw /INCREMENTAL overrode MQB's required AddressSanitizer /INCREMENTAL:NO policy"
}

# Prove `mqb run` does not depend on the invoking shell already containing the
# selected compiler directory. LINK/CL use MQB's captured toolchain environment;
# the user program receives only the selected toolchain PATH when ASan is active.
Push-Location $fixture
try {
    $runOutput = @(
        Invoke-WithMinimalParentPath {
            & $MqbPath run main.cpp --debug --no-discover -o asan_run_probe `
                /fsanitize=address 2>&1
        }
    )
    $runExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($runExit -ne 0) {
    throw "AddressSanitizer mqb run failed with compiler paths removed from parent PATH:`n$($runOutput -join [Environment]::NewLine)"
}
if (($runOutput -join [Environment]::NewLine) -notmatch '\[run\]\s+asan_run_probe\.exe') {
    throw "AddressSanitizer ordinary run gate did not reach the user program"
}

# Exercise the same compile->link->run policy through the named-module route.
Set-Content -LiteralPath (Join-Path $fixture 'math.ixx') -Encoding utf8 -Value @(
    'export module math;',
    'export int answer() { return 42; }'
)
Set-Content -LiteralPath (Join-Path $fixture 'module_main.cpp') -Encoding utf8 -Value @(
    'import math;',
    'int main() { return answer() == 42 ? 0 : 1; }'
)
Push-Location $fixture
try {
    $moduleRunOutput = @(
        Invoke-WithMinimalParentPath {
            & $MqbPath run module_main.cpp math.ixx --debug --no-discover `
                -o asan_module_run /fsanitize=address 2>&1
        }
    )
    $moduleRunExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($moduleRunExit -ne 0) {
    throw "AddressSanitizer module mqb run failed with compiler paths removed from parent PATH:`n$($moduleRunOutput -join [Environment]::NewLine)"
}
if (($moduleRunOutput -join [Environment]::NewLine) -notmatch '\[run\]\s+asan_module_run\.exe') {
    throw "AddressSanitizer module run gate did not reach the user program"
}

# The advanced opt-out spelling must remain a recognized native LINK option.
# This non-sanitized target has no ASan unresolveds, so the check isolates
# parameter routing without requiring manual sanitizer library declarations.
Push-Location $fixture
try {
    $optOut = @(
        & $MqbPath build main.cpp --debug --no-discover -o inferasanlibs_no `
            /link /INFERASANLIBS:NO 2>&1
    )
    $optOutExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($optOutExit -ne 0) {
    throw "/INFERASANLIBS:NO was not accepted as a native linker override:`n$($optOut -join [Environment]::NewLine)"
}

Write-Host 'Real MSVC AddressSanitizer link freshness, non-incremental LINK, and mqb run runtime checks passed.'
exit 0

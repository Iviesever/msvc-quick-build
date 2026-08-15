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

Write-Host 'Real MSVC AddressSanitizer inferred-library freshness and non-incremental link checks passed.'
exit 0

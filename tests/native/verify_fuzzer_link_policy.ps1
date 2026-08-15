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

$fixture = Join-Path $RepoRoot 'native-test-work/fuzzer-link-policy'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Path $fixture -Force | Out-Null

Set-Content -LiteralPath (Join-Path $fixture 'fuzzer.cpp') -Encoding utf8 -Value @(
    '#include <cstddef>',
    '#include <cstdint>',
    'extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {',
    '    return (size != 0 && data[0] == 0xff) ? 0 : 0;',
    '}'
)

Set-Content -LiteralPath (Join-Path $fixture 'fuzzer_with_main.cpp') -Encoding utf8 -Value @(
    '#include <cstddef>',
    '#include <cstdint>',
    'extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }',
    'int main() {',
    '    const std::uint8_t value = 0;',
    '    return LLVMFuzzerTestOneInput(&value, 1);',
    '}'
)

function Invoke-FuzzerBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Target,
        [string]$Runtime,
        [string[]]$Libraries = @(),
        [string[]]$LinkerArguments = @()
    )

    $arguments = @('build', $Source, '--debug', '--no-discover', '-o', $Target)
    if (-not [string]::IsNullOrWhiteSpace($Runtime)) {
        $arguments += @('--runtime', $Runtime)
    }
    foreach ($library in $Libraries) {
        $arguments += @('--lib', $library)
    }
    $arguments += '/fsanitize=fuzzer'
    if ($LinkerArguments.Count -ne 0) {
        $arguments += '/link'
        $arguments += $LinkerArguments
    }

    Push-Location $fixture
    try {
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

function Get-LinkCacheText {
    param([Parameter(Mandatory = $true)][string]$Target)

    $cache = Join-Path $fixture ".mqb/cache/link/$Target.linkcache"
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        throw "LibFuzzer link cache missing: $cache"
    }
    return [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($cache))
}

$dynamicDebugLibrary = 'clang_rt.fuzzer_MDd-x86_64.lib'
$staticReleaseLibrary = 'clang_rt.fuzzer_MT-x86_64.lib'
$noMainDebugLibrary = 'clang_rt.fuzzer_no_main_MDd-x86_64.lib'

# Default Debug CRT is /MDd. The source deliberately has no main(): a successful
# link proves cl.exe's hidden LibFuzzer default-library directive supplied the
# runtime entry point, while the cache assertion proves MQB sealed that input.
$cold = Invoke-FuzzerBuild -Source 'fuzzer.cpp' -Target 'fuzzer_probe'
if ($cold.ExitCode -ne 0) {
    throw "Cold LibFuzzer build failed:`n$($cold.Text)"
}
$exe = Join-Path $fixture '.mqb/bin/fuzzer_probe.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "LibFuzzer build did not produce expected executable: $exe"
}
$cacheText = Get-LinkCacheText -Target 'fuzzer_probe'
if ($cacheText -notmatch [Regex]::Escape($dynamicDebugLibrary)) {
    throw "LibFuzzer link cache did not seal inferred runtime input '$dynamicDebugLibrary'"
}

$warm = Invoke-FuzzerBuild -Source 'fuzzer.cpp' -Target 'fuzzer_probe'
if ($warm.ExitCode -ne 0) {
    throw "Warm LibFuzzer build failed:`n$($warm.Text)"
}
if ($warm.Text -notmatch '\[up-to-date\]\s+fuzzer_probe\.exe') {
    throw "Warm LibFuzzer build did not reuse sealed runtime input:`n$($warm.Text)"
}

# Explicit CRT selection must choose the matching hidden runtime instead of
# treating /fsanitize=fuzzer as a link policy independent of RuntimeLibrary.
$static = Invoke-FuzzerBuild -Source 'fuzzer.cpp' -Target 'fuzzer_mt_probe' -Runtime 'MT'
if ($static.ExitCode -ne 0) {
    throw "LibFuzzer /MT build failed:`n$($static.Text)"
}
$staticCacheText = Get-LinkCacheText -Target 'fuzzer_mt_probe'
if ($staticCacheText -notmatch [Regex]::Escape($staticReleaseLibrary)) {
    throw "LibFuzzer /MT link cache did not seal '$staticReleaseLibrary'"
}
if ($staticCacheText -match [Regex]::Escape($dynamicDebugLibrary)) {
    throw "LibFuzzer /MT link cache incorrectly retained the /MDd runtime"
}

# /NODEFAULTLIB:<runtime> must suppress the compiler-injected main runtime.
# Supply the official no-main variant explicitly and provide our own main so
# the real LINK succeeds. The final cache must contain the explicit no-main
# library but must not claim the suppressed default runtime as freshness input.
$suppressed = Invoke-FuzzerBuild `
    -Source 'fuzzer_with_main.cpp' `
    -Target 'fuzzer_no_main_probe' `
    -Libraries @($noMainDebugLibrary) `
    -LinkerArguments @("/NODEFAULTLIB:$dynamicDebugLibrary")
if ($suppressed.ExitCode -ne 0) {
    throw "LibFuzzer no-main suppression build failed:`n$($suppressed.Text)"
}
$suppressedCacheText = Get-LinkCacheText -Target 'fuzzer_no_main_probe'
if ($suppressedCacheText -notmatch [Regex]::Escape($noMainDebugLibrary)) {
    throw "Suppression build did not seal explicit no-main runtime '$noMainDebugLibrary'"
}
if ($suppressedCacheText -match [Regex]::Escape($dynamicDebugLibrary)) {
    throw "Suppressed LibFuzzer default runtime was incorrectly sealed as a link input"
}

Write-Host 'Real MSVC LibFuzzer default-runtime, CRT selection, suppression, and warm-cache checks passed.'
exit 0

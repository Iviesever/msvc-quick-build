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

$fixture = Join-Path $RepoRoot 'native-test-work/openmp-runtime-policy'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8 -Value @(
    '#include <omp.h>',
    'int main() {',
    '    int total = 0;',
    '    #pragma omp parallel for reduction(+:total)',
    '    for (int i = 0; i < 100; ++i) { total += i; }',
    '    return total == 4950 ? 0 : 1;',
    '}'
)
Set-Content -LiteralPath (Join-Path $fixture 'plain.cpp') -Encoding utf8 -Value 'int main() { return 0; }'

function Invoke-OpenMpBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Target,
        [string[]]$ExtraArguments = @()
    )

    Push-Location $fixture
    try {
        $arguments = @('build', $Source, '--release', '--no-discover', '-o', $Target, '/openmp')
        $arguments += $ExtraArguments
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

function Get-LinkCachePath([string]$Target) {
    Join-Path $fixture ".mqb/cache/link/$Target.linkcache"
}

function Get-LinkCacheText([string]$Target) {
    $cache = Get-LinkCachePath $Target
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        throw "OpenMP link cache missing: $cache"
    }
    [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($cache))
}

function Find-SealedVcompPath([string]$CacheText) {
    $match = [regex]::Match(
        $CacheText,
        '(?i)(?<path>[A-Z]:/[^\x00-\x1f]*?/vcomp\.lib)')
    if (-not $match.Success) {
        throw "OpenMP link cache did not expose a sealed vcomp.lib path"
    }
    [System.IO.Path]::GetFullPath($match.Groups['path'].Value.Replace('/', '\'))
}

$cold = Invoke-OpenMpBuild -Source 'main.cpp' -Target 'openmp_probe'
if ($cold.ExitCode -ne 0) {
    throw "Cold classic OpenMP build failed:`n$($cold.Text)"
}
$exe = Join-Path $fixture '.mqb/bin/openmp_probe.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Classic OpenMP build did not produce expected executable: $exe"
}
& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Classic OpenMP executable failed at runtime (exit $LASTEXITCODE)"
}

$cacheText = Get-LinkCacheText 'openmp_probe'
if ($cacheText -notmatch '(?i)vcomp\.lib') {
    throw "Classic OpenMP link cache did not seal vcomp.lib freshness evidence"
}
$vcomp = Find-SealedVcompPath $cacheText
if (-not (Test-Path -LiteralPath $vcomp -PathType Leaf)) {
    throw "Sealed OpenMP import library does not exist: $vcomp"
}

$warm = Invoke-OpenMpBuild -Source 'main.cpp' -Target 'openmp_probe'
if ($warm.ExitCode -ne 0) {
    throw "Warm classic OpenMP build failed:`n$($warm.Text)"
}
if ($warm.Text -notmatch '\[up-to-date\]\s+openmp_probe\.exe') {
    throw "Warm classic OpenMP build did not reuse sealed runtime input:`n$($warm.Text)"
}

# Put an exact copy of the toolchain import library first in LINK search order.
# This makes the compiler-injected /DEFAULTLIB:vcomp directive resolve to a
# fixture-owned file whose freshness we can mutate without touching VS itself.
$localLibDir = Join-Path $fixture 'local-lib'
New-Item -ItemType Directory -Path $localLibDir -Force | Out-Null
$localVcomp = Join-Path $localLibDir 'vcomp.lib'
Copy-Item -LiteralPath $vcomp -Destination $localVcomp -Force

$localCold = Invoke-OpenMpBuild `
    -Source 'main.cpp' `
    -Target 'openmp_local_probe' `
    -ExtraArguments @('-L', $localLibDir)
if ($localCold.ExitCode -ne 0) {
    throw "Local-vcomp OpenMP cold build failed:`n$($localCold.Text)"
}
$localCacheText = Get-LinkCacheText 'openmp_local_probe'
$normalizedLocal = [System.IO.Path]::GetFullPath($localVcomp).Replace('\', '/')
if ($localCacheText -notmatch [Regex]::Escape($normalizedLocal)) {
    throw "OpenMP freshness did not seal the -L-preferred fixture vcomp.lib"
}

$localWarm = Invoke-OpenMpBuild `
    -Source 'main.cpp' `
    -Target 'openmp_local_probe' `
    -ExtraArguments @('-L', $localLibDir)
if ($localWarm.ExitCode -ne 0) {
    throw "Local-vcomp OpenMP warm build failed:`n$($localWarm.Text)"
}
if ($localWarm.Text -notmatch '\[up-to-date\]\s+openmp_local_probe\.exe') {
    throw "Local-vcomp OpenMP warm build did not reuse link cache:`n$($localWarm.Text)"
}

(Get-Item -LiteralPath $localVcomp).LastWriteTimeUtc = (Get-Date).ToUniversalTime().AddSeconds(5)
$changed = Invoke-OpenMpBuild `
    -Source 'main.cpp' `
    -Target 'openmp_local_probe' `
    -ExtraArguments @('-L', $localLibDir)
if ($changed.ExitCode -ne 0) {
    throw "Changed-vcomp OpenMP build failed:`n$($changed.Text)"
}
if ($changed.Text -notmatch '\[up-to-date\]\s+main\.cpp') {
    throw "OpenMP runtime-only freshness change unexpectedly recompiled main.cpp:`n$($changed.Text)"
}
if ($changed.Text -notmatch '\[link\]\s+openmp_local_probe\.exe') {
    throw "Changed OpenMP import library did not trigger relink:`n$($changed.Text)"
}
if ($changed.Text -notmatch 'link inputs changed') {
    throw "OpenMP runtime-driven relink was not explained as link inputs changed:`n$($changed.Text)"
}

# /NODEFAULTLIB remains authoritative. Use a source with no OpenMP unresolveds
# so LINK can succeed while proving MQB also drops the suppressed freshness edge.
$suppressed = Invoke-OpenMpBuild `
    -Source 'plain.cpp' `
    -Target 'openmp_suppressed' `
    -ExtraArguments @('/link', '/NODEFAULTLIB:vcomp.lib', '/NODEFAULTLIB:vcompd.lib')
if ($suppressed.ExitCode -ne 0) {
    throw "OpenMP NODEFAULTLIB suppression probe failed:`n$($suppressed.Text)"
}
$suppressedCache = Get-LinkCacheText 'openmp_suppressed'
if ($suppressedCache -match '(?i)vcompd?\.lib') {
    throw "OpenMP link cache retained runtime freshness evidence suppressed by /NODEFAULTLIB"
}

# Exercise the compiler->link projection through the Modules pipeline as well.
Set-Content -LiteralPath (Join-Path $fixture 'math.ixx') -Encoding utf8 -Value @(
    'export module math;',
    'export int answer() { return 42; }'
)
Set-Content -LiteralPath (Join-Path $fixture 'module_main.cpp') -Encoding utf8 -Value @(
    '#include <omp.h>',
    'import math;',
    'int main() {',
    '    int total = 0;',
    '    #pragma omp parallel for reduction(+:total)',
    '    for (int i = 0; i < 10; ++i) { total += i; }',
    '    return answer() == 42 && total == 45 ? 0 : 1;',
    '}'
)
Push-Location $fixture
try {
    $moduleOutput = @(
        & $MqbPath build module_main.cpp math.ixx --release --no-discover `
            -o openmp_module_probe /openmp 2>&1
    )
    $moduleExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($moduleExit -ne 0) {
    throw "Module classic OpenMP build failed:`n$($moduleOutput -join [Environment]::NewLine)"
}
if ((Get-LinkCacheText 'openmp_module_probe') -notmatch '(?i)vcomp\.lib') {
    throw "Module OpenMP link cache did not seal vcomp.lib freshness evidence"
}

# LLVM OpenMP selects libomp and a different DLL deployment contract. It must
# not silently enter the classic vcomp freshness model.
Push-Location $fixture
try {
    $llvmRejected = @(
        & $MqbPath build plain.cpp --release --no-discover -o openmp_llvm_rejected `
            /openmp:llvm 2>&1
    )
    $llvmExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
$llvmText = $llvmRejected -join [Environment]::NewLine
if ($llvmExit -eq 0) {
    throw "/openmp:llvm unexpectedly entered MQB's classic OpenMP runtime pipeline"
}
if ($llvmText -notmatch 'libomp runtime') {
    throw "Rejected /openmp:llvm did not explain its separate runtime ownership boundary:`n$llvmText"
}
if (Test-Path -LiteralPath (Join-Path $fixture '.mqb/bin/openmp_llvm_rejected.exe') -PathType Leaf) {
    throw "Rejected /openmp:llvm unexpectedly produced an executable"
}

Write-Host 'Real MSVC classic OpenMP runtime freshness, NODEFAULTLIB, Modules, and LLVM ownership checks passed.'
exit 0

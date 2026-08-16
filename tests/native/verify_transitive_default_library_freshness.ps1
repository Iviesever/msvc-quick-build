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

$fixture = Join-Path $RepoRoot 'native-test-work/transitive-defaultlib-freshness'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
$low = Join-Path $fixture 'low'
$high = Join-Path $fixture 'high'
New-Item -ItemType Directory -Force -Path $low | Out-Null
New-Item -ItemType Directory -Force -Path $high | Out-Null

function Invoke-Mqb {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Push-Location $WorkingDirectory
    try {
        $output = @(& $MqbPath @Arguments 2>&1)
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

function Require-Success {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Result.ExitCode -ne 0) {
        throw "$Description failed (exit $($Result.ExitCode)):`n$($Result.Text)"
    }
}

function Invoke-Program {
    param([Parameter(Mandatory = $true)][string]$Path)
    & $Path
    return $LASTEXITCODE
}

function Link-Cache-Text {
    $cache = Join-Path $fixture '.mqb/cache/link/consumer.linkcache'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        throw "consumer link cache missing: $cache"
    }
    return [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($cache))
}

function Cache-Path-Token {
    param([Parameter(Mandatory = $true)][string]$Path)
    return ([System.IO.Path]::GetFullPath($Path) -replace '\\', '/')
}

Set-Content -LiteralPath (Join-Path $low 'dep.cpp') -Encoding utf8 -Value @(
    'extern "C" int dep_value() { return 7; }'
)
$lowBuild = Invoke-Mqb -WorkingDirectory $low -Arguments @(
    'dep.cpp', '--release', '--no-discover', '--env', 'vs', '--type', 'static', '-o', 'dep'
)
Require-Success $lowBuild 'low-priority dep.lib build'
$lowLibrary = Join-Path $low '.mqb/bin/dep.lib'
if (-not (Test-Path -LiteralPath $lowLibrary -PathType Leaf)) {
    throw "low-priority dep.lib missing: $lowLibrary"
}

Set-Content -LiteralPath (Join-Path $fixture 'producer.cpp') -Encoding utf8 -Value @(
    '#pragma comment(lib, "dep.lib")',
    'extern "C" int dep_value();',
    'extern "C" int producer_value() { return dep_value(); }'
)
$producerBuild = Invoke-Mqb -WorkingDirectory $fixture -Arguments @(
    'producer.cpp', '--release', '--no-discover', '--env', 'vs', '--type', 'static', '-o', 'producer'
)
Require-Success $producerBuild 'producer archive build'
$producerLibrary = Join-Path $fixture '.mqb/bin/producer.lib'
if (-not (Test-Path -LiteralPath $producerLibrary -PathType Leaf)) {
    throw "producer.lib missing: $producerLibrary"
}

Set-Content -LiteralPath (Join-Path $fixture 'consumer.cpp') -Encoding utf8 -Value @(
    'extern "C" int producer_value();',
    'int main() { return producer_value(); }'
)

$consumerArgs = @(
    'consumer.cpp', '--release', '--no-discover', '--env', 'vs',
    '-L', 'high/.mqb/bin',
    '-L', 'low/.mqb/bin',
    '-L', '.mqb/bin',
    '-l', 'producer',
    '-o', 'consumer'
)
$cold = Invoke-Mqb -WorkingDirectory $fixture -Arguments $consumerArgs
Require-Success $cold 'cold transitive-defaultlib consumer link'
$consumer = Join-Path $fixture '.mqb/bin/consumer.exe'
if (-not (Test-Path -LiteralPath $consumer -PathType Leaf)) {
    throw "consumer executable missing: $consumer"
}
if ((Invoke-Program $consumer) -ne 7) {
    throw 'cold consumer did not resolve dep.lib through producer archive .drectve'
}

$cacheText = Link-Cache-Text
$lowToken = Cache-Path-Token $lowLibrary
if ($cacheText -notmatch [Regex]::Escape($lowToken)) {
    throw "link cache did not seal transitive dep.lib selected by LINK: $lowToken"
}
if ($cold.Text -match '(?im)^\s*Searching\s+.*\.lib') {
    throw "internal /VERBOSE:LIB observation leaked into ordinary MQB output:`n$($cold.Text)"
}

$warm = Invoke-Mqb -WorkingDirectory $fixture -Arguments $consumerArgs
Require-Success $warm 'warm transitive-defaultlib consumer link'
if ($warm.Text -notmatch '\[up-to-date\]\s+consumer\.cpp') {
    throw "warm consumer unexpectedly recompiled its source:`n$($warm.Text)"
}
if ($warm.Text -notmatch '\[up-to-date\]\s+consumer\.exe') {
    throw "warm consumer did not reuse sealed transitive library evidence:`n$($warm.Text)"
}

# Change only the transitive library. producer.lib and consumer.cpp stay byte-for-byte
# unchanged. The cached dep.lib freshness edge must force LINK, not CL.
Set-Content -LiteralPath (Join-Path $low 'dep.cpp') -Encoding utf8 -Value @(
    'extern "C" int dep_value() { return 8; }'
)
$lowMutation = Invoke-Mqb -WorkingDirectory $low -Arguments @(
    'dep.cpp', '--release', '--no-discover', '--env', 'vs', '--type', 'static', '-o', 'dep'
)
Require-Success $lowMutation 'mutated low-priority dep.lib build'

$relink = Invoke-Mqb -WorkingDirectory $fixture -Arguments $consumerArgs
Require-Success $relink 'consumer relink after transitive dep.lib mutation'
if ($relink.Text -notmatch '\[up-to-date\]\s+consumer\.cpp') {
    throw "transitive library mutation recompiled consumer.cpp:`n$($relink.Text)"
}
if ($relink.Text -notmatch '\[link\]\s+consumer\.exe') {
    throw "transitive library mutation did not rerun LINK:`n$($relink.Text)"
}
if ($relink.Text -notmatch 'link inputs changed') {
    throw "transitive library mutation did not report link-input invalidation:`n$($relink.Text)"
}
if ((Invoke-Program $consumer) -ne 8) {
    throw 'consumer did not observe mutated transitive dep.lib behavior'
}

# The argv stays identical. Introduce a same-name dep.lib in the already-declared
# higher-priority -L directory. Warm validation must re-resolve the cached basename,
# notice LINK would now choose a different path, and relink before any source changes.
Set-Content -LiteralPath (Join-Path $high 'dep.cpp') -Encoding utf8 -Value @(
    'extern "C" int dep_value() { return 9; }'
)
$highBuild = Invoke-Mqb -WorkingDirectory $high -Arguments @(
    'dep.cpp', '--release', '--no-discover', '--env', 'vs', '--type', 'static', '-o', 'dep'
)
Require-Success $highBuild 'high-priority dep.lib build'
$highLibrary = Join-Path $high '.mqb/bin/dep.lib'
if (-not (Test-Path -LiteralPath $highLibrary -PathType Leaf)) {
    throw "high-priority dep.lib missing: $highLibrary"
}

$reroute = Invoke-Mqb -WorkingDirectory $fixture -Arguments $consumerArgs
Require-Success $reroute 'consumer relink after transitive library search reroute'
if ($reroute.Text -notmatch '\[up-to-date\]\s+consumer\.cpp') {
    throw "search-priority reroute recompiled consumer.cpp:`n$($reroute.Text)"
}
if ($reroute.Text -notmatch '\[link\]\s+consumer\.exe') {
    throw "new higher-priority transitive dep.lib did not rerun LINK:`n$($reroute.Text)"
}
if ($reroute.Text -notmatch 'link inputs changed') {
    throw "search-priority reroute did not report link-input invalidation:`n$($reroute.Text)"
}
if ((Invoke-Program $consumer) -ne 9) {
    throw 'consumer did not switch to the newly higher-priority transitive dep.lib'
}

$reroutedCacheText = Link-Cache-Text
$highToken = Cache-Path-Token $highLibrary
if ($reroutedCacheText -notmatch [Regex]::Escape($highToken)) {
    throw "resealed link cache did not contain high-priority dep.lib: $highToken"
}
if ($reroutedCacheText -match [Regex]::Escape($lowToken)) {
    throw "resealed link cache retained stale low-priority transitive dep.lib: $lowToken"
}

# Repeat the archive-directive route with /GL + /LTCG. DUMPBIN /DIRECTIVES cannot
# inspect /GL objects, so this proves the implementation is based on actual LINK
# search evidence rather than a COFF/.drectve parser that silently loses ILTCG.
Set-Content -LiteralPath (Join-Path $fixture 'producer_gl.cpp') -Encoding utf8 -Value @(
    '#pragma comment(lib, "dep.lib")',
    'extern "C" int dep_value();',
    'extern "C" int producer_gl_value() { return dep_value(); }'
)
$producerGl = Invoke-Mqb -WorkingDirectory $fixture -Arguments @(
    'producer_gl.cpp', '--release', '--no-discover', '--env', 'vs', '--type', 'static', '--ltcg', '-o', 'producer_gl'
)
Require-Success $producerGl 'LTCG producer archive build'

Set-Content -LiteralPath (Join-Path $fixture 'consumer_gl.cpp') -Encoding utf8 -Value @(
    'extern "C" int producer_gl_value();',
    'int main() { return producer_gl_value(); }'
)
$consumerGlArgs = @(
    'consumer_gl.cpp', '--release', '--no-discover', '--env', 'vs', '--ltcg',
    '-L', 'high/.mqb/bin',
    '-L', '.mqb/bin',
    '-l', 'producer_gl',
    '-o', 'consumer_gl'
)
$glCold = Invoke-Mqb -WorkingDirectory $fixture -Arguments $consumerGlArgs
Require-Success $glCold 'cold LTCG transitive-defaultlib consumer link'
$consumerGl = Join-Path $fixture '.mqb/bin/consumer_gl.exe'
if ((Invoke-Program $consumerGl) -ne 9) {
    throw 'LTCG consumer did not resolve dep.lib embedded in /GL archive member directives'
}
$glCache = Join-Path $fixture '.mqb/cache/link/consumer_gl.linkcache'
$glCacheText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($glCache))
if ($glCacheText -notmatch [Regex]::Escape($highToken)) {
    throw 'LTCG consumer link cache did not seal transitive dep.lib from actual LINK evidence'
}

Write-Host 'Real MSVC transitive .drectve default-library freshness, search reroute, and LTCG checks passed.'
exit 0

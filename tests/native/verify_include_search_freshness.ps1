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

$root = Join-Path $RepoRoot 'native-test-work/include-search-freshness'
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $root | Out-Null

function Write-Utf8 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Lines
    )
    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $Path -Encoding utf8 -Value $Lines
}

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

function Require-Compile {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Result.Text -notmatch ("\[compile\]\s+" + [Regex]::Escape($Source))) {
        throw "$Description did not compile $Source:`n$($Result.Text)"
    }
}

function Require-UpToDate {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Result.Text -notmatch ("\[up-to-date\]\s+" + [Regex]::Escape($Source))) {
        throw "$Description did not report $Source up-to-date:`n$($Result.Text)"
    }
    if ($Result.Text -match ("\[compile\]\s+" + [Regex]::Escape($Source))) {
        throw "$Description launched an unnecessary compiler for $Source:`n$($Result.Text)"
    }
}

function Invoke-Program {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "program output missing: $Path"
    }
    & $Path
    return $LASTEXITCODE
}

function Program-Path {
    param(
        [Parameter(Mandatory = $true)][string]$Fixture,
        [Parameter(Mandatory = $true)][string]$OutputName
    )
    return Join-Path $Fixture ".mqb/bin/$OutputName.exe"
}

function New-CaseDirectory {
    param([Parameter(Mandatory = $true)][string]$Name)
    $path = Join-Path $root $Name
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

# 1 + 2. Higher-priority typed -I root gains a same-name angle header, then
# the active higher-priority header is removed. Neither transition changes argv.
$case1 = New-CaseDirectory 'typed-angle-shadow'
$case1High = Join-Path $case1 'high'
$case1Low = Join-Path $case1 'low'
New-Item -ItemType Directory -Force -Path $case1High | Out-Null
New-Item -ItemType Directory -Force -Path $case1Low | Out-Null
Write-Utf8 (Join-Path $case1Low 'pick.hpp') @('inline int selected_value() { return 7; }')
Write-Utf8 (Join-Path $case1 'main.cpp') @(
    '#include <pick.hpp>',
    'int main() { return selected_value(); }'
)
$case1Args = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'high', '-I', 'low', '-o', 'typed-angle')
$case1Cold = Invoke-Mqb $case1 $case1Args
Require-Success $case1Cold 'typed angle cold build'
if ((Invoke-Program (Program-Path $case1 'typed-angle')) -ne 7) {
    throw 'typed angle cold build did not resolve low/pick.hpp'
}
$case1Warm = Invoke-Mqb $case1 $case1Args
Require-Success $case1Warm 'typed angle warm build'
Require-UpToDate $case1Warm 'main.cpp' 'typed angle warm build'
Start-Sleep -Milliseconds 50
Write-Utf8 (Join-Path $case1High 'pick.hpp') @('inline int selected_value() { return 9; }')
$case1Shadow = Invoke-Mqb $case1 $case1Args
Require-Success $case1Shadow 'typed angle higher-priority shadow build'
Require-Compile $case1Shadow 'main.cpp' 'typed angle higher-priority shadow build'
if ((Invoke-Program (Program-Path $case1 'typed-angle')) -ne 9) {
    throw 'new higher-priority typed -I header did not replace the cached low header'
}
Remove-Item -LiteralPath (Join-Path $case1High 'pick.hpp') -Force
$case1Removed = Invoke-Mqb $case1 $case1Args
Require-Success $case1Removed 'typed angle active-header removal build'
Require-Compile $case1Removed 'main.cpp' 'typed angle active-header removal build'
if ((Invoke-Program (Program-Path $case1 'typed-angle')) -ne 7) {
    throw 'removing the active higher-priority header did not fall back to low/pick.hpp'
}

# 3. Include-directory order is compile identity and must invalidate even when
# the directory contents themselves remain unchanged.
$case3 = New-CaseDirectory 'include-order'
New-Item -ItemType Directory -Force -Path (Join-Path $case3 'a') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case3 'b') | Out-Null
Write-Utf8 (Join-Path $case3 'a/pick.hpp') @('inline int selected_value() { return 3; }')
Write-Utf8 (Join-Path $case3 'b/pick.hpp') @('inline int selected_value() { return 4; }')
Write-Utf8 (Join-Path $case3 'main.cpp') @('#include <pick.hpp>', 'int main() { return selected_value(); }')
$case3AFirst = Invoke-Mqb $case3 @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'a', '-I', 'b', '-o', 'order')
Require-Success $case3AFirst 'include-order a-first build'
if ((Invoke-Program (Program-Path $case3 'order')) -ne 3) { throw 'a-first include order was not honored' }
$case3BFirst = Invoke-Mqb $case3 @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'b', '-I', 'a', '-o', 'order')
Require-Success $case3BFirst 'include-order b-first build'
Require-Compile $case3BFirst 'main.cpp' 'include-order b-first build'
if ((Invoke-Program (Program-Path $case3 'order')) -ne 4) { throw 'reordered include search path was not honored' }

# 4. Quoted include resolution searches the source directory before /I. Adding
# a source-adjacent same-name header must invalidate the cached /I resolution.
$case4 = New-CaseDirectory 'quoted-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case4 'low') | Out-Null
Write-Utf8 (Join-Path $case4 'low/pick.hpp') @('inline int selected_value() { return 5; }')
Write-Utf8 (Join-Path $case4 'main.cpp') @('#include "pick.hpp"', 'int main() { return selected_value(); }')
$case4Args = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'low', '-o', 'quoted')
$case4Cold = Invoke-Mqb $case4 $case4Args
Require-Success $case4Cold 'quoted cold build'
if ((Invoke-Program (Program-Path $case4 'quoted')) -ne 5) { throw 'quoted cold build did not resolve low/pick.hpp' }
$case4Warm = Invoke-Mqb $case4 $case4Args
Require-Success $case4Warm 'quoted warm build'
Require-UpToDate $case4Warm 'main.cpp' 'quoted warm build'
Start-Sleep -Milliseconds 50
Write-Utf8 (Join-Path $case4 'pick.hpp') @('inline int selected_value() { return 6; }')
$case4Shadow = Invoke-Mqb $case4 $case4Args
Require-Success $case4Shadow 'quoted source-directory shadow build'
Require-Compile $case4Shadow 'main.cpp' 'quoted source-directory shadow build'
if ((Invoke-Program (Program-Path $case4 'quoted')) -ne 6) { throw 'quoted source-directory shadow was not selected' }

# 5. Native MSVC /I syntax participates in the same search freshness model;
# this specifically covers raw passthrough roots rather than typed -I policy.
$case5 = New-CaseDirectory 'native-angle-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case5 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case5 'low') | Out-Null
Write-Utf8 (Join-Path $case5 'low/pick.hpp') @('inline int selected_value() { return 11; }')
Write-Utf8 (Join-Path $case5 'main.cpp') @('#include <pick.hpp>', 'int main() { return selected_value(); }')
$case5Args = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '/Ihigh', '/Ilow', '-o', 'native-angle')
$case5Cold = Invoke-Mqb $case5 $case5Args
Require-Success $case5Cold 'native /I angle cold build'
if ((Invoke-Program (Program-Path $case5 'native-angle')) -ne 11) { throw 'native /I cold build did not resolve low/pick.hpp' }
Start-Sleep -Milliseconds 50
Write-Utf8 (Join-Path $case5 'high/pick.hpp') @('inline int selected_value() { return 12; }')
$case5Shadow = Invoke-Mqb $case5 $case5Args
Require-Success $case5Shadow 'native /I angle shadow build'
Require-Compile $case5Shadow 'main.cpp' 'native /I angle shadow build'
if ((Invoke-Program (Program-Path $case5 'native-angle')) -ne 12) { throw 'native /I search reroute did not select high/pick.hpp' }

# 6 + 8. A named-module provider uses an angle header. The unchanged warm build
# must reuse both P1689 metadata and compile caches; adding a higher-priority
# header must rerun the scan and compile without any argv/source mutation.
$case6 = New-CaseDirectory 'p1689-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case6 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case6 'low') | Out-Null
Write-Utf8 (Join-Path $case6 'low/config.hpp') @('#define MQB_VALUE 21')
Write-Utf8 (Join-Path $case6 'math.ixx') @(
    'module;',
    '#include <config.hpp>',
    'export module math;',
    'export int module_value() { return MQB_VALUE; }'
)
Write-Utf8 (Join-Path $case6 'main.cpp') @('import math;', 'int main() { return module_value(); }')
$case6Args = @('main.cpp', 'math.ixx', '--release', '--no-discover', '--env', 'vs', '--std', 'latest', '-I', 'high', '-I', 'low', '-o', 'p1689')
$case6Cold = Invoke-Mqb $case6 $case6Args
Require-Success $case6Cold 'P1689 cold build'
if ((Invoke-Program (Program-Path $case6 'p1689')) -ne 21) { throw 'P1689 cold build did not use low/config.hpp' }
$scanFile = Join-Path $case6 '.mqb/scan/math.ixx.json'
if (-not (Test-Path -LiteralPath $scanFile -PathType Leaf)) { throw "P1689 scan artifact missing: $scanFile" }
$scanColdTime = (Get-Item -LiteralPath $scanFile).LastWriteTimeUtc
$case6Warm = Invoke-Mqb $case6 $case6Args
Require-Success $case6Warm 'P1689 warm no-op build'
Require-UpToDate $case6Warm 'math.ixx' 'P1689 warm no-op build'
Require-UpToDate $case6Warm 'main.cpp' 'P1689 warm no-op build'
$scanWarmTime = (Get-Item -LiteralPath $scanFile).LastWriteTimeUtc
if ($scanWarmTime -ne $scanColdTime) {
    throw 'unchanged modules no-op rewrote P1689 scan metadata, indicating an unnecessary scanner launch'
}
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case6 'high/config.hpp') @('#define MQB_VALUE 22')
$case6Shadow = Invoke-Mqb $case6 $case6Args
Require-Success $case6Shadow 'P1689 higher-priority shadow build'
Require-Compile $case6Shadow 'math.ixx' 'P1689 higher-priority shadow build'
$scanShadowTime = (Get-Item -LiteralPath $scanFile).LastWriteTimeUtc
if ($scanShadowTime -le $scanWarmTime) {
    throw 'higher-priority module header did not refresh P1689 scan metadata'
}
if ((Invoke-Program (Program-Path $case6 'p1689')) -ne 22) {
    throw 'module rebuild did not observe the newly higher-priority config.hpp'
}

# 7. Header-unit source identity is discovered from P1689. A new higher-priority
# angle header must therefore invalidate scan evidence, allocate/rebuild the new
# header-unit provider, and rebuild its consumer.
$case7 = New-CaseDirectory 'header-unit-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case7 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case7 'low') | Out-Null
Write-Utf8 (Join-Path $case7 'low/util.hpp') @('inline int header_value() { return 31; }')
Write-Utf8 (Join-Path $case7 'main.cpp') @('import <util.hpp>;', 'int main() { return header_value(); }')
$case7Args = @('main.cpp', '--release', '--env', 'vs', '--std', 'latest', '-I', 'high', '-I', 'low', '-o', 'header-unit-shadow')
$case7Cold = Invoke-Mqb $case7 $case7Args
Require-Success $case7Cold 'header-unit cold build'
if ((Invoke-Program (Program-Path $case7 'header-unit-shadow')) -ne 31) { throw 'header-unit cold build did not use low/util.hpp' }
$case7Warm = Invoke-Mqb $case7 $case7Args
Require-Success $case7Warm 'header-unit warm build'
Require-UpToDate $case7Warm 'main.cpp' 'header-unit warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case7 'high/util.hpp') @('inline int header_value() { return 32; }')
$case7Shadow = Invoke-Mqb $case7 $case7Args
Require-Success $case7Shadow 'header-unit higher-priority shadow build'
Require-Compile $case7Shadow 'main.cpp' 'header-unit higher-priority shadow build'
if ((Invoke-Program (Program-Path $case7 'header-unit-shadow')) -ne 32) {
    throw 'header-unit resolution did not move to the newly higher-priority util.hpp'
}

# PCH creator + consumer coverage. The synthetic creator must stay warm when
# unchanged, but a transitive include search reroute inside the forced PCH
# header must rebuild the PCH and propagate rebuild to every consumer.
$casePch = New-CaseDirectory 'pch-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $casePch 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $casePch 'low') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $casePch 'include') | Out-Null
Write-Utf8 (Join-Path $casePch 'low/pick.hpp') @('inline int pch_selected_value() { return 41; }')
Write-Utf8 (Join-Path $casePch 'include/pch.hpp') @('#pragma once', '#include <pick.hpp>')
Write-Utf8 (Join-Path $casePch 'main.cpp') @('int main() { return pch_selected_value(); }')
$casePchArgs = @('build', 'main.cpp', '--release', '--no-discover', '--env', 'vs', '--pch', 'include/pch.hpp', '-I', 'high', '-I', 'low', '-o', 'pch-shadow')
$casePchCold = Invoke-Mqb $casePch $casePchArgs
Require-Success $casePchCold 'PCH include-search cold build'
if ((Invoke-Program (Program-Path $casePch 'pch-shadow')) -ne 41) { throw 'PCH cold build did not use low/pick.hpp' }
$casePchWarm = Invoke-Mqb $casePch $casePchArgs
Require-Success $casePchWarm 'PCH include-search warm build'
if ($casePchWarm.Text -notmatch '\[up-to-date\]\s+pch') {
    throw "unchanged PCH creator did not remain warm:`n$($casePchWarm.Text)"
}
Require-UpToDate $casePchWarm 'main.cpp' 'PCH include-search warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $casePch 'high/pick.hpp') @('inline int pch_selected_value() { return 42; }')
$casePchShadow = Invoke-Mqb $casePch $casePchArgs
Require-Success $casePchShadow 'PCH higher-priority transitive shadow build'
if ($casePchShadow.Text -notmatch '\[pch\]') {
    throw "PCH transitive search reroute did not rebuild the creator:`n$($casePchShadow.Text)"
}
Require-Compile $casePchShadow 'main.cpp' 'PCH higher-priority transitive shadow build'
if ((Invoke-Program (Program-Path $casePch 'pch-shadow')) -ne 42) {
    throw 'PCH consumer did not observe newly higher-priority transitive header'
}

Write-Host 'Include search resolution freshness checks passed: typed/native /I, removal, order, quote/angle shadowing, P1689 reuse, header units, PCH creator/consumer, and zero-process no-op behavior.'
exit 0

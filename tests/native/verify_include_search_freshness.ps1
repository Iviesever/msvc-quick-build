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
    throw ('MQB executable not found: {0}' -f $MqbPath)
}

$root = Join-Path $RepoRoot 'native-test-work/include-search-freshness'
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $root | Out-Null

function Write-Utf8 {
    param([string]$Path, [string[]]$Lines)
    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $Path -Encoding utf8 -Value $Lines
}

function New-CaseDirectory {
    param([string]$Name)
    $path = Join-Path $root $Name
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

function Invoke-MqbCase {
    param([string]$WorkingDirectory, [string[]]$Arguments)
    Push-Location $WorkingDirectory
    try {
        $output = @(& $MqbPath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    return [PSCustomObject]@{
        ExitCode = $exitCode
        Text = ($output -join [Environment]::NewLine)
    }
}

function Require-Success {
    param($Result, [string]$Description)
    if ($Result.ExitCode -ne 0) {
        throw ('{0} failed (exit {1}):{2}{3}' -f $Description, $Result.ExitCode, [Environment]::NewLine, $Result.Text)
    }
}

function Require-Compile {
    param($Result, [string]$Source, [string]$Description)
    if ($Result.Text -notmatch ('\[compile\]\s+' + [Regex]::Escape($Source))) {
        throw ('{0} did not compile {1}:{2}{3}' -f $Description, $Source, [Environment]::NewLine, $Result.Text)
    }
}

function Require-UpToDate {
    param($Result, [string]$Source, [string]$Description)
    if ($Result.Text -notmatch ('\[up-to-date\]\s+' + [Regex]::Escape($Source))) {
        throw ('{0} did not report {1} up-to-date:{2}{3}' -f $Description, $Source, [Environment]::NewLine, $Result.Text)
    }
    if ($Result.Text -match ('\[compile\]\s+' + [Regex]::Escape($Source))) {
        throw ('{0} launched an unnecessary compiler for {1}:{2}{3}' -f $Description, $Source, [Environment]::NewLine, $Result.Text)
    }
}

function Program-Path {
    param([string]$Fixture, [string]$OutputName)
    return Join-Path $Fixture ('.mqb/bin/{0}.exe' -f $OutputName)
}

function Require-ProgramExit {
    param([string]$Path, [int]$Expected, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ('{0}: program output missing: {1}' -f $Description, $Path)
    }
    & $Path | Out-Null
    $actual = $LASTEXITCODE
    if ($actual -ne $Expected) {
        throw ('{0}: expected exit {1}, got {2}' -f $Description, $Expected, $actual)
    }
}

# 1 + 2 + 5. Typed /I angle resolution: high root initially misses, then gains
# the same-name header, then loses it again. None of these transitions changes argv.
$case1 = New-CaseDirectory 'typed-angle-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case1 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case1 'low') | Out-Null
Write-Utf8 (Join-Path $case1 'low/pick.hpp') @('inline int selected_value() { return 7; }')
Write-Utf8 (Join-Path $case1 'main.cpp') @('#include <pick.hpp>', 'int main() { return selected_value(); }')
$args1 = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'high', '-I', 'low', '-o', 'typed-angle')
$cold1 = Invoke-MqbCase $case1 $args1
Require-Success $cold1 'typed angle cold build'
Require-ProgramExit (Program-Path $case1 'typed-angle') 7 'typed angle cold build'
$warm1 = Invoke-MqbCase $case1 $args1
Require-Success $warm1 'typed angle warm build'
Require-UpToDate $warm1 'main.cpp' 'typed angle warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case1 'high/pick.hpp') @('inline int selected_value() { return 9; }')
$shadow1 = Invoke-MqbCase $case1 $args1
Require-Success $shadow1 'typed angle higher-priority shadow build'
Require-Compile $shadow1 'main.cpp' 'typed angle higher-priority shadow build'
Require-ProgramExit (Program-Path $case1 'typed-angle') 9 'typed angle higher-priority shadow build'
Remove-Item -LiteralPath (Join-Path $case1 'high/pick.hpp') -Force
$removed1 = Invoke-MqbCase $case1 $args1
Require-Success $removed1 'typed angle active-header removal build'
Require-Compile $removed1 'main.cpp' 'typed angle active-header removal build'
Require-ProgramExit (Program-Path $case1 'typed-angle') 7 'typed angle active-header removal build'

# 3. Search-root order is compile identity.
$case3 = New-CaseDirectory 'include-order'
New-Item -ItemType Directory -Force -Path (Join-Path $case3 'a') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case3 'b') | Out-Null
Write-Utf8 (Join-Path $case3 'a/pick.hpp') @('inline int selected_value() { return 3; }')
Write-Utf8 (Join-Path $case3 'b/pick.hpp') @('inline int selected_value() { return 4; }')
Write-Utf8 (Join-Path $case3 'main.cpp') @('#include <pick.hpp>', 'int main() { return selected_value(); }')
$aFirst = Invoke-MqbCase $case3 @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'a', '-I', 'b', '-o', 'order')
Require-Success $aFirst 'include-order a-first build'
Require-ProgramExit (Program-Path $case3 'order') 3 'include-order a-first build'
$bFirst = Invoke-MqbCase $case3 @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'b', '-I', 'a', '-o', 'order')
Require-Success $bFirst 'include-order b-first build'
Require-Compile $bFirst 'main.cpp' 'include-order b-first build'
Require-ProgramExit (Program-Path $case3 'order') 4 'include-order b-first build'

# 4a. Quoted lookup prefers the including source directory over /I.
$case4 = New-CaseDirectory 'quoted-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case4 'low') | Out-Null
Write-Utf8 (Join-Path $case4 'low/pick.hpp') @('inline int selected_value() { return 5; }')
Write-Utf8 (Join-Path $case4 'main.cpp') @('#include "pick.hpp"', 'int main() { return selected_value(); }')
$args4 = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'low', '-o', 'quoted')
$cold4 = Invoke-MqbCase $case4 $args4
Require-Success $cold4 'quoted cold build'
Require-ProgramExit (Program-Path $case4 'quoted') 5 'quoted cold build'
$warm4 = Invoke-MqbCase $case4 $args4
Require-Success $warm4 'quoted warm build'
Require-UpToDate $warm4 'main.cpp' 'quoted warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case4 'pick.hpp') @('inline int selected_value() { return 6; }')
$shadow4 = Invoke-MqbCase $case4 $args4
Require-Success $shadow4 'quoted source-directory shadow build'
Require-Compile $shadow4 'main.cpp' 'quoted source-directory shadow build'
Require-ProgramExit (Program-Path $case4 'quoted') 6 'quoted source-directory shadow build'

# 4b. Nested quote search: candidate subdirectory exists before cold build, so
# only that nested namespace changes when the shadow file appears.
$case4b = New-CaseDirectory 'nested-quoted-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case4b 'src/nested') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case4b 'low/nested') | Out-Null
Write-Utf8 (Join-Path $case4b 'low/nested/pick.hpp') @('inline int nested_value() { return 51; }')
Write-Utf8 (Join-Path $case4b 'src/outer.hpp') @('#pragma once', '#include "nested/pick.hpp"', 'inline int outer_value() { return nested_value(); }')
Write-Utf8 (Join-Path $case4b 'main.cpp') @('#include "src/outer.hpp"', 'int main() { return outer_value(); }')
$args4b = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '-I', 'low', '-o', 'nested-quoted')
$cold4b = Invoke-MqbCase $case4b $args4b
Require-Success $cold4b 'nested quoted cold build'
Require-ProgramExit (Program-Path $case4b 'nested-quoted') 51 'nested quoted cold build'
$warm4b = Invoke-MqbCase $case4b $args4b
Require-Success $warm4b 'nested quoted warm build'
Require-UpToDate $warm4b 'main.cpp' 'nested quoted warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case4b 'src/nested/pick.hpp') @('inline int nested_value() { return 52; }')
$shadow4b = Invoke-MqbCase $case4b $args4b
Require-Success $shadow4b 'nested quoted header-directory shadow build'
Require-Compile $shadow4b 'main.cpp' 'nested quoted header-directory shadow build'
Require-ProgramExit (Program-Path $case4b 'nested-quoted') 52 'nested quoted header-directory shadow build'

# Native MSVC /I passthrough participates in the same model.
$case5 = New-CaseDirectory 'native-angle-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case5 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case5 'low') | Out-Null
Write-Utf8 (Join-Path $case5 'low/pick.hpp') @('inline int selected_value() { return 11; }')
Write-Utf8 (Join-Path $case5 'main.cpp') @('#include <pick.hpp>', 'int main() { return selected_value(); }')
$args5 = @('main.cpp', '--release', '--no-discover', '--env', 'vs', '/Ihigh', '/Ilow', '-o', 'native-angle')
$cold5 = Invoke-MqbCase $case5 $args5
Require-Success $cold5 'native /I angle cold build'
Require-ProgramExit (Program-Path $case5 'native-angle') 11 'native /I angle cold build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case5 'high/pick.hpp') @('inline int selected_value() { return 12; }')
$shadow5 = Invoke-MqbCase $case5 $args5
Require-Success $shadow5 'native /I angle shadow build'
Require-Compile $shadow5 'main.cpp' 'native /I angle shadow build'
Require-ProgramExit (Program-Path $case5 'native-angle') 12 'native /I angle shadow build'

# 6 + 8. P1689 scan reuse must stay process-free on an unchanged build and
# invalidate when a higher-priority header appears.
$case6 = New-CaseDirectory 'p1689-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case6 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case6 'low') | Out-Null
Write-Utf8 (Join-Path $case6 'low/config.hpp') @('#define MQB_VALUE 21')
Write-Utf8 (Join-Path $case6 'math.ixx') @('module;', '#include <config.hpp>', 'export module math;', 'export int module_value() { return MQB_VALUE; }')
Write-Utf8 (Join-Path $case6 'main.cpp') @('import math;', 'int main() { return module_value(); }')
$args6 = @('main.cpp', 'math.ixx', '--release', '--no-discover', '--env', 'vs', '--std', 'latest', '-I', 'high', '-I', 'low', '-o', 'p1689')
$cold6 = Invoke-MqbCase $case6 $args6
Require-Success $cold6 'P1689 cold build'
Require-ProgramExit (Program-Path $case6 'p1689') 21 'P1689 cold build'
$scanFile = Join-Path $case6 '.mqb/scan/math.ixx.json'
if (-not (Test-Path -LiteralPath $scanFile -PathType Leaf)) { throw ('P1689 scan artifact missing: {0}' -f $scanFile) }
$scanColdTime = (Get-Item -LiteralPath $scanFile).LastWriteTimeUtc
$warm6 = Invoke-MqbCase $case6 $args6
Require-Success $warm6 'P1689 warm no-op build'
Require-UpToDate $warm6 'math.ixx' 'P1689 warm no-op build'
Require-UpToDate $warm6 'main.cpp' 'P1689 warm no-op build'
$scanWarmTime = (Get-Item -LiteralPath $scanFile).LastWriteTimeUtc
if ($scanWarmTime -ne $scanColdTime) { throw 'unchanged modules no-op rewrote P1689 scan metadata' }
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case6 'high/config.hpp') @('#define MQB_VALUE 22')
$shadow6 = Invoke-MqbCase $case6 $args6
Require-Success $shadow6 'P1689 higher-priority shadow build'
Require-Compile $shadow6 'math.ixx' 'P1689 higher-priority shadow build'
$scanShadowTime = (Get-Item -LiteralPath $scanFile).LastWriteTimeUtc
if ($scanShadowTime -le $scanWarmTime) { throw 'higher-priority module header did not refresh P1689 scan metadata' }
Require-ProgramExit (Program-Path $case6 'p1689') 22 'P1689 higher-priority shadow build'

# 7. Header Unit resolution must move to the newly higher-priority header.
$case7 = New-CaseDirectory 'header-unit-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $case7 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $case7 'low') | Out-Null
Write-Utf8 (Join-Path $case7 'low/util.hpp') @('inline int header_value() { return 31; }')
Write-Utf8 (Join-Path $case7 'main.cpp') @('import <util.hpp>;', 'int main() { return header_value(); }')
$args7 = @('main.cpp', '--release', '--env', 'vs', '--std', 'latest', '-I', 'high', '-I', 'low', '-o', 'header-unit-shadow')
$cold7 = Invoke-MqbCase $case7 $args7
Require-Success $cold7 'header-unit cold build'
Require-ProgramExit (Program-Path $case7 'header-unit-shadow') 31 'header-unit cold build'
$warm7 = Invoke-MqbCase $case7 $args7
Require-Success $warm7 'header-unit warm build'
Require-UpToDate $warm7 'main.cpp' 'header-unit warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $case7 'high/util.hpp') @('inline int header_value() { return 32; }')
$shadow7 = Invoke-MqbCase $case7 $args7
Require-Success $shadow7 'header-unit higher-priority shadow build'
Require-Compile $shadow7 'main.cpp' 'header-unit higher-priority shadow build'
Require-ProgramExit (Program-Path $case7 'header-unit-shadow') 32 'header-unit higher-priority shadow build'

# PCH creator + consumer: unchanged creator remains warm, transitive search
# reroute rebuilds the PCH and propagates to its consumer.
$casePch = New-CaseDirectory 'pch-shadow'
New-Item -ItemType Directory -Force -Path (Join-Path $casePch 'high') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $casePch 'low') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $casePch 'include') | Out-Null
Write-Utf8 (Join-Path $casePch 'low/pick.hpp') @('inline int pch_selected_value() { return 41; }')
Write-Utf8 (Join-Path $casePch 'include/pch.hpp') @('#pragma once', '#include <pick.hpp>')
Write-Utf8 (Join-Path $casePch 'main.cpp') @('int main() { return pch_selected_value(); }')
$argsPch = @('build', 'main.cpp', '--release', '--no-discover', '--env', 'vs', '--pch', 'include/pch.hpp', '-I', 'high', '-I', 'low', '-o', 'pch-shadow')
$coldPch = Invoke-MqbCase $casePch $argsPch
Require-Success $coldPch 'PCH include-search cold build'
Require-ProgramExit (Program-Path $casePch 'pch-shadow') 41 'PCH include-search cold build'
$warmPch = Invoke-MqbCase $casePch $argsPch
Require-Success $warmPch 'PCH include-search warm build'
if ($warmPch.Text -notmatch '\[up-to-date\]\s+pch') { throw ('unchanged PCH creator did not remain warm:{0}{1}' -f [Environment]::NewLine, $warmPch.Text) }
Require-UpToDate $warmPch 'main.cpp' 'PCH include-search warm build'
Start-Sleep -Milliseconds 100
Write-Utf8 (Join-Path $casePch 'high/pick.hpp') @('inline int pch_selected_value() { return 42; }')
$shadowPch = Invoke-MqbCase $casePch $argsPch
Require-Success $shadowPch 'PCH higher-priority transitive shadow build'
if ($shadowPch.Text -notmatch '\[pch\]') { throw ('PCH transitive search reroute did not rebuild the creator:{0}{1}' -f [Environment]::NewLine, $shadowPch.Text) }
Require-Compile $shadowPch 'main.cpp' 'PCH higher-priority transitive shadow build'
Require-ProgramExit (Program-Path $casePch 'pch-shadow') 42 'PCH higher-priority transitive shadow build'

Write-Host 'Include search resolution freshness checks passed: typed/native /I, removal, order, direct/nested quote and angle shadowing, P1689 reuse, header units, PCH creator/consumer, and zero-process no-op behavior.'
exit 0

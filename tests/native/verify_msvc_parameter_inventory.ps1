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

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

$MqbPath = Get-FullPath $MqbPath
$RepoRoot = Get-FullPath $RepoRoot
$SharedProductLibraryPath = Get-FullPath $SharedProductLibraryPath
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $RepoRoot 'native-out/msvc-parameter-inventory.tsv'
}
else {
    $OutputPath = Get-FullPath $OutputPath
}
foreach ($path in @($MqbPath, $SharedProductLibraryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required inventory input not found: $path"
    }
}

# These are immutable Git blob snapshots, not moving branch URLs. Updating an
# official option snapshot is therefore an explicit repository change with a
# reviewable expected-count change.
$snapshots = [ordered]@{
    compiler = [pscustomobject]@{
        Date = '2026-05-25'
        Blob = '8e2d8e81772e49c9a80cdc1fd73bcf1550ede90c'
        Path = 'docs/build/reference/compiler-options-listed-alphabetically.md'
    }
    linker = [pscustomobject]@{
        Date = '2025-03-14'
        Blob = 'a647917101a47610e662d1c23583505500d8bf96'
        Path = 'docs/build/reference/linker-options.md'
    }
    linker_debug = [pscustomobject]@{
        Date = '2025-09-08'
        Blob = '9716f477b5689e114a66faec7d40461fa33cfd79'
        Path = 'docs/build/reference/debug-generate-debug-info.md'
    }
    librarian = [pscustomobject]@{
        Date = '2020-02-09'
        Blob = '56b1a94bc78d89adee574dd009efd4bf12651fb8'
        Path = 'docs/build/reference/overview-of-lib.md'
    }
}
$expectedCounts = [ordered]@{
    compiler = 309
    linker = 114
    librarian = 21
}

function Get-GitHubBlobText {
    param([Parameter(Mandatory = $true)][string]$Blob)

    if ($Blob -notmatch '^[0-9a-f]{40}$') {
        throw "Invalid MicrosoftDocs blob SHA: $Blob"
    }
    $headers = @{
        Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = 'mqb-msvc-parameter-inventory'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GH_TOKEN)) {
        $headers.Authorization = "Bearer $($env:GH_TOKEN)"
    }
    $uri = "https://api.github.com/repos/MicrosoftDocs/cpp-docs/git/blobs/$Blob"
    $response = Invoke-RestMethod -Uri $uri -Headers $headers
    if ([string]$response.encoding -ne 'base64') {
        throw "Unexpected GitHub blob encoding for $Blob: $($response.encoding)"
    }
    $bytes = [Convert]::FromBase64String(([string]$response.content -replace '\s', ''))
    return [Text.Encoding]::UTF8.GetString($bytes)
}

function Assert-SnapshotDate {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$ExpectedDate
    )
    $match = [regex]::Match($Text, '(?m)^ms\.date:\s*["'']?(?<date>\d{2}/\d{2}/\d{4}|\d{4}-\d{2}-\d{2})["'']?\s*$')
    if (-not $match.Success) {
        throw "Snapshot '$Name' has no parseable ms.date"
    }
    $raw = $match.Groups['date'].Value
    $actual = if ($raw -match '^\d{2}/') {
        [DateTime]::ParseExact($raw, 'MM/dd/yyyy', [Globalization.CultureInfo]::InvariantCulture).ToString('yyyy-MM-dd')
    }
    else { $raw }
    if ($actual -ne $ExpectedDate) {
        throw "Snapshot '$Name' date drift: expected $ExpectedDate, got $actual"
    }
}

function Get-TableOptionExpressions {
    param([Parameter(Mandatory = $true)][string]$Markdown)

    $result = [System.Collections.Generic.List[string]]::new()
    foreach ($line in ($Markdown -split "`r?`n")) {
        if ($line -notmatch '^\|') { continue }
        $parts = $line -split ' \| ', 2
        if ($parts.Count -lt 2) { continue }
        $cell = [string]$parts[0]
        if ($cell -match '^\|\s*Option\s*$' -or $cell -match '^\|[-: ]+$') { continue }

        $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($match in [regex]::Matches($cell, '`(?<value>[^`]+)`')) {
            $value = $match.Groups['value'].Value.Trim()
            if (($value.StartsWith('/') -or $value -eq '@') -and $seen.Add($value)) {
                $result.Add($value)
            }
        }
        foreach ($match in [regex]::Matches($cell, '\*\*(?<value>/[^*]+)\*\*')) {
            $value = $match.Groups['value'].Value.Trim()
            if ($seen.Add($value)) { $result.Add($value) }
        }
    }
    return @($result)
}

function Expand-CompilerExpression {
    param([Parameter(Mandatory = $true)][string]$Expression)

    if ($Expression.EndsWith('[-]')) {
        $base = $Expression.Substring(0, $Expression.Length - 3)
        return @($base, $base + '-')
    }
    if ($Expression -match '^/favor:<(?<values>[^>]+)>$') {
        return @($matches['values'].Split('|') | ForEach-Object { '/favor:' + $_ })
    }
    if ($Expression -match '^/vd\{(?<values>[^}]+)\}$') {
        return @($matches['values'].Split('|') | ForEach-Object { '/vd' + $_ })
    }
    if ($Expression -match '^/ZH:\[(?<values>[^]]+)\]$') {
        return @($matches['values'].Split('|') | ForEach-Object { '/ZH:' + $_ })
    }
    return @($Expression)
}

$compilerText = Get-GitHubBlobText -Blob $snapshots.compiler.Blob
$linkerText = Get-GitHubBlobText -Blob $snapshots.linker.Blob
$linkerDebugText = Get-GitHubBlobText -Blob $snapshots.linker_debug.Blob
$librarianText = Get-GitHubBlobText -Blob $snapshots.librarian.Blob
Assert-SnapshotDate -Name 'compiler' -Text $compilerText -ExpectedDate $snapshots.compiler.Date
Assert-SnapshotDate -Name 'linker' -Text $linkerText -ExpectedDate $snapshots.linker.Date
Assert-SnapshotDate -Name 'linker-debug' -Text $linkerDebugText -ExpectedDate $snapshots.linker_debug.Date
Assert-SnapshotDate -Name 'librarian' -Text $librarianText -ExpectedDate $snapshots.librarian.Date

$inventory = [System.Collections.Generic.List[object]]::new()
foreach ($expression in (Get-TableOptionExpressions -Markdown $compilerText)) {
    foreach ($canonical in (Expand-CompilerExpression -Expression $expression)) {
        $inventory.Add([pscustomobject]@{ Tool = 'compiler'; Canonical = $canonical })
    }
}
foreach ($canonical in (Get-TableOptionExpressions -Markdown $linkerText)) {
    $inventory.Add([pscustomobject]@{ Tool = 'linker'; Canonical = $canonical })
}
# /DEBUG's alphabetical row is one family, while its syntax page has three
# named modes. Keep each mode independently visible so lifecycle changes cannot
# hide behind the family probe.
foreach ($canonical in @('/DEBUG:FULL', '/DEBUG:NONE', '/DEBUG:FASTLINK')) {
    $inventory.Add([pscustomobject]@{ Tool = 'linker'; Canonical = $canonical })
}
foreach ($canonical in (Get-TableOptionExpressions -Markdown $librarianText)) {
    $inventory.Add([pscustomobject]@{ Tool = 'librarian'; Canonical = $canonical })
}
# Running LIB documents command files and the /WX:NO spelling outside the
# overview table. They are independent canonical command-line forms.
$inventory.Add([pscustomobject]@{ Tool = 'librarian'; Canonical = '@' })
$inventory.Add([pscustomobject]@{ Tool = 'librarian'; Canonical = '/WX:NO' })

$keys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($entry in $inventory) {
    $key = "$($entry.Tool)`t$($entry.Canonical)"
    if (-not $keys.Add($key)) { throw "Duplicate exact inventory entry: $key" }
}
foreach ($tool in $expectedCounts.Keys) {
    $actual = @($inventory | Where-Object Tool -eq $tool).Count
    if ($actual -ne [int]$expectedCounts[$tool]) {
        throw "Official $tool inventory count drift: expected $($expectedCounts[$tool]), got $actual"
    }
}

function ConvertTo-CppString {
    param([Parameter(Mandatory = $true)][string]$Value)
    return '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"'
}

$workRoot = Join-Path $RepoRoot 'native-test-work/msvc-parameter-inventory'
if (Test-Path -LiteralPath $workRoot) { Remove-Item -LiteralPath $workRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
$sourcePath = Join-Path $workRoot 'msvc_parameter_inventory.cpp'
$source = [System.Collections.Generic.List[string]]::new()
$source.Add('#include <iostream>')
$source.Add('#include <string>')
$source.Add('#include <string_view>')
$source.Add('#include "mqb/msvc/MsvcParameterEngine.hpp"')
$source.Add('namespace {')
$source.Add('constexpr std::string_view unregistered = "not present in MQB''s current MSVC parameter registry";')
$source.Add('const char* ownership_name(mqb::msvc::ParameterOwnership value) { switch (value) { case mqb::msvc::ParameterOwnership::mqb_owned: return "mqb_owned"; case mqb::msvc::ParameterOwnership::semantic: return "semantic"; case mqb::msvc::ParameterOwnership::passthrough: return "passthrough"; case mqb::msvc::ParameterOwnership::unsupported: return "unsupported"; } return "invalid"; }')
$source.Add('const char* tool_name(mqb::msvc::ParameterTool value) { switch (value) { case mqb::msvc::ParameterTool::compiler: return "compiler"; case mqb::msvc::ParameterTool::linker: return "linker"; case mqb::msvc::ParameterTool::librarian: return "librarian"; } return "invalid"; }')
$source.Add('bool registered(const mqb::msvc::ParameterClassification& value) { return value.rationale.find(unregistered) == std::string::npos; }')
$source.Add('int failures = 0;')
$source.Add('void verify(mqb::msvc::ParameterTool tool, std::string_view canonical) { std::string probe{canonical}; auto c = mqb::msvc::MsvcParameterEngine::classify(tool, probe); if (!registered(c)) { probe += ":mqb_probe"; c = mqb::msvc::MsvcParameterEngine::classify(tool, probe); } if (!registered(c)) { ++failures; std::cerr << "UNREGISTERED\t" << tool_name(tool) << "\t" << canonical << "\n"; return; } std::cout << tool_name(tool) << "\t" << canonical << "\t" << probe << "\t" << ownership_name(c.ownership) << "\n"; }')
$source.Add('void expect_ownership(mqb::msvc::ParameterTool tool, std::string_view option, mqb::msvc::ParameterOwnership expected) { const auto c = mqb::msvc::MsvcParameterEngine::classify(tool, option); if (!registered(c) || c.ownership != expected) { ++failures; std::cerr << "OWNERSHIP\t" << tool_name(tool) << "\t" << option << "\texpected=" << ownership_name(expected) << "\tactual=" << ownership_name(c.ownership) << "\n"; } }')
$source.Add('}')
$source.Add('int main() {')
$source.Add('std::cout << "tool\tcanonical\tprobe\townership\n";')
foreach ($entry in $inventory) {
    $tool = "mqb::msvc::ParameterTool::$($entry.Tool)"
    $canonical = ConvertTo-CppString -Value ([string]$entry.Canonical)
    $source.Add("verify($tool, $canonical);")
}
$source.Add('expect_ownership(mqb::msvc::ParameterTool::linker, "/DEBUG:NONE", mqb::msvc::ParameterOwnership::passthrough);')
$source.Add('expect_ownership(mqb::msvc::ParameterTool::compiler, "/Zc:trigraphs", mqb::msvc::ParameterOwnership::unsupported);')
$source.Add('if (failures != 0) { std::cerr << failures << " exact MSVC inventory failure(s)\n"; return 1; }')
$source.Add('return 0;')
$source.Add('}')
$source | Set-Content -LiteralPath $sourcePath -Encoding utf8

$config = Get-Content -LiteralPath (Join-Path $RepoRoot 'cpp/mqb.json') -Raw | ConvertFrom-Json
$arguments = [System.Collections.ArrayList]::new()
[void]$arguments.Add([System.IO.Path]::GetRelativePath($RepoRoot, $sourcePath).Replace('\', '/'))
[void]$arguments.Add('--env'); [void]$arguments.Add('vs')
[void]$arguments.Add($(if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }))
[void]$arguments.Add('--std'); [void]$arguments.Add([string]$config.build.standard)
[void]$arguments.Add('--runtime'); [void]$arguments.Add($(if ($Configuration -eq 'Debug') { 'MTd' } else { 'MT' }))
foreach ($include in @($config.build.include_dirs)) {
    [void]$arguments.Add('-I'); [void]$arguments.Add('cpp/' + ([string]$include).Replace('\', '/'))
}
foreach ($compilerArg in @($config.build.compiler_args)) {
    [void]$arguments.Add('--compiler-arg'); [void]$arguments.Add([string]$compilerArg)
}
[void]$arguments.Add('-D'); [void]$arguments.Add('MQB_VERSION="native-tests"')
[void]$arguments.Add('--no-discover')
[void]$arguments.Add('--lib'); [void]$arguments.Add($SharedProductLibraryPath)
[void]$arguments.Add('--linker-arg'); [void]$arguments.Add("/WHOLEARCHIVE:$SharedProductLibraryPath")
[void]$arguments.Add('--lib'); [void]$arguments.Add('shell32.lib')
[void]$arguments.Add('-o'); [void]$arguments.Add('native_msvc_parameter_inventory')

Push-Location $RepoRoot
try {
    $buildOutput = @(& $MqbPath @arguments 2>&1)
    $buildExit = $LASTEXITCODE
    foreach ($line in $buildOutput) { Write-Host $line }
}
finally { Pop-Location }
if ($buildExit -ne 0) { throw "Failed to build exact MSVC inventory probe (exit $buildExit)" }

$exe = Join-Path $RepoRoot '.mqb/bin/native_msvc_parameter_inventory.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Inventory probe executable missing: $exe" }
$matrix = @(& $exe 2>&1)
$probeExit = $LASTEXITCODE
foreach ($line in $matrix) { Write-Host $line }
if ($probeExit -ne 0) { throw "Exact MSVC inventory probe failed (exit $probeExit)" }

$parent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$matrix | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Exact MSVC parameter inventory: compiler=$($expectedCounts.compiler), linker=$($expectedCounts.linker), librarian=$($expectedCounts.librarian), total=$($inventory.Count)"
Write-Host "Coverage matrix: $OutputPath"

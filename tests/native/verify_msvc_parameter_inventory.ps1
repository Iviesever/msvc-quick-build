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

function Full([string]$Path) { [System.IO.Path]::GetFullPath($Path) }
$MqbPath = Full $MqbPath
$RepoRoot = Full $RepoRoot
$SharedProductLibraryPath = Full $SharedProductLibraryPath
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $RepoRoot 'native-out/msvc-parameter-inventory.tsv'
} else { $OutputPath = Full $OutputPath }
foreach ($path in @($MqbPath, $SharedProductLibraryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required inventory input not found: $path" }
}

# Immutable MicrosoftDocs Git blobs. A documentation refresh is an explicit,
# reviewable change instead of silently changing the CI denominator.
$snapshots = [ordered]@{
    compiler = @('2026-05-25', '8e2d8e81772e49c9a80cdc1fd73bcf1550ede90c')
    linker = @('2025-03-14', 'a647917101a47610e662d1c23583505500d8bf96')
    linker_debug = @('2025-09-08', '9716f477b5689e114a66faec7d40461fa33cfd79')
    librarian = @('2020-02-09', '56b1a94bc78d89adee574dd009efd4bf12651fb8')
}
$expectedCounts = [ordered]@{ compiler = 309; linker = 114; librarian = 21 }

function BlobText([string]$Blob) {
    if ($Blob -notmatch '^[0-9a-f]{40}$') { throw "Invalid MicrosoftDocs blob SHA: $Blob" }
    $headers = @{
        Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = 'mqb-msvc-parameter-inventory'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GH_TOKEN)) { $headers.Authorization = "Bearer $($env:GH_TOKEN)" }
    $response = Invoke-RestMethod -Uri "https://api.github.com/repos/MicrosoftDocs/cpp-docs/git/blobs/$Blob" -Headers $headers
    if ([string]$response.encoding -ne 'base64') { throw "Unexpected GitHub blob encoding for $Blob" }
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(([string]$response.content -replace '\s', '')))
}

function AssertDate([string]$Name, [string]$Text, [string]$Expected) {
    $m = [regex]::Match($Text, '(?m)^ms\.date:\s*["'']?(?<d>\d{2}/\d{2}/\d{4}|\d{4}-\d{2}-\d{2})["'']?\s*$')
    if (-not $m.Success) { throw "Snapshot '$Name' has no parseable ms.date" }
    $raw = $m.Groups['d'].Value
    $actual = if ($raw -match '^\d{2}/') {
        [DateTime]::ParseExact($raw, 'MM/dd/yyyy', [Globalization.CultureInfo]::InvariantCulture).ToString('yyyy-MM-dd')
    } else { $raw }
    if ($actual -ne $Expected) { throw "Snapshot '$Name' date drift: expected $Expected, got $actual" }
}

function OptionExpressions([string]$Markdown) {
    $result = [System.Collections.Generic.List[string]]::new()
    foreach ($line in ($Markdown -split "`r?`n")) {
        if ($line -notmatch '^\|') { continue }
        $cell = $null
        # Compiler/LINK tables use spaced separators. LIB's overview table uses
        # compact |**/OPTION**|Description| separators.
        $spaced = $line -split ' \| ', 2
        if ($spaced.Count -ge 2) { $cell = [string]$spaced[0] }
        elseif ($line -match '^\|(?<cell>\*\*/[^*]+\*\*)\|') { $cell = $matches['cell'] }
        if ($null -eq $cell -or $cell -match '^\|\s*Option\s*$') { continue }

        $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($m in [regex]::Matches($cell, '`(?<v>[^`]+)`')) {
            $v = $m.Groups['v'].Value.Trim()
            if (($v.StartsWith('/') -or $v -eq '@') -and $seen.Add($v)) { $result.Add($v) }
        }
        foreach ($m in [regex]::Matches($cell, '\*\*(?<v>/[^*]+)\*\*')) {
            $v = $m.Groups['v'].Value.Trim()
            if ($seen.Add($v)) { $result.Add($v) }
        }
    }
    @($result)
}

function ExpandCompiler([string]$Expression) {
    if ($Expression.EndsWith('[-]')) {
        $base = $Expression.Substring(0, $Expression.Length - 3)
        return @($base, ($base + '-'))
    }
    if ($Expression -match '^/favor:<(?<v>[^>]+)>$') {
        return @($matches['v'].Split('|') | ForEach-Object { '/favor:' + $_ })
    }
    if ($Expression -match '^/vd\{(?<v>[^}]+)\}$') {
        return @($matches['v'].Split('|') | ForEach-Object { '/vd' + $_ })
    }
    if ($Expression -match '^/ZH:\[(?<v>[^]]+)\]$') {
        return @($matches['v'].Split('|') | ForEach-Object { '/ZH:' + $_ })
    }
    @($Expression)
}

$compilerText = BlobText $snapshots.compiler[1]
$linkerText = BlobText $snapshots.linker[1]
$linkerDebugText = BlobText $snapshots.linker_debug[1]
$librarianText = BlobText $snapshots.librarian[1]
AssertDate 'compiler' $compilerText $snapshots.compiler[0]
AssertDate 'linker' $linkerText $snapshots.linker[0]
AssertDate 'linker-debug' $linkerDebugText $snapshots.linker_debug[0]
AssertDate 'librarian' $librarianText $snapshots.librarian[0]

# Collapse only identical canonical spellings after expansion. Distinct enabled
# and disabled spellings produced by `[-]` remain independent denominator rows.
$inventory = [System.Collections.Generic.List[object]]::new()
$canonicalKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
function AddCanonical([string]$Tool, [string]$Canonical) {
    $key = "$Tool`t$Canonical"
    if ($canonicalKeys.Add($key)) {
        $inventory.Add([pscustomobject]@{ Tool=$Tool; Canonical=$Canonical })
    }
}

foreach ($expression in (OptionExpressions $compilerText)) {
    foreach ($canonical in (ExpandCompiler $expression)) { AddCanonical 'compiler' $canonical }
}
foreach ($canonical in (OptionExpressions $linkerText)) { AddCanonical 'linker' $canonical }
# The alphabetical LINK table has one /DEBUG family row. Its syntax page names
# the modes independently, including the VS-2026-removed FASTLINK lifecycle case.
foreach ($canonical in @('/DEBUG:FULL', '/DEBUG:NONE', '/DEBUG:FASTLINK')) { AddCanonical 'linker' $canonical }
foreach ($canonical in (OptionExpressions $librarianText)) { AddCanonical 'librarian' $canonical }
# Running LIB documents these outside the overview table.
AddCanonical 'librarian' '@'
AddCanonical 'librarian' '/WX:NO'

$countFailures = [System.Collections.Generic.List[string]]::new()
foreach ($tool in $expectedCounts.Keys) {
    $actual = @($inventory | Where-Object Tool -eq $tool).Count
    Write-Host "Official $tool canonical inventory: $actual"
    if ($actual -ne [int]$expectedCounts[$tool]) {
        $countFailures.Add("$tool expected $($expectedCounts[$tool]), got $actual")
    }
}
if ($countFailures.Count -ne 0) {
    throw "Official inventory count drift: $($countFailures -join '; ')"
}

function ConvertToCppString([string]$Value) { '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"' }
$work = Join-Path $RepoRoot 'native-test-work/msvc-parameter-inventory'
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$sourcePath = Join-Path $work 'msvc_parameter_inventory.cpp'
$source = [System.Collections.Generic.List[string]]::new()
$source.Add('#include <iostream>')
$source.Add('#include <string>')
$source.Add('#include <string_view>')
$source.Add('#include "mqb/msvc/MsvcParameterEngine.hpp"')
$source.Add('namespace {')
$source.Add('constexpr std::string_view missing = "not present in MQB''s current MSVC parameter registry";')
$source.Add('const char* own(mqb::msvc::ParameterOwnership v) { switch(v) { case mqb::msvc::ParameterOwnership::mqb_owned:return "mqb_owned"; case mqb::msvc::ParameterOwnership::semantic:return "semantic"; case mqb::msvc::ParameterOwnership::passthrough:return "passthrough"; case mqb::msvc::ParameterOwnership::unsupported:return "unsupported"; } return "invalid"; }')
$source.Add('const char* tool(mqb::msvc::ParameterTool v) { switch(v) { case mqb::msvc::ParameterTool::compiler:return "compiler"; case mqb::msvc::ParameterTool::linker:return "linker"; case mqb::msvc::ParameterTool::librarian:return "librarian"; } return "invalid"; }')
$source.Add('bool registered(const mqb::msvc::ParameterClassification& c) { return c.rationale.find(missing) == std::string::npos; }')
$source.Add('int failures = 0;')
# Classify the exact canonical spelling first. Families whose documented syntax
# requires a colon payload receive one neutral probe only if the bare family is
# unregistered. This never rescues exact variants such as /DEBUG:NONE because
# appending another colon cannot match an exact registry entry.
$source.Add('void verify(mqb::msvc::ParameterTool t, std::string_view canonical) { std::string probe{canonical}; auto c=mqb::msvc::MsvcParameterEngine::classify(t,probe); if(!registered(c)){ probe += ":mqb_probe"; c=mqb::msvc::MsvcParameterEngine::classify(t,probe); } if(!registered(c)){ ++failures; std::cerr<<"UNREGISTERED\t"<<tool(t)<<"\t"<<canonical<<"\n"; return; } std::cout<<tool(t)<<"\t"<<canonical<<"\t"<<probe<<"\t"<<own(c.ownership)<<"\n"; }')
$source.Add('void expect(mqb::msvc::ParameterTool t,std::string_view option,mqb::msvc::ParameterOwnership e){ auto c=mqb::msvc::MsvcParameterEngine::classify(t,option); if(!registered(c)||c.ownership!=e){++failures;std::cerr<<"OWNERSHIP\t"<<tool(t)<<"\t"<<option<<"\texpected="<<own(e)<<"\tactual="<<own(c.ownership)<<"\n";} }')
$source.Add('}')
$source.Add('int main(){ std::cout<<"tool\tcanonical\tprobe\townership\n";')
foreach ($entry in $inventory) {
    $source.Add("verify(mqb::msvc::ParameterTool::$($entry.Tool), $(ConvertToCppString ([string]$entry.Canonical)));")
}
$source.Add('expect(mqb::msvc::ParameterTool::linker,"/DEBUG:NONE",mqb::msvc::ParameterOwnership::passthrough);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/Zc:trigraphs",mqb::msvc::ParameterOwnership::unsupported);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/std:c11",mqb::msvc::ParameterOwnership::passthrough);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/std:c17",mqb::msvc::ParameterOwnership::passthrough);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/std:clatest",mqb::msvc::ParameterOwnership::passthrough);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/Z7",mqb::msvc::ParameterOwnership::passthrough);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/Zi",mqb::msvc::ParameterOwnership::unsupported);')
$source.Add('expect(mqb::msvc::ParameterTool::compiler,"/ZI",mqb::msvc::ParameterOwnership::unsupported);')
$source.Add('if(failures){std::cerr<<failures<<" exact MSVC inventory failure(s)\n";return 1;} return 0;}')
$source | Set-Content -LiteralPath $sourcePath -Encoding utf8

$config = Get-Content -LiteralPath (Join-Path $RepoRoot 'cpp/mqb.json') -Raw | ConvertFrom-Json
$args = [System.Collections.ArrayList]::new()
[void]$args.Add([System.IO.Path]::GetRelativePath($RepoRoot, $sourcePath).Replace('\','/'))
[void]$args.Add('--env'); [void]$args.Add('vs')
[void]$args.Add($(if ($Configuration -eq 'Debug') {'--debug'} else {'--release'}))
[void]$args.Add('--std'); [void]$args.Add([string]$config.build.standard)
[void]$args.Add('--runtime'); [void]$args.Add($(if ($Configuration -eq 'Debug') {'MTd'} else {'MT'}))
foreach ($include in @($config.build.include_dirs)) { [void]$args.Add('-I'); [void]$args.Add('cpp/' + ([string]$include).Replace('\','/')) }
foreach ($compilerArg in @($config.build.compiler_args)) { [void]$args.Add('--compiler-arg'); [void]$args.Add([string]$compilerArg) }
[void]$args.Add('-D'); [void]$args.Add('MQB_VERSION="native-tests"')
[void]$args.Add('--no-discover')
[void]$args.Add('--lib'); [void]$args.Add($SharedProductLibraryPath)
[void]$args.Add('--linker-arg'); [void]$args.Add("/WHOLEARCHIVE:$SharedProductLibraryPath")
[void]$args.Add('--lib'); [void]$args.Add('shell32.lib')
[void]$args.Add('-o'); [void]$args.Add('native_msvc_parameter_inventory')

Push-Location $RepoRoot
try { $build = @(& $MqbPath @args 2>&1); $buildExit=$LASTEXITCODE; foreach($line in $build){Write-Host $line} }
finally { Pop-Location }
if ($buildExit -ne 0) { throw "Failed to build exact MSVC inventory probe (exit $buildExit)" }
$exe = Join-Path $RepoRoot '.mqb/bin/native_msvc_parameter_inventory.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Inventory probe executable missing: $exe" }
$matrix = @(& $exe 2>&1); $probeExit=$LASTEXITCODE
foreach ($line in $matrix) { Write-Host $line }
if ($probeExit -ne 0) { throw "Exact MSVC inventory probe failed (exit $probeExit)" }

$parent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$matrix | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Exact MSVC parameter inventory: compiler=$($expectedCounts.compiler), linker=$($expectedCounts.linker), librarian=$($expectedCounts.librarian), total=$($inventory.Count)"
Write-Host "Coverage matrix: $OutputPath"
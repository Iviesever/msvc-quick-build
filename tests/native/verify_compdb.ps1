[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) { throw $Message }
}

function Invoke-MqbCaptured {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Push-Location $WorkingDirectory
    try {
        $text = (& $MqbPath @Arguments 2>&1 | Out-String)
        return [PSCustomObject]@{
            ExitCode = $LASTEXITCODE
            Text = $text
        }
    }
    finally {
        Pop-Location
    }
}

function Assert-NoBuildArtifacts {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    foreach ($relative in @('.mqb/obj', '.mqb/deps', '.mqb/scan', '.mqb/ifc', '.mqb/bin', '.mqb/pch')) {
        $path = Join-Path $ProjectRoot $relative
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $files = @(Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction Stop)
        Assert-True ($files.Count -eq 0) "mqb compdb produced build artifact(s) under $relative"
    }
}

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
Assert-True (Test-Path -LiteralPath $MqbPath -PathType Leaf) "MQB executable not found: $MqbPath"

$root = Join-Path $RepoRoot 'native-out/compdb-evidence/编译 数据库'
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
New-Item -ItemType Directory -Path $root -Force | Out-Null

# Ordinary C++: exact argv, Unicode-safe JSON, custom output, deterministic bytes,
# and no compile/link/archive side effects.
$ordinary = Join-Path $root 'ordinary 项目'
New-Item -ItemType Directory -Path (Join-Path $ordinary 'include') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ordinary 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $ordinary 'include/value.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'int value();'
)
Set-Content -LiteralPath (Join-Path $ordinary 'src/主.cpp') -Encoding utf8 -Value @(
    '#include "value.hpp"',
    'int main() { return value() == 7 ? 0 : 1; }'
)
Set-Content -LiteralPath (Join-Path $ordinary 'src/value.cpp') -Encoding utf8 -Value @(
    '#include "value.hpp"',
    'int value() { return VALUE; }'
)

$ordinaryArgs = @(
    'compdb',
    'src/主.cpp',
    'src/value.cpp',
    '--no-discover',
    '--std', '23',
    '-I', 'include',
    '-D', 'VALUE=7',
    '--output', 'out/compile_commands.json'
)
$ordinaryRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $ordinaryArgs
Assert-True ($ordinaryRun.ExitCode -eq 0) "ordinary compdb failed:`n$($ordinaryRun.Text)"

$databasePath = Join-Path $ordinary 'out/compile_commands.json'
Assert-True (Test-Path -LiteralPath $databasePath -PathType Leaf) 'compile_commands.json was not created'
$database = @(Get-Content -LiteralPath $databasePath -Raw -Encoding utf8 | ConvertFrom-Json)
Assert-True ($database.Count -eq 2) "expected 2 compilation database entries, got $($database.Count)"

$files = @($database | ForEach-Object { [string]$_.file })
Assert-True ($files[0] -lt $files[1]) 'compilation database entries are not deterministic path-sorted output'
Assert-True (($files -join "`n") -match '主\.cpp') 'Unicode source path was not preserved in JSON'
foreach ($entry in $database) {
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.directory)) 'directory must be absolute'
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.file)) 'file must be absolute'
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.output)) 'output must be absolute'
    $arguments = @($entry.arguments | ForEach-Object { [string]$_ })
    Assert-True ($arguments.Count -gt 1) 'arguments array must include compiler plus compiler argv'
    Assert-True ([System.IO.Path]::GetFileName($arguments[0]).Equals('cl.exe', [System.StringComparison]::OrdinalIgnoreCase)) 'arguments[0] must be cl.exe'
    Assert-True (($arguments -join "`n") -match 'VALUE=7') 'typed define did not reach exact compiler argv'
    Assert-True (($arguments -join "`n") -match 'include') 'include directory did not reach exact compiler argv'
}
Assert-NoBuildArtifacts -ProjectRoot $ordinary

$firstHash = (Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash
$ordinaryRun2 = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $ordinaryArgs
Assert-True ($ordinaryRun2.ExitCode -eq 0) "second ordinary compdb failed:`n$($ordinaryRun2.Text)"
$secondHash = (Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash
Assert-True ($firstHash -eq $secondHash) 'identical compdb inputs did not produce byte-identical JSON'
Assert-NoBuildArtifacts -ProjectRoot $ordinary

# First-class PCH: compdb models consumer recipes without creating the synthetic
# creator source, .pch, creator object, or any consumer object.
$pch = Join-Path $root 'pch-project'
New-Item -ItemType Directory -Path (Join-Path $pch 'include') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $pch 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $pch 'include/pch.hpp') -Encoding utf8 -Value '#pragma once'
Set-Content -LiteralPath (Join-Path $pch 'src/main.cpp') -Encoding utf8 -Value 'int main() { return 0; }'
Set-Content -LiteralPath (Join-Path $pch 'src/helper.cpp') -Encoding utf8 -Value 'int helper() { return 1; }'
Set-Content -LiteralPath (Join-Path $pch 'mqb.json') -Encoding utf8 -Value @'
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp"
  }
}
'@

$pchRun = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments @(
    'compdb', 'src/main.cpp', 'src/helper.cpp', '--no-discover'
)
Assert-True ($pchRun.ExitCode -eq 0) "PCH compdb failed:`n$($pchRun.Text)"
$pchDatabasePath = Join-Path $pch 'compile_commands.json'
$pchDatabase = @(Get-Content -LiteralPath $pchDatabasePath -Raw -Encoding utf8 | ConvertFrom-Json)
Assert-True ($pchDatabase.Count -eq 2) 'PCH compdb should expose both consumer translation units'
foreach ($entry in $pchDatabase) {
    $joined = (@($entry.arguments | ForEach-Object { [string]$_ }) -join "`n")
    Assert-True ($joined -match '/Yu') 'PCH consumer recipe is missing /Yu'
    Assert-True ($joined -match '/Fp') 'PCH consumer recipe is missing /Fp'
    Assert-True ($joined -match '/FI') 'PCH consumer recipe is missing forced include policy'
}
Assert-NoBuildArtifacts -ProjectRoot $pch

# Modules/Header Units intentionally fail closed in this first user-facing slice.
$modules = Join-Path $root 'module-project'
New-Item -ItemType Directory -Path $modules -Force | Out-Null
Set-Content -LiteralPath (Join-Path $modules 'math.ixx') -Encoding utf8 -Value @(
    'export module math;',
    'export int answer() { return 42; }'
)
Set-Content -LiteralPath (Join-Path $modules 'main.cpp') -Encoding utf8 -Value @(
    'import math;',
    'int main() { return answer() == 42 ? 0 : 1; }'
)
$moduleRun = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments @(
    'compdb', 'main.cpp', 'math.ixx', '--no-discover', '--output', 'module.json'
)
Assert-True ($moduleRun.ExitCode -eq 2) "module compdb should fail closed with exit 2:`n$($moduleRun.Text)"
Assert-True ($moduleRun.Text -match 'Modules/Header Units') 'module fail-closed diagnostic is missing the unsupported boundary'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $modules 'module.json'))) 'module fail-closed path must not publish partial JSON'
Assert-NoBuildArtifacts -ProjectRoot $modules

Write-Host 'mqb compdb evidence passed'

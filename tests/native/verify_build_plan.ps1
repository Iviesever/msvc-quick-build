[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Invoke-MqbCaptured {
    param([string]$WorkingDirectory, [string[]]$Arguments)
    Push-Location $WorkingDirectory
    try {
        $text = (& $MqbPath @Arguments 2>&1 | Out-String)
        $exitCode = $LASTEXITCODE
        $global:LASTEXITCODE = 0
        return [PSCustomObject]@{ ExitCode = $exitCode; Text = $text }
    }
    finally { Pop-Location }
}

function Assert-NoProducedBuildArtifacts {
    param([string]$ProjectRoot)
    foreach ($relative in @('.mqb/obj', '.mqb/deps', '.mqb/bin', '.mqb/pch')) {
        $path = Join-Path $ProjectRoot $relative
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $files = @(Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction Stop)
        Assert-True ($files.Count -eq 0) "mqb plan produced build artifact(s) under $relative"
    }
}

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
Assert-True (Test-Path -LiteralPath $MqbPath -PathType Leaf) "MQB executable not found: $MqbPath"

$root = Join-Path $RepoRoot 'native-out/build-plan-evidence'
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
New-Item -ItemType Directory -Path $root -Force | Out-Null

# Ordinary executable: cold text/JSON plans predict two compiles + link without building.
$ordinary = Join-Path $root 'ordinary-plan'
New-Item -ItemType Directory -Path $ordinary -Force | Out-Null
Set-Content -LiteralPath (Join-Path $ordinary 'main.cpp') -Encoding utf8 -Value 'int helper(); int main() { return helper() == 7 ? 0 : 1; }'
Set-Content -LiteralPath (Join-Path $ordinary 'helper.cpp') -Encoding utf8 -Value 'int helper() { return 7; }'

$coldText = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'plan', 'main.cpp', 'helper.cpp', '--no-discover', '--debug', '-o', 'plan_app'
)
Assert-True ($coldText.ExitCode -eq 0) "cold text plan failed:`n$($coldText.Text)"
Assert-True (($coldText.Text | Select-String -Pattern '\[build\] compile' -AllMatches).Matches.Count -eq 2) 'cold text plan should contain two compile build phases'
Assert-True ($coldText.Text -match '\[build\] link plan_app\.exe') 'cold text plan should contain link build phase'
Assert-NoProducedBuildArtifacts -ProjectRoot $ordinary

$coldJsonRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'plan', 'main.cpp', 'helper.cpp', '--no-discover', '--debug', '-o', 'plan_app', '--format', 'json'
)
Assert-True ($coldJsonRun.ExitCode -eq 0) "cold JSON plan failed:`n$($coldJsonRun.Text)"
$coldJson = $coldJsonRun.Text | ConvertFrom-Json
Assert-True ($coldJson.target -eq 'plan_app') 'JSON plan target mismatch'
$compilePhases = @($coldJson.phases | Where-Object kind -eq 'compile')
$linkPhases = @($coldJson.phases | Where-Object kind -eq 'link')
Assert-True ($compilePhases.Count -eq 2) 'JSON plan should contain two compile phases'
Assert-True (@($compilePhases | Where-Object status -eq 'build').Count -eq 2) 'cold compile phases should be build'
Assert-True ($linkPhases.Count -eq 1 -and $linkPhases[0].status -eq 'build') 'cold link phase should be build'
foreach ($phase in $compilePhases) {
    Assert-True ($null -ne $phase.process) 'compile plan phase must expose exact process recipe'
    Assert-True ([System.IO.Path]::GetFileName([string]$phase.process.executable).Equals('cl.exe', [System.StringComparison]::OrdinalIgnoreCase)) 'compile plan process must use cl.exe'
    Assert-True (@($phase.process.arguments).Count -gt 1) 'compile plan must expose compiler argv'
}
Assert-NoProducedBuildArtifacts -ProjectRoot $ordinary

# Real build seals state; subsequent plan must be entirely up-to-date and remain read-only.
$build = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'build', 'main.cpp', 'helper.cpp', '--no-discover', '--debug', '-o', 'plan_app'
)
Assert-True ($build.ExitCode -eq 0) "ordinary build failed:`n$($build.Text)"
$exe = Join-Path $ordinary '.mqb/bin/plan_app.exe'
Assert-True (Test-Path -LiteralPath $exe -PathType Leaf) 'ordinary build did not produce executable'
$exeHashBefore = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$warm = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'plan', 'main.cpp', 'helper.cpp', '--no-discover', '--debug', '-o', 'plan_app', '--format=json'
)
Assert-True ($warm.ExitCode -eq 0) "warm JSON plan failed:`n$($warm.Text)"
$warmJson = $warm.Text | ConvertFrom-Json
Assert-True (@($warmJson.phases | Where-Object status -ne 'up-to-date').Count -eq 0) 'warm plan should report every phase up-to-date'
$exeHashAfter = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
Assert-True ($exeHashBefore -eq $exeHashAfter) 'warm plan mutated executable output'

# PCH: cold plan predicts PCH creator, both forced consumer compiles, and final relink.
$pch = Join-Path $root 'pch-plan'
New-Item -ItemType Directory -Path (Join-Path $pch 'include') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $pch 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $pch 'include/pch.hpp') -Encoding utf8 -Value '#pragma once`ninline constexpr int PCH_VALUE = 9;'
Set-Content -LiteralPath (Join-Path $pch 'src/main.cpp') -Encoding utf8 -Value 'int helper(); int main() { return helper() == PCH_VALUE ? 0 : 1; }'
Set-Content -LiteralPath (Join-Path $pch 'src/helper.cpp') -Encoding utf8 -Value 'int helper() { return PCH_VALUE; }'
Set-Content -LiteralPath (Join-Path $pch 'mqb.json') -Encoding utf8 -Value @'
{
  "version": 1,
  "build": {
    "entry": "src/main.cpp",
    "pch": "include/pch.hpp",
    "output": "pch_plan"
  }
}
'@
$pchColdRun = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments @(
    'plan', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug', '--format=json'
)
Assert-True ($pchColdRun.ExitCode -eq 0) "cold PCH plan failed:`n$($pchColdRun.Text)"
$pchCold = $pchColdRun.Text | ConvertFrom-Json
$pchPhase = @($pchCold.phases | Where-Object kind -eq 'pch')
$pchConsumers = @($pchCold.phases | Where-Object kind -eq 'compile')
$pchLink = @($pchCold.phases | Where-Object kind -eq 'link')
Assert-True ($pchPhase.Count -eq 1 -and $pchPhase[0].status -eq 'build') 'cold PCH plan should build creator'
Assert-True ($pchConsumers.Count -eq 2 -and @($pchConsumers | Where-Object status -eq 'build').Count -eq 2) 'cold PCH plan should force both consumers'
Assert-True ($pchLink.Count -eq 1 -and $pchLink[0].status -eq 'build') 'cold PCH plan should relink target'
Assert-True ((@($pchPhase[0].process.arguments) -join "`n") -match '/Yc') 'PCH plan recipe must expose /Yc'
Assert-NoProducedBuildArtifacts -ProjectRoot $pch

$pchBuild = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments @(
    'build', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug'
)
Assert-True ($pchBuild.ExitCode -eq 0) "PCH build failed:`n$($pchBuild.Text)"
$pchWarmRun = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments @(
    'plan', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug', '--format=json'
)
Assert-True ($pchWarmRun.ExitCode -eq 0) "warm PCH plan failed:`n$($pchWarmRun.Text)"
$pchWarm = $pchWarmRun.Text | ConvertFrom-Json
Assert-True (@($pchWarm.phases | Where-Object status -ne 'up-to-date').Count -eq 0) 'warm PCH plan should report all phases up-to-date'

# Explicit scope boundaries fail closed rather than producing misleading plans.
$staticRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'plan', 'main.cpp', 'helper.cpp', '--no-discover', '--type', 'static'
)
Assert-True ($staticRun.ExitCode -eq 2) 'static plan should fail closed with exit 2'
Assert-True ($staticRun.Text -match 'static-library') 'static fail-closed diagnostic missing'

$modules = Join-Path $root 'module-plan'
New-Item -ItemType Directory -Path $modules -Force | Out-Null
Set-Content -LiteralPath (Join-Path $modules 'math.ixx') -Encoding utf8 -Value 'export module math; export int answer() { return 42; }'
Set-Content -LiteralPath (Join-Path $modules 'main.cpp') -Encoding utf8 -Value 'import math; int main() { return answer() == 42 ? 0 : 1; }'
$moduleRun = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments @(
    'plan', 'main.cpp', 'math.ixx', '--no-discover'
)
Assert-True ($moduleRun.ExitCode -eq 2) 'module plan should fail closed with exit 2'
Assert-True ($moduleRun.Text -match 'Modules/Header Units') 'module fail-closed diagnostic missing'

Write-Host 'mqb build plan evidence passed'

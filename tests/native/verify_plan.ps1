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

function Read-PlanJson {
    param([object]$Run, [string]$Context)
    Assert-True ($Run.ExitCode -eq 0) "$Context failed:`n$($Run.Text)"
    try { return ($Run.Text | ConvertFrom-Json) }
    catch { throw "$Context did not emit valid JSON:`n$($Run.Text)" }
}

function Assert-NoMqbState {
    param([string]$ProjectRoot, [string]$Context)
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot '.mqb'))) `
        "$Context created project .mqb state"
}

function Get-StateFingerprint {
    param([string]$ProjectRoot)
    $state = Join-Path $ProjectRoot '.mqb'
    if (-not (Test-Path -LiteralPath $state)) { return '<absent>' }
    $records = @(
        Get-ChildItem -LiteralPath $state -Recurse -File | Sort-Object FullName | ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            "$($_.FullName.Substring($ProjectRoot.Length))|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)|$hash"
        }
    )
    return ($records -join "`n")
}

function Step-ByKind {
    param([object]$Plan, [string]$Kind)
    return @($Plan.steps | Where-Object { [string]$_.kind -eq $Kind })
}

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
Assert-True (Test-Path -LiteralPath $MqbPath -PathType Leaf) "MQB executable not found: $MqbPath"

& (Join-Path $RepoRoot 'tests/native/assert_cpp_layout.ps1') -CppRoot (Join-Path $RepoRoot 'cpp')

$root = Join-Path $RepoRoot 'native-out/plan-evidence'
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
New-Item -ItemType Directory -Path $root -Force | Out-Null

$ordinary = Join-Path $root 'ordinary'
New-Item -ItemType Directory -Path (Join-Path $ordinary 'src') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ordinary 'include') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $ordinary 'include/value.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'int helper();'
)
Set-Content -LiteralPath (Join-Path $ordinary 'src/main.cpp') -Encoding utf8 -Value @(
    '#include "value.hpp"',
    'int main() { return helper() == 7 ? 0 : 1; }'
)
Set-Content -LiteralPath (Join-Path $ordinary 'src/helper.cpp') -Encoding utf8 -Value 'int helper() { return VALUE; }'

$planArgs = @(
    'plan', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '-o', 'plan_app', '--format', 'json'
)
$coldRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $planArgs
$cold = Read-PlanJson -Run $coldRun -Context 'cold ordinary plan'
Assert-NoMqbState -ProjectRoot $ordinary -Context 'cold ordinary plan'
Assert-True ([int]$cold.summary.planned -eq 3) 'cold ordinary plan should schedule 2 compiles + 1 link'
Assert-True ([int]$cold.summary.up_to_date -eq 0) 'cold ordinary plan should have no reusable steps'
$compileSteps = Step-ByKind -Plan $cold -Kind 'compile'
$linkSteps = Step-ByKind -Plan $cold -Kind 'link'
Assert-True ($compileSteps.Count -eq 2) 'cold ordinary plan should contain two compile steps'
Assert-True ($linkSteps.Count -eq 1) 'cold ordinary plan should contain one link step'
$expectedSourceDir = [System.IO.Path]::GetFullPath((Join-Path $ordinary 'src')).TrimEnd('\')
foreach ($step in $compileSteps) {
    Assert-True ([string]$step.status -eq 'planned') 'cold compile step should be planned'
    Assert-True ([System.IO.Path]::GetFileName([string]$step.process.executable).Equals('cl.exe', [System.StringComparison]::OrdinalIgnoreCase)) `
        'compile plan process must use cl.exe'
    $working = ([System.IO.Path]::GetFullPath([string]$step.process.working_directory)).TrimEnd('\')
    Assert-True ($working.Equals($expectedSourceDir, [System.StringComparison]::OrdinalIgnoreCase)) `
        "compile plan working directory drifted from source parent: $working"
    Assert-True ((@($step.process.arguments) -join "`n") -match 'VALUE=7') 'compile recipe lost typed define'
}
Assert-True ([System.IO.Path]::GetFileName([string]$linkSteps[0].process.executable).Equals('link.exe', [System.StringComparison]::OrdinalIgnoreCase)) `
    'link plan process must use link.exe'
$expectedProject = [System.IO.Path]::GetFullPath($ordinary).TrimEnd('\')
$linkWorking = ([System.IO.Path]::GetFullPath([string]$linkSteps[0].process.working_directory)).TrimEnd('\')
Assert-True ($linkWorking.Equals($expectedProject, [System.StringComparison]::OrdinalIgnoreCase)) `
    'link plan working directory must be project root'
Assert-True ((@($linkSteps[0].process.environment | Where-Object { $_.name -eq 'LINK' -and $_.remove }).Count -eq 1)) `
    'link recipe must suppress ambient LINK'
Assert-True ((@($linkSteps[0].process.environment | Where-Object { $_.name -eq '_LINK_' -and $_.remove }).Count -eq 1)) `
    'link recipe must suppress ambient _LINK_'
Assert-True ((@($linkSteps[0].process.environment | Where-Object { $_.name -eq 'LINK_REPRO' -and $_.remove }).Count -eq 1)) `
    'link recipe must suppress ambient LINK_REPRO'

$coldRun2 = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $planArgs
Assert-True ($coldRun2.ExitCode -eq 0) "second cold plan failed:`n$($coldRun2.Text)"
Assert-True ($coldRun.Text -eq $coldRun2.Text) 'identical cold plans should produce byte-identical JSON'
Assert-NoMqbState -ProjectRoot $ordinary -Context 'second cold ordinary plan'

$buildArgs = @(
    'build', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '-o', 'plan_app'
)
$build = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $buildArgs
Assert-True ($build.ExitCode -eq 0) "ordinary build failed:`n$($build.Text)"
$beforeWarm = Get-StateFingerprint -ProjectRoot $ordinary
$warmRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $planArgs
$warm = Read-PlanJson -Run $warmRun -Context 'warm ordinary plan'
$afterWarm = Get-StateFingerprint -ProjectRoot $ordinary
Assert-True ($beforeWarm -eq $afterWarm) 'warm plan mutated .mqb cache/artifact state'
Assert-True ([int]$warm.summary.planned -eq 0) 'warm ordinary plan should schedule no work'
Assert-True ([int]$warm.summary.up_to_date -eq 3) 'warm ordinary plan should report all steps up-to-date'
foreach ($step in @($warm.steps)) {
    Assert-True ([string]$step.status -eq 'up_to_date') 'warm step should be up-to-date'
    Assert-True ($null -eq $step.PSObject.Properties['process']) 'up-to-date step should not claim an execution recipe'
}

$compdb = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'compdb', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '--output', 'compile_commands.json'
)
Assert-True ($compdb.ExitCode -eq 0) "compdb regression check failed:`n$($compdb.Text)"
$database = @(Get-Content -LiteralPath (Join-Path $ordinary 'compile_commands.json') -Raw -Encoding utf8 | ConvertFrom-Json)
Assert-True ($database.Count -eq 2) 'compdb regression fixture expected two entries'
foreach ($entry in $database) {
    $directory = ([System.IO.Path]::GetFullPath([string]$entry.directory)).TrimEnd('\')
    Assert-True ($directory.Equals($expectedSourceDir, [System.StringComparison]::OrdinalIgnoreCase)) `
        "compdb directory must match real source-parent compile working directory: $directory"
}

$pch = Join-Path $root 'pch'
New-Item -ItemType Directory -Path (Join-Path $pch 'include') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $pch 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $pch 'include/pch.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'inline constexpr int PCH_VALUE = 1;'
)
Set-Content -LiteralPath (Join-Path $pch 'src/main.cpp') -Encoding utf8 -Value 'int main() { return PCH_VALUE == 1 ? 0 : 1; }'
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
$pchPlanArgs = @('plan', '--no-discover', '--debug', '--format', 'json')
$pchColdRun = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments $pchPlanArgs
$pchCold = Read-PlanJson -Run $pchColdRun -Context 'cold PCH plan'
Assert-NoMqbState -ProjectRoot $pch -Context 'cold PCH plan'
Assert-True ([int]$pchCold.summary.planned -eq 3) 'cold PCH plan should schedule PCH creator + consumer + link'
$pchStep = @(Step-ByKind -Plan $pchCold -Kind 'pch')
Assert-True ($pchStep.Count -eq 1 -and [string]$pchStep[0].status -eq 'planned') 'cold PCH creator should be planned'
Assert-True ((@($pchStep[0].process.arguments) -join "`n") -match '/Yc') 'PCH plan recipe must include /Yc'
Assert-True ((@($pchStep[0].process.arguments) -join "`n") -match '/Fp') 'PCH plan recipe must include /Fp'

$pchBuild = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments @('build', '--no-discover', '--debug')
Assert-True ($pchBuild.ExitCode -eq 0) "PCH build failed:`n$($pchBuild.Text)"
Start-Sleep -Milliseconds 50
Set-Content -LiteralPath (Join-Path $pch 'include/pch.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'inline constexpr int PCH_VALUE = 2;'
)
$beforePchPlan = Get-StateFingerprint -ProjectRoot $pch
$pchChangedRun = Invoke-MqbCaptured -WorkingDirectory $pch -Arguments $pchPlanArgs
$pchChanged = Read-PlanJson -Run $pchChangedRun -Context 'header-changed PCH plan'
$afterPchPlan = Get-StateFingerprint -ProjectRoot $pch
Assert-True ($beforePchPlan -eq $afterPchPlan) 'PCH rebuild plan mutated sealed .mqb state'
Assert-True ([int]$pchChanged.summary.planned -eq 3) 'PCH header change should plan creator, consumer and link'
Assert-True ((@($pchChanged.steps | Where-Object { $_.kind -eq 'pch' }).reasons -join "`n") -match 'dependency') `
    'PCH header change should expose dependency freshness reason'

$textRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'plan', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '-o', 'plan_app', '--format', 'text'
)
Assert-True ($textRun.ExitCode -eq 0) "text plan failed:`n$($textRun.Text)"
Assert-True ($textRun.Text -match 'MQB build plan') 'text plan missing heading'
Assert-True ($textRun.Text -match 'summary:') 'text plan missing summary'

$module = Join-Path $root 'module'
New-Item -ItemType Directory -Path $module -Force | Out-Null
Set-Content -LiteralPath (Join-Path $module 'hello.ixx') -Encoding utf8 -Value 'export module hello; export int value() { return 1; }'
$moduleRun = Invoke-MqbCaptured -WorkingDirectory $module -Arguments @('plan', 'hello.ixx', '--no-discover', '--format', 'json')
Assert-True ($moduleRun.ExitCode -eq 2) 'module plan should fail closed with exit 2'
Assert-True ($moduleRun.Text -match 'Modules/Header Units') 'module plan should explain graph-aware boundary'
Assert-NoMqbState -ProjectRoot $module -Context 'module fail-closed plan'

$static = Join-Path $root 'static'
New-Item -ItemType Directory -Path $static -Force | Out-Null
Set-Content -LiteralPath (Join-Path $static 'main.cpp') -Encoding utf8 -Value 'int value() { return 1; }'
$staticRun = Invoke-MqbCaptured -WorkingDirectory $static -Arguments @('plan', 'main.cpp', '--no-discover', '--type', 'static', '--format', 'json')
Assert-True ($staticRun.ExitCode -eq 2) 'static plan should fail closed with exit 2'
Assert-True ($staticRun.Text -match 'transactional temporary archive path') 'static plan should explain exact lib.exe recipe boundary'
Assert-NoMqbState -ProjectRoot $static -Context 'static fail-closed plan'

$global:LASTEXITCODE = 0
Write-Host 'MQB plan evidence passed.'

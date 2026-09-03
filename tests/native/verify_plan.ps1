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

function Step-ByRole {
    param([object]$Plan, [string]$Role)
    return @($Plan.steps | Where-Object {
        $_.PSObject.Properties['role'] -and [string]$_.role -eq $Role
    })
}

function Assert-NoProcessesOnWarmPlan {
    param([object]$Plan, [string]$Context)
    foreach ($step in @($Plan.steps)) {
        Assert-True ([string]$step.status -eq 'up_to_date') `
            "$Context contains non-warm step '$($step.kind) $($step.label)'"
        Assert-True ($null -eq $step.PSObject.Properties['process']) `
            "$Context warm step claims an execution recipe"
    }
}

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
Assert-True (Test-Path -LiteralPath $MqbPath -PathType Leaf) `
    "MQB executable not found: $MqbPath"

& (Join-Path $RepoRoot 'tests/native/assert_cpp_layout.ps1') `
    -CppRoot (Join-Path $RepoRoot 'cpp')

$root = Join-Path $RepoRoot 'native-out/plan-evidence'
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
New-Item -ItemType Directory -Path $root -Force | Out-Null

# Ordinary target: cold/warm determinism, exact compiler/linker recipes, and
# compilation-database working-directory parity.
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
Set-Content -LiteralPath (Join-Path $ordinary 'src/helper.cpp') -Encoding utf8 `
    -Value 'int helper() { return VALUE; }'

$planArgs = @(
    'plan', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '-o', 'plan_app', '--format', 'json'
)
$coldRun = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $planArgs
$cold = Read-PlanJson -Run $coldRun -Context 'cold ordinary plan'
Assert-NoMqbState -ProjectRoot $ordinary -Context 'cold ordinary plan'
Assert-True ([string]$cold.pipeline -eq 'ordinary') 'ordinary plan should identify its pipeline'
Assert-True ([int]$cold.summary.planned -eq 3) `
    'cold ordinary plan should schedule 2 compiles + 1 link'
$compileSteps = @(Step-ByKind -Plan $cold -Kind 'compile')
$linkSteps = @(Step-ByKind -Plan $cold -Kind 'link')
Assert-True ($compileSteps.Count -eq 2) 'cold ordinary plan should contain two compiles'
Assert-True ($linkSteps.Count -eq 1) 'cold ordinary plan should contain one link'
$expectedSourceDir = [System.IO.Path]::GetFullPath((Join-Path $ordinary 'src')).TrimEnd('\')
foreach ($step in $compileSteps) {
    Assert-True ([System.IO.Path]::GetFileName([string]$step.process.executable).Equals(
        'cl.exe', [System.StringComparison]::OrdinalIgnoreCase)) `
        'ordinary compile plan process must use cl.exe'
    $working = ([System.IO.Path]::GetFullPath(
        [string]$step.process.working_directory)).TrimEnd('\')
    Assert-True ($working.Equals(
        $expectedSourceDir, [System.StringComparison]::OrdinalIgnoreCase)) `
        "ordinary compile working directory drifted: $working"
    Assert-True ((@($step.process.arguments) -join "`n") -match 'VALUE=7') `
        'ordinary compile recipe lost typed define'
}
Assert-True ([System.IO.Path]::GetFileName([string]$linkSteps[0].process.executable).Equals(
    'link.exe', [System.StringComparison]::OrdinalIgnoreCase)) `
    'ordinary link plan process must use link.exe'
Assert-True ((@($linkSteps[0].process.environment | Where-Object {
    $_.name -eq 'LINK' -and $_.remove
}).Count -eq 1)) 'link recipe must suppress ambient LINK'
Assert-True ((@($linkSteps[0].process.environment | Where-Object {
    $_.name -eq '_LINK_' -and $_.remove
}).Count -eq 1)) 'link recipe must suppress ambient _LINK_'
Assert-True ((@($linkSteps[0].process.environment | Where-Object {
    $_.name -eq 'LINK_REPRO' -and $_.remove
}).Count -eq 1)) 'link recipe must suppress ambient LINK_REPRO'

$coldRun2 = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $planArgs
Assert-True ($coldRun2.ExitCode -eq 0) "second cold ordinary plan failed:`n$($coldRun2.Text)"
Assert-True ($coldRun.Text -eq $coldRun2.Text) `
    'identical cold ordinary plans should emit byte-identical JSON'
Assert-NoMqbState -ProjectRoot $ordinary -Context 'second cold ordinary plan'

$ordinaryBuild = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'build', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '-o', 'plan_app'
)
Assert-True ($ordinaryBuild.ExitCode -eq 0) `
    "ordinary build failed:`n$($ordinaryBuild.Text)"
$beforeOrdinaryWarm = Get-StateFingerprint -ProjectRoot $ordinary
$ordinaryWarm = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments $planArgs) `
    -Context 'warm ordinary plan'
$afterOrdinaryWarm = Get-StateFingerprint -ProjectRoot $ordinary
Assert-True ($beforeOrdinaryWarm -eq $afterOrdinaryWarm) `
    'warm ordinary plan mutated .mqb state'
Assert-True ([int]$ordinaryWarm.summary.planned -eq 0) `
    'warm ordinary plan should schedule no work'
Assert-NoProcessesOnWarmPlan -Plan $ordinaryWarm -Context 'warm ordinary plan'

$compdb = Invoke-MqbCaptured -WorkingDirectory $ordinary -Arguments @(
    'compdb', 'src/main.cpp', 'src/helper.cpp', '--no-discover', '--debug',
    '-I', 'include', '-D', 'VALUE=7', '--output', 'compile_commands.json'
)
Assert-True ($compdb.ExitCode -eq 0) "compdb regression failed:`n$($compdb.Text)"
$database = @(Get-Content -LiteralPath (Join-Path $ordinary 'compile_commands.json') `
    -Raw -Encoding utf8 | ConvertFrom-Json)
Assert-True ($database.Count -eq 2) 'compdb regression expected two entries'
foreach ($entry in $database) {
    $directory = ([System.IO.Path]::GetFullPath([string]$entry.directory)).TrimEnd('\')
    Assert-True ($directory.Equals(
        $expectedSourceDir, [System.StringComparison]::OrdinalIgnoreCase)) `
        "compdb directory drifted from compile recipe: $directory"
}

# First-class PCH: cold recipe and dependency propagation remain intact.
$pch = Join-Path $root 'pch'
New-Item -ItemType Directory -Path (Join-Path $pch 'include') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $pch 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $pch 'include/pch.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'inline constexpr int PCH_VALUE = 1;'
)
Set-Content -LiteralPath (Join-Path $pch 'src/main.cpp') -Encoding utf8 `
    -Value 'int main() { return PCH_VALUE == 1 ? 0 : 1; }'
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
$pchCold = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $pch -Arguments $pchPlanArgs) `
    -Context 'cold PCH plan'
Assert-NoMqbState -ProjectRoot $pch -Context 'cold PCH plan'
Assert-True ([int]$pchCold.summary.planned -eq 3) `
    'cold PCH plan should schedule creator, consumer, and link'
$pchStep = @(Step-ByKind -Plan $pchCold -Kind 'pch')
Assert-True ($pchStep.Count -eq 1) 'cold PCH plan should contain one creator'
Assert-True ((@($pchStep[0].process.arguments) -join "`n") -match '/Yc') `
    'PCH creator recipe must include /Yc'
Assert-True ((@($pchStep[0].process.arguments) -join "`n") -match '/Fp') `
    'PCH creator recipe must include /Fp'
$pchBuild = Invoke-MqbCaptured -WorkingDirectory $pch `
    -Arguments @('build', '--no-discover', '--debug')
Assert-True ($pchBuild.ExitCode -eq 0) "PCH build failed:`n$($pchBuild.Text)"
Start-Sleep -Milliseconds 50
Set-Content -LiteralPath (Join-Path $pch 'include/pch.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'inline constexpr int PCH_VALUE = 2;'
)
$beforePch = Get-StateFingerprint -ProjectRoot $pch
$pchChanged = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $pch -Arguments $pchPlanArgs) `
    -Context 'changed PCH plan'
$afterPch = Get-StateFingerprint -ProjectRoot $pch
Assert-True ($beforePch -eq $afterPch) 'changed PCH plan mutated sealed state'
Assert-True ([int]$pchChanged.summary.planned -eq 3) `
    'PCH header change should plan creator, consumer, and link'

# Static library: deterministic transactional lib.exe recipe remains exact.
$static = Join-Path $root 'static'
New-Item -ItemType Directory -Path (Join-Path $static 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $static 'src/value.cpp') -Encoding utf8 `
    -Value 'int value() { return 7; }'
$staticPlanArgs = @(
    'plan', 'src/value.cpp', '--no-discover', '--type', 'static', '--debug',
    '-o', 'static_plan', '--format', 'json', '/lib', '/WX'
)
$staticColdRun = Invoke-MqbCaptured -WorkingDirectory $static -Arguments $staticPlanArgs
$staticCold = Read-PlanJson -Run $staticColdRun -Context 'cold static plan'
Assert-NoMqbState -ProjectRoot $static -Context 'cold static plan'
Assert-True ([int]$staticCold.summary.planned -eq 2) `
    'cold static plan should schedule compile and archive'
$archive = @(Step-ByKind -Plan $staticCold -Kind 'archive')
Assert-True ($archive.Count -eq 1) 'cold static plan should contain one archive'
Assert-True ([System.IO.Path]::GetFileName([string]$archive[0].process.executable).Equals(
    'lib.exe', [System.StringComparison]::OrdinalIgnoreCase)) `
    'static archive recipe must use lib.exe'
$archiveArgs = @($archive[0].process.arguments)
Assert-True (($archiveArgs -join "`n") -match '/MACHINE:X64') `
    'static archive recipe should expose /MACHINE:X64'
Assert-True (($archiveArgs -join "`n") -match '/WX') `
    'static archive recipe should preserve /lib /WX'
$outArgument = @($archiveArgs | Where-Object { $_ -like '/OUT:*' })
Assert-True ($outArgument.Count -eq 1 -and [string]$outArgument[0] -match '\.lib\.mqb-tmp$') `
    'static archive /OUT must use deterministic transaction path'
$staticColdRun2 = Invoke-MqbCaptured -WorkingDirectory $static -Arguments $staticPlanArgs
Assert-True ($staticColdRun.Text -eq $staticColdRun2.Text) `
    'identical cold static plans should be byte-identical'
$staticBuild = Invoke-MqbCaptured -WorkingDirectory $static -Arguments @(
    'build', 'src/value.cpp', '--no-discover', '--type', 'static', '--debug',
    '-o', 'static_plan', '/lib', '/WX'
)
Assert-True ($staticBuild.ExitCode -eq 0) "static build failed:`n$($staticBuild.Text)"
$beforeStatic = Get-StateFingerprint -ProjectRoot $static
$staticWarm = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $static -Arguments $staticPlanArgs) `
    -Context 'warm static plan'
$afterStatic = Get-StateFingerprint -ProjectRoot $static
Assert-True ($beforeStatic -eq $afterStatic) 'warm static plan mutated state'
Assert-NoProcessesOnWarmPlan -Plan $staticWarm -Context 'warm static plan'

# Named modules: cold plans expose only exact scans; warm plans expose the real
# dependency graph, compile levels, /reference recipes, and final link decision.
$module = Join-Path $root 'module'
New-Item -ItemType Directory -Path (Join-Path $module 'modules') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $module 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $module 'modules/math.ixx') -Encoding utf8 -Value @(
    'export module math;',
    'export int module_value() { return 7; }'
)
Set-Content -LiteralPath (Join-Path $module 'src/main.cpp') -Encoding utf8 -Value @(
    'import math;',
    'int main() { return module_value() == 7 ? 0 : 1; }'
)
$modulePlanArgs = @(
    'plan', 'src/main.cpp', 'modules/math.ixx', '--no-discover',
    '--std', 'latest', '--debug', '-o', 'module_plan', '--format', 'json'
)
$moduleColdRun = Invoke-MqbCaptured -WorkingDirectory $module -Arguments $modulePlanArgs
$moduleCold = Read-PlanJson -Run $moduleColdRun -Context 'cold module plan'
Assert-NoMqbState -ProjectRoot $module -Context 'cold module plan'
Assert-True ([string]$moduleCold.pipeline -eq 'modules') `
    'module plan should identify its pipeline'
Assert-True ([string]$moduleCold.module_graph.status -eq 'pending') `
    'cold module plan should report a pending graph'
$moduleColdScans = @(Step-ByKind -Plan $moduleCold -Kind 'module_scan')
Assert-True ($moduleColdScans.Count -eq 2) `
    'cold named-module plan should contain one scan per requested source'
Assert-True (@(Step-ByKind -Plan $moduleCold -Kind 'compile').Count -eq 0) `
    'cold module plan must not guess compile waves'
Assert-True (@(Step-ByKind -Plan $moduleCold -Kind 'link').Count -eq 0) `
    'cold module plan must not guess link work'
foreach ($scan in $moduleColdScans) {
    Assert-True ([string]$scan.status -eq 'planned') `
        'cold module scans should be planned'
    Assert-True ((@($scan.process.arguments) -contains '/scanDependencies')) `
        'module scan recipe must contain /scanDependencies'
    Assert-True ([System.IO.Path]::GetFileName([string]$scan.process.executable).Equals(
        'cl.exe', [System.StringComparison]::OrdinalIgnoreCase)) `
        'module scan recipe must use cl.exe'
}
$moduleColdRun2 = Invoke-MqbCaptured -WorkingDirectory $module -Arguments $modulePlanArgs
Assert-True ($moduleColdRun.Text -eq $moduleColdRun2.Text) `
    'identical cold module plans should be byte-identical'
Assert-NoMqbState -ProjectRoot $module -Context 'second cold module plan'

$moduleBuildArgs = @(
    'build', 'src/main.cpp', 'modules/math.ixx', '--no-discover',
    '--std', 'latest', '--debug', '-o', 'module_plan'
)
$moduleBuild = Invoke-MqbCaptured -WorkingDirectory $module -Arguments $moduleBuildArgs
Assert-True ($moduleBuild.ExitCode -eq 0) `
    "named-module build failed:`n$($moduleBuild.Text)"
$beforeModuleWarm = Get-StateFingerprint -ProjectRoot $module
$moduleWarm = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $module -Arguments $modulePlanArgs) `
    -Context 'warm module plan'
$afterModuleWarm = Get-StateFingerprint -ProjectRoot $module
Assert-True ($beforeModuleWarm -eq $afterModuleWarm) `
    'warm module plan mutated scan/compile/link state'
Assert-True ([string]$moduleWarm.module_graph.status -eq 'ready') `
    'warm module plan should expose a ready graph'
Assert-True (@($moduleWarm.module_graph.compile_levels).Count -eq 2) `
    'provider/consumer module graph should contain two levels'
Assert-True (@(Step-ByKind -Plan $moduleWarm -Kind 'module_scan').Count -eq 2) `
    'warm module plan should retain scan observability'
Assert-True (@(Step-ByKind -Plan $moduleWarm -Kind 'compile').Count -eq 2) `
    'warm module plan should contain provider and consumer compiles'
Assert-True (@(Step-ByKind -Plan $moduleWarm -Kind 'link').Count -eq 1) `
    'warm module plan should contain final link inspection'
Assert-NoProcessesOnWarmPlan -Plan $moduleWarm -Context 'warm module plan'

$moduleInterfaceStep = @(Step-ByRole -Plan $moduleWarm -Role 'module_interface')
Assert-True ($moduleInterfaceStep.Count -eq 1) `
    'warm named-module plan should identify one module interface'
$providerIfc = @($moduleInterfaceStep[0].outputs | Where-Object {
    [System.IO.Path]::GetExtension([string]$_).Equals(
        '.ifc', [System.StringComparison]::OrdinalIgnoreCase)
})
Assert-True ($providerIfc.Count -eq 1) `
    'module interface plan should expose exactly one IFC output'
Remove-Item -LiteralPath ([string]$providerIfc[0]) -Force
$beforeModuleRepair = Get-StateFingerprint -ProjectRoot $module
$moduleRepair = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $module -Arguments $modulePlanArgs) `
    -Context 'module IFC repair plan'
$afterModuleRepair = Get-StateFingerprint -ProjectRoot $module
Assert-True ($beforeModuleRepair -eq $afterModuleRepair) `
    'module IFC repair plan mutated state'
Assert-True ([string]$moduleRepair.module_graph.status -eq 'ready') `
    'missing IFC should not invalidate reusable topology'
$repairCompiles = @(Step-ByKind -Plan $moduleRepair -Kind 'compile')
Assert-True (@($repairCompiles | Where-Object { $_.status -eq 'planned' }).Count -eq 2) `
    'missing provider IFC should plan provider and consumer'
$repairProvider = @(Step-ByRole -Plan $moduleRepair -Role 'module_interface')[0]
Assert-True ((@($repairProvider.process.arguments) -contains '/interface')) `
    'provider repair recipe should retain /interface'
Assert-True ((@($repairProvider.process.arguments) -contains '/ifcOutput')) `
    'provider repair recipe should retain /ifcOutput'
$repairConsumer = @(Step-ByRole -Plan $moduleRepair -Role 'translation_unit')[0]
Assert-True ((@($repairConsumer.reasons) -contains 'explicit rebuild')) `
    'provider repair should propagate explicit rebuild to consumer'
Assert-True ((@($repairConsumer.process.arguments) -contains '/reference')) `
    'consumer repair recipe should expose /reference'
Assert-True ((@($repairConsumer.process.arguments) -join "`n") -match 'math=.*\.ifc') `
    'consumer repair recipe should map math to the provider IFC'
Assert-True (@(Step-ByKind -Plan $moduleRepair -Kind 'link')[0].status -eq 'planned') `
    'provider repair should plan final relink'

$moduleRepairBuild = Invoke-MqbCaptured -WorkingDirectory $module -Arguments $moduleBuildArgs
Assert-True ($moduleRepairBuild.ExitCode -eq 0) `
    "module repair build failed:`n$($moduleRepairBuild.Text)"
Start-Sleep -Milliseconds 50
Set-Content -LiteralPath (Join-Path $module 'src/main.cpp') -Encoding utf8 -Value @(
    'import math;',
    'int main() { return module_value() == 7 ? 0 : 2; }'
)
$beforeStaleModule = Get-StateFingerprint -ProjectRoot $module
$moduleStale = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $module -Arguments $modulePlanArgs) `
    -Context 'stale topology module plan'
$afterStaleModule = Get-StateFingerprint -ProjectRoot $module
Assert-True ($beforeStaleModule -eq $afterStaleModule) `
    'stale topology plan mutated sealed module state'
Assert-True ([string]$moduleStale.module_graph.status -eq 'pending') `
    'stale source topology should make graph pending'
Assert-True (@(Step-ByKind -Plan $moduleStale -Kind 'module_scan' |
    Where-Object { $_.status -eq 'planned' }).Count -eq 1) `
    'one changed module source should plan exactly one scan'
Assert-True (@(Step-ByKind -Plan $moduleStale -Kind 'compile').Count -eq 0) `
    'stale topology must suppress obsolete compile graph'
Assert-True (@(Step-ByKind -Plan $moduleStale -Kind 'link').Count -eq 0) `
    'stale topology must suppress obsolete link decision'
$textModule = Invoke-MqbCaptured -WorkingDirectory $module -Arguments @(
    'plan', 'src/main.cpp', 'modules/math.ixx', '--no-discover',
    '--std', 'latest', '--debug', '-o', 'module_plan', '--format', 'text'
)
Assert-True ($textModule.ExitCode -eq 0) "module text plan failed:`n$($textModule.Text)"
Assert-True ($textModule.Text -match 'pipeline: modules') `
    'module text plan should identify the module pipeline'
Assert-True ($textModule.Text -match 'graph:\s+pending') `
    'module text plan should explain pending topology'

# Header Units: discovery selects the module pipeline, warm P1689 exposes the
# producer/consumer graph, and an IFC repair carries exact header-unit recipes.
$headerUnit = Join-Path $root 'header-unit'
New-Item -ItemType Directory -Path (Join-Path $headerUnit 'include') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $headerUnit 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $headerUnit 'include/util.hpp') -Encoding utf8 -Value @(
    '#pragma once',
    'inline int header_answer() { return 42; }'
)
Set-Content -LiteralPath (Join-Path $headerUnit 'src/main.cpp') -Encoding utf8 -Value @(
    'import "util.hpp";',
    'int main() { return header_answer() == 42 ? 0 : 1; }'
)
$headerPlanArgs = @(
    'plan', 'src/main.cpp', '--discover', '--std', 'latest', '--debug',
    '-I', 'include', '-o', 'header_plan', '--format', 'json'
)
$headerCold = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerPlanArgs) `
    -Context 'cold Header Unit plan'
Assert-NoMqbState -ProjectRoot $headerUnit -Context 'cold Header Unit plan'
Assert-True ([string]$headerCold.pipeline -eq 'modules') `
    'Header Unit source should route to module-aware plan'
Assert-True ([string]$headerCold.module_graph.status -eq 'pending') `
    'cold Header Unit plan should wait for consumer P1689'
Assert-True (@(Step-ByKind -Plan $headerCold -Kind 'module_scan').Count -eq 1) `
    'cold Header Unit plan should scan the requested consumer only'

$headerBuildArgs = @(
    'build', 'src/main.cpp', '--discover', '--std', 'latest', '--debug',
    '-I', 'include', '-o', 'header_plan'
)
$headerBuild = Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerBuildArgs
Assert-True ($headerBuild.ExitCode -eq 0) `
    "Header Unit build failed:`n$($headerBuild.Text)"
$beforeHeaderWarm = Get-StateFingerprint -ProjectRoot $headerUnit
$headerWarm = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerPlanArgs) `
    -Context 'warm Header Unit plan'
$afterHeaderWarm = Get-StateFingerprint -ProjectRoot $headerUnit
Assert-True ($beforeHeaderWarm -eq $afterHeaderWarm) `
    'warm Header Unit plan mutated state'
Assert-True ([string]$headerWarm.module_graph.status -eq 'ready') `
    'warm Header Unit plan should expose a ready graph'
$headerProducer = @(Step-ByRole -Plan $headerWarm -Role 'header_unit')
Assert-True ($headerProducer.Count -eq 1) `
    'warm Header Unit plan should identify one producer'
Assert-NoProcessesOnWarmPlan -Plan $headerWarm -Context 'warm Header Unit plan'
$headerIfc = @($headerProducer[0].outputs | Where-Object {
    [System.IO.Path]::GetExtension([string]$_).Equals(
        '.ifc', [System.StringComparison]::OrdinalIgnoreCase)
})
Assert-True ($headerIfc.Count -eq 1) `
    'Header Unit producer should expose one IFC output'
Remove-Item -LiteralPath ([string]$headerIfc[0]) -Force
$beforeHeaderRepair = Get-StateFingerprint -ProjectRoot $headerUnit
$headerRepair = Read-PlanJson `
    -Run (Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerPlanArgs) `
    -Context 'Header Unit IFC repair plan'
$afterHeaderRepair = Get-StateFingerprint -ProjectRoot $headerUnit
Assert-True ($beforeHeaderRepair -eq $afterHeaderRepair) `
    'Header Unit repair plan mutated state'
$plannedHeader = @(Step-ByRole -Plan $headerRepair -Role 'header_unit')[0]
Assert-True ([string]$plannedHeader.status -eq 'planned') `
    'missing Header Unit IFC should plan its producer'
Assert-True ((@($plannedHeader.process.arguments) -contains '/exportHeader')) `
    'Header Unit producer recipe should include /exportHeader'
Assert-True ((@($plannedHeader.process.arguments) -contains '/headerName:quote')) `
    'Header Unit producer recipe should preserve quote lookup'
Assert-True ((@($plannedHeader.process.arguments) -contains '/ifcOutput')) `
    'Header Unit producer recipe should include /ifcOutput'
$plannedHeaderConsumer = @(Step-ByRole -Plan $headerRepair -Role 'translation_unit')[0]
Assert-True ([string]$plannedHeaderConsumer.status -eq 'planned') `
    'Header Unit provider repair should plan its consumer'
Assert-True ((@($plannedHeaderConsumer.reasons) -contains 'explicit rebuild')) `
    'Header Unit provider repair should propagate explicit rebuild'
Assert-True ((@($plannedHeaderConsumer.process.arguments) -contains '/headerUnit:quote')) `
    'Header Unit consumer recipe should preserve /headerUnit:quote'
Assert-True ((@($plannedHeaderConsumer.process.arguments) -join "`n") -match 'util\.hpp=.*\.ifc') `
    'Header Unit consumer recipe should map the header name to its IFC'
Assert-True (@(Step-ByKind -Plan $headerRepair -Kind 'link')[0].status -eq 'planned') `
    'Header Unit repair should plan final relink'

$global:LASTEXITCODE = 0
Write-Host 'MQB plan evidence passed.'

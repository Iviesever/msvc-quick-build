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
        $exitCode = $LASTEXITCODE
        # Some evidence cases intentionally exercise a non-zero MQB exit code.
        # Preserve it in the returned record, but do not leak native-process
        # status into the verifier's own final exit status.
        $global:LASTEXITCODE = 0
        return [PSCustomObject]@{
            ExitCode = $exitCode
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

function Get-MqbStateFingerprint {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

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

function Read-Database {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Context
    )

    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "$Context output is missing: $Path"
    try {
        return @(Get-Content -LiteralPath $Path -Raw -Encoding utf8 | ConvertFrom-Json)
    }
    catch {
        throw "$Context did not produce valid JSON: $($_.Exception.Message)"
    }
}

function Entry-ByExtension {
    param(
        [Parameter(Mandatory = $true)][object[]]$Database,
        [Parameter(Mandatory = $true)][string]$Extension
    )

    return @($Database | Where-Object {
        [System.IO.Path]::GetExtension([string]$_.file).Equals(
            $Extension,
            [System.StringComparison]::OrdinalIgnoreCase)
    })
}

function Arguments-Of {
    param([Parameter(Mandatory = $true)][object]$Entry)
    return @($Entry.arguments | ForEach-Object { [string]$_ })
}

function Argument-After {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Option
    )

    for ($index = 0; $index + 1 -lt $Arguments.Count; ++$index) {
        if ($Arguments[$index] -eq $Option) {
            return $Arguments[$index + 1]
        }
    }
    return $null
}

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
Assert-True (Test-Path -LiteralPath $MqbPath -PathType Leaf) "MQB executable not found: $MqbPath"

& (Join-Path $RepoRoot 'tests/native/assert_cpp_layout.ps1') -CppRoot (Join-Path $RepoRoot 'cpp')

$root = Join-Path $RepoRoot 'native-out/compdb-evidence/编译 数据库'
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
New-Item -ItemType Directory -Path $root -Force | Out-Null

# Ordinary C++: exact argv, Unicode-safe CLI/project paths/JSON, custom output,
# deterministic bytes, and no compile/link/archive side effects.
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
$database = Read-Database -Path $databasePath -Context 'ordinary compdb'
Assert-True ($database.Count -eq 2) "expected 2 compilation database entries, got $($database.Count)"

$files = @($database | ForEach-Object { [string]$_.file })
Assert-True ($files[0] -lt $files[1]) 'compilation database entries are not deterministic path-sorted output'
Assert-True (($files -join "`n") -match '编译 数据库') 'Unicode project path was not preserved in JSON'
Assert-True (($files -join "`n") -match '主\.cpp') 'Unicode source argv/path was not preserved in JSON'
foreach ($entry in $database) {
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.directory)) 'directory must be absolute'
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.file)) 'file must be absolute'
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.output)) 'output must be absolute'
    Assert-True (([string]$entry.directory) -match '编译 数据库') 'Unicode directory field was not preserved'
    $arguments = Arguments-Of -Entry $entry
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
$pchDatabase = Read-Database -Path $pchDatabasePath -Context 'PCH compdb'
Assert-True ($pchDatabase.Count -eq 2) 'PCH compdb should expose both consumer translation units'
foreach ($entry in $pchDatabase) {
    $joined = ((Arguments-Of -Entry $entry) -join "`n")
    Assert-True ($joined -match '/Yu') 'PCH consumer recipe is missing /Yu'
    Assert-True ($joined -match '/Fp') 'PCH consumer recipe is missing /Fp'
    Assert-True ($joined -match '/FI') 'PCH consumer recipe is missing forced include policy'
}
Assert-NoBuildArtifacts -ProjectRoot $pch

# Named modules fail closed until a successful build has sealed trustworthy
# P1689, then export exact provider and consumer recipes without link coupling.
$modules = Join-Path $root 'module-project'
New-Item -ItemType Directory -Path (Join-Path $modules 'modules') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $modules 'src') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $modules 'modules/math.ixx') -Encoding utf8 -Value @(
    'export module math;',
    'export int answer() { return 42; }'
)
Set-Content -LiteralPath (Join-Path $modules 'src/main.cpp') -Encoding utf8 -Value @(
    'import math;',
    'int main() { return answer() == 42 ? 0 : 1; }'
)
$moduleDatabasePath = Join-Path $modules 'module.json'
$moduleCompdbArgs = @(
    'compdb', 'src/main.cpp', 'modules/math.ixx', '--no-discover',
    '--std', 'latest', '--output', 'module.json'
)
$moduleCold = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleCompdbArgs
Assert-True ($moduleCold.ExitCode -eq 2) "cold module compdb should fail closed:`n$($moduleCold.Text)"
Assert-True ($moduleCold.Text -match 'reusable P1689 topology') 'cold module diagnostic should explain the topology boundary'
Assert-True (-not (Test-Path -LiteralPath $moduleDatabasePath)) 'cold module compdb must not publish partial JSON'
Assert-NoBuildArtifacts -ProjectRoot $modules

$moduleBuildArgs = @(
    'build', 'src/main.cpp', 'modules/math.ixx', '--no-discover',
    '--std', 'latest', '-o', 'module_compdb'
)
$moduleBuild = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleBuildArgs
Assert-True ($moduleBuild.ExitCode -eq 0) "named-module build failed:`n$($moduleBuild.Text)"
$beforeModuleCompdb = Get-MqbStateFingerprint -ProjectRoot $modules
$moduleWarmRun = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleCompdbArgs
$afterModuleCompdb = Get-MqbStateFingerprint -ProjectRoot $modules
Assert-True ($moduleWarmRun.ExitCode -eq 0) "warm module compdb failed:`n$($moduleWarmRun.Text)"
Assert-True ($beforeModuleCompdb -eq $afterModuleCompdb) 'warm module compdb mutated .mqb state'
$moduleDatabase = Read-Database -Path $moduleDatabasePath -Context 'warm module compdb'
Assert-True ($moduleDatabase.Count -eq 2) 'warm named-module compdb should contain provider and consumer'
$moduleInterfaces = Entry-ByExtension -Database $moduleDatabase -Extension '.ixx'
$moduleConsumers = Entry-ByExtension -Database $moduleDatabase -Extension '.cpp'
Assert-True ($moduleInterfaces.Count -eq 1) 'named-module compdb should contain one interface entry'
Assert-True ($moduleConsumers.Count -eq 1) 'named-module compdb should contain one consumer entry'
$interfaceArguments = Arguments-Of -Entry $moduleInterfaces[0]
$consumerArguments = Arguments-Of -Entry $moduleConsumers[0]
Assert-True (($interfaceArguments -contains '/interface')) 'module interface recipe is missing /interface'
Assert-True (($interfaceArguments -contains '/ifcOutput')) 'module interface recipe is missing /ifcOutput'
Assert-True ([System.IO.Path]::GetExtension([string]$moduleInterfaces[0].output).Equals('.obj', [System.StringComparison]::OrdinalIgnoreCase)) 'module interface primary compdb output should be its object'
Assert-True (($consumerArguments -contains '/reference')) 'module consumer recipe is missing /reference'
Assert-True (($consumerArguments -join "`n") -match 'math=.*\.ifc') 'module consumer recipe does not map math to its IFC'
Assert-True ([System.IO.Path]::GetExtension([string]$moduleConsumers[0].output).Equals('.obj', [System.StringComparison]::OrdinalIgnoreCase)) 'module consumer primary output should be its object'
foreach ($entry in $moduleDatabase) {
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.file)) 'module compdb file must be absolute'
    Assert-True ([System.IO.Path]::IsPathFullyQualified([string]$entry.output)) 'module compdb output must be absolute'
    Assert-True (([System.IO.Path]::GetFullPath([string]$entry.directory)).TrimEnd('\').Equals(
        ([System.IO.Path]::GetFullPath($modules)).TrimEnd('\'),
        [System.StringComparison]::OrdinalIgnoreCase)) 'module compile directory must match the real project-root recipe'
}
$moduleHash = (Get-FileHash -LiteralPath $moduleDatabasePath -Algorithm SHA256).Hash
$moduleWarmRun2 = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleCompdbArgs
Assert-True ($moduleWarmRun2.ExitCode -eq 0) "second warm module compdb failed:`n$($moduleWarmRun2.Text)"
Assert-True ($moduleHash -eq (Get-FileHash -LiteralPath $moduleDatabasePath -Algorithm SHA256).Hash) 'warm module compdb is not byte deterministic'

$providerIfc = Argument-After -Arguments $interfaceArguments -Option '/ifcOutput'
Assert-True (-not [string]::IsNullOrWhiteSpace($providerIfc)) 'module interface recipe did not expose its IFC path'
Remove-Item -LiteralPath $providerIfc -Force
$beforeMissingIfc = Get-MqbStateFingerprint -ProjectRoot $modules
$missingIfcRun = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleCompdbArgs
$afterMissingIfc = Get-MqbStateFingerprint -ProjectRoot $modules
Assert-True ($missingIfcRun.ExitCode -eq 0) "missing-IFC module compdb failed:`n$($missingIfcRun.Text)"
Assert-True ($beforeMissingIfc -eq $afterMissingIfc) 'module compdb repaired or mutated missing-IFC state'
Assert-True (-not (Test-Path -LiteralPath $providerIfc)) 'module compdb must not repair a missing provider IFC'
$missingIfcDatabase = Read-Database -Path $moduleDatabasePath -Context 'missing-IFC module compdb'
Assert-True ((Arguments-Of -Entry (Entry-ByExtension -Database $missingIfcDatabase -Extension '.cpp')[0]) -contains '/reference') 'missing-IFC compdb lost the consumer reference recipe'

$moduleRepair = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleBuildArgs
Assert-True ($moduleRepair.ExitCode -eq 0) "module repair build failed:`n$($moduleRepair.Text)"
Start-Sleep -Milliseconds 50
Set-Content -LiteralPath (Join-Path $modules 'src/main.cpp') -Encoding utf8 -Value @(
    'import math;',
    'int main() { return answer() == 42 ? 0 : 2; }'
)
$sentinel = '{"preserve":"existing database"}'
Set-Content -LiteralPath $moduleDatabasePath -Encoding utf8 -NoNewline -Value $sentinel
$beforeStaleModule = Get-MqbStateFingerprint -ProjectRoot $modules
$staleModule = Invoke-MqbCaptured -WorkingDirectory $modules -Arguments $moduleCompdbArgs
$afterStaleModule = Get-MqbStateFingerprint -ProjectRoot $modules
Assert-True ($staleModule.ExitCode -eq 2) "stale-topology module compdb should fail closed:`n$($staleModule.Text)"
Assert-True ($staleModule.Text -match 'reusable P1689 topology') 'stale module diagnostic should explain the topology boundary'
Assert-True ((Get-Content -LiteralPath $moduleDatabasePath -Raw -Encoding utf8) -eq $sentinel) 'stale module compdb replaced the previous database with partial output'
Assert-True ($beforeStaleModule -eq $afterStaleModule) 'stale module compdb mutated .mqb state'

# Header Units use the same warm graph: the producer receives an IFC primary
# output and the consumer receives the exact /headerUnit mapping.
$headerUnit = Join-Path $root 'header-unit-project'
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
$headerDatabasePath = Join-Path $headerUnit 'header.json'
$headerCompdbArgs = @(
    'compdb', 'src/main.cpp', '--discover', '--std', 'latest',
    '-I', 'include', '--output', 'header.json'
)
$headerCold = Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerCompdbArgs
Assert-True ($headerCold.ExitCode -eq 2) "cold Header Unit compdb should fail closed:`n$($headerCold.Text)"
Assert-True (-not (Test-Path -LiteralPath $headerDatabasePath)) 'cold Header Unit compdb must not publish partial JSON'

$headerBuildArgs = @(
    'build', 'src/main.cpp', '--discover', '--std', 'latest',
    '-I', 'include', '-o', 'header_compdb'
)
$headerBuild = Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerBuildArgs
Assert-True ($headerBuild.ExitCode -eq 0) "Header Unit build failed:`n$($headerBuild.Text)"
$beforeHeaderCompdb = Get-MqbStateFingerprint -ProjectRoot $headerUnit
$headerWarmRun = Invoke-MqbCaptured -WorkingDirectory $headerUnit -Arguments $headerCompdbArgs
$afterHeaderCompdb = Get-MqbStateFingerprint -ProjectRoot $headerUnit
Assert-True ($headerWarmRun.ExitCode -eq 0) "warm Header Unit compdb failed:`n$($headerWarmRun.Text)"
Assert-True ($beforeHeaderCompdb -eq $afterHeaderCompdb) 'warm Header Unit compdb mutated .mqb state'
$headerDatabase = Read-Database -Path $headerDatabasePath -Context 'warm Header Unit compdb'
Assert-True ($headerDatabase.Count -eq 2) 'Header Unit compdb should contain producer and consumer'
$headerProducers = Entry-ByExtension -Database $headerDatabase -Extension '.hpp'
$headerConsumers = Entry-ByExtension -Database $headerDatabase -Extension '.cpp'
Assert-True ($headerProducers.Count -eq 1) 'Header Unit compdb should contain one header producer'
Assert-True ($headerConsumers.Count -eq 1) 'Header Unit compdb should contain one source consumer'
$headerArguments = Arguments-Of -Entry $headerProducers[0]
$headerConsumerArguments = Arguments-Of -Entry $headerConsumers[0]
Assert-True (($headerArguments -contains '/exportHeader')) 'Header Unit producer is missing /exportHeader'
Assert-True (($headerArguments -contains '/headerName:quote')) 'Header Unit producer lost quote lookup policy'
Assert-True (($headerArguments -contains '/ifcOutput')) 'Header Unit producer is missing /ifcOutput'
Assert-True ([System.IO.Path]::GetExtension([string]$headerProducers[0].output).Equals('.ifc', [System.StringComparison]::OrdinalIgnoreCase)) 'Header Unit compdb primary output must be its IFC'
Assert-True (($headerConsumerArguments -contains '/headerUnit:quote')) 'Header Unit consumer is missing /headerUnit:quote'
Assert-True (($headerConsumerArguments -join "`n") -match 'util\.hpp=.*\.ifc') 'Header Unit consumer does not map util.hpp to its IFC'
Assert-True ([System.IO.Path]::GetExtension([string]$headerConsumers[0].output).Equals('.obj', [System.StringComparison]::OrdinalIgnoreCase)) 'Header Unit consumer primary output should be its object'

$global:LASTEXITCODE = 0
Write-Host 'mqb compdb evidence passed'

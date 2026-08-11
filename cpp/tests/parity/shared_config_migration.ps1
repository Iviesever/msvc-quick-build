param(
    [Parameter(Mandatory = $true)]
    [string]$MqbPath,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$mqb = [System.IO.Path]::GetFullPath($MqbPath)
$repo = [System.IO.Path]::GetFullPath($RepoRoot)
$buildPs1 = Join-Path $repo 'build.ps1'
$fixturesRoot = Join-Path $repo 'cpp/tests/parity/fixtures/config_migration'

if (-not (Test-Path -LiteralPath $mqb -PathType Leaf)) {
    throw "mqb executable does not exist: $mqb"
}
if (-not (Test-Path -LiteralPath $buildPs1 -PathType Leaf)) {
    throw "PowerShell Golden Reference does not exist: $buildPs1"
}
if (-not (Test-Path -LiteralPath $fixturesRoot -PathType Container)) {
    throw "config-migration fixture root does not exist: $fixturesRoot"
}

function Invoke-Captured {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    Push-Location $WorkingDirectory
    try {
        $lines = @(& $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Lines = $lines
    }
}

function Assert-Success {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($Result.ExitCode -ne 0) {
        Write-Host "--- $Label output ---"
        $Result.Lines | ForEach-Object { Write-Host $_ }
        throw "$Label failed with exit code $($Result.ExitCode)"
    }
}

function Assert-ContainsLine {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not ($Result.Lines -contains $Expected)) {
        Write-Host "--- $Label output ---"
        $Result.Lines | ForEach-Object { Write-Host $_ }
        throw "$Label did not contain expected line '$Expected'"
    }
}

function Assert-NotFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        throw "$Label unexpectedly exists: $Path"
    }
}

function Copy-ProjectFixture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $source = Join-Path $fixturesRoot $Name
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "missing config-migration fixture: $source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $Destination -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $Destination 'nested/work') | Out-Null
}

function Invoke-GoldenBuild {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $pwsh = (Get-Process -Id $PID).Path
    $allArguments = @(
        '-NoLogo',
        '-NoProfile',
        '-File',
        $buildPs1
    ) + $Arguments
    return Invoke-Captured -FilePath $pwsh -WorkingDirectory $WorkingDirectory -Arguments $allArguments
}

function Invoke-NativeBuild {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    return Invoke-Captured -FilePath $mqb -WorkingDirectory $WorkingDirectory -Arguments $Arguments
}

function Invoke-Program {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    return Invoke-Captured -FilePath $Executable -WorkingDirectory $WorkingDirectory -Arguments @()
}

$runRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-config-migration-parity-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

try {
    # Config-only parity. Both entry points start two levels below the project
    # config, so success proves upward discovery plus config-relative include paths.
    $configOnlyRoot = Join-Path $runRoot 'config-only'
    $configOnlyPs = Join-Path $configOnlyRoot 'powershell'
    $configOnlyCpp = Join-Path $configOnlyRoot 'cpp'
    Copy-ProjectFixture -Name 'config_only' -Destination $configOnlyPs
    Copy-ProjectFixture -Name 'config_only' -Destination $configOnlyCpp

    $configOnlyPsWork = Join-Path $configOnlyPs 'nested/work'
    $configOnlyCppWork = Join-Path $configOnlyCpp 'nested/work'
    $relativeSource = '..\..\src\main.cpp'

    $psConfigOnly = Invoke-GoldenBuild -WorkingDirectory $configOnlyPsWork -Arguments @($relativeSource)
    $cppConfigOnly = Invoke-NativeBuild -WorkingDirectory $configOnlyCppWork -Arguments @(
        $relativeSource, '--env', 'vs'
    )
    Assert-Success $psConfigOnly 'PowerShell config-only migration build'
    Assert-Success $cppConfigOnly 'C++ config-only migration build'

    $psConfigOnlyExe = Join-Path $configOnlyPsWork 'config_file.exe'
    $cppConfigOnlyExe = Join-Path $configOnlyCpp '.mqb/bin/config_file.exe'
    if (-not (Test-Path -LiteralPath $psConfigOnlyExe -PathType Leaf)) {
        throw "PowerShell config output does not exist: $psConfigOnlyExe"
    }
    if (-not (Test-Path -LiteralPath $cppConfigOnlyExe -PathType Leaf)) {
        throw "C++ config output does not exist: $cppConfigOnlyExe"
    }

    $psConfigOnlyRun = Invoke-Program -Executable $psConfigOnlyExe -WorkingDirectory $configOnlyPsWork
    $cppConfigOnlyRun = Invoke-Program -Executable $cppConfigOnlyExe -WorkingDirectory $configOnlyCppWork
    Assert-Success $psConfigOnlyRun 'PowerShell config-only migration program'
    Assert-Success $cppConfigOnlyRun 'C++ config-only migration program'
    $configOnlyExpected = 'config=release;define=41;include=31;std=17'
    Assert-ContainsLine $psConfigOnlyRun $configOnlyExpected 'PowerShell config-only migration program'
    Assert-ContainsLine $cppConfigOnlyRun $configOnlyExpected 'C++ config-only migration program'

    # Scalar CLI precedence parity. The files deliberately request Release,
    # static output, and a losing target name. CLI must override those scalars
    # while config define/include/standard remain active and CLI define is additive.
    $overrideRoot = Join-Path $runRoot 'cli-override'
    $overridePs = Join-Path $overrideRoot 'powershell'
    $overrideCpp = Join-Path $overrideRoot 'cpp'
    Copy-ProjectFixture -Name 'cli_override' -Destination $overridePs
    Copy-ProjectFixture -Name 'cli_override' -Destination $overrideCpp

    $overridePsWork = Join-Path $overridePs 'nested/work'
    $overrideCppWork = Join-Path $overrideCpp 'nested/work'

    $psOverride = Invoke-GoldenBuild -WorkingDirectory $overridePsWork -Arguments @(
        $relativeSource,
        '-config', 'debug',
        '-type', 'exe',
        '-o', 'config_cli',
        '-D', 'CLI_OVERRIDE=59'
    )
    $cppOverride = Invoke-NativeBuild -WorkingDirectory $overrideCppWork -Arguments @(
        $relativeSource,
        '--env', 'vs',
        '--config', 'debug',
        '--type', 'exe',
        '-o', 'config_cli',
        '-D', 'CLI_OVERRIDE=59'
    )
    Assert-Success $psOverride 'PowerShell config CLI-override build'
    Assert-Success $cppOverride 'C++ config CLI-override build'

    $psOverrideExe = Join-Path $overridePsWork 'config_cli.exe'
    $cppOverrideExe = Join-Path $overrideCpp '.mqb/bin/config_cli.exe'
    if (-not (Test-Path -LiteralPath $psOverrideExe -PathType Leaf)) {
        throw "PowerShell CLI-override output does not exist: $psOverrideExe"
    }
    if (-not (Test-Path -LiteralPath $cppOverrideExe -PathType Leaf)) {
        throw "C++ CLI-override output does not exist: $cppOverrideExe"
    }

    Assert-NotFile (Join-Path $overridePsWork 'should_not_win.lib') 'PowerShell losing config target'
    Assert-NotFile (Join-Path $overrideCpp '.mqb/bin/should_not_win.lib') 'C++ losing config target'

    $psOverrideRun = Invoke-Program -Executable $psOverrideExe -WorkingDirectory $overridePsWork
    $cppOverrideRun = Invoke-Program -Executable $cppOverrideExe -WorkingDirectory $overrideCppWork
    Assert-Success $psOverrideRun 'PowerShell config CLI-override program'
    Assert-Success $cppOverrideRun 'C++ config CLI-override program'
    $overrideExpected = 'config=debug;config_define=41;cli_define=59;include=31;std=17'
    Assert-ContainsLine $psOverrideRun $overrideExpected 'PowerShell config CLI-override program'
    Assert-ContainsLine $cppOverrideRun $overrideExpected 'C++ config CLI-override program'

    Write-Host 'shared project-config migration parity passed'
}
finally {
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

exit 0

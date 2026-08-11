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
$fixturesRoot = Join-Path $repo 'cpp/tests/parity/fixtures'

if (-not (Test-Path -LiteralPath $mqb -PathType Leaf)) {
    throw "mqb executable does not exist: $mqb"
}
if (-not (Test-Path -LiteralPath $buildPs1 -PathType Leaf)) {
    throw "PowerShell Golden Reference does not exist: $buildPs1"
}
if (-not (Test-Path -LiteralPath $fixturesRoot -PathType Container)) {
    throw "parity fixture root does not exist: $fixturesRoot"
}

function Invoke-Captured {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
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

function Copy-Fixture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $source = Join-Path $fixturesRoot $Name
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "missing parity fixture: $source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $Destination -Recurse -Force
}

function Invoke-GoldenBuild {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $pwsh = (Get-Process -Id $PID).Path
    return Invoke-Captured -FilePath $pwsh -WorkingDirectory $WorkingDirectory -Arguments @(
        '-NoLogo',
        '-NoProfile',
        '-File',
        $buildPs1
    ) + $Arguments
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

$runRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-shared-parity-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

try {
    # Incremental observable parity: unchanged builds must not rewrite the target;
    # a source mutation must reach the rebuilt program on both implementations.
    $incrementalRoot = Join-Path $runRoot 'incremental'
    $psRoot = Join-Path $incrementalRoot 'powershell'
    $cppRoot = Join-Path $incrementalRoot 'cpp'
    Copy-Fixture -Name 'incremental' -Destination $psRoot
    Copy-Fixture -Name 'incremental' -Destination $cppRoot

    $psCold = Invoke-GoldenBuild -WorkingDirectory $psRoot -Arguments @('main.cpp', '-o', 'parity_incremental')
    $cppCold = Invoke-NativeBuild -WorkingDirectory $cppRoot -Arguments @('main.cpp', '--env', 'vs', '-o', 'parity_incremental')
    Assert-Success $psCold 'PowerShell incremental cold build'
    Assert-Success $cppCold 'C++ incremental cold build'

    $psExe = Join-Path $psRoot 'parity_incremental.exe'
    $cppExe = Join-Path $cppRoot '.mqb/bin/parity_incremental.exe'
    if (-not (Test-Path -LiteralPath $psExe -PathType Leaf)) { throw "missing PowerShell parity output: $psExe" }
    if (-not (Test-Path -LiteralPath $cppExe -PathType Leaf)) { throw "missing C++ parity output: $cppExe" }

    $psColdRun = Invoke-Program -Executable $psExe -WorkingDirectory $psRoot
    $cppColdRun = Invoke-Program -Executable $cppExe -WorkingDirectory $cppRoot
    Assert-Success $psColdRun 'PowerShell incremental cold program'
    Assert-Success $cppColdRun 'C++ incremental cold program'
    Assert-ContainsLine $psColdRun 'incremental=1' 'PowerShell incremental cold program'
    Assert-ContainsLine $cppColdRun 'incremental=1' 'C++ incremental cold program'

    $psColdStamp = (Get-Item -LiteralPath $psExe).LastWriteTimeUtc.Ticks
    $cppColdStamp = (Get-Item -LiteralPath $cppExe).LastWriteTimeUtc.Ticks

    $psWarm = Invoke-GoldenBuild -WorkingDirectory $psRoot -Arguments @('main.cpp', '-o', 'parity_incremental')
    $cppWarm = Invoke-NativeBuild -WorkingDirectory $cppRoot -Arguments @('main.cpp', '--env', 'vs', '-o', 'parity_incremental')
    Assert-Success $psWarm 'PowerShell incremental warm build'
    Assert-Success $cppWarm 'C++ incremental warm build'

    $psWarmStamp = (Get-Item -LiteralPath $psExe).LastWriteTimeUtc.Ticks
    $cppWarmStamp = (Get-Item -LiteralPath $cppExe).LastWriteTimeUtc.Ticks
    if ($psWarmStamp -ne $psColdStamp) {
        throw 'PowerShell warm no-op rewrote the target executable'
    }
    if ($cppWarmStamp -ne $cppColdStamp) {
        throw 'C++ warm no-op rewrote the target executable'
    }

    $mutatedSource = @'
#include <iostream>

int main() {
    std::cout << "incremental=2\n";
    return 0;
}
'@
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText((Join-Path $psRoot 'main.cpp'), $mutatedSource, $utf8NoBom)
    [System.IO.File]::WriteAllText((Join-Path $cppRoot 'main.cpp'), $mutatedSource, $utf8NoBom)
    $future = [DateTime]::UtcNow.AddSeconds(2)
    (Get-Item -LiteralPath (Join-Path $psRoot 'main.cpp')).LastWriteTimeUtc = $future
    (Get-Item -LiteralPath (Join-Path $cppRoot 'main.cpp')).LastWriteTimeUtc = $future

    $psChanged = Invoke-GoldenBuild -WorkingDirectory $psRoot -Arguments @('main.cpp', '-o', 'parity_incremental')
    $cppChanged = Invoke-NativeBuild -WorkingDirectory $cppRoot -Arguments @('main.cpp', '--env', 'vs', '-o', 'parity_incremental')
    Assert-Success $psChanged 'PowerShell incremental changed-source build'
    Assert-Success $cppChanged 'C++ incremental changed-source build'

    $psChangedRun = Invoke-Program -Executable $psExe -WorkingDirectory $psRoot
    $cppChangedRun = Invoke-Program -Executable $cppExe -WorkingDirectory $cppRoot
    Assert-Success $psChangedRun 'PowerShell incremental changed program'
    Assert-Success $cppChangedRun 'C++ incremental changed program'
    Assert-ContainsLine $psChangedRun 'incremental=2' 'PowerShell incremental changed program'
    Assert-ContainsLine $cppChangedRun 'incremental=2' 'C++ incremental changed program'

    # Run-argv parity: compare the logical token sequence shared by legacy -a
    # and native structured -- argv. Quoted/escaped legacy tokenizer behavior
    # remains an explicit migration boundary rather than being silently copied.
    $argvRoot = Join-Path $runRoot 'run-argv'
    $argvPsRoot = Join-Path $argvRoot 'powershell'
    $argvCppRoot = Join-Path $argvRoot 'cpp'
    Copy-Fixture -Name 'run_argv' -Destination $argvPsRoot
    Copy-Fixture -Name 'run_argv' -Destination $argvCppRoot

    $psArgv = Invoke-GoldenBuild -WorkingDirectory $argvPsRoot -Arguments @(
        'main.cpp', '-o', 'parity_run_argv', '-run', '-a', 'alpha beta 42'
    )
    $cppArgv = Invoke-NativeBuild -WorkingDirectory $argvCppRoot -Arguments @(
        'main.cpp', '--env', 'vs', '-o', 'parity_run_argv', '--run', '--', 'alpha', 'beta', '42'
    )
    Assert-Success $psArgv 'PowerShell run-argv build/run'
    Assert-Success $cppArgv 'C++ run-argv build/run'
    Assert-ContainsLine $psArgv 'argv=alpha|beta|42' 'PowerShell run-argv build/run'
    Assert-ContainsLine $cppArgv 'argv=alpha|beta|42' 'C++ run-argv build/run'

    Write-Host 'shared incremental/run-argv parity passed'
}
finally {
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

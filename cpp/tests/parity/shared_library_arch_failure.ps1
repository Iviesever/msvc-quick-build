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

function Assert-Failure {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($Result.ExitCode -eq 0) {
        Write-Host "--- $Label output ---"
        $Result.Lines | ForEach-Object { Write-Host $_ }
        throw "$Label unexpectedly succeeded"
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

$runRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-shared-parity-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

try {
    # Library parity: each implementation builds the same static-library source,
    # then the same consumer resolves and links that library through its public CLI.
    $libraryRoot = Join-Path $runRoot 'library'
    $libraryPsRoot = Join-Path $libraryRoot 'powershell'
    $libraryCppRoot = Join-Path $libraryRoot 'cpp'
    Copy-Fixture -Name 'library' -Destination $libraryPsRoot
    Copy-Fixture -Name 'library' -Destination $libraryCppRoot

    $psLibrary = Invoke-GoldenBuild -WorkingDirectory $libraryPsRoot -Arguments @(
        'support.cpp', '-type', 'static', '-o', 'parity_support'
    )
    $cppLibrary = Invoke-NativeBuild -WorkingDirectory $libraryCppRoot -Arguments @(
        'support.cpp', '--env', 'vs', '--type', 'static', '-o', 'parity_support'
    )
    Assert-Success $psLibrary 'PowerShell static-library build'
    Assert-Success $cppLibrary 'C++ static-library build'

    $psLibraryPath = Join-Path $libraryPsRoot 'parity_support.lib'
    $cppLibraryPath = Join-Path $libraryCppRoot '.mqb/bin/parity_support.lib'
    if (-not (Test-Path -LiteralPath $psLibraryPath -PathType Leaf)) {
        throw "PowerShell static library does not exist: $psLibraryPath"
    }
    if (-not (Test-Path -LiteralPath $cppLibraryPath -PathType Leaf)) {
        throw "C++ static library does not exist: $cppLibraryPath"
    }

    $psConsumer = Invoke-GoldenBuild -WorkingDirectory $libraryPsRoot -Arguments @(
        'consumer.cpp', '-libpath', '.', '-libs', 'parity_support', '-o', 'parity_library_app'
    )
    $cppConsumer = Invoke-NativeBuild -WorkingDirectory $libraryCppRoot -Arguments @(
        'consumer.cpp', '--env', 'vs', '--lib-path', '.mqb/bin', '--lib', 'parity_support', '-o', 'parity_library_app'
    )
    Assert-Success $psConsumer 'PowerShell library consumer build'
    Assert-Success $cppConsumer 'C++ library consumer build'

    $psLibraryExe = Join-Path $libraryPsRoot 'parity_library_app.exe'
    $cppLibraryExe = Join-Path $libraryCppRoot '.mqb/bin/parity_library_app.exe'
    $psLibraryRun = Invoke-Program -Executable $psLibraryExe -WorkingDirectory $libraryPsRoot
    $cppLibraryRun = Invoke-Program -Executable $cppLibraryExe -WorkingDirectory $libraryCppRoot
    Assert-Success $psLibraryRun 'PowerShell library consumer program'
    Assert-Success $cppLibraryRun 'C++ library consumer program'
    Assert-ContainsLine $psLibraryRun 'library=73' 'PowerShell library consumer program'
    Assert-ContainsLine $cppLibraryRun 'library=73' 'C++ library consumer program'

    # Architecture parity: legacy default is x64; both public CLIs must also
    # produce a runnable x86 target exposing the selected MSVC architecture macro.
    $architectures = @(
        [pscustomobject]@{ Name = 'x64'; GoldenFlag = $null; NativeFlag = '--x64'; Expected = 'arch=x64' },
        [pscustomobject]@{ Name = 'x86'; GoldenFlag = '-x86'; NativeFlag = '--x86'; Expected = 'arch=x86' }
    )
    foreach ($architecture in $architectures) {
        $archRoot = Join-Path $runRoot ("architecture-" + $architecture.Name)
        $archPsRoot = Join-Path $archRoot 'powershell'
        $archCppRoot = Join-Path $archRoot 'cpp'
        Copy-Fixture -Name 'architecture' -Destination $archPsRoot
        Copy-Fixture -Name 'architecture' -Destination $archCppRoot

        $outputName = "parity_arch_$($architecture.Name)"
        $psArchArguments = @('main.cpp')
        if ($architecture.GoldenFlag) {
            $psArchArguments += $architecture.GoldenFlag
        }
        $psArchArguments += @('-o', $outputName)
        $psArch = Invoke-GoldenBuild -WorkingDirectory $archPsRoot -Arguments $psArchArguments
        $cppArch = Invoke-NativeBuild -WorkingDirectory $archCppRoot -Arguments @(
            'main.cpp', '--env', 'vs', $architecture.NativeFlag, '-o', $outputName
        )
        Assert-Success $psArch "PowerShell $($architecture.Name) build"
        Assert-Success $cppArch "C++ $($architecture.Name) build"

        $psArchExe = Join-Path $archPsRoot ($outputName + '.exe')
        $cppArchExe = Join-Path $archCppRoot ('.mqb/bin/' + $outputName + '.exe')
        $psArchRun = Invoke-Program -Executable $psArchExe -WorkingDirectory $archPsRoot
        $cppArchRun = Invoke-Program -Executable $cppArchExe -WorkingDirectory $archCppRoot
        Assert-Success $psArchRun "PowerShell $($architecture.Name) program"
        Assert-Success $cppArchRun "C++ $($architecture.Name) program"
        Assert-ContainsLine $psArchRun $architecture.Expected "PowerShell $($architecture.Name) program"
        Assert-ContainsLine $cppArchRun $architecture.Expected "C++ $($architecture.Name) program"
    }

    # Failure parity: syntax-invalid source must fail the build and leave no target.
    $failureRoot = Join-Path $runRoot 'failure-compile'
    $failurePsRoot = Join-Path $failureRoot 'powershell'
    $failureCppRoot = Join-Path $failureRoot 'cpp'
    Copy-Fixture -Name 'failure_compile' -Destination $failurePsRoot
    Copy-Fixture -Name 'failure_compile' -Destination $failureCppRoot

    $psFailure = Invoke-GoldenBuild -WorkingDirectory $failurePsRoot -Arguments @(
        'broken.cpp', '-o', 'parity_failure'
    )
    $cppFailure = Invoke-NativeBuild -WorkingDirectory $failureCppRoot -Arguments @(
        'broken.cpp', '--env', 'vs', '-o', 'parity_failure'
    )
    Assert-Failure $psFailure 'PowerShell compile-failure build'
    Assert-Failure $cppFailure 'C++ compile-failure build'

    $psFailureExe = Join-Path $failurePsRoot 'parity_failure.exe'
    $cppFailureExe = Join-Path $failureCppRoot '.mqb/bin/parity_failure.exe'
    if (Test-Path -LiteralPath $psFailureExe -PathType Leaf) {
        throw "PowerShell compile failure left a target executable: $psFailureExe"
    }
    if (Test-Path -LiteralPath $cppFailureExe -PathType Leaf) {
        throw "C++ compile failure left a target executable: $cppFailureExe"
    }

    Write-Host 'shared library/architecture/failure parity passed'
}
finally {
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

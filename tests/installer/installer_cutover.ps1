[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$mqb = [IO.Path]::GetFullPath($MqbPath)
$repo = [IO.Path]::GetFullPath($RepoRoot)
$installPs1 = Join-Path $repo 'install.ps1'
$installBat = Join-Path $repo 'install.bat'

foreach ($path in @($mqb, $installPs1, $installBat)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "installer prerequisite missing: $path"
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Q {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Invoke-PS5 {
    param([string]$Command, [string]$Label)
    $lines = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command $Command 2>&1 |
        ForEach-Object { $_.ToString() })
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host "--- $Label output ---"
        $lines | ForEach-Object { Write-Host $_ }
        throw "$Label failed with exit code $code"
    }
    return $lines
}

function Normalize-PathEntry {
    param([AllowEmptyString()][string]$Entry)
    if ([string]::IsNullOrWhiteSpace($Entry)) { return '' }
    $value = $Entry.Trim().Trim('"')
    try { $value = [IO.Path]::GetFullPath($value) } catch {}
    return $value.TrimEnd('\')
}

function Count-UserPathEntry {
    param([string]$Entry)
    $path = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $path) { return 0 }
    $needle = Normalize-PathEntry $Entry
    $count = 0
    foreach ($part in @($path -split ';')) {
        if ([string]::Equals((Normalize-PathEntry $part), $needle, [StringComparison]::OrdinalIgnoreCase)) {
            ++$count
        }
    }
    return $count
}

function Help-Line {
    param([string]$Command)
    $lines = @(& $Command --help 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0) { throw "help failed: $Command" }
    return $lines[0]
}

function Install-Mqb {
    param([string]$Root)
    $command = '& ' + (Q $installPs1) +
        ' -Action Install -MqbPath ' + (Q $mqb) +
        ' -InstallRoot ' + (Q $Root)
    Invoke-PS5 $command "install $Root" | Out-Null
}

function Uninstall-Mqb {
    param([string]$Root)
    $wrapper = Join-Path $Root 'uninstall-mqb.ps1'
    Assert-True (Test-Path -LiteralPath $wrapper -PathType Leaf) "uninstall wrapper missing: $wrapper"
    $command = '& ' + (Q $wrapper) + ' -InstallRoot ' + (Q $Root)
    Invoke-PS5 $command "uninstall $Root" | Out-Null
}

$runRoot = Join-Path ([IO.Path]::GetTempPath()) ("mqb-native-installer-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$originalUserPath = [Environment]::GetEnvironmentVariable('Path', 'User')

try {
    $installRoot = Join-Path $runRoot 'bin'

    # Public batch entry must install only the native command and native maintenance files.
    $output = @(& $installBat `
        -MqbPath $mqb `
        -InstallRoot $installRoot 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        $output | ForEach-Object { Write-Host $_ }
        throw "install.bat failed with exit code $LASTEXITCODE"
    }

    $installedMqb = Join-Path $installRoot 'mqb.exe'
    foreach ($required in @(
        $installedMqb,
        (Join-Path $installRoot 'mqb-install.ps1'),
        (Join-Path $installRoot 'uninstall-mqb.ps1'),
        (Join-Path $installRoot 'mqb-install-state.json')
    )) {
        Assert-True (Test-Path -LiteralPath $required -PathType Leaf) "native install missing: $required"
    }

    foreach ($legacyName in @('build.cmd', 'build.ps1', 'build-legacy.ps1', 'Microsoft.PowerShell_profile.ps1')) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $installRoot $legacyName))) "legacy artifact was installed: $legacyName"
    }

    Assert-True ((Help-Line $installedMqb) -ceq (Help-Line $mqb)) 'installed mqb.exe identity differs from validated input binary'
    Assert-True ((Count-UserPathEntry $installRoot) -eq 1) 'install did not own exactly one PATH entry'

    # Reinstall remains idempotent without introducing compatibility artifacts.
    Install-Mqb $installRoot
    Assert-True ((Count-UserPathEntry $installRoot) -eq 1) 'reinstall duplicated PATH'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $installRoot 'build.cmd'))) 'reinstall introduced build compatibility shim'

    Uninstall-Mqb $installRoot
    Assert-True (-not (Test-Path -LiteralPath $installedMqb -PathType Leaf)) 'uninstall left mqb.exe behind'
    Assert-True ((Count-UserPathEntry $installRoot) -eq 0) 'uninstall left installer-owned PATH entry'
    foreach ($owned in @('mqb-install.ps1', 'uninstall-mqb.ps1', 'mqb-install-state.json')) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $installRoot $owned) -PathType Leaf)) "uninstall left installer-owned file: $owned"
    }

    # Clean-break contract: old PowerShell-era parameters are rejected instead of migrated.
    $legacyCommand = '& ' + (Q $installPs1) +
        ' -Action Install -MqbPath ' + (Q $mqb) +
        ' -InstallRoot ' + (Q (Join-Path $runRoot 'legacy-args')) +
        ' -LegacyBuildPath ignored.ps1'
    $legacyOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command $legacyCommand 2>&1 |
        ForEach-Object { $_.ToString() })
    Assert-True ($LASTEXITCODE -ne 0) 'legacy installer parameter unexpectedly remained supported'
    Assert-True (($legacyOutput -join "`n") -match 'LegacyBuildPath') 'legacy parameter rejection was not explicit'

    Write-Host 'native-only installer install / reinstall / uninstall validation passed'
}
finally {
    [Environment]::SetEnvironmentVariable('Path', $originalUserPath, 'User')
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

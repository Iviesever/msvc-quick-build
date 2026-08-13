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
$uninstallBat = Join-Path $repo 'uninstall.bat'

foreach ($path in @($mqb, $installPs1, $installBat, $uninstallBat)) {
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

function Invoke-BatNoPause {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$Label
    )

    $hadNoPause = Test-Path Env:MQB_NO_PAUSE
    $oldNoPause = $env:MQB_NO_PAUSE
    $code = 0
    try {
        $env:MQB_NO_PAUSE = '1'
        $lines = @(& $Path @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $code = $LASTEXITCODE
    }
    finally {
        if ($hadNoPause) {
            $env:MQB_NO_PAUSE = $oldNoPause
        } else {
            Remove-Item Env:MQB_NO_PAUSE -ErrorAction SilentlyContinue
        }
    }

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

function Add-TestUserPathEntry {
    param([Parameter(Mandatory = $true)][string]$Entry)
    if ((Count-UserPathEntry $Entry) -ne 0) { return }
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $current) { $current = '' }
    $parts = @($current -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    [Environment]::SetEnvironmentVariable('Path', (($parts + $Entry) -join ';'), 'User')
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

    Invoke-BatNoPause $installBat @('-MqbPath', $mqb, '-InstallRoot', $installRoot) 'install.bat' | Out-Null

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

    Install-Mqb $installRoot
    Assert-True ((Count-UserPathEntry $installRoot) -eq 1) 'reinstall duplicated PATH'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $installRoot 'build.cmd'))) 'reinstall introduced build compatibility shim'

    Invoke-BatNoPause $uninstallBat @('-InstallRoot', $installRoot) 'uninstall.bat' | Out-Null
    Assert-True (-not (Test-Path -LiteralPath $installedMqb -PathType Leaf)) 'uninstall left mqb.exe behind'
    Assert-True ((Count-UserPathEntry $installRoot) -eq 0) 'uninstall left installer-owned PATH entry'
    foreach ($owned in @('mqb-install.ps1', 'uninstall-mqb.ps1', 'mqb-install-state.json')) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $installRoot $owned) -PathType Leaf)) "uninstall left installer-owned file: $owned"
    }

    # A PATH entry that existed before installation remains user-owned and must survive uninstall.
    $preexistingRoot = Join-Path $runRoot 'user-owned-path'
    New-Item -ItemType Directory -Force -Path $preexistingRoot | Out-Null
    Add-TestUserPathEntry $preexistingRoot
    Assert-True ((Count-UserPathEntry $preexistingRoot) -eq 1) 'failed to establish pre-existing PATH fixture'

    Install-Mqb $preexistingRoot
    $preexistingStatePath = Join-Path $preexistingRoot 'mqb-install-state.json'
    $preexistingState = Get-Content -LiteralPath $preexistingStatePath -Raw | ConvertFrom-Json
    Assert-True (-not [bool]$preexistingState.path_added) 'installer incorrectly claimed ownership of a pre-existing PATH entry'

    Uninstall-Mqb $preexistingRoot
    Assert-True ((Count-UserPathEntry $preexistingRoot) -eq 1) 'uninstall removed a user-owned pre-existing PATH entry'

    # Obsolete installer parameters must fail closed instead of activating a migration path.
    $legacyCommand = '& ' + (Q $installPs1) +
        ' -Action Install -MqbPath ' + (Q $mqb) +
        ' -InstallRoot ' + (Q (Join-Path $runRoot 'legacy-args')) +
        ' -LegacyBuildPath ignored.ps1'
    $legacyOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command $legacyCommand 2>&1 |
        ForEach-Object { $_.ToString() })
    Assert-True ($LASTEXITCODE -ne 0) 'obsolete installer parameter unexpectedly remained supported'
    Assert-True (($legacyOutput -join "`n") -match 'LegacyBuildPath') 'obsolete parameter rejection was not explicit'

    Write-Host 'native installer install / reinstall / uninstall / PATH ownership validation passed'
}
finally {
    [Environment]::SetEnvironmentVariable('Path', $originalUserPath, 'User')
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

exit 0

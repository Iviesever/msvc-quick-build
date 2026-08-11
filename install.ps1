[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall', 'Rollback')]
    [string]$Action = 'Install',

    [string]$MqbPath,
    [string]$LegacyBuildPath,
    [string]$InstallRoot = (Join-Path $HOME 'bin'),
    [string[]]$ProfilePaths,
    [switch]$SkipUserPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$CppMarkerStart = '# >>> MQB v5 C++ default >>>'
$CppMarkerEnd = '# <<< MQB v5 C++ default <<<'
$LegacyMarkerStart = '# >>> MQB v5 legacy rollback >>>'
$LegacyMarkerEnd = '# <<< MQB v5 legacy rollback <<<'

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-DefaultProfilePaths {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if ([string]::IsNullOrWhiteSpace($documents)) {
        $documents = Join-Path $HOME 'Documents'
    }
    return @(
        (Join-Path $documents 'WindowsPowerShell\Microsoft.PowerShell_profile.ps1'),
        (Join-Path $documents 'PowerShell\Microsoft.PowerShell_profile.ps1')
    )
}

function ConvertTo-SingleQuotedLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Remove-ManagedBlock {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content,
        [Parameter(Mandatory = $true)][string]$StartMarker,
        [Parameter(Mandatory = $true)][string]$EndMarker
    )
    $pattern = '(?ms)^[ \t]*' + [regex]::Escape($StartMarker) + '[ \t]*\r?\n.*?^[ \t]*' +
        [regex]::Escape($EndMarker) + '[ \t]*(?:\r?\n)?'
    return [regex]::Replace($Content, $pattern, '')
}

function Add-ManagedBlock {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content,
        [Parameter(Mandatory = $true)][string]$Block
    )
    $trimmed = $Content.TrimEnd()
    if ($trimmed.Length -eq 0) {
        return $Block.TrimEnd() + [Environment]::NewLine
    }
    return $trimmed + [Environment]::NewLine + [Environment]::NewLine + $Block.TrimEnd() + [Environment]::NewLine
}

function Test-RecognizedLegacyBuildFunction {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content)
    return (
        $Content -match '(?is)function\s+(?:global:)?build\s*\{.*?(?:\$HOME|%USERPROFILE%|USERPROFILE).*?bin[\\/]+build\.ps1'
    )
}

function Test-AnyBuildFunction {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content)
    return ($Content -match '(?im)^[ \t]*function\s+(?:global:)?build\b')
}

function New-CppProfileBlock {
    param([Parameter(Mandatory = $true)][string]$Executable)
    $exeLiteral = ConvertTo-SingleQuotedLiteral $Executable
    return @"
$CppMarkerStart
function global:build {
    & $exeLiteral @args
}
$CppMarkerEnd
"@
}

function New-LegacyProfileBlock {
    param([Parameter(Mandatory = $true)][string]$LegacyScript)
    $legacyLiteral = ConvertTo-SingleQuotedLiteral $LegacyScript
    return @"
$LegacyMarkerStart
function global:build {
    if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) {
        & pwsh.exe -NoProfile -File $legacyLiteral @args
    } else {
        & $legacyLiteral @args
    }
}
$LegacyMarkerEnd
"@
}

function Read-TextFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    return [System.IO.File]::ReadAllText($Path)
}

function Write-TextFileIfChanged {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
    )
    $existing = Read-TextFile $Path
    if ($existing -ceq $Content) {
        return
    }
    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Content, (New-Object System.Text.UTF8Encoding($false)))
}

function Remove-AllMqbProfileBlocks {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content)
    $result = Remove-ManagedBlock $Content $CppMarkerStart $CppMarkerEnd
    $result = Remove-ManagedBlock $result $LegacyMarkerStart $LegacyMarkerEnd
    return $result
}

function Update-ProfileForInstall {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Executable
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    $original = Read-TextFile $Path
    $hadCppBlock = $original.Contains($CppMarkerStart)
    $base = Remove-AllMqbProfileBlocks $original
    $recognizedLegacy = Test-RecognizedLegacyBuildFunction $base

    if (-not $recognizedLegacy -and -not $hadCppBlock) {
        if (Test-AnyBuildFunction $base) {
            Write-Warning "Custom PowerShell 'build' function left untouched in $Path. Use 'mqb', or rename that function to use the compatibility build.cmd."
        }
        return $false
    }

    $updated = Add-ManagedBlock $base (New-CppProfileBlock $Executable)
    Write-TextFileIfChanged $Path $updated
    return $true
}

function Update-ProfileForRollback {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$LegacyScript
    )

    $original = Read-TextFile $Path
    $base = Remove-AllMqbProfileBlocks $original
    $recognizedLegacy = Test-RecognizedLegacyBuildFunction $base

    if ((Test-AnyBuildFunction $base) -and -not $recognizedLegacy) {
        Write-Warning "Custom PowerShell 'build' function left untouched during rollback in $Path."
        Write-TextFileIfChanged $Path $base
        return $false
    }

    $updated = Add-ManagedBlock $base (New-LegacyProfileBlock $LegacyScript)
    Write-TextFileIfChanged $Path $updated
    return $true
}

function Remove-ProfileCutoverBlocks {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    $original = Read-TextFile $Path
    $updated = Remove-AllMqbProfileBlocks $original
    Write-TextFileIfChanged $Path $updated
}

function Normalize-PathEntry {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Entry)
    $value = $Entry.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($value)) {
        return ''
    }
    try {
        $value = [System.IO.Path]::GetFullPath($value)
    } catch {
        # Preserve unusual PATH entries rather than rejecting the user's PATH.
    }
    return $value.TrimEnd('\')
}

function Test-PathContains {
    param(
        [AllowEmptyString()][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Entry
    )
    $needle = Normalize-PathEntry $Entry
    foreach ($part in @($PathValue -split ';')) {
        if ([string]::Equals((Normalize-PathEntry $part), $needle, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Add-UserPathEntry {
    param([Parameter(Mandatory = $true)][string]$Entry)
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $current) { $current = '' }
    if (Test-PathContains $current $Entry) {
        return $false
    }
    $parts = @($current -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $newValue = (($parts + $Entry) -join ';')
    [Environment]::SetEnvironmentVariable('Path', $newValue, 'User')
    return $true
}

function Remove-UserPathEntry {
    param([Parameter(Mandatory = $true)][string]$Entry)
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $current) { return }
    $needle = Normalize-PathEntry $Entry
    $parts = @()
    foreach ($part in @($current -split ';')) {
        if ([string]::IsNullOrWhiteSpace($part)) { continue }
        if (-not [string]::Equals((Normalize-PathEntry $part), $needle, [System.StringComparison]::OrdinalIgnoreCase)) {
            $parts += $part
        }
    }
    [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
}

function Read-InstallState {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    try {
        return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
    } catch {
        Write-Warning "Ignoring unreadable install state: $Path"
        return $null
    }
}

function Write-InstallState {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$State
    )
    $json = $State | ConvertTo-Json -Depth 5
    [System.IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
}

function Resolve-LegacySource {
    if (-not [string]::IsNullOrWhiteSpace($LegacyBuildPath)) {
        return (Get-FullPath $LegacyBuildPath)
    }

    $candidates = @(
        (Join-Path $PSScriptRoot 'legacy\build.ps1'),
        (Join-Path $PSScriptRoot 'build.ps1')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Get-FullPath $candidate)
        }
    }
    return $null
}

function Resolve-MqbSource {
    if (-not [string]::IsNullOrWhiteSpace($MqbPath)) {
        return (Get-FullPath $MqbPath)
    }
    return (Get-FullPath (Join-Path $PSScriptRoot 'mqb.exe'))
}

function Copy-FileUnlessSame {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    $sourceFull = Get-FullPath $Source
    $destinationFull = Get-FullPath $Destination
    if ([string]::Equals($sourceFull, $destinationFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return
    }
    Copy-Item -LiteralPath $sourceFull -Destination $destinationFull -Force
}

$InstallRoot = Get-FullPath $InstallRoot
$statePath = Join-Path $InstallRoot 'mqb-install-state.json'
$previousState = Read-InstallState $statePath

if (-not $PSBoundParameters.ContainsKey('ProfilePaths') -or $null -eq $ProfilePaths -or $ProfilePaths.Count -eq 0) {
    if ($null -ne $previousState -and $null -ne $previousState.profile_paths) {
        $ProfilePaths = @($previousState.profile_paths)
    } else {
        $ProfilePaths = Get-DefaultProfilePaths
    }
}
$ProfilePaths = @($ProfilePaths | ForEach-Object { Get-FullPath $_ } | Select-Object -Unique)

if ($Action -eq 'Install') {
    $mqbSource = Resolve-MqbSource
    if (-not (Test-Path -LiteralPath $mqbSource -PathType Leaf)) {
        throw "mqb.exe not found. Expected '$mqbSource'. Use -MqbPath when installing from a source checkout."
    }

    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null

    $destinationMqb = Join-Path $InstallRoot 'mqb.exe'
    $buildCmd = Join-Path $InstallRoot 'build.cmd'
    $legacyInstalled = Join-Path $InstallRoot 'build-legacy.ps1'
    $priorBuildPs1 = Join-Path $InstallRoot 'build.ps1'
    $maintenanceInstall = Join-Path $InstallRoot 'mqb-install.ps1'
    $maintenanceUninstall = Join-Path $InstallRoot 'uninstall-mqb.ps1'

    $legacyBackupCreated = $false
    if ($null -ne $previousState -and $null -ne $previousState.legacy_backup_created) {
        $legacyBackupCreated = [bool]$previousState.legacy_backup_created
    }

    if (-not (Test-Path -LiteralPath $legacyInstalled -PathType Leaf)) {
        if (Test-Path -LiteralPath $priorBuildPs1 -PathType Leaf) {
            Copy-Item -LiteralPath $priorBuildPs1 -Destination $legacyInstalled -Force
            $legacyBackupCreated = $true
        } else {
            $legacySource = Resolve-LegacySource
            if ($null -ne $legacySource -and (Test-Path -LiteralPath $legacySource -PathType Leaf)) {
                Copy-Item -LiteralPath $legacySource -Destination $legacyInstalled -Force
                $legacyBackupCreated = $true
            }
        }
    }

    Copy-FileUnlessSame $mqbSource $destinationMqb

    $shim = "@echo off`r`n`"%~dp0mqb.exe`" %*`r`nexit /b %errorlevel%`r`n"
    [System.IO.File]::WriteAllText($buildCmd, $shim, [System.Text.Encoding]::ASCII)

    Copy-FileUnlessSame $PSCommandPath $maintenanceInstall
    $uninstallSource = Join-Path $PSScriptRoot 'uninstall.ps1'
    if (Test-Path -LiteralPath $uninstallSource -PathType Leaf) {
        Copy-FileUnlessSame $uninstallSource $maintenanceUninstall
    }

    $profileTouched = @()
    foreach ($profilePath in $ProfilePaths) {
        if (Update-ProfileForInstall $profilePath $destinationMqb) {
            $profileTouched += $profilePath
        }
    }

    $pathAdded = $false
    if ($null -ne $previousState -and $null -ne $previousState.path_added) {
        $pathAdded = [bool]$previousState.path_added
    }
    if (-not $SkipUserPath) {
        if (Add-UserPathEntry $InstallRoot) {
            $pathAdded = $true
        }
    }

    $help = @(& $destinationMqb --help 2>&1 | ForEach-Object { $_.ToString() })
    $helpExit = $LASTEXITCODE
    if ($helpExit -ne 0 -or $help.Count -eq 0) {
        throw "Installed mqb.exe failed verification with exit code $helpExit"
    }

    $state = [ordered]@{
        version = 1
        install_root = $InstallRoot
        path_added = [bool]$pathAdded
        profile_paths = @($ProfilePaths)
        profile_touched = @($profileTouched)
        legacy_backup_created = [bool]$legacyBackupCreated
        prior_build_ps1 = [bool](Test-Path -LiteralPath $priorBuildPs1 -PathType Leaf)
        installed_help_line = $help[0]
        installed_at_utc = [DateTime]::UtcNow.ToString('o')
    }
    Write-InstallState $statePath $state

    Write-Host "Installed MQB C++ default to: $destinationMqb"
    Write-Host "Canonical command: mqb"
    Write-Host "Compatibility command: build"
    Write-Host "Verified: $($help[0])"
    if (-not $SkipUserPath) {
        Write-Host "User PATH contains: $InstallRoot"
    }
    if (Test-Path -LiteralPath $legacyInstalled -PathType Leaf) {
        Write-Host "Legacy rollback copy: $legacyInstalled"
    }
    Write-Host "Restart open terminals so PATH/profile changes are reloaded."
    exit 0
}

$destinationMqb = Join-Path $InstallRoot 'mqb.exe'
$buildCmd = Join-Path $InstallRoot 'build.cmd'
$legacyInstalled = Join-Path $InstallRoot 'build-legacy.ps1'
$priorBuildPs1 = Join-Path $InstallRoot 'build.ps1'
$maintenanceInstall = Join-Path $InstallRoot 'mqb-install.ps1'
$maintenanceUninstall = Join-Path $InstallRoot 'uninstall-mqb.ps1'

if ($Action -eq 'Rollback') {
    $legacyTarget = $null
    $priorExists = Test-Path -LiteralPath $priorBuildPs1 -PathType Leaf
    if ($priorExists) {
        $legacyTarget = $priorBuildPs1
    } elseif (Test-Path -LiteralPath $legacyInstalled -PathType Leaf) {
        $legacyTarget = $legacyInstalled
    }

    if ($null -eq $legacyTarget) {
        throw "Rollback requested, but no legacy PowerShell build script is available in '$InstallRoot'."
    }

    foreach ($profilePath in $ProfilePaths) {
        Update-ProfileForRollback $profilePath $legacyTarget | Out-Null
    }
} else {
    foreach ($profilePath in $ProfilePaths) {
        Remove-ProfileCutoverBlocks $profilePath
    }
}

if (-not $SkipUserPath -and $null -ne $previousState -and $null -ne $previousState.path_added -and [bool]$previousState.path_added) {
    Remove-UserPathEntry $InstallRoot
}

Remove-Item -LiteralPath $destinationMqb -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $buildCmd -Force -ErrorAction SilentlyContinue

if ($Action -eq 'Uninstall') {
    if ($null -ne $previousState -and $null -ne $previousState.legacy_backup_created -and [bool]$previousState.legacy_backup_created) {
        Remove-Item -LiteralPath $legacyInstalled -Force -ErrorAction SilentlyContinue
    }
    Write-Host "Uninstalled the stable-v5 C++ default entry. Pre-existing legacy files/profile content were left untouched."
} else {
    Write-Host "Rolled back the default 'build' entry to the legacy PowerShell implementation."
    Write-Host "Legacy script: $legacyTarget"
}

Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $maintenanceUninstall -Force -ErrorAction SilentlyContinue
if (-not [string]::Equals((Get-FullPath $PSCommandPath), (Get-FullPath $maintenanceInstall), [System.StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $maintenanceInstall -Force -ErrorAction SilentlyContinue
} else {
    # The current script can be deleted after it has been parsed; defer until the final operation.
    Remove-Item -LiteralPath $maintenanceInstall -Force -ErrorAction SilentlyContinue
}

Write-Host "Restart open terminals so PATH/profile changes are reloaded."
exit 0

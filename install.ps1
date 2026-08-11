[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall')]
    [string]$Action = 'Install',

    [string]$MqbPath,
    [string]$InstallRoot = (Join-Path $HOME 'bin'),
    [switch]$SkipUserPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Normalize-PathEntry {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Entry)
    $value = $Entry.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($value)) { return '' }
    try { $value = [System.IO.Path]::GetFullPath($value) } catch {}
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
    if (Test-PathContains $current $Entry) { return $false }
    $parts = @($current -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    [Environment]::SetEnvironmentVariable('Path', (($parts + $Entry) -join ';'), 'User')
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
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try { return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json) }
    catch {
        Write-Warning "Ignoring unreadable install state: $Path"
        return $null
    }
}

function Write-InstallState {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$State
    )
    $json = $State | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText(
        $Path,
        $json + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false)))
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

if ($Action -eq 'Install') {
    $mqbSource = Resolve-MqbSource
    if (-not (Test-Path -LiteralPath $mqbSource -PathType Leaf)) {
        throw "mqb.exe not found. Expected '$mqbSource'. Use -MqbPath when installing from a source checkout."
    }

    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null

    $destinationMqb = Join-Path $InstallRoot 'mqb.exe'
    $maintenanceInstall = Join-Path $InstallRoot 'mqb-install.ps1'
    $maintenanceUninstall = Join-Path $InstallRoot 'uninstall-mqb.ps1'

    Copy-FileUnlessSame $mqbSource $destinationMqb
    Copy-FileUnlessSame $PSCommandPath $maintenanceInstall

    $uninstallSource = Join-Path $PSScriptRoot 'uninstall.ps1'
    if (Test-Path -LiteralPath $uninstallSource -PathType Leaf) {
        Copy-FileUnlessSame $uninstallSource $maintenanceUninstall
    }

    $pathAdded = $false
    if ($null -ne $previousState -and $null -ne $previousState.path_added) {
        $pathAdded = [bool]$previousState.path_added
    }
    if (-not $SkipUserPath -and (Add-UserPathEntry $InstallRoot)) {
        $pathAdded = $true
    }

    $help = @(& $destinationMqb --help 2>&1 | ForEach-Object { $_.ToString() })
    $helpExit = $LASTEXITCODE
    if ($helpExit -ne 0 -or $help.Count -eq 0) {
        throw "Installed mqb.exe failed verification with exit code $helpExit"
    }

    Write-InstallState $statePath ([ordered]@{
        version = 2
        install_root = $InstallRoot
        path_added = [bool]$pathAdded
        installed_help_line = $help[0]
        installed_at_utc = [DateTime]::UtcNow.ToString('o')
    })

    Write-Host "MQB installed: $destinationMqb"
    Write-Host "Command: mqb"
    if (-not $SkipUserPath) {
        Write-Host 'Restart the terminal if this is the first install so the updated user PATH is visible.'
    }
    exit 0
}

$pathOwned = $false
if ($null -ne $previousState -and $null -ne $previousState.path_added) {
    $pathOwned = [bool]$previousState.path_added
}
if (-not $SkipUserPath -and $pathOwned) {
    Remove-UserPathEntry $InstallRoot
}

foreach ($name in @('mqb.exe', 'mqb-install-state.json')) {
    $target = Join-Path $InstallRoot $name
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        Remove-Item -LiteralPath $target -Force
    }
}

# These files are installer-owned. PowerShell loads script text before execution, so
# removing the maintenance scripts at the end is safe on Windows.
foreach ($name in @('uninstall-mqb.ps1', 'mqb-install.ps1')) {
    $target = Join-Path $InstallRoot $name
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "MQB uninstalled from: $InstallRoot"
exit 0

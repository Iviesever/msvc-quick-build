[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MqbPath,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$mqb = [System.IO.Path]::GetFullPath($MqbPath)
$repo = [System.IO.Path]::GetFullPath($RepoRoot)
$installPs1 = Join-Path $repo 'install.ps1'
$installBat = Join-Path $repo 'install.bat'
$legacySource = Join-Path $repo 'build.ps1'

foreach ($required in @($mqb, $installPs1, $installBat, $legacySource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "installer cutover prerequisite missing: $required"
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Quote-PowerShellLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Invoke-WindowsPowerShell {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $lines = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command $Command 2>&1 |
        ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-Host "--- $Label output ---"
        $lines | ForEach-Object { Write-Host $_ }
        throw "$Label failed with exit code $exitCode"
    }
    return $lines
}

function Invoke-InstallPowerShell {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$Profiles
    )
    $profileExpr = '@(' + (($Profiles | ForEach-Object { Quote-PowerShellLiteral $_ }) -join ',') + ')'
    $command = '& ' + (Quote-PowerShellLiteral $installPs1) +
        ' -Action Install -MqbPath ' + (Quote-PowerShellLiteral $mqb) +
        ' -LegacyBuildPath ' + (Quote-PowerShellLiteral $legacySource) +
        ' -InstallRoot ' + (Quote-PowerShellLiteral $Root) +
        ' -ProfilePaths ' + $profileExpr
    return Invoke-WindowsPowerShell $command "PowerShell install into $Root"
}

function Invoke-Maintenance {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$Profiles,
        [switch]$RestoreLegacy
    )
    $wrapper = Join-Path $Root 'uninstall-mqb.ps1'
    Assert-True (Test-Path -LiteralPath $wrapper -PathType Leaf) "maintenance wrapper missing: $wrapper"
    $profileExpr = '@(' + (($Profiles | ForEach-Object { Quote-PowerShellLiteral $_ }) -join ',') + ')'
    $command = '& ' + (Quote-PowerShellLiteral $wrapper) +
        ' -InstallRoot ' + (Quote-PowerShellLiteral $Root) +
        ' -ProfilePaths ' + $profileExpr
    if ($RestoreLegacy) {
        $command += ' -RestoreLegacy'
    }
    return Invoke-WindowsPowerShell $command "maintenance action for $Root"
}

function Normalize-PathEntry {
    param([AllowEmptyString()][string]$Entry)
    if ([string]::IsNullOrWhiteSpace($Entry)) { return '' }
    $value = $Entry.Trim().Trim('"')
    try { $value = [System.IO.Path]::GetFullPath($value) } catch {}
    return $value.TrimEnd('\')
}

function Count-UserPathEntry {
    param([Parameter(Mandatory = $true)][string]$Entry)
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $current) { return 0 }
    $needle = Normalize-PathEntry $Entry
    $count = 0
    foreach ($part in @($current -split ';')) {
        if ([string]::Equals((Normalize-PathEntry $part), $needle, [System.StringComparison]::OrdinalIgnoreCase)) {
            ++$count
        }
    }
    return $count
}

function Get-HelpLine {
    param([Parameter(Mandatory = $true)][string]$Executable)
    $lines = @(& $Executable --help 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0) {
        throw "help invocation failed: $Executable"
    }
    return $lines[0]
}

function Get-BuildShimHelpLine {
    param([Parameter(Mandatory = $true)][string]$BuildCmd)
    $lines = @(& $BuildCmd --help 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0) {
        throw "build.cmd help invocation failed: $BuildCmd"
    }
    return $lines[0]
}

$runRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mqb-installer-cutover-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

$originalUserPath = [Environment]::GetEnvironmentVariable('Path', 'User')

try {
    # 1. Clean-machine style install through install.bat.
    $cleanRoot = Join-Path $runRoot 'clean\bin'
    $cleanProfile = Join-Path $runRoot 'clean\profile.ps1'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $cleanProfile) | Out-Null
    $cleanSentinel = "# clean-profile-sentinel`r`n"
    [System.IO.File]::WriteAllText($cleanProfile, $cleanSentinel, (New-Object System.Text.UTF8Encoding($false)))

    $cleanInstallOutput = @(& $installBat `
        -MqbPath $mqb `
        -LegacyBuildPath $legacySource `
        -InstallRoot $cleanRoot `
        -ProfilePaths $cleanProfile 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        $cleanInstallOutput | ForEach-Object { Write-Host $_ }
        throw "install.bat clean install failed with exit code $LASTEXITCODE"
    }

    $cleanMqb = Join-Path $cleanRoot 'mqb.exe'
    $cleanBuild = Join-Path $cleanRoot 'build.cmd'
    $cleanLegacy = Join-Path $cleanRoot 'build-legacy.ps1'
    Assert-True (Test-Path -LiteralPath $cleanMqb -PathType Leaf) 'clean install did not deploy mqb.exe'
    Assert-True (Test-Path -LiteralPath $cleanBuild -PathType Leaf) 'clean install did not deploy build.cmd'
    Assert-True (Test-Path -LiteralPath $cleanLegacy -PathType Leaf) 'clean install did not deploy rollback Golden Reference'
    Assert-True (Test-Path -LiteralPath (Join-Path $cleanRoot 'mqb-install.ps1') -PathType Leaf) 'clean install did not deploy maintenance installer'
    Assert-True (Test-Path -LiteralPath (Join-Path $cleanRoot 'uninstall-mqb.ps1') -PathType Leaf) 'clean install did not deploy uninstall wrapper'
    Assert-True (([System.IO.File]::ReadAllText($cleanProfile)) -ceq $cleanSentinel) 'clean install unexpectedly rewrote an unrelated PowerShell profile'
    Assert-True ((Get-HelpLine $cleanMqb) -ceq (Get-BuildShimHelpLine $cleanBuild)) 'build compatibility shim does not forward to the installed mqb.exe'
    Assert-True ((Count-UserPathEntry $cleanRoot) -eq 1) 'clean install did not add exactly one user PATH entry'

    # Reinstall must remain idempotent, especially PATH state.
    Invoke-InstallPowerShell $cleanRoot @($cleanProfile) | Out-Null
    Assert-True ((Count-UserPathEntry $cleanRoot) -eq 1) 'reinstall duplicated the user PATH entry'

    Invoke-Maintenance $cleanRoot @($cleanProfile) | Out-Null
    Assert-True (-not (Test-Path -LiteralPath $cleanMqb -PathType Leaf)) 'uninstall left mqb.exe behind'
    Assert-True (-not (Test-Path -LiteralPath $cleanBuild -PathType Leaf)) 'uninstall left build.cmd behind'
    Assert-True (-not (Test-Path -LiteralPath $cleanLegacy -PathType Leaf)) 'clean uninstall left an installer-created legacy backup behind'
    Assert-True ((Count-UserPathEntry $cleanRoot) -eq 0) 'clean uninstall did not remove the installer-owned PATH entry'
    Assert-True (([System.IO.File]::ReadAllText($cleanProfile)) -ceq $cleanSentinel) 'clean uninstall changed unrelated profile content'

    # 2. Upgrade from the current PowerShell release line.
    $upgradeRoot = Join-Path $runRoot 'upgrade\bin'
    New-Item -ItemType Directory -Force -Path $upgradeRoot | Out-Null
    $priorBuild = Join-Path $upgradeRoot 'build.ps1'
    Copy-Item -LiteralPath $legacySource -Destination $priorBuild -Force
    $priorBuildHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $priorBuild).Hash

    $legacyProfileBody = @'
$OutputEncoding = [console]::InputEncoding = [console]::OutputEncoding = New-Object System.Text.UTF8Encoding
function build {
    if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) {
        & pwsh.exe -NoProfile -File "$HOME\bin\build.ps1" @args
    } else {
        & "$HOME\bin\build.ps1" @args
    }
}
'@
    $upgradeProfiles = @(
        (Join-Path $runRoot 'upgrade\WindowsPowerShell\Microsoft.PowerShell_profile.ps1'),
        (Join-Path $runRoot 'upgrade\PowerShell\Microsoft.PowerShell_profile.ps1')
    )
    foreach ($profile in $upgradeProfiles) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $profile) | Out-Null
        $body = "# user-before`r`n" + $legacyProfileBody.TrimEnd() + "`r`n# user-after`r`n"
        [System.IO.File]::WriteAllText($profile, $body, (New-Object System.Text.UTF8Encoding($false)))
    }

    Invoke-InstallPowerShell $upgradeRoot $upgradeProfiles | Out-Null

    $upgradeMqb = Join-Path $upgradeRoot 'mqb.exe'
    $upgradeLegacy = Join-Path $upgradeRoot 'build-legacy.ps1'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $priorBuild).Hash -ceq $priorBuildHash) 'upgrade modified the existing build.ps1 Golden Reference'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $upgradeLegacy).Hash -ceq $priorBuildHash) 'upgrade did not preserve the installed legacy build.ps1 as build-legacy.ps1'
    Assert-True ((Count-UserPathEntry $upgradeRoot) -eq 1) 'upgrade did not add the C++ install root to user PATH'

    $expectedHelp = Get-HelpLine $upgradeMqb
    foreach ($profile in $upgradeProfiles) {
        $content = [System.IO.File]::ReadAllText($profile)
        Assert-True $content.Contains('# user-before') 'upgrade lost user profile prefix content'
        Assert-True $content.Contains('# user-after') 'upgrade lost user profile suffix content'
        Assert-True $content.Contains('# >>> MQB v5 C++ default >>>') 'upgrade did not append the managed C++ default profile block'

        $command = '. ' + (Quote-PowerShellLiteral $profile) + '; build --help | Select-Object -First 1'
        $buildHelp = @(Invoke-WindowsPowerShell $command "profile build shim from $profile")
        Assert-True ($buildHelp.Count -gt 0 -and $buildHelp[-1] -ceq $expectedHelp) 'PowerShell build function did not resolve to mqb.exe after upgrade'
    }

    Invoke-Maintenance $upgradeRoot $upgradeProfiles -RestoreLegacy | Out-Null
    Assert-True (-not (Test-Path -LiteralPath $upgradeMqb -PathType Leaf)) 'rollback left mqb.exe as the default binary'
    Assert-True (Test-Path -LiteralPath $upgradeLegacy -PathType Leaf) 'rollback removed the preserved legacy implementation'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $priorBuild).Hash -ceq $priorBuildHash) 'rollback modified the pre-existing build.ps1'
    Assert-True ((Count-UserPathEntry $upgradeRoot) -eq 0) 'rollback did not remove the installer-owned PATH entry'
    foreach ($profile in $upgradeProfiles) {
        $content = [System.IO.File]::ReadAllText($profile)
        Assert-True (-not $content.Contains('# >>> MQB v5 C++ default >>>')) 'rollback left the C++ default profile block'
        Assert-True $content.Contains('# >>> MQB v5 legacy rollback >>>') 'rollback did not install the explicit legacy fallback block'
        Assert-True $content.Contains('# user-before') 'rollback lost user profile prefix content'
        Assert-True $content.Contains('# user-after') 'rollback lost user profile suffix content'
    }

    # 3. Clean install can still roll back because the package ships the Golden Reference.
    $rollbackRoot = Join-Path $runRoot 'clean-rollback\bin'
    $rollbackProfile = Join-Path $runRoot 'clean-rollback\PowerShell\Microsoft.PowerShell_profile.ps1'
    Invoke-InstallPowerShell $rollbackRoot @($rollbackProfile) | Out-Null
    Invoke-Maintenance $rollbackRoot @($rollbackProfile) -RestoreLegacy | Out-Null

    $rollbackLegacy = Join-Path $rollbackRoot 'build-legacy.ps1'
    Assert-True (Test-Path -LiteralPath $rollbackLegacy -PathType Leaf) 'clean rollback has no legacy implementation to return to'
    $rollbackContent = [System.IO.File]::ReadAllText($rollbackProfile)
    Assert-True $rollbackContent.Contains('# >>> MQB v5 legacy rollback >>>') 'clean rollback did not create a legacy profile entry'
    Assert-True $rollbackContent.Contains('build-legacy.ps1') 'clean rollback profile does not point at the preserved Golden Reference'
    Assert-True ((Count-UserPathEntry $rollbackRoot) -eq 0) 'clean rollback left a C++ PATH entry behind'

    Write-Host 'installer clean-install / upgrade / rollback validation passed'
}
finally {
    [Environment]::SetEnvironmentVariable('Path', $originalUserPath, 'User')
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

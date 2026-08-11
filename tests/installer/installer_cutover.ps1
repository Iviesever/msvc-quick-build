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
$legacySource = Join-Path $repo 'build.ps1'

foreach ($path in @($mqb, $installPs1, $installBat, $legacySource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "installer cutover prerequisite missing: $path"
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

function Profiles-Expr {
    param([string[]]$Profiles)
    return '@(' + (($Profiles | ForEach-Object { Q $_ }) -join ',') + ')'
}

function Install-Mqb {
    param([string]$Root, [string[]]$Profiles)
    $command = '& ' + (Q $installPs1) +
        ' -Action Install -MqbPath ' + (Q $mqb) +
        ' -LegacyBuildPath ' + (Q $legacySource) +
        ' -InstallRoot ' + (Q $Root) +
        ' -ProfilePaths ' + (Profiles-Expr $Profiles)
    Invoke-PS5 $command "install $Root" | Out-Null
}

function Maintain-Mqb {
    param([string]$Root, [string[]]$Profiles, [switch]$RestoreLegacy)
    $wrapper = Join-Path $Root 'uninstall-mqb.ps1'
    Assert-True (Test-Path -LiteralPath $wrapper -PathType Leaf) "maintenance wrapper missing: $wrapper"
    $command = '& ' + (Q $wrapper) +
        ' -InstallRoot ' + (Q $Root) +
        ' -ProfilePaths ' + (Profiles-Expr $Profiles)
    if ($RestoreLegacy) { $command += ' -RestoreLegacy' }
    Invoke-PS5 $command "maintenance $Root" | Out-Null
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

$runRoot = Join-Path ([IO.Path]::GetTempPath()) ("mqb-installer-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$originalUserPath = [Environment]::GetEnvironmentVariable('Path', 'User')

try {
    # Clean install through the public batch entry.
    $cleanRoot = Join-Path $runRoot 'clean\bin'
    $cleanProfile = Join-Path $runRoot 'clean\profile.ps1'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $cleanProfile) | Out-Null
    $sentinel = "# clean-profile-sentinel`r`n"
    [IO.File]::WriteAllText($cleanProfile, $sentinel, (New-Object Text.UTF8Encoding($false)))

    $output = @(& $installBat `
        -MqbPath $mqb `
        -LegacyBuildPath $legacySource `
        -InstallRoot $cleanRoot `
        -ProfilePaths $cleanProfile 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        $output | ForEach-Object { Write-Host $_ }
        throw "install.bat failed with exit code $LASTEXITCODE"
    }

    $cleanMqb = Join-Path $cleanRoot 'mqb.exe'
    $cleanBuild = Join-Path $cleanRoot 'build.cmd'
    $cleanLegacy = Join-Path $cleanRoot 'build-legacy.ps1'
    foreach ($required in @(
        $cleanMqb,
        $cleanBuild,
        $cleanLegacy,
        (Join-Path $cleanRoot 'mqb-install.ps1'),
        (Join-Path $cleanRoot 'uninstall-mqb.ps1'),
        (Join-Path $cleanRoot 'mqb-install-state.json')
    )) {
        Assert-True (Test-Path -LiteralPath $required -PathType Leaf) "clean install missing: $required"
    }
    Assert-True (([IO.File]::ReadAllText($cleanProfile)) -ceq $sentinel) 'clean install rewrote unrelated profile content'
    Assert-True ((Help-Line $cleanMqb) -ceq (Help-Line $cleanBuild)) 'build.cmd does not resolve to the installed mqb.exe'
    Assert-True ((Count-UserPathEntry $cleanRoot) -eq 1) 'clean install did not own exactly one PATH entry'

    Install-Mqb $cleanRoot @($cleanProfile)
    Assert-True ((Count-UserPathEntry $cleanRoot) -eq 1) 'reinstall duplicated PATH'
    Maintain-Mqb $cleanRoot @($cleanProfile)
    Assert-True (-not (Test-Path -LiteralPath $cleanMqb -PathType Leaf)) 'uninstall left mqb.exe'
    Assert-True (-not (Test-Path -LiteralPath $cleanBuild -PathType Leaf)) 'uninstall left build.cmd'
    Assert-True (-not (Test-Path -LiteralPath $cleanLegacy -PathType Leaf)) 'uninstall left installer-owned legacy backup'
    Assert-True ((Count-UserPathEntry $cleanRoot) -eq 0) 'uninstall left installer-owned PATH'
    Assert-True (([IO.File]::ReadAllText($cleanProfile)) -ceq $sentinel) 'uninstall changed unrelated profile content'

    # Upgrade from the existing PowerShell installation shape.
    $upgradeRoot = Join-Path $runRoot 'upgrade\bin'
    New-Item -ItemType Directory -Force -Path $upgradeRoot | Out-Null
    $priorBuild = Join-Path $upgradeRoot 'build.ps1'
    Copy-Item -LiteralPath $legacySource -Destination $priorBuild -Force
    $priorHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $priorBuild).Hash

    $legacyProfile = @'
$OutputEncoding = [console]::InputEncoding = [console]::OutputEncoding = New-Object System.Text.UTF8Encoding
function build {
    if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) {
        & pwsh.exe -NoProfile -File "$HOME\bin\build.ps1" @args
    } else {
        & "$HOME\bin\build.ps1" @args
    }
}
'@
    $profiles = @(
        (Join-Path $runRoot 'upgrade\WindowsPowerShell\Microsoft.PowerShell_profile.ps1'),
        (Join-Path $runRoot 'upgrade\PowerShell\Microsoft.PowerShell_profile.ps1')
    )
    foreach ($profile in $profiles) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $profile) | Out-Null
        [IO.File]::WriteAllText(
            $profile,
            "# user-before`r`n" + $legacyProfile.TrimEnd() + "`r`n# user-after`r`n",
            (New-Object Text.UTF8Encoding($false)))
    }

    Install-Mqb $upgradeRoot $profiles
    $upgradeMqb = Join-Path $upgradeRoot 'mqb.exe'
    $upgradeLegacy = Join-Path $upgradeRoot 'build-legacy.ps1'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $priorBuild).Hash -ceq $priorHash) 'upgrade modified prior build.ps1'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $upgradeLegacy).Hash -ceq $priorHash) 'upgrade did not preserve prior build.ps1'
    Assert-True ((Count-UserPathEntry $upgradeRoot) -eq 1) 'upgrade did not add installer-owned PATH'
    $expected = Help-Line $upgradeMqb

    foreach ($profile in $profiles) {
        $content = [IO.File]::ReadAllText($profile)
        Assert-True ($content.Contains('# user-before') -and $content.Contains('# user-after')) 'upgrade lost user profile content'
        Assert-True $content.Contains('# >>> MQB v5 C++ default >>>') 'upgrade did not add C++ profile block'
        $line = @(Invoke-PS5 ('. ' + (Q $profile) + '; build --help | Select-Object -First 1') "profile shim $profile")
        Assert-True ($line.Count -gt 0 -and $line[-1] -ceq $expected) 'upgraded build function does not resolve to mqb.exe'
    }

    Maintain-Mqb $upgradeRoot $profiles -RestoreLegacy
    Assert-True (-not (Test-Path -LiteralPath $upgradeMqb -PathType Leaf)) 'rollback left mqb.exe'
    Assert-True (Test-Path -LiteralPath $upgradeLegacy -PathType Leaf) 'rollback removed legacy backup'
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $priorBuild).Hash -ceq $priorHash) 'rollback modified prior build.ps1'
    Assert-True ((Count-UserPathEntry $upgradeRoot) -eq 0) 'rollback left installer-owned PATH'
    foreach ($profile in $profiles) {
        $content = [IO.File]::ReadAllText($profile)
        Assert-True (-not $content.Contains('# >>> MQB v5 C++ default >>>')) 'rollback left C++ profile block'
        Assert-True $content.Contains('# >>> MQB v5 legacy rollback >>>') 'rollback did not install legacy fallback'
        Assert-True ($content.Contains('# user-before') -and $content.Contains('# user-after')) 'rollback lost user profile content'
    }

    # A clean v5 install also has a package Golden Reference to roll back to.
    $fallbackRoot = Join-Path $runRoot 'fallback\bin'
    $fallbackProfile = Join-Path $runRoot 'fallback\PowerShell\Microsoft.PowerShell_profile.ps1'
    Install-Mqb $fallbackRoot @($fallbackProfile)
    Maintain-Mqb $fallbackRoot @($fallbackProfile) -RestoreLegacy
    $fallbackLegacy = Join-Path $fallbackRoot 'build-legacy.ps1'
    $fallbackContent = [IO.File]::ReadAllText($fallbackProfile)
    Assert-True (Test-Path -LiteralPath $fallbackLegacy -PathType Leaf) 'clean rollback removed package Golden Reference'
    Assert-True $fallbackContent.Contains('# >>> MQB v5 legacy rollback >>>') 'clean rollback has no legacy profile entry'
    Assert-True $fallbackContent.Contains('build-legacy.ps1') 'clean rollback does not target build-legacy.ps1'
    Assert-True ((Count-UserPathEntry $fallbackRoot) -eq 0) 'clean rollback left installer-owned PATH'

    Write-Host 'installer clean-install / upgrade / rollback validation passed'
}
finally {
    [Environment]::SetEnvironmentVariable('Path', $originalUserPath, 'User')
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

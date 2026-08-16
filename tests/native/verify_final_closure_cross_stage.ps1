[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$MqbPath = [System.IO.Path]::GetFullPath($MqbPath)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if (-not (Test-Path -LiteralPath $MqbPath -PathType Leaf)) {
    throw "MQB executable not found: $MqbPath"
}

$root = Join-Path $RepoRoot 'native-out/final-closure-cross-stage'
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
New-Item -ItemType Directory -Path $root -Force | Out-Null

function Invoke-MqbChecked {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Push-Location $WorkingDirectory
    try {
        $output = @(& $MqbPath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    foreach ($line in $output) {
        Write-Host $line
    }
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode"
    }
    return @($output | ForEach-Object { [string]$_ })
}

$previousLocalAppData = $env:LOCALAPPDATA
try {
    # Final Closure architecture contract: MQB-owned persistent state must remain
    # inside the active project's .mqb tree. Point LOCALAPPDATA at a fresh decoy
    # so a regression to the old user-local VS discovery cache is observable.
    $stateProject = Join-Path $root 'state-project'
    $decoyLocalAppData = Join-Path $root 'decoy-local-app-data'
    New-Item -ItemType Directory -Path $stateProject -Force | Out-Null
    New-Item -ItemType Directory -Path $decoyLocalAppData -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $stateProject 'main.cpp') -Encoding utf8 -Value 'int main() { return 0; }'
    $env:LOCALAPPDATA = $decoyLocalAppData

    [void](Invoke-MqbChecked `
        -WorkingDirectory $stateProject `
        -Arguments @('build', 'main.cpp', '--env', 'vs', '--no-discover') `
        -Description 'project-local state cold build')

    $projectCache = Join-Path $stateProject '.mqb/cache/toolchain/vs-x64.cache'
    if (-not (Test-Path -LiteralPath $projectCache -PathType Leaf)) {
        throw "project-local VS discovery cache missing: $projectCache"
    }
    $legacyUserLocalState = Join-Path $decoyLocalAppData 'MQB'
    if (Test-Path -LiteralPath $legacyUserLocalState) {
        throw "MQB wrote persistent state outside the project .mqb tree: $legacyUserLocalState"
    }

    $warmOutput = Invoke-MqbChecked `
        -WorkingDirectory $stateProject `
        -Arguments @('build', 'main.cpp', '--env', 'vs', '--no-discover') `
        -Description 'project-local state warm build'
    $warmText = $warmOutput -join "`n"
    if ($warmText -notmatch '\[up-to-date\]') {
        throw 'project-local toolchain cache migration broke the warm/no-op build path'
    }

    # Final Closure #5 cross-stage contract: application-level project scope must
    # use the same Windows identity authority as caches/discovery. The source is
    # passed through a different ASCII-case spelling of the exact same directory.
    # Project-only extra_sources is required to satisfy the link, so the build
    # fails on the pre-fix lexical/case-sensitive containment path.
    $caseProject = Join-Path $root 'CaseProject'
    New-Item -ItemType Directory -Path $caseProject -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $caseProject 'main.cpp') -Encoding utf8 -Value @(
        'int helper();',
        'int main() { return helper() == 42 ? 0 : 1; }'
    )
    Set-Content -LiteralPath (Join-Path $caseProject 'helper.cpp') -Encoding utf8 -Value 'int helper() { return 42; }'
    Set-Content -LiteralPath (Join-Path $caseProject 'mqb.json') -Encoding utf8 -Value @'
{
  "version": 1,
  "build": {
    "configuration": "debug",
    "architecture": "x64",
    "standard": "23",
    "type": "exe"
  },
  "discovery": {
    "enabled": true,
    "extra_sources": ["helper.cpp"]
  }
}
'@

    $aliasedDirectory = $caseProject.ToUpperInvariant()
    $aliasedEntry = Join-Path $aliasedDirectory 'main.cpp'
    if (-not (Test-Path -LiteralPath $aliasedEntry -PathType Leaf)) {
        throw "Windows case-alias fixture did not resolve to the same entry: $aliasedEntry"
    }

    $caseOutput = Invoke-MqbChecked `
        -WorkingDirectory $caseProject `
        -Arguments @('build', $aliasedEntry, '--env', 'vs', '--verbose') `
        -Description 'Windows case-alias project discovery build'
    $caseText = $caseOutput -join "`n"
    if ($caseText -notmatch '\[discover\]\s+2 translation units') {
        throw "case-alias entry did not retain project-scoped smart discovery:`n$caseText"
    }
}
finally {
    if ($null -eq $previousLocalAppData) {
        Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
    }
    else {
        $env:LOCALAPPDATA = $previousLocalAppData
    }
}

Write-Host 'Final Closure cross-stage state/path gate passed.'

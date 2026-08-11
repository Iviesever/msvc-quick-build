[CmdletBinding()]
param(
    [switch]$RestoreLegacy,
    [string]$InstallRoot,
    [string[]]$ProfilePaths,
    [switch]$SkipUserPath
)

$ErrorActionPreference = 'Stop'

$engineCandidates = @(
    (Join-Path $PSScriptRoot 'mqb-install.ps1'),
    (Join-Path $PSScriptRoot 'install.ps1')
)
$engine = $null
foreach ($candidate in $engineCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $engine = $candidate
        break
    }
}
if ($null -eq $engine) {
    throw "MQB maintenance installer not found next to this script."
}

$params = @{
    Action = $(if ($RestoreLegacy) { 'Rollback' } else { 'Uninstall' })
}
if ($PSBoundParameters.ContainsKey('InstallRoot')) {
    $params.InstallRoot = $InstallRoot
}
if ($PSBoundParameters.ContainsKey('ProfilePaths')) {
    $params.ProfilePaths = $ProfilePaths
}
if ($SkipUserPath) {
    $params.SkipUserPath = $true
}

& $engine @params
exit $LASTEXITCODE

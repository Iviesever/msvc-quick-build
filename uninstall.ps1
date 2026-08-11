[CmdletBinding()]
param(
    [string]$InstallRoot,
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
    throw 'MQB maintenance installer not found next to this script.'
}

$params = @{ Action = 'Uninstall' }
if ($PSBoundParameters.ContainsKey('InstallRoot')) {
    $params.InstallRoot = $InstallRoot
}
if ($SkipUserPath) {
    $params.SkipUserPath = $true
}

& $engine @params
exit $LASTEXITCODE

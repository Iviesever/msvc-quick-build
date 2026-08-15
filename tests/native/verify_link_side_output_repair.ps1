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

$fixture = Join-Path $RepoRoot 'native-test-work/link-side-output-repair'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
@'
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
int main() { return 0; }
'@ | Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8

$binary = Join-Path $fixture '.mqb/bin/link_side_output_probe.exe'
$pdb = Join-Path $fixture '.mqb/bin/link_side_output_probe.pdb'
$manifest = Join-Path $fixture '.mqb/bin/link_side_output_probe.exe.manifest'
$map = Join-Path $fixture 'link_side_output_probe.map'

function Invoke-ProbeBuild {
    Push-Location $fixture
    try {
        $output = @(
            & $MqbPath build main.cpp --debug --no-discover -o link_side_output_probe `
                /link "/MAP:$map" 2>&1
        )
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    [PSCustomObject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { [string]$_ })
        Text = ($output -join [Environment]::NewLine)
    }
}

$cold = Invoke-ProbeBuild
if ($cold.ExitCode -ne 0) {
    throw "Cold Debug side-output probe failed:`n$($cold.Text)"
}
foreach ($path in @($binary, $pdb, $manifest, $map)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Cold Debug link did not produce expected output: $path`n$($cold.Text)"
    }
}

$warm = Invoke-ProbeBuild
if ($warm.ExitCode -ne 0) {
    throw "Warm side-output probe failed:`n$($warm.Text)"
}
if ($warm.Text -notmatch '\[up-to-date\]\s+link_side_output_probe\.exe') {
    throw "Warm build did not reuse sealed link outputs:`n$($warm.Text)"
}

Remove-Item -LiteralPath $pdb -Force
$pdbRepair = Invoke-ProbeBuild
if ($pdbRepair.ExitCode -ne 0) {
    throw "PDB repair build failed:`n$($pdbRepair.Text)"
}
if (-not (Test-Path -LiteralPath $pdb -PathType Leaf)) {
    throw "Deleted linker PDB was not repaired"
}
if ($pdbRepair.Text -notmatch '\[link\]\s+link_side_output_probe\.exe') {
    throw "Deleted linker PDB did not invalidate the warm link cache:`n$($pdbRepair.Text)"
}

Remove-Item -LiteralPath $manifest -Force
$manifestRepair = Invoke-ProbeBuild
if ($manifestRepair.ExitCode -ne 0) {
    throw "Manifest repair build failed:`n$($manifestRepair.Text)"
}
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Deleted external linker manifest was not repaired"
}
if ($manifestRepair.Text -notmatch '\[link\]\s+link_side_output_probe\.exe') {
    throw "Deleted external manifest did not invalidate the warm link cache:`n$($manifestRepair.Text)"
}

Remove-Item -LiteralPath $map -Force
$mapRepair = Invoke-ProbeBuild
if ($mapRepair.ExitCode -ne 0) {
    throw "MAP repair build failed:`n$($mapRepair.Text)"
}
if (-not (Test-Path -LiteralPath $map -PathType Leaf)) {
    throw "Deleted linker mapfile was not repaired"
}
if ($mapRepair.Text -notmatch '\[link\]\s+link_side_output_probe\.exe') {
    throw "Deleted mapfile did not invalidate the warm link cache:`n$($mapRepair.Text)"
}

Write-Host 'Real MSVC linker PDB/conditional-manifest/MAP repair checks passed.'
exit 0

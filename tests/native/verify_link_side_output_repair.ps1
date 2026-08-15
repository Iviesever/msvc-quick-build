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
Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8 -Value 'int main() { return 0; }'

function Invoke-ProbeBuild {
    param([string[]]$ExtraArguments = @())

    Push-Location $fixture
    try {
        $output = @(& $MqbPath build main.cpp --debug --no-discover -o link_side_output_probe @ExtraArguments 2>&1)
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

$binary = Join-Path $fixture '.mqb/bin/link_side_output_probe.exe'
$pdb = Join-Path $fixture '.mqb/bin/link_side_output_probe.pdb'
$manifest = Join-Path $fixture '.mqb/bin/link_side_output_probe.exe.manifest'

$cold = Invoke-ProbeBuild
if ($cold.ExitCode -ne 0) {
    throw "Cold Debug side-output probe failed:`n$($cold.Text)"
}
foreach ($path in @($binary, $pdb, $manifest)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Cold Debug link did not produce required output: $path`n$($cold.Text)"
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

$map = Invoke-ProbeBuild -ExtraArguments @('/link', '/MAP')
if ($map.ExitCode -eq 0) {
    throw "Untracked /MAP output was unexpectedly admitted"
}
if ($map.Text -notmatch 'mapfile output is not yet represented') {
    throw "Rejected /MAP did not explain the artifact-ownership boundary:`n$($map.Text)"
}

Write-Host 'Real MSVC linker PDB/manifest repair and /MAP fail-closed checks passed.'

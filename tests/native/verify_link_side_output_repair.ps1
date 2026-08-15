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
Set-Content -LiteralPath (Join-Path $fixture 'static.cpp') -Encoding utf8 -Value 'int static_value() { return 42; }'

$binary = Join-Path $fixture '.mqb/bin/link_side_output_probe.exe'
$pdb = Join-Path $fixture '.mqb/bin/link_side_output_probe.pdb'
$manifest = Join-Path $fixture '.mqb/bin/link_side_output_probe.exe.manifest'
$map = Join-Path $fixture 'link_side_output_probe.map'
$staticLibrary = Join-Path $fixture '.mqb/bin/link_repro_static.lib'
$linkRepro = Join-Path $fixture 'ambient-link-repro'
New-Item -ItemType Directory -Path $linkRepro -Force | Out-Null

function Invoke-WithAmbientLinkRepro {
    param([Parameter(Mandatory = $true)][scriptblock]$Action)

    $hadPrevious = Test-Path Env:link_repro
    $previous = if ($hadPrevious) { $env:link_repro } else { $null }
    $env:link_repro = $linkRepro
    try {
        & $Action
    }
    finally {
        if ($hadPrevious) {
            $env:link_repro = $previous
        }
        else {
            Remove-Item Env:link_repro -ErrorAction SilentlyContinue
        }
    }
}

function Assert-NoAmbientLinkReproArtifacts {
    $artifacts = @(Get-ChildItem -LiteralPath $linkRepro -Force -ErrorAction Stop)
    if ($artifacts.Count -ne 0) {
        throw "Ambient link_repro leaked diagnostic artifacts: $($artifacts.FullName -join ', ')"
    }
}

function Invoke-ProbeBuild {
    Push-Location $fixture
    try {
        $output = @(
            Invoke-WithAmbientLinkRepro {
                & $MqbPath build main.cpp --debug --no-discover -o link_side_output_probe `
                    /link "/MAP:$map" 2>&1
            }
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
Assert-NoAmbientLinkReproArtifacts

$warm = Invoke-ProbeBuild
if ($warm.ExitCode -ne 0) {
    throw "Warm side-output probe failed:`n$($warm.Text)"
}
if ($warm.Text -notmatch '\[up-to-date\]\s+link_side_output_probe\.exe') {
    throw "Warm build did not reuse sealed link outputs:`n$($warm.Text)"
}
Assert-NoAmbientLinkReproArtifacts

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
Assert-NoAmbientLinkReproArtifacts

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
Assert-NoAmbientLinkReproArtifacts

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
Assert-NoAmbientLinkReproArtifacts

Push-Location $fixture
try {
    $staticOutput = @(
        Invoke-WithAmbientLinkRepro {
            & $MqbPath build static.cpp --debug --no-discover --type static -o link_repro_static 2>&1
        }
    )
    $staticExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($staticExit -ne 0) {
    throw "Static-library ambient link_repro probe failed:`n$($staticOutput -join [Environment]::NewLine)"
}
if (-not (Test-Path -LiteralPath $staticLibrary -PathType Leaf)) {
    throw "Static-library ambient link_repro probe did not produce expected archive: $staticLibrary"
}
Assert-NoAmbientLinkReproArtifacts

$explicitRepro = Join-Path $fixture 'explicit-link-repro'
New-Item -ItemType Directory -Path $explicitRepro -Force | Out-Null
Push-Location $fixture
try {
    $rejected = @(& $MqbPath build main.cpp --debug --no-discover -o rejected_repro /link "/LINKREPRO:$explicitRepro" 2>&1)
    $rejectedExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($rejectedExit -eq 0) {
    throw "Explicit /LINKREPRO artifact mode was unexpectedly admitted"
}
$rejectedText = $rejected -join [Environment]::NewLine
if ($rejectedText -notmatch 'diagnostic artifact') {
    throw "Rejected /LINKREPRO did not explain its artifact-ownership boundary:`n$rejectedText"
}
if (@(Get-ChildItem -LiteralPath $explicitRepro -Force).Count -ne 0) {
    throw "Rejected /LINKREPRO still produced diagnostic artifacts"
}

# Raw /WHOLEARCHIVE:<library> is itself a required LINK input. It must not need
# a duplicate structured --lib declaration merely to participate in freshness.
$wholeArchiveRoot = Join-Path $fixture 'wholearchive'
New-Item -ItemType Directory -Path $wholeArchiveRoot -Force | Out-Null
$wholeLibrarySource = Join-Path $wholeArchiveRoot 'library.cpp'
$wholeMainSource = Join-Path $wholeArchiveRoot 'consumer.cpp'
Set-Content -LiteralPath $wholeLibrarySource -Encoding utf8 -Value 'int whole_value() { return 100; }'
Set-Content -LiteralPath $wholeMainSource -Encoding utf8 -Value @(
    'int whole_value();',
    'int main() { return whole_value() == 100 ? 0 : 1; }'
)

function Invoke-WholeArchiveLibraryBuild {
    Push-Location $wholeArchiveRoot
    try {
        $output = @(& $MqbPath build library.cpp --debug --no-discover --type static -o whole_input 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    [PSCustomObject]@{ ExitCode = $exitCode; Text = ($output -join [Environment]::NewLine) }
}

function Invoke-WholeArchiveConsumerBuild {
    Push-Location $wholeArchiveRoot
    try {
        $output = @(
            & $MqbPath build consumer.cpp --debug --no-discover -o whole_consumer `
                -L '.mqb/bin' /link '/WHOLEARCHIVE:whole_input.lib' 2>&1
        )
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    [PSCustomObject]@{ ExitCode = $exitCode; Text = ($output -join [Environment]::NewLine) }
}

$wholeLibrary = Join-Path $wholeArchiveRoot '.mqb/bin/whole_input.lib'
$wholeExecutable = Join-Path $wholeArchiveRoot '.mqb/bin/whole_consumer.exe'
$wholeColdLibrary = Invoke-WholeArchiveLibraryBuild
if ($wholeColdLibrary.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $wholeLibrary -PathType Leaf)) {
    throw "WHOLEARCHIVE fixture library cold build failed:`n$($wholeColdLibrary.Text)"
}
$wholeCold = Invoke-WholeArchiveConsumerBuild
if ($wholeCold.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $wholeExecutable -PathType Leaf)) {
    throw "Raw WHOLEARCHIVE cold consumer build failed:`n$($wholeCold.Text)"
}
& $wholeExecutable
if ($LASTEXITCODE -ne 0) {
    throw "Raw WHOLEARCHIVE cold executable did not consume the library"
}

$wholeWarm = Invoke-WholeArchiveConsumerBuild
if ($wholeWarm.ExitCode -ne 0) {
    throw "Raw WHOLEARCHIVE warm consumer build failed:`n$($wholeWarm.Text)"
}
if ($wholeWarm.Text -notmatch '\[up-to-date\]\s+whole_consumer\.exe') {
    throw "Raw WHOLEARCHIVE warm build did not reuse link cache:`n$($wholeWarm.Text)"
}

$wholeExecutableTime = (Get-Item -LiteralPath $wholeExecutable).LastWriteTimeUtc
Set-Content -LiteralPath $wholeLibrarySource -Encoding utf8 -Value 'int whole_value() { return 101; }'
$wholeChangedLibrary = Invoke-WholeArchiveLibraryBuild
if ($wholeChangedLibrary.ExitCode -ne 0) {
    throw "WHOLEARCHIVE fixture library rebuild failed:`n$($wholeChangedLibrary.Text)"
}
(Get-Item -LiteralPath $wholeLibrary).LastWriteTimeUtc = $wholeExecutableTime.AddSeconds(2)

$wholeChanged = Invoke-WholeArchiveConsumerBuild
if ($wholeChanged.ExitCode -ne 0) {
    throw "Raw WHOLEARCHIVE changed-library build failed:`n$($wholeChanged.Text)"
}
if ($wholeChanged.Text -notmatch '\[up-to-date\]\s+consumer\.cpp') {
    throw "WHOLEARCHIVE library-only change unexpectedly recompiled consumer.cpp:`n$($wholeChanged.Text)"
}
if ($wholeChanged.Text -notmatch '\[link\]\s+whole_consumer\.exe') {
    throw "Changed raw WHOLEARCHIVE library did not trigger relink:`n$($wholeChanged.Text)"
}
if ($wholeChanged.Text -notmatch 'link inputs changed') {
    throw "WHOLEARCHIVE library-driven relink was not explained as link inputs changed:`n$($wholeChanged.Text)"
}
& $wholeExecutable
if ($LASTEXITCODE -eq 0) {
    throw "Relinked WHOLEARCHIVE executable still contains stale library behavior"
}

Write-Host 'Real MSVC linker side-output/repro isolation plus raw WHOLEARCHIVE freshness checks passed.'
exit 0

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

$fixture = Join-Path $RepoRoot 'native-test-work/kasan-ownership'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8 -Value 'int main() { return 0; }'

Push-Location $fixture
try {
    $rejected = @(
        & $MqbPath build main.cpp --debug --no-discover -o kasan_rejected `
            /fsanitize=kernel-address 2>&1
    )
    $rejectedExit = $LASTEXITCODE
}
finally {
    Pop-Location
}

$rejectedText = $rejected -join [Environment]::NewLine
if ($rejectedExit -eq 0) {
    throw "Kernel AddressSanitizer unexpectedly reached the native exe pipeline and built successfully"
}
if ($rejectedText -notmatch '/fsanitize=kernel-address') {
    throw "Rejected KASAN request did not identify the native compiler option:`n$rejectedText"
}
if ($rejectedText -notmatch 'WDK kernel-driver pipeline') {
    throw "Rejected KASAN request did not explain the first-class driver ownership boundary:`n$rejectedText"
}
if ($rejectedText -notmatch 'target-OS-matched Windows SDK') {
    throw "Rejected KASAN request did not explain the target SDK compatibility-library requirement:`n$rejectedText"
}

$unexpectedExe = Join-Path $fixture '.mqb/bin/kasan_rejected.exe'
if (Test-Path -LiteralPath $unexpectedExe -PathType Leaf) {
    throw "Rejected KASAN request unexpectedly produced a native executable: $unexpectedExe"
}

Write-Host 'MSVC /fsanitize=kernel-address WDK/driver ownership boundary check passed.'
exit 0

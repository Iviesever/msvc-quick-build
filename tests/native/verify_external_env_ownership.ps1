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

$fixture = Join-Path $RepoRoot 'native-test-work/external-env-ownership'
if (Test-Path -LiteralPath $fixture) {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
$includeDir = Join-Path $fixture 'external include'
New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
Set-Content -LiteralPath (Join-Path $includeDir 'external_value.hpp') -Encoding utf8 -Value '#define MQB_EXTERNAL_VALUE 73'
Set-Content -LiteralPath (Join-Path $fixture 'main.cpp') -Encoding utf8 -Value @(
    '#include <external_value.hpp>',
    'int main() { return MQB_EXTERNAL_VALUE == 73 ? 0 : 1; }'
)

$hadPrevious = Test-Path Env:MQB_EXTERNAL_INCLUDE
$previous = if ($hadPrevious) { $env:MQB_EXTERNAL_INCLUDE } else { $null }
$env:MQB_EXTERNAL_INCLUDE = $includeDir
try {
    Push-Location $fixture
    try {
        $rejected = @(
            & $MqbPath build main.cpp --debug --no-discover -o external_env_rejected `
                /external:W0 /external:env:MQB_EXTERNAL_INCLUDE 2>&1
        )
        $rejectedExit = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    if ($hadPrevious) {
        $env:MQB_EXTERNAL_INCLUDE = $previous
    }
    else {
        Remove-Item Env:MQB_EXTERNAL_INCLUDE -ErrorAction SilentlyContinue
    }
}

$rejectedText = $rejected -join [Environment]::NewLine
if ($rejectedExit -eq 0) {
    throw "Environment-backed /external:env unexpectedly reached cl.exe and compiled successfully"
}
if ($rejectedText -notmatch '/external:env:MQB_EXTERNAL_INCLUDE') {
    throw "Rejected /external:env request did not identify the native option:`n$rejectedText"
}
if ($rejectedText -notmatch 'environment-backed external include') {
    throw "Rejected /external:env request did not explain the hidden include-search ownership boundary:`n$rejectedText"
}

# The deterministic native replacement stays fully supported: explicit
# /external:I carries the actual directory in argv/compile identity and MQB's
# discovery/include model rather than naming ambient environment state.
Push-Location $fixture
try {
    $explicit = @(
        & $MqbPath run main.cpp --debug --no-discover -o external_explicit `
            /external:W0 "/external:I$includeDir" 2>&1
    )
    $explicitExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($explicitExit -ne 0) {
    throw "Explicit /external:I replacement failed:`n$($explicit -join [Environment]::NewLine)"
}

Write-Host 'MSVC /external:env ownership boundary and explicit /external:I replacement checks passed.'
exit 0

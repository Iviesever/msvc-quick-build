[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$ChangedPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$exactPaths = @(
    'VERSION',
    '.github/workflows/native-ci.yml'
)

$pathPrefixes = @(
    'cpp/',
    'tests/ci/',
    'tests/native/',
    'tests/selfhost/'
)

foreach ($path in $ChangedPath) {
    if ([string]::IsNullOrWhiteSpace($path)) {
        continue
    }

    $normalized = $path.Trim().Replace('\', '/')
    while ($normalized.StartsWith('./', [System.StringComparison]::Ordinal)) {
        $normalized = $normalized.Substring(2)
    }

    if ($exactPaths.Contains($normalized)) {
        Write-Output 'true'
        exit 0
    }

    foreach ($prefix in $pathPrefixes) {
        if ($normalized.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
            Write-Output 'true'
            exit 0
        }
    }
}

Write-Output 'false'

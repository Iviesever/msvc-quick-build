[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [int]::MaxValue)]
    [int]$ExpectedChangedFileCount,

    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$PullRequestFilesJson,

    [Parameter(Mandatory = $false)]
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($ExpectedChangedFileCount -lt 1) {
    throw "Pull request reported an invalid changed-file count: $ExpectedChangedFileCount"
}

$json = $PullRequestFilesJson.Trim()
if (-not ($json.StartsWith('[', [System.StringComparison]::Ordinal) -and
          $json.EndsWith(']', [System.StringComparison]::Ordinal))) {
    throw 'Pull-request files response must be a JSON array.'
}

$jsonDocument = $null
try {
    $jsonDocument = [System.Text.Json.JsonDocument]::Parse($json)
    if ($jsonDocument.RootElement.ValueKind -ne [System.Text.Json.JsonValueKind]::Array) {
        throw 'Pull-request files response must be a JSON array.'
    }
}
catch {
    throw "Pull-request files response is not strict JSON: $($_.Exception.Message)"
}
finally {
    if ($null -ne $jsonDocument) {
        $jsonDocument.Dispose()
    }
}

$pages = ConvertFrom-Json -InputObject $json -Depth 20 -NoEnumerate
$files = [System.Collections.Generic.List[object]]::new()
foreach ($page in $pages) {
    if ($page -isnot [System.Collections.IList]) {
        throw 'Pull-request files response contains a non-array page.'
    }
    foreach ($file in $page) {
        $files.Add($file)
    }
}

if ($files.Count -ne $ExpectedChangedFileCount) {
    throw "Pull-request file enumeration is incomplete: expected $ExpectedChangedFileCount, received $($files.Count)."
}

$paths = [System.Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    $filenameProperty = $file.PSObject.Properties['filename']
    if ($null -eq $filenameProperty -or [string]::IsNullOrWhiteSpace([string]$filenameProperty.Value)) {
        throw 'Pull-request file entry is missing filename.'
    }
    $paths.Add([string]$filenameProperty.Value)

    $previousFilenameProperty = $file.PSObject.Properties['previous_filename']
    if ($null -ne $previousFilenameProperty -and
        -not [string]::IsNullOrWhiteSpace([string]$previousFilenameProperty.Value)) {
        $paths.Add([string]$previousFilenameProperty.Value)
    }
}

$classifier = Join-Path $RepoRoot 'tests/ci/classify_native_ci_impact.ps1'
if (-not (Test-Path -LiteralPath $classifier -PathType Leaf)) {
    throw "Native-impact classifier is missing: $classifier"
}

& $classifier -ChangedPath $paths.ToArray()

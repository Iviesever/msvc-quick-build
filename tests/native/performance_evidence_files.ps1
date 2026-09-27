# File-only evidence helpers. No executable invocation, timer, retry or environment dump.
function Write-PerformanceEvidence {
    param([Parameter(Mandatory)][string]$Directory,
          [Parameter(Mandatory)][string]$Name,
          [Parameter(Mandatory)]$Value)
    if ($Name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*\.json$') {
        throw 'Evidence name must be one JSON leaf.'
    }
    $path = Join-Path $Directory $Name
    # Serialize before opening; create-new refuses old/partial evidence, even in races.
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 24) + "`n")
    $file = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $file.Write($bytes, 0, $bytes.Length) } finally { $file.Dispose() }
}

function New-PerformanceEvidenceDirectory {
    param([Parameter(Mandatory)][string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $full) { throw 'Fresh performance evidence directory required.' }
    $null = New-Item -ItemType Directory -Path $full -ErrorAction Stop
    return $full
}

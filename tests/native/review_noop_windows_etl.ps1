param([Parameter(Mandatory)][string]$WorkRoot, [Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows -or -not [Environment]::Is64BitProcess -or $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'Only x64 Windows / PowerShell 7 native file decoding is supported.'
}
$out = [IO.Path]::GetFullPath($OutputRoot)
$work = [IO.Path]::GetFullPath($WorkRoot)
$inputData = Get-Content (Join-Path $out 'input.json') -Raw | ConvertFrom-Json
if ($inputData.capture_run -cne '35872432138' -or $inputData.traces.Count -ne 12 -or $inputData.clears_hold) {
    throw 'Wrong fixed offline review inputs.'
}
$source = Join-Path $PSScriptRoot 'read_noop_windows_etl.cs'
Copy-Item -LiteralPath $source -Destination (Join-Path $out 'reader-source.cs')
@{reader_sha256=(Get-FileHash $source -Algorithm SHA256).Hash.ToLowerInvariant()
  powershell=$PSVersionTable.PSVersion.ToString(); os=[Environment]::OSVersion.VersionString
  image_version=$env:ImageVersion; execution_sha=$env:GITHUB_SHA; run_id=$env:GITHUB_RUN_ID
  new_mqb_calls=0; new_etw_sessions=0; clears_hold=$false} |
  ConvertTo-Json | Set-Content (Join-Path $out 'native-host.json') -Encoding utf8

Add-Type -Path $source
$markerProvider = [Guid]'ce1dbfb4-137e-4da6-87b0-3f59aa102cbc'
$cases = @(
    @(150, [Guid]::Empty, 0, $true), @(100, [Guid]::Empty, 0, $true),
    @(200, [Guid]::Empty, 0, $true), @(99, [Guid]::Empty, 34, $false),
    @(99, $markerProvider, 34, $true), @(201, $markerProvider, 35, $false)
)
foreach ($case in $cases) {
    $actual = [MqbOfflineEtl.Reader]::Select([long]$case[0],100,200,[Guid]$case[1],[byte]$case[2])
    if ($actual -ne [bool]$case[3]) { throw 'Native compiled selection contract failed.' }
}
@{passed=6; capture_invocations=0} | ConvertTo-Json |
    Set-Content (Join-Path $out 'selection-test.json') -Encoding utf8
foreach ($trace in $inputData.traces) {
    $path = Join-Path $work ('evidence/traces/' + $trace.cell + '/trace.etl')
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $trace.sha256) {
        throw 'Trace changed before native decode.'
    }
    [MqbOfflineEtl.Reader]::Read($path, (Join-Path $out $trace.cell), [long]$trace.lo, [long]$trace.hi)
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $trace.sha256) {
        throw 'Trace changed after native decode.'
    }
    Write-Host ('Decoded existing ETL: ' + $trace.cell)
}
@{status='native_file_decode_complete_analysis_pending'; traces=12
  new_mqb_calls=0; new_etw_sessions=0; clears_hold=$false; cause=$null} |
  ConvertTo-Json | Set-Content (Join-Path $out 'completion.json') -Encoding utf8

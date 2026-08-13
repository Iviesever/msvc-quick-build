[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuilderMqbPath,
    [Parameter(Mandatory = $true)][string]$TestMqbPath,
    [Parameter(Mandatory = $true)][string]$RepoRoot,
    [ValidateSet('Debug', 'Release')][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$SharedProductLibraryPath,
    [ValidateRange(0, 63)][int]$OuterShardIndex,
    [ValidateRange(1, 64)][int]$OuterShardCount = 4,
    [ValidateRange(0, 63)][int]$SubshardA,
    [ValidateRange(0, 63)][int]$SubshardB,
    [ValidateRange(1, 64)][int]$ShardCount = 8
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

if ($OuterShardIndex -ge $OuterShardCount) {
    throw "OuterShardIndex must be smaller than OuterShardCount: $OuterShardIndex >= $OuterShardCount"
}
if ($SubshardA -ge $ShardCount -or $SubshardB -ge $ShardCount) {
    throw "Subshard index must be smaller than ShardCount $ShardCount: $SubshardA,$SubshardB"
}
if ($SubshardB -ne ($SubshardA + 1) -or $SubshardA -ne ($OuterShardIndex * 2)) {
    throw "Invalid outer/subshard mapping: outer=$OuterShardIndex pair=$SubshardA,$SubshardB"
}
if ($ShardCount -ne ($OuterShardCount * 2)) {
    throw "Paired native shards require ShardCount == 2 * OuterShardCount: $ShardCount vs $OuterShardCount"
}

$RepoRoot = Get-FullPath $RepoRoot
$BuilderMqbPath = Get-FullPath $BuilderMqbPath
$TestMqbPath = Get-FullPath $TestMqbPath
$SharedProductLibraryPath = Get-FullPath $SharedProductLibraryPath
$testScript = Join-Path $RepoRoot 'tests/native/run_native_tests.ps1'
foreach ($path in @($BuilderMqbPath, $TestMqbPath, $SharedProductLibraryPath, $testScript)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required paired native-test input not found: $path"
    }
}

# run_native_tests.ps1 intentionally communicates success/failure with `exit`.
# Launch each subshard in its own pwsh.exe process so that exit remains local to
# the child and the parent can audit each PID, output stream, and exit code.
$configurationKey = $Configuration.ToLowerInvariant()
$logRoot = Join-Path $RepoRoot "native-test-work/$configurationKey/outer-$OuterShardIndex-of-$OuterShardCount/subshard-logs"
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$pwshExe = (Get-Command pwsh -ErrorAction Stop).Source

function Start-NativeSubshard {
    param([Parameter(Mandatory = $true)][int]$Index)

    $stdout = Join-Path $logRoot "subshard-$Index.stdout.log"
    $stderr = Join-Path $logRoot "subshard-$Index.stderr.log"
    Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    $arguments = @(
        '-NoLogo',
        '-NoProfile',
        '-NonInteractive',
        '-File', $testScript,
        '-BuilderMqbPath', $BuilderMqbPath,
        '-TestMqbPath', $TestMqbPath,
        '-RepoRoot', $RepoRoot,
        '-Configuration', $Configuration,
        '-ShardIndex', [string]$Index,
        '-ShardCount', [string]$ShardCount,
        '-SharedProductLibraryPath', $SharedProductLibraryPath
    )
    $process = Start-Process `
        -FilePath $pwshExe `
        -ArgumentList $arguments `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr

    Write-Host "Started $Configuration subshard $Index/$ShardCount as PID $($process.Id)"
    return [PSCustomObject]@{
        Index = $Index
        Process = $process
        Stdout = $stdout
        Stderr = $stderr
    }
}

Write-Host "Outer $Configuration shard $OuterShardIndex/$OuterShardCount owns subshards $SubshardA/$ShardCount and $SubshardB/$ShardCount"
$childA = Start-NativeSubshard -Index $SubshardA
$childB = Start-NativeSubshard -Index $SubshardB
$children = @($childA, $childB)
$uniqueIndices = @($children | Select-Object -ExpandProperty Index -Unique)
if ($uniqueIndices.Count -ne 2) {
    throw "Subshard identity collision before execution: $($uniqueIndices -join ',')"
}

foreach ($child in $children) {
    $child.Process.WaitForExit()
}

$failed = $false
foreach ($child in @($children | Sort-Object Index)) {
    Write-Host ''
    Write-Host "===== $Configuration subshard $($child.Index)/$ShardCount ====="
    if (Test-Path -LiteralPath $child.Stdout) {
        Get-Content -LiteralPath $child.Stdout | ForEach-Object { Write-Host $_ }
    }
    if (Test-Path -LiteralPath $child.Stderr) {
        $stderrLines = @(Get-Content -LiteralPath $child.Stderr)
        if ($stderrLines.Count -ne 0) {
            Write-Host '----- stderr -----'
            $stderrLines | ForEach-Object { Write-Host $_ }
        }
    }
    $exitCode = $child.Process.ExitCode
    Write-Host "$Configuration subshard $($child.Index)/$ShardCount exit code: $exitCode"
    if ($exitCode -ne 0) {
        $failed = $true
    }
}

if ($failed) {
    exit 1
}
Write-Host "Paired $Configuration native subshards passed."
exit 0

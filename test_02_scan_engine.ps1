$ErrorActionPreference = 'Stop'

$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PWD "build.ps1"), [ref]$null, [ref]$null)
$funcAst = $ast.FindAll({$args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $args[0].Name -eq 'Invoke-MacroBatchScan'}, $true)
if (-not $funcAst) { throw "未找到 Invoke-MacroBatchScan 函数" }

Invoke-Expression $funcAst[0].Extent.Text

Write-Host "开始测试 Invoke-MacroBatchScan..."

$testDir = Join-Path $PWD "test_scan_out"
if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
New-Item -ItemType Directory -Path $testDir | Out-Null

$cpp1 = Join-Path $testDir "a.cpp"
$cpp2 = Join-Path $testDir "b.cpp"

Set-Content -Path $cpp1 -Value "int main(){}"
Set-Content -Path $cpp2 -Value "int b(){return 0;}"

try {
    # Test 1: CLI mode
    Invoke-MacroBatchScan -SourceFiles @($cpp1, $cpp2) -CacheDir $testDir -BaseFlags "/std:c++20 /EHsc"
    
    $jsonFiles = Get-ChildItem -Path $testDir -Filter "*.json"
    if ($jsonFiles.Count -lt 2) {
        throw "Test 1 失败：未能生成所有的 JSON 文件"
    }
    Write-Host "Test 1: Passed (CLI Mode)"

    Remove-Item $jsonFiles.FullName -Force

    # Test 2: RSP mode
    $realDummies = @()
    for ($i=0; $i -lt 150; $i++) {
        $path = Join-Path $testDir ("dummy_" + ('x' * 60) + "_$i.cpp")
        Set-Content -Path $path -Value "int d$i(){return 0;}"
        $realDummies += $path
    }

    Invoke-MacroBatchScan -SourceFiles $realDummies -CacheDir $testDir -BaseFlags "/std:c++20 /EHsc"
    
    if (-not (Test-Path (Join-Path $testDir "scan.rsp"))) {
        throw "Test 2 失败：未生成 scan.rsp 文件"
    }
    $jsonFilesRsp = Get-ChildItem -Path $testDir -Filter "*.json"
    if ($jsonFilesRsp.Count -lt 150) {
        throw "Test 2 失败：RSP 模式下未成功为所有文件生成 JSON ($($jsonFilesRsp.Count)/150)"
    }
    Write-Host "Test 2: Passed (RSP Mode)"

    Write-Host "测试通过！ [Invoke-MacroBatchScan]" -ForegroundColor Green
}
finally {
    if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
}

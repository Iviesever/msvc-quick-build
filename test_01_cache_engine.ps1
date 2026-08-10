$ErrorActionPreference = 'Stop'

# 通过 AST 提取 build.ps1 中的 Get-CompilerStateHash 函数进行隔离测试
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PWD "build.ps1"), [ref]$null, [ref]$null)
$funcAst = $ast.FindAll({$args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $args[0].Name -eq 'Get-CompilerStateHash'}, $true)
if (-not $funcAst) { throw "未在 build.ps1 中找到 Get-CompilerStateHash 函数" }

Invoke-Expression $funcAst[0].Extent.Text

Write-Host "开始测试 Get-CompilerStateHash..."

$dummyCompiler = ".\dummy_compiler.exe"
Set-Content -Path $dummyCompiler -Value "dummy"
(Get-Item $dummyCompiler).LastWriteTimeUtc = Get-Date "2026-01-01 12:00:00Z"

try {
    $args1 = @("/O2", "/std:c++20", "-I", "C:\include")
    $hash1 = Get-CompilerStateHash -RawArgs $args1 -CompilerPath $dummyCompiler

    if ([string]::IsNullOrWhiteSpace($hash1)) { throw "Test 1 失败：哈希为空" }
    Write-Host "Test 1: $hash1"

    $args2 = @("/nologo", "/O2", "/MP", "/std:c++20", "-I", "C:\include", "/showIncludes")
    $hash2 = Get-CompilerStateHash -RawArgs $args2 -CompilerPath $dummyCompiler

    if ($hash1 -ne $hash2) { throw "Test 2 失败：噪音参数改变了哈希值 (`n$hash1 != `n$hash2)" }
    Write-Host "Test 2: Passed"

    $args3 = @("/Od", "/std:c++20", "-I", "C:\include")
    $hash3 = Get-CompilerStateHash -RawArgs $args3 -CompilerPath $dummyCompiler

    if ($hash1 -eq $hash3) { throw "Test 3 失败：核心参数修改未改变哈希值" }
    Write-Host "Test 3: Passed"

    (Get-Item $dummyCompiler).LastWriteTimeUtc = Get-Date "2026-02-01 12:00:00Z"
    $hash4 = Get-CompilerStateHash -RawArgs $args1 -CompilerPath $dummyCompiler

    if ($hash1 -eq $hash4) { throw "Test 4 失败：编译器时间戳变更未改变哈希值" }
    Write-Host "Test 4: Passed"

    $expectedError = $false
    try {
        Get-CompilerStateHash -RawArgs $args1 -CompilerPath ".\not_exist.exe" | Out-Null
    } catch [System.IO.FileNotFoundException] {
        $expectedError = $true
    }
    if (-not $expectedError) { throw "Test 5 失败：未抛出预期的 FileNotFoundException" }
    Write-Host "Test 5: Passed"

    Write-Host "测试通过！ [Get-CompilerStateHash]" -ForegroundColor Green
}
finally {
    if (Test-Path $dummyCompiler) { Remove-Item $dummyCompiler -Force }
}

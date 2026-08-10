$ErrorActionPreference = 'Stop'

$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PWD "build.ps1"), [ref]$null, [ref]$null)
# 提取所需要的全部依赖函数
$funcNames = @('Get-CompilerStateHash', 'Get-NormalizedDependencySet', 'Group-SourcesForConcurrentBuild', 'Invoke-Compile')
foreach ($name in $funcNames) {
    $funcAst = $ast.FindAll({$args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $args[0].Name -eq $name}, $true)
    if (-not $funcAst) { throw "未找到 $name 函数" }
    Invoke-Expression $funcAst[0].Extent.Text
}

Write-Host "开始测试 Invoke-Compile 与哈希分组并发管线..."

$testDir = Join-Path $PWD "test_compile_out"
if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
New-Item -ItemType Directory -Path $testDir | Out-Null

$dirA = Join-Path $testDir "A"
$dirB = Join-Path $testDir "B"
New-Item -ItemType Directory -Path $dirA | Out-Null
New-Item -ItemType Directory -Path $dirB | Out-Null

$cppA = Join-Path $dirA "utils.cpp"
$cppB = Join-Path $dirB "utils.cpp"

Set-Content -Path $cppA -Value "#include `"header.h`"`nint utilsA(){return 0;}"
Set-Content -Path $cppB -Value "int utilsB(){return 0;}"
Set-Content -Path (Join-Path $dirA "header.h") -Value "int a_header = 1;"

$ctx = @{
    Files = @($cppA, $cppB)
    IxxFiles = @()
    CppFiles = @($cppA, $cppB)
    BaseFlags = "/nologo /EHsc"
    AnyModuleRecompiled = $false
    BuildSuccess = $true
}

# 清理当前目录可能残存的旧缓存以防干扰
if (Test-Path ".cache") { Remove-Item ".cache" -Recurse -Force }

try {
    # 第一次编译
    Invoke-Compile $ctx

    if (-not $ctx.BuildSuccess) { throw "Test 1 失败：编译未成功" }
    
    $depsDir = Join-Path $PWD ".cache\deps"
    $hashDirs = Get-ChildItem $depsDir -Directory
    if ($hashDirs.Count -ne 2) {
        throw "Test 1 失败：预期的两组依赖物理隔离目录未生成，实际有 $($hashDirs.Count) 个"
    }

    $jsonCount = 0
    foreach ($dir in $hashDirs) {
        $jsons = Get-ChildItem $dir.FullName -Filter "*.json"
        $jsonCount += $jsons.Count
    }
    
    if ($jsonCount -ne 2) {
        throw "Test 1 失败：未能生成所有的 JSON 依赖树"
    }
    
    Write-Host "Test 1: Passed (初始编译生成了正确的并发哈希隔离缓存)"

    # 第二次编译，测试增量跳过 (Test-UpToDate 和 JSON 解析)
    $script:compileShown = $false
    Invoke-Compile $ctx
    # 如果发生重编，会打印 "1>  正在编译..." 并置位
    if ($script:compileShown) {
        throw "Test 2 失败：增量缓存击穿失效（不应发生重编）"
    }
    Write-Host "Test 2: Passed (无修改时精确命中缓存，完美跳过)"

    # 第三次编译，模拟幽灵依赖或依赖项变更
    # 我们修改 A\header.h
    Start-Sleep -Seconds 2
    Set-Content -Path (Join-Path $dirA "header.h") -Value "int a_header = 2;"
    $script:compileShown = $false
    Invoke-Compile $ctx
    if (-not $script:compileShown) {
        throw "Test 3 失败：头文件变更未能触发精确重编！依赖树追踪失败"
    }
    Write-Host "Test 3: Passed (头文件变更成功击穿增量缓存触发重编)"

    Write-Host "测试通过！ [Invoke-Compile]" -ForegroundColor Green
}
finally {
    # if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
    # if (Test-Path ".cache") { Remove-Item ".cache" -Recurse -Force }
}

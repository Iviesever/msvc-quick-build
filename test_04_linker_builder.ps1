$ErrorActionPreference = 'Stop'

$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PWD "build.ps1"), [ref]$null, [ref]$null)
$funcNames = @('Build-LinkerArguments')
foreach ($name in $funcNames) {
    $funcAst = $ast.FindAll({$args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $args[0].Name -eq $name}, $true)
    if (-not $funcAst) { throw "未找到 $name 函数" }
    Invoke-Expression $funcAst[0].Extent.Text
}

Write-Host "开始测试 Build-LinkerArguments 白名单独裁与组装..."

# Test 1: 普通 EXE 模式，未过滤任何参数
$ctxExe = @{
    Exe = "out.exe"
    Arch = "x64"
    IsDebug = $false
    Preset = $true
    Bound = @{ libs = @("mylib") }
    Cfg = @{ libpath = @("C:\mylibs") }
    CfgLtcg = $true
}

$argsExe = Build-LinkerArguments -TargetType "exe" -ConfigCtx $ctxExe
$argsStrExe = $argsExe -join ' '

if ($argsStrExe -notmatch '/LIBPATH:"C:\\mylibs"') { throw "Test 1 失败：EXE 模式丢失 LIBPATH" }
if ($argsStrExe -notmatch '"mylib\.lib"') { throw "Test 1 失败：EXE 模式丢失 libs" }
if ($argsStrExe -notmatch '"kernel32\.lib"') { throw "Test 1 失败：EXE 模式丢失系统库" }
if ($argsStrExe -notmatch '/LTCG') { throw "Test 1 失败：EXE 模式丢失 LTCG" }

Write-Host "Test 1: Passed (EXE 模式正常拼接)"

# Test 2: Static 模式，触发白名单独裁，剥离所有库
$ctxStatic = @{
    Exe = "out.lib"
    Arch = "x64"
    IsDebug = $false
    Preset = $true
    Bound = @{ libs = @("mylib"); link_flags = @("/LTCG", "/OPT:REF", "/DEF:exports.def") }
    Cfg = @{ libpath = @("C:\mylibs") }
    CfgLtcg = $true
}

$argsStatic = Build-LinkerArguments -TargetType "static" -ConfigCtx $ctxStatic
$argsStrStatic = $argsStatic -join ' '

if ($argsStrStatic -match '/LIBPATH') { throw "Test 2 失败：Static 模式未拦截 LIBPATH" }
if ($argsStrStatic -match '"mylib\.lib"') { throw "Test 2 失败：Static 模式未拦截外部库依赖" }
if ($argsStrStatic -notmatch '/LTCG') { throw "Test 2 失败：Static 模式未放行白名单参数 /LTCG" }
if ($argsStrStatic -notmatch '/DEF:exports\.def') { throw "Test 2 失败：Static 模式未放行白名单参数 /DEF" }
if ($argsStrStatic -match '/OPT:REF') { throw "Test 2 失败：Static 模式未拦截黑名单参数 /OPT:REF" }

Write-Host "Test 2: Passed (Static 模式严格独裁与白名单放行)"

Write-Host "测试通过！ [Build-LinkerArguments]" -ForegroundColor Green

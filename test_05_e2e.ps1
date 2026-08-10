$ErrorActionPreference = 'Stop'

$testDir = Join-Path $PWD "test_e2e_out"
if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
New-Item -ItemType Directory -Path $testDir | Out-Null
Set-Location $testDir

New-Item -ItemType Directory -Path "A" | Out-Null
New-Item -ItemType Directory -Path "B" | Out-Null

Set-Content -Path "A\utils.h" -Value "int get_utils_a();"
Set-Content -Path "A\utils.cpp" -Value "#include `"utils.h`"`nint get_utils_a(){return 1;}"
Set-Content -Path "B\utils.h" -Value "int get_utils_b();"
Set-Content -Path "B\utils.cpp" -Value "#include `"utils.h`"`nint get_utils_b(){return 2;}"
Set-Content -Path "main.cpp" -Value "
#include <iostream>
#include `"A/utils.h`"
#include `"B/utils.h`"
int main() {
    std::cout << get_utils_a() + get_utils_b() << std::endl;
    return 0;
}
"

$buildPs1 = Join-Path $PWD.Path "..\build.ps1"

Write-Host "========== Test 1: Full Build =========="
$output1 = & pwsh -NoProfile -ExecutionPolicy Bypass -File $buildPs1 "main.cpp" -I "." -o myapp
if ($LASTEXITCODE -ne 0) {
    Write-Host ($output1 -join "`n")
    throw "Test 1 失败：初始全量编译失败"
}
if (-not (Test-Path "myapp.exe")) { throw "Test 1 失败：未能生成 myapp.exe" }
Write-Host "Test 1: Passed"

Write-Host "========== Test 2: Incremental Zero-Rebuild =========="
$output2 = (& pwsh -NoProfile -ExecutionPolicy Bypass -File $buildPs1 "main.cpp" -I "." -o myapp) -join "`n"
if ($output2 -match "正在编译") {
    throw "Test 2 失败：发生了意外重编`n$output2"
}
if ($output2 -notmatch "1 最新") {
    throw "Test 2 失败：未识别出最新状态`n$output2"
}
Write-Host "Test 2: Passed"

Write-Host "========== Test 3: Header Invalidation (Partial Rebuild) =========="
Start-Sleep -Seconds 2
Add-Content -Path "A\utils.h" -Value "`n// Modified"
$output3 = (& pwsh -NoProfile -ExecutionPolicy Bypass -File $buildPs1 "main.cpp" -I "." -o myapp) -join "`n"
if ($output3 -notmatch "main\.cpp") {
    throw "Test 3 失败：未能重编 main.cpp（过度缓存）`n$output3"
}
$utilsCount = ([regex]::Matches($output3, '(?m)^1>\s*utils\.cpp\s*$')).Count
if ($utilsCount -ne 1) {
    throw "Test 3 失败：utils.cpp 的重编次数不为 1 (实际: $utilsCount)`n$output3"
}
Write-Host "Test 3: Passed"

Write-Host "========== Test 4: Toolchain/State Hash Invalidation (Full Rebuild) =========="
Start-Sleep -Seconds 2
$output4 = (& pwsh -NoProfile -ExecutionPolicy Bypass -File $buildPs1 "main.cpp" -I "." -o myapp -config release) -join "`n"
if ($output4 -notmatch "main\.cpp" -or $output4 -notmatch "utils\.cpp") {
    throw "Test 4 失败：修改编译配置未能触发全量重编`n$output4"
}
Write-Host "Test 4: Passed"

Set-Location ..
if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
Write-Host "测试通过！ [E2E 管线集成测试]" -ForegroundColor Green

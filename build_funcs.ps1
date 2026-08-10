]
    [string[]]$Sources,

    [Alias('output')]
    [string]$o,

    [switch]$run,

    [ValidateSet('14', '17', '20', '23', 'latest')]
    [string]$std,

    [Alias('a')]
    [string]$ProgramArgs,

    [switch]$x86,

    [Alias('include')]
    [string[]]$I,

    [Alias('libpath')]
    [string[]]$L,

    [string[]]$libs,

    [ValidateSet('debug', 'release')]
    [string]$config,

    [ValidateSet('unicode', 'mbcs')]
    [string]$charset,

    [ValidateSet('console', 'windows')]
    [string]$subsystem,

    [ValidateSet('Od', 'O1', 'O2', 'Ox')]
    [string]$optimize,

    [ValidateSet('MD', 'MDd', 'MT', 'MTd')]
    [string]$runtime,

    [ValidateSet('W0', 'W1', 'W3', 'W4', 'Wall')]
    [string]$warnings,

    [switch]$WX,

    [ValidateSet('off', 'Zi', 'ZI', 'Z7')]
    [string]$debug_info,

    [ValidateSet('EHsc', 'EHa', 'off')]
    [string]$exceptions,

    [switch]$rtc1,
    [switch]$jmc,
    [switch]$sdl,
    [switch]$permissive,

    [ValidateSet('precise', 'strict', 'fast')]
    [string]$fp,

    [switch]$ltcg,
    [switch]$incremental,

    [Alias('D')]
    [string[]]$defines,
    [string[]]$flags,
    [string[]]$link_flags,

    [ValidateSet('vs', 'portable', 'port', 'p', 'auto')]
    [Alias('env')]
    [string]$setenv,
    [Alias('h', '?')]
    [switch]$help
)

function Show-Help {
    Write-Host ''
    Write-Host 'build �?MSVC 快速编译工具（�?MSBuild 1:1 对齐�? -ForegroundColor Cyan
    Write-Host '  用法�?
    Write-Host '    build <源文�?..> [选项]'
    Write-Host ''
    Write-Host '  基础选项�? -ForegroundColor DarkCyan
    Write-Host '    -o <名称>          输出文件名（不含 .exe�?
    Write-Host '    -run               编译成功后自动运�?
    Write-Host '    -std <版本>        C++ 标准�?4 / 17 / 20 / 23 / latest'
    Write-Host '    -a <参数>          运行时传给程序的参数'
    Write-Host '    -x86               编译�?32 位（默认 64 位）'
    Write-Host ''
    Write-Host '  路径与依赖：' -ForegroundColor DarkCyan
    Write-Host '    -I <路径...>       头文件包含路径（别名 -include�?
    Write-Host '    -L <路径...>       库文件搜索路径（别名 -libpath�?
    Write-Host '    -libs <库名...>    链接库（不含 .lib�?
    Write-Host '    -D <�?..>         预处理器宏定义（别名 -defines�?
    Write-Host '    JSON: "exclude"    依赖解析时排除的文件通配�?
    Write-Host ''
    Write-Host '  工程配置（与 msvc_list.json 1:1 对应，CLI 优先级最高）�? -ForegroundColor DarkCyan
    Write-Host '    -config <预设>         配置预设：debug / release'
    Write-Host '    -optimize <级别>       优化：Od / O1 / O2 / Ox'
    Write-Host '    -runtime <类型>        运行库：MD / MDd / MT / MTd'
    Write-Host '    -warnings <级别>       警告：W0 / W1 / W3 / W4 / Wall'
    Write-Host '    -WX                    视警告为错误�?WX�?
    Write-Host '    -debug_info <模式>     调试信息：off / Zi / ZI / Z7'
    Write-Host '    -exceptions <模式>     异常处理：EHsc / EHa / off'
    Write-Host '    -fp <模式>             浮点模型：precise / strict / fast'
    Write-Host '    -charset <字符�?      字符宏：unicode / mbcs'
    Write-Host '    -rtc1                  运行时检查（/RTC1�?
    Write-Host '    -jmc                   Just My Code�?JMC�?
    Write-Host '    -sdl                   安全开发检查（/GS /sdl�?
    Write-Host '    -permissive            严格标准一致性（/permissive-�?
    Write-Host '    -ltcg                  全程序优化（/GL /LTCG�?
    Write-Host '    -flags <标志...>       追加编译器标志（直接透传�?
    Write-Host ''
    Write-Host '  链接器配置：' -ForegroundColor DarkCyan
    Write-Host '    -subsystem <子系�?    子系统：console / windows'
    Write-Host '    -incremental           增量链接�?INCREMENTAL�?
    Write-Host '    -link_flags <标志...>  追加链接器标志（直接透传�?
    Write-Host ''
    Write-Host '  环境管理�? -ForegroundColor DarkCyan
    Write-Host '    -env vs                强制使用本机 Visual Studio'
    Write-Host '    -env portable          强制使用便携版工具链'
    Write-Host '    -env auto              恢复自动检�?
    Write-Host ''
    Write-Host '  示例�? -ForegroundColor DarkCyan
    Write-Host '    build main.cpp -run                             # 编译运行'
    Write-Host '    build main.cpp -std latest -run                 # 指定 C++ 标准'
    Write-Host '    build main.cpp -config debug -run               # Debug 配置'
    Write-Host '    build main.cpp -config release -run             # Release 配置'
    Write-Host '    build *.cpp -o app -std 23 -config release -run # 多文�?+ Release'
    Write-Host '    build main.cpp mod.ixx -std latest -run         # C++20 Modules'
    Write-Host '    build solver.c -run -a "input.txt 42"           # 传参运行'
    Write-Host '    build main.cpp -x86 -run                        # 32 位编�?
    Write-Host ''
    Write-Host '  构建管线�? -ForegroundColor DarkCyan
    Write-Host '    源依赖解�?�?cl /scanDependencies �?cl /c /interface *.ixx  '
    Write-Host '               �?cl /c *.cpp �?link.exe'
    Write-Host '    · 从指定文�?BFS 扫描 #include / import，自动发现关联文�?
    Write-Host '    · 模块拓扑排序 + 分层并行编译（cl /MP�?
    Write-Host '    · .ixx / .cpp 按时间戳增量编译�?obj 未变更时跳过链接'
    Write-Host '    · import std 首次编译后缓存，config 预设自动链接 12 个默认库'
    Write-Host ''
    Write-Host '  项目配置：在目录创建 msvc_list.json（自动向上查�?5 层），参�?README' -ForegroundColor DarkGray
    Write-Host '  支持文件�?c  .cpp  .cxx  .cc  .ixx' -ForegroundColor DarkGray
    Write-Host '  GitHub：https://github.com/Iviesever/vscode-msvc-quick-build' -ForegroundColor DarkGray
    Write-Host ''
}

function Resolve-Sources([hashtable]$ctx) {
    $result = @()
    foreach ($src in $ctx.Sources) {
        if ($src -eq '-run' -or $src -eq '-x86') { continue }
        $resolved = @(Resolve-Path $src -ErrorAction SilentlyContinue)
        if ($resolved.Count -gt 0) {
            foreach ($r in $resolved) {
                $extRe = '\.(c|cpp|cxx|cc|ixx|h|hpp)$'
                if ($r.Path -match $extRe) {
                    $result += $r.Path
                }
            }
        }
        else {
            Write-Host "error : 找不到源文件 $src" -ForegroundColor Red
            exit 1
        }
    }
    if ($result.Count -eq 0) {
        Write-Host 'error : 未指定源文件' -ForegroundColor Red
        exit 1
    }
    $ctx.Files = $result
}

function Find-ProjectConfig([hashtable]$ctx) {
    $searchDir = $PWD.Path
    for ($lvl = 0; $lvl -lt 5; $lvl++) {
        $cfgPath = Join-Path $searchDir 'msvc_list.json'
        if (Test-Path $cfgPath) {
            $ctx.Cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
            $ctx.CfgDir = $searchDir
            Write-Host "1>  正在读取项目文件""msvc_list.json""�? -ForegroundColor DarkGray
            break
        }
        $parent = [System.IO.Path]::GetDirectoryName($searchDir)
        if (-not $parent -or $parent -eq $searchDir) { break }
        $searchDir = $parent
    }

    if ($ctx.Cfg -and -not $ctx.Std -and $ctx.Cfg.std) { $ctx.Std = $ctx.Cfg.std }
    if ($ctx.Cfg -and $ctx.Cfg.output -and -not $ctx.Bound.ContainsKey('o')) {
        $ctx.OutputName = $ctx.Cfg.output
    }
}

function Find-SmartDeps([hashtable]$ctx) {
    $focusedFile = $ctx.Files[0]
    $dir = [System.IO.Path]::GetDirectoryName($focusedFile)

    $allCandidates = @{}
    $dirItems = Get-ChildItem $dir -File -Recurse | Where-Object { $_.Extension -match '^\.(c|cpp|cxx|cc|ixx|h|hpp)$' }
    foreach ($f in $dirItems) {
        $raw = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
        if (-not $raw) { $raw = '' }
        $stripped = [regex]::Replace($raw, '/\*[\s\S]*?\*/', '')
        $stripped = [regex]::Replace($stripped, '//[^\n]*', '')
        $allCandidates[$f.FullName] = $stripped
    }

    $modProviders = @{}
    $fileModule = @{}
    foreach ($path in $allCandidates.Keys) {
        if ($path -match '\.ixx$') {
            $c = $allCandidates[$path]
            if ($c -match 'export\s+module\s+([\w.:]+)\s*;') {
                $fullModName = $Matches[1]
                $modProviders[$fullModName] = $path
                $fileModule[$path] = ($fullModName -split ':')[0]
            }
        }
    }

    $depGraph = @{}
    foreach ($path in $allCandidates.Keys) { $depGraph[$path] = @() }

    foreach ($path in $allCandidates.Keys) {
        $content = $allCandidates[$path]

        foreach ($m in [regex]::Matches($content, '#include\s+"([^"]+)"')) {
            $headerName = $m.Groups[1].Value
            $includerDir = [System.IO.Path]::GetDirectoryName($path)
            $headerPath = [System.IO.Path]::GetFullPath((Join-Path $includerDir $headerName))
            if ($allCandidates.ContainsKey($headerPath)) {
                $depGraph[$path] += $headerPath
                $depGraph[$headerPath] += $path

                $hdrBase = [System.IO.Path]::GetFileNameWithoutExtension($headerName)
                $hdrDir = [System.IO.Path]::GetDirectoryName($headerPath)
                foreach ($ext in @('.cpp', '.c', '.cxx', '.cc')) {
                    $implPath = [System.IO.Path]::GetFullPath((Join-Path $hdrDir "$hdrBase$ext"))
                    if ($allCandidates.ContainsKey($implPath)) {
                        $depGraph[$headerPath] += $implPath
                        $depGraph[$implPath] += $headerPath
                    }
                }
            }
        }

        foreach ($m in [regex]::Matches($content, '(?:export\s+)?import\s*(:[\w.]+|[\w.:]+)\s*;')) {
            $modName = $m.Groups[1].Value
            if ($modName -eq 'std' -or $modName -eq 'std.compat') { continue }
            if ($modName.StartsWith(':')) {
                $parentMod = $fileModule[$path]
                if ($parentMod) { $modName = "$parentMod$modName" }
            }
            if ($modProviders.ContainsKey($modName)) {
                $ixxPath = $modProviders[$modName]
                $depGraph[$path] += $ixxPath
                $depGraph[$ixxPath] += $path
            }
        }
    }

    $visited = @{}
    $bfsQueue = [System.Collections.Queue]::new()
    $bfsQueue.Enqueue($focusedFile)
    $visited[$focusedFile] = $true

    while ($bfsQueue.Count -gt 0) {
        $current = $bfsQueue.Dequeue()
        if ($depGraph.ContainsKey($current)) {
            foreach ($neighbor in $depGraph[$current]) {
                if (-not $visited.ContainsKey($neighbor)) {
                    $visited[$neighbor] = $true
                    $bfsQueue.Enqueue($neighbor)
                }
            }
        }
    }

    $ctx.SmartAllFiles = @($visited.Keys)

    $ctx.Files = @($visited.Keys | Where-Object {
        $_ -match '\.(c|cpp|cxx|cc|ixx)$'
    } | Where-Object {
        if ($_ -eq $focusedFile) { return $true }
        if ($_ -match '\.ixx$') { return $true }
        $fc = $allCandidates[$_]
        if ($fc -match '\bmain\s*\(') { return $false }
        if ($ctx.Cfg -and $ctx.Cfg.exclude) {
            $fn = [System.IO.Path]::GetFileName($_)
            foreach ($pat in $ctx.Cfg.exclude) {
                if ($fn -like $pat) { return $false }
            }
        }
        return $true
    } | Sort-Object)

    if ($ctx.Files.Count -eq 0) {
        Write-Host 'error : 未发现可编译的源文件' -ForegroundColor Red
        exit 1
    }

    if ($ctx.Files.Count -gt 1) {
        if ($ctx.Files.Count -le 8) {
            $names = ($ctx.Files | ForEach-Object { [System.IO.Path]::GetFileName($_) }) -join ', '
            Write-Host "1>  正在解析源依赖项: $names" -ForegroundColor DarkGray
        } else {
            $first5 = ($ctx.Files | Select-Object -First 5 | ForEach-Object { [System.IO.Path]::GetFileName($_) }) -join ', '
            Write-Host "1>  正在解析源依赖项: $first5 �?$($ctx.Files.Count) 个文�? -ForegroundColor DarkGray
        }
    }

    if (-not $ctx.OutputName) {
        $ctx.OutputName = [System.IO.Path]::GetFileNameWithoutExtension($focusedFile)
    }
}

function Resolve-Output([hashtable]$ctx) {
    $ctx.IxxFiles = @($ctx.Files | Where-Object { $_ -match '\.ixx$' })
    $ctx.CppFiles = @($ctx.Files | Where-Object { $_ -match '\.(cpp|cxx|cc|c)$' })

    if (-not $ctx.OutputName) {
        $firstCpp = $ctx.CppFiles | Select-Object -First 1
        if ($firstCpp) {
            $ctx.OutputName = [System.IO.Path]::GetFileNameWithoutExtension($firstCpp)
        }
        elseif ($ctx.IxxFiles.Count -gt 0) {
            $ctx.OutputName = [System.IO.Path]::GetFileNameWithoutExtension($ctx.IxxFiles[0])
        }
    }

    $ctx.HasCpp = ($ctx.CppFiles | Where-Object { $_ -match '\.(cpp|cxx|cc)$' }) -or ($ctx.IxxFiles.Count -gt 0)
}

function Get-CompilerStateHash([string[]]$RawArgs, [string]$CompilerPath) {
    if (-not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) {
        throw [System.IO.FileNotFoundException]::new("Compiler not found: $CompilerPath", $CompilerPath)
    }

    $compilerInfo = Get-Item -LiteralPath $CompilerPath
    $abiStamp = $compilerInfo.LastWriteTimeUtc.ToString("o")

    $noiseMask = @(
        '/nologo', '-nologo',
        '/MP', '-MP',
        '/FS', '-FS',
        '/showIncludes', '-showIncludes',
        '/errorReport:queue', '-errorReport:queue',
        '/diagnostics:column', '-diagnostics:column',
        '/FC', '-FC',
        '/c', '-c'
    )

    $filteredArgs = @()
    foreach ($arg in $RawArgs) {
        $isNoise = $false
        foreach ($noise in $noiseMask) {
            if ($arg -eq $noise) {
                $isNoise = $true
                break
            }
        }
        if (-not $isNoise) {
            $filteredArgs += $arg
        }
    }

    $argsString = $filteredArgs -join " "
    $payload = "$abiStamp|$argsString"
    
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $hashBytes = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($payload))
    $hashString = [System.BitConverter]::ToString($hashBytes).Replace('-', '').ToLower()

    return $hashString
}

function Test-UpToDate([hashtable]$ctx) {
    if (-not (Test-Path $ctx.Exe)) { return $false }
    $exeTime = (Get-Item $ctx.Exe).LastWriteTime
    $checkFiles = if ($ctx.SmartAllFiles.Count -gt 0) { $ctx.SmartAllFiles } else { $ctx.Files }
    foreach ($cf in $checkFiles) {
        if ((Test-Path $cf) -and (Get-Item $cf).LastWriteTime -gt $exeTime) {
            return $false
        }
    }

    return $true
}

function Initialize-Env([hashtable]$ctx) {
    $envPref = $null
    $cfgFile = Join-Path $PSScriptRoot '.msvc_build_env'
    if (-not (Test-Path $cfgFile)) { $cfgFile = Join-Path "$HOME\bin" '.msvc_build_env' }
    if (Test-Path $cfgFile) { $envPref = (Get-Content $cfgFile -Raw).Trim() }
    if ($env:MSVC_BUILD_ENV) { $envPref = $env:MSVC_BUILD_ENV }

    $portableBase = Join-Path $PSScriptRoot "portable_msvc"
    if (-not (Test-Path $portableBase)) {
        $portableBase = Join-Path "$HOME\bin" "portable_msvc"
    }

    $ctx.IsPortable = ($envPref -ne 'vs') -and (Test-Path $portableBase)
    if ($envPref -eq 'portable' -and -not (Test-Path $portableBase)) {
        Write-Host 'warning : MSVC_BUILD_ENV=portable 但未找到 portable_msvc，回退到本�?VS' -ForegroundColor Yellow
    }

    if ($ctx.IsPortable) {
        

        $vcBase    = Join-Path $portableBase "VC\Tools\MSVC"
        $ctx.VcDir = (Get-ChildItem $vcBase -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
        $wkIncBase = Join-Path $portableBase "Windows Kits\10\Include"
        $wkInc     = (Get-ChildItem $wkIncBase -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
        $wkLibBase = Join-Path $portableBase "Windows Kits\10\Lib"
        $wkLib     = (Get-ChildItem $wkLibBase -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
        $wkBinBase = Join-Path $portableBase "Windows Kits\10\bin"
        $wkBin     = (Get-ChildItem $wkBinBase -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName

        $env:PATH    = "$($ctx.VcDir)\bin\$($ctx.HostArch)\$($ctx.Arch);$wkBin\$($ctx.Arch);" + $env:PATH
        $sysIncludes = @("$($ctx.VcDir)\include","$wkInc\ucrt","$wkInc\shared","$wkInc\um","$wkInc\winrt","$wkInc\cppwinrt")
        $env:INCLUDE = ($sysIncludes -join ";") + ";" + $env:INCLUDE
        $sysLibs     = @("$($ctx.VcDir)\lib\$($ctx.Arch)","$wkLib\ucrt\$($ctx.Arch)","$wkLib\um\$($ctx.Arch)")
        $env:LIB     = ($sysLibs -join ";") + ";" + $env:LIB
    }
    else {
        $pf86    = [System.Environment]::GetFolderPath('ProgramFilesX86')
        $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
        $vsPath  = $null

        if (Test-Path $vswhere) {
            $vsPath = (& $vswhere -latest -property installationPath 2>$null)
            if ($vsPath) { $vsPath = $vsPath.Trim() }
        }

        if (-not $vsPath) {
            $candidates = @(
                (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community'),
                (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Professional'),
                (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community'),
                (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional')
            )
            foreach ($cand in $candidates) {
                if (Test-Path $cand) { $vsPath = $cand; break }
            }
        }

        if (-not $vsPath) {
            Write-Host 'error : 未找到编译工具集。请安装 Visual Studio 或将 portable_msvc 放入脚本目录' -ForegroundColor Red
            Write-Host '  安装 VS 或将 portable_msvc 放入脚本同目�?/ $HOME\bin\ �? -ForegroundColor Yellow
            exit 1
        }

        $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
        if (-not (Test-Path $vcvars)) {
            Write-Host 'error : 未找�?vcvarsall.bat' -ForegroundColor Red
            exit 1
        }

        $envCache = Join-Path $env:TEMP "msvc_env_cache_$($ctx.Arch).txt"
        $cacheValid = $false
        if (Test-Path $envCache) {
            $cacheAge = (Get-Date) - (Get-Item $envCache).LastWriteTime
            if ($cacheAge.TotalHours -lt 168) { $cacheValid = $true }
        }

        if ($cacheValid) {
            
            foreach ($line in (Get-Content $envCache)) {
                if ($line -match '^([^=]+)=(.*)$') {
                    [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
                }
            }
        }
        else {
            Write-Host "1>  正在初始化编译环�?.." -ForegroundColor DarkGray
            $envDump = cmd.exe /c "`"$vcvars`" $($ctx.Arch) >nul 2>&1 && set"
            $envDump | Set-Content $envCache -Force
            foreach ($line in $envDump) {
                if ($line -match '^([^=]+)=(.*)$') {
                    [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
                }
            }
        }

        if ($env:VCToolsInstallDir) {
            $ctx.VcDir = $env:VCToolsInstallDir.TrimEnd('\')
        }
        else {
            $msvcToolsDir = Join-Path $vsPath 'VC\Tools\MSVC'
            if (Test-Path $msvcToolsDir) {
                $ctx.VcDir = (Get-ChildItem $msvcToolsDir -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
            }
        }
    }
}

function Build-CompilerFlags([hashtable]$ctx) {

    $presets = @{
        debug = @{
            optimize = 'Od';        runtime = 'MDd';       warnings = 'W3'
            WX = $false; debug_info = 'ZI';     exceptions = 'EHsc'
            rtc1 = $true;            jmc = $true;            sdl = $true
            permissive = $false;   fp = 'precise';   ltcg = $false
            incremental = $true
            auto_defines = @('_DEBUG')
        }
        release = @{
            optimize = 'O2';        runtime = 'MD';        warnings = 'W3'
            WX = $false; debug_info = 'Zi';     exceptions = 'EHsc'
            rtc1 = $false;           jmc = $false;           sdl = $true
            permissive = $false;   fp = 'precise';   ltcg = $true
            incremental = $false
            auto_defines = @('NDEBUG')
        }
    }

    $preset = $null
    $cfgConfigSrc = if ($ctx.Bound.ContainsKey('config')) { $config } elseif ($ctx.Cfg -and $ctx.Cfg.config) { $ctx.Cfg.config } else { $null }
    if ($cfgConfigSrc) {
        if ($presets.ContainsKey($cfgConfigSrc)) {
            $preset = $presets[$cfgConfigSrc]
        } else {
            Write-Host "warning : 未知的配�? $cfgConfigSrc，忽�? -ForegroundColor Yellow
        }
    }

    function Get-Val([string]$field, $fallback) {
        if ($ctx.Bound.ContainsKey($field)) { return $ctx.Bound[$field] }
        if ($ctx.Cfg -and $null -ne (& { try { $ctx.Cfg.$field } catch { $null } })) {
            return $ctx.Cfg.$field
        }
        if ($preset -and $preset.ContainsKey($field)) { return $preset[$field] }
        return $fallback
    }

    $cfgOptimize    = Get-Val 'optimize'         $null
    $cfgRuntime     = Get-Val 'runtime'          $null
    $cfgWarnings    = Get-Val 'warnings'         'W3'
    $cfgWX = Get-Val 'WX'               $false
    $cfgDebugInfo   = Get-Val 'debug_info'       $null
    $cfgExceptions  = Get-Val 'exceptions'       'EHsc'
    $cfgRtc1         = Get-Val 'rtc1'             $false
    $cfgJmc         = Get-Val 'jmc'              $false
    $cfgSdl    = Get-Val 'sdl'              $false
    $cfgPermissive = Get-Val 'permissive'       $false
    $cfgFp     = Get-Val 'fp'               $null
    $cfgLtcg        = Get-Val 'ltcg'             $false
    $cfgIncr    = Get-Val 'incremental'      $null

    $activeConfig = if ($ctx.Bound.ContainsKey('config')) { $config } elseif ($ctx.Cfg -and $ctx.Cfg.config) { $ctx.Cfg.config } else { $null }
    $isDebug = ($activeConfig -eq 'debug')

    $bf = '/nologo'
    if (-not $preset) { $bf += ' /utf-8' }

    $warnMap = @{ W0 = '/W0'; W1 = '/W1'; W3 = '/W3'; W4 = '/W4'; Wall = '/Wall' }
    if ($warnMap.ContainsKey($cfgWarnings)) { $bf += " $($warnMap[$cfgWarnings])" } else { $bf += ' /W3' }
    if ($cfgWX) { $bf += ' /WX' }

    $excMap = @{ EHsc = '/EHsc'; EHa = '/EHa'; off = '' }
    if ($excMap.ContainsKey($cfgExceptions) -and $excMap[$cfgExceptions]) {
        $bf += " $($excMap[$cfgExceptions])"
    }

    $optMap = @{ Od = '/Od'; O1 = '/O1'; O2 = '/O2'; Ox = '/Ox' }
    if ($cfgOptimize -and $optMap.ContainsKey($cfgOptimize)) {
        $bf += " $($optMap[$cfgOptimize])"
        if ($cfgOptimize -eq 'O2') { $bf += ' /Oi' }
    }

    if ($cfgRuntime) {
        $bf += " /$cfgRuntime"
    }

    if ($cfgDebugInfo -and $cfgDebugInfo -cne 'off') {
        switch -CaseSensitive ($cfgDebugInfo) {
            'Zi' { $bf += ' /Zi' }
            'ZI' { $bf += ' /ZI' }
            'Z7' { $bf += ' /Z7' }
        }
    }

    if ($cfgRtc1 -and $isDebug)         { $bf += ' /RTC1' }
    if ($cfgJmc -and $isDebug)         { $bf += ' /JMC' }
    if ($cfgSdl)                  { $bf += ' /GS /sdl' }

    $fpMap = @{ precise = '/fp:precise'; strict = '/fp:strict'; fast = '/fp:fast' }
    if ($cfgFp -and $fpMap.ContainsKey($cfgFp)) {
        $bf += " $($fpMap[$cfgFp])"
    }

    if ($cfgPermissive) {
        $bf += ' /permissive-'
        if (-not $preset) { $bf += ' /Zc:__cplusplus /Zc:preprocessor' }
    }
    if ($cfgLtcg -and -not $isDebug)           { $bf += ' /GL /Gy' }

    $bf += ' /Zc:forScope /Zc:wchar_t /Zc:inline /FC /FS /Gm- /WX- /diagnostics:column /external:W3 /Gd /errorReport:queue /MP'

    if ($preset -and $preset.auto_defines) {
        foreach ($ad in $preset.auto_defines) { $bf += " /D$ad" }
    }

    $cfgCharset = if ($ctx.Bound.ContainsKey('charset')) { $charset } elseif ($ctx.Cfg -and $ctx.Cfg.charset) { $ctx.Cfg.charset } else { $null }
    if ($cfgCharset -eq 'unicode')      { $bf += ' /DUNICODE /D_UNICODE' }
    elseif ($cfgCharset -eq 'mbcs')     { $bf += ' /D_MBCS' }

    if ($ctx.Cfg -and $ctx.Cfg.defines) {
        foreach ($def in $ctx.Cfg.defines) { $bf += " /D$def" }
    }
    if ($defines) {
        foreach ($def in $defines) { $bf += " /D$def" }
    }

    if ($ctx.Cfg -and $ctx.Cfg.flags) {
        foreach ($f in $ctx.Cfg.flags) { $bf += " $f" }
    }
    if ($ctx.Bound.ContainsKey('flags') -and $flags) {
        foreach ($f in $flags) { $bf += " $f" }
    }

    if ($ctx.HasCpp -and $ctx.Std)                          { $bf += " /std:c++$($ctx.Std)" }
    elseif ($ctx.IxxFiles.Count -gt 0 -and -not $ctx.Std)   { $bf += ' /std:c++latest' }

    foreach ($inc in $I) {
        $bf += " /I`"$inc`""
    }
    if ($ctx.Cfg -and $ctx.Cfg.include) {
        foreach ($inc in $ctx.Cfg.include) {
            if ([System.IO.Path]::IsPathRooted($inc)) { $resolved = $inc } else { $resolved = Join-Path $ctx.CfgDir $inc }
            $bf += " /I`"$resolved`""
        }
    }

    $ctx.BaseFlags = $bf

    $ctx.Preset     = $preset
    $ctx.IsDebug    = $isDebug
    $ctx.CfgLtcg    = $cfgLtcg
    $ctx.CfgIncr = $cfgIncr
}

function Build-LinkFlags([hashtable]$ctx) {
    $lf = ''
    $allLibPaths = @()
    if ($L) { $allLibPaths += $L }
    if ($ctx.Cfg -and $ctx.Cfg.libpath) {
        foreach ($lp in $ctx.Cfg.libpath) {
            if ([System.IO.Path]::IsPathRooted($lp)) { $resolved = $lp } else { $resolved = Join-Path $ctx.CfgDir $lp }
            $allLibPaths += $resolved
        }
    }

    $cfgSubsystem = if ($ctx.Bound.ContainsKey('subsystem')) { $subsystem } elseif ($ctx.Cfg -and $ctx.Cfg.subsystem) { $ctx.Cfg.subsystem } else { $null }
    $needLink = ($allLibPaths.Count -gt 0) -or $cfgSubsystem -or $ctx.Preset -or ($ctx.Cfg -and $ctx.Cfg.link_flags) -or ($ctx.Bound.ContainsKey('link_flags') -and $link_flags)

    if ($needLink) {
        $lf = ' /link /NOLOGO /MACHINE:X64 /ERRORREPORT:QUEUE'
        foreach ($lp in $allLibPaths) { $lf += " /LIBPATH:`"$lp`"" }
        $actualSubsys = if ($cfgSubsystem) { $cfgSubsystem.ToUpper() } elseif ($ctx.Preset) { 'CONSOLE' } else { $null }
        if ($actualSubsys) { $lf += " /SUBSYSTEM:$actualSubsys" }

        if ($ctx.Preset) {
            $exeBase = [System.IO.Path]::ChangeExtension($ctx.Exe, $null).TrimEnd('.')
            $lf += " /DEBUG /PDB:`"$exeBase.pdb`" /DYNAMICBASE /NXCOMPAT /MANIFEST /manifest:embed /TLBID:1 /IMPLIB:`"$exeBase.lib`""
            $lf += ' /MANIFESTUAC:"level=''asInvoker'' uiAccess=''false''"'
            if ($ctx.CfgLtcg -and -not $ctx.IsDebug) {
                $lf += ' /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO'
            }
            elseif ($ctx.CfgIncr) { $lf += ' /INCREMENTAL' }
            else                      { $lf += ' /INCREMENTAL:NO' }
        }

        if ($ctx.Cfg -and $ctx.Cfg.link_flags) {
            foreach ($f in $ctx.Cfg.link_flags) { $lf += " $f" }
        }
        if ($ctx.Bound.ContainsKey('link_flags') -and $link_flags) {
            foreach ($f in $link_flags) { $lf += " $f" }
        }
    }

    $libStr = ''
    $allLibs = @()
    if ($ctx.Preset) {
        $allLibs += @('kernel32','user32','gdi32','winspool','comdlg32','advapi32','shell32','ole32','oleaut32','uuid','odbc32','odbccp32')
    }
    if ($libs) { $allLibs += $libs }
    if ($ctx.Cfg -and $ctx.Cfg.libs) { $allLibs += $ctx.Cfg.libs }
    foreach ($lib in $allLibs) { $libStr += " `"$lib.lib`"" }

    $ctx.LinkFlags = $lf
    $ctx.LibFiles  = $libStr
}

function Compile-Modules([hashtable]$ctx) {
    $script:compileShown = $false
    Write-Host "1>  正在扫描源以查找模块依赖�?.."
    foreach ($f in $ctx.Files) { Write-Host "1>  $([System.IO.Path]::GetFileName($f))" }

    $scanDir = Join-Path $env:TEMP "msvc_build_scan_$$"
    New-Item $scanDir -ItemType Directory -Force | Out-Null

    $scanBat = Join-Path $scanDir '_scan_all.bat'
    $batLines = @("@echo off", "chcp 65001 >nul")
    foreach ($ixx in $ctx.IxxFiles) {
        $ixxName = [System.IO.Path]::GetFileNameWithoutExtension($ixx)
        $depJson = Join-Path $scanDir "$ixxName.dep.json"
        $batLines += "cl $($ctx.BaseFlags) /scanDependencies `"$depJson`" /c `"$ixx`" >nul 2>&1"
        $batLines += "if errorlevel 1 exit /b 1"
    }
    $batLines -join "`r`n" | Set-Content $scanBat -Encoding utf8
    cmd.exe /c "`"$scanBat`"" 2>$null >$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "error : 模块依赖扫描失败" -ForegroundColor Red
        if (Test-Path $scanDir) { Remove-Item $scanDir -Recurse -Force -ErrorAction SilentlyContinue }
        exit 1
    }

    $modNameToFile      = @{}
    $modFileToRequires  = @{}
    $usesImportStd       = $false
    $usesImportStdCompat = $false

    foreach ($f in $ctx.Files) {
        $content = Get-Content $f -Raw -ErrorAction SilentlyContinue
        if ($content -match '(?m)^\s*import\s+std\s*;') { $usesImportStd = $true }
        if ($content -match '(?m)^\s*import\s+std\.compat\s*;') { $usesImportStdCompat = $true }
    }

    foreach ($ixx in $ctx.IxxFiles) {
        $ixxName = [System.IO.Path]::GetFileNameWithoutExtension($ixx)
        $depJson = Join-Path $scanDir "$ixxName.dep.json"
        if (-not (Test-Path $depJson)) {
            Write-Host "error : 模块依赖扫描失败: $([System.IO.Path]::GetFileName($ixx))" -ForegroundColor Red
            if (Test-Path $scanDir) { Remove-Item $scanDir -Recurse -Force -ErrorAction SilentlyContinue }
            exit 1
        }

        $dep  = Get-Content $depJson -Raw | ConvertFrom-Json
        $rule = $dep.rules[0]

        if ($rule.provides) {
            foreach ($p in $rule.provides) {
                $modNameToFile[$p.'logical-name'] = $ixx
            }
        }

        $requires = @()
        if ($rule.requires) {
            foreach ($r in $rule.requires) {
                $name = $r.'logical-name'
                if ($name -eq 'std')             { $usesImportStd = $true }
                elseif ($name -eq 'std.compat')  { $usesImportStdCompat = $true; $usesImportStd = $true }
                else                             { $requires += $name }
            }
        }
        $modFileToRequires[$ixx] = $requires
    }

    if (Test-Path $scanDir) { Remove-Item $scanDir -Recurse -Force -ErrorAction SilentlyContinue }

    $stdModObjs = @()

    if ($usesImportStd) {
        $vcVer       = Split-Path (Split-Path $ctx.VcDir -Parent) -Leaf
        $flagsHash   = [System.BitConverter]::ToString([System.Security.Cryptography.MD5]::Create().ComputeHash([System.Text.Encoding]::UTF8.GetBytes("$($ctx.BaseFlags)|$vcVer"))).Replace('-','').Substring(0,8)
        $cacheDir      = Join-Path $env:TEMP "msvc_std_module_cache\$flagsHash"
        $stdObjCached  = Join-Path $cacheDir 'std.obj'
        $stdIfcCached  = Join-Path $cacheDir 'std.ifc'

        if ((Test-Path $stdObjCached) -and (Test-Path $stdIfcCached)) {
            
            Copy-Item $stdObjCached, $stdIfcCached $PWD -Force
        }
        else {
            $stdIxx = Join-Path $ctx.VcDir 'modules\std.ixx'
            if (-not (Test-Path $stdIxx)) {
                $foundStd = $false
                $candidates = @($ctx.VcDir)
                if ($env:ProgramFiles) { $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\*" }
                if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\*" }
                
                foreach ($cand in $candidates) {
                    if (Test-Path $cand) {
                        $fallback = Get-ChildItem -Path $cand -Filter "std.ixx" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
                        if ($fallback) {
                            $stdIxx = $fallback.FullName
                            $foundStd = $true
                            break
                        }
                    }
                }
                
                if (-not $foundStd) {
                    Write-Host "error : 未找�?std.ixx: $stdIxx" -ForegroundColor Red
                    exit 1
                }
            }

            Write-Host "1>  正在从库""microsoft/STL""编译模块""std""..." -ForegroundColor DarkGray
            cmd.exe /c "cl $($ctx.BaseFlags) /c /interface `"$stdIxx`"" 2>&1 | ForEach-Object { $s = $_.ToString().Trim(); if ($s -and $s -notmatch "^Microsoft|^Copyright|^cl :") { Write-Host "1>  $s" } }
            if ($LASTEXITCODE -ne 0) {
                Write-Host 'error : std 模块编译失败' -ForegroundColor Red
                exit 1
            }

            if (-not (Test-Path $cacheDir)) { New-Item $cacheDir -ItemType Directory -Force | Out-Null }
            Copy-Item (Join-Path $PWD 'std.obj'), (Join-Path $PWD 'std.ifc') $cacheDir -Force
        }
        $stdModObjs += 'std.obj'
    }

    if ($usesImportStdCompat) {
        $cacheDir         = Join-Path $env:TEMP "msvc_std_module_cache\$flagsHash"
        $compatObjCached  = Join-Path $cacheDir 'std.compat.obj'
        $compatIfcCached  = Join-Path $cacheDir 'std.compat.ifc'

        if ((Test-Path $compatObjCached) -and (Test-Path $compatIfcCached)) {
            
            Copy-Item $compatObjCached, $compatIfcCached $PWD -Force
        }
        else {
            $compatIxx = Join-Path $ctx.VcDir 'modules\std.compat.ixx'
            if (-not (Test-Path $compatIxx)) {
                $foundCompat = $false
                $candidates = @($ctx.VcDir)
                if ($env:ProgramFiles) { $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\*" }
                if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\*" }
                
                foreach ($cand in $candidates) {
                    if (Test-Path $cand) {
                        $fallback = Get-ChildItem -Path $cand -Filter "std.compat.ixx" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
                        if ($fallback) {
                            $compatIxx = $fallback.FullName
                            $foundCompat = $true
                            break
                        }
                    }
                }
                
                if (-not $foundCompat) {
                    Write-Host "error : 未找�?std.compat.ixx: $compatIxx" -ForegroundColor Red
                    exit 1
                }
            }

            Write-Host "1>  正在从库""microsoft/STL""编译模块""std.compat""..." -ForegroundColor DarkGray
            cmd.exe /c "cl $($ctx.BaseFlags) /c /interface `"$compatIxx`"" 2>&1 | ForEach-Object { $s = $_.ToString().Trim(); if ($s -and $s -notmatch "^Microsoft|^Copyright|^cl :") { Write-Host "1>  $s" } }
            if ($LASTEXITCODE -ne 0) {
                Write-Host 'error : std.compat 模块编译失败' -ForegroundColor Red
                exit 1
            }

            if (-not (Test-Path $cacheDir)) { New-Item $cacheDir -ItemType Directory -Force | Out-Null }
            Copy-Item (Join-Path $PWD 'std.compat.obj'), (Join-Path $PWD 'std.compat.ifc') $cacheDir -Force
        }
        $stdModObjs += 'std.compat.obj'
    }

    $inDegree = @{}
    $graph    = @{}
    foreach ($ixx in $ctx.IxxFiles) {
        $inDegree[$ixx] = 0
        $graph[$ixx]    = @()
    }
    foreach ($ixx in $ctx.IxxFiles) {
        foreach ($dep in $modFileToRequires[$ixx]) {
            if ($modNameToFile.ContainsKey($dep)) {
                $depFile = $modNameToFile[$dep]
                $graph[$depFile] += $ixx
                $inDegree[$ixx]++
            }
        }
    }

    $queue = [System.Collections.Queue]::new()
    foreach ($ixx in $ctx.IxxFiles) {
        if ($inDegree[$ixx] -eq 0) { $queue.Enqueue($ixx) }
    }
    $sorted = @()
    while ($queue.Count -gt 0) {
        $cur = $queue.Dequeue()
        $sorted += $cur
        foreach ($next in $graph[$cur]) {
            $inDegree[$next]--
            if ($inDegree[$next] -eq 0) { $queue.Enqueue($next) }
        }
    }

    if ($sorted.Count -ne $ctx.IxxFiles.Count) {
        Write-Host 'error : 模块之间存在循环依赖' -ForegroundColor Red
        exit 1
    }

    $projPath    = [System.IO.Path]::GetFullPath($PWD.Path).ToLower()
    $projHash    = [System.BitConverter]::ToString([System.Security.Cryptography.MD5]::Create().ComputeHash([System.Text.Encoding]::UTF8.GetBytes($projPath))).Replace('-','').Substring(0,8)
    $modCacheDir = Join-Path $env:TEMP "msvc_mod_cache\$projHash"
    if (-not (Test-Path $modCacheDir)) { New-Item $modCacheDir -ItemType Directory -Force | Out-Null }

    Get-ChildItem $modCacheDir -Filter '*.ifc' -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $PWD $_.Name) -Force
    }

    $topoLevel = @{}
    foreach ($ixx in $sorted) {
        $maxDep = -1
        foreach ($dep in $modFileToRequires[$ixx]) {
            if ($modNameToFile.ContainsKey($dep) -and $topoLevel.ContainsKey($modNameToFile[$dep])) {
                $maxDep = [Math]::Max($maxDep, $topoLevel[$modNameToFile[$dep]])
            }
        }
        $topoLevel[$ixx] = $maxDep + 1
    }
    $maxLevel = if ($topoLevel.Count -gt 0) { ($topoLevel.Values | Measure-Object -Maximum).Maximum } else { -1 }

    $recompiled   = @{}
    $skippedCount = 0

    for ($lvl = 0; $lvl -le $maxLevel; $lvl++) {
        $batch = @($sorted | Where-Object { $topoLevel[$_] -eq $lvl })

        $toCompile = @()
        foreach ($ixx in $batch) {
            $ixxName   = [System.IO.Path]::GetFileName($ixx)
            $objName   = [System.IO.Path]::ChangeExtension($ixxName, '.obj')
            $cachedObj = Join-Path $modCacheDir $objName

            $needRecompile = $false
            if (-not (Test-Path $cachedObj)) {
                $needRecompile = $true
            }
            elseif ((Get-Item $ixx).LastWriteTime -gt (Get-Item $cachedObj).LastWriteTime) {
                $needRecompile = $true
            }
            else {
                foreach ($dep in $modFileToRequires[$ixx]) {
                    if ($modNameToFile.ContainsKey($dep) -and $recompiled.ContainsKey($modNameToFile[$dep])) {
                        $needRecompile = $true; break
                    }
                }
            }

            if ($needRecompile) {
                $toCompile += $ixx
            }
            else {
                $skippedCount++
                Copy-Item $cachedObj (Join-Path $PWD $objName) -Force
            }
        }

        if ($toCompile.Count -gt 0) {
            $names = ($toCompile | ForEach-Object { [System.IO.Path]::GetFileName($_) }) -join ', '
            if (-not $script:compileShown) { Write-Host "1>  正在编译..."; $script:compileShown = $true }
            $ixxList = ($toCompile | ForEach-Object { "`"$_`"" }) -join ' '
            cmd.exe /c "cl $($ctx.BaseFlags) /c /interface $ixxList" 2>&1 | ForEach-Object { $s = $_.ToString().Trim(); if ($s -and $s -notmatch "^Microsoft|^Copyright|^cl :") { Write-Host "1>  $s" } }
            if ($LASTEXITCODE -ne 0) {
                Write-Host '1>error : 模块编译失败' -ForegroundColor Red
                exit 1
            }
            foreach ($ixx in $toCompile) {
                $objName   = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFileName($ixx), '.obj')
                $localObj  = Join-Path $PWD $objName
                $cachedObj = Join-Path $modCacheDir $objName
                if (Test-Path $localObj) { Copy-Item $localObj $cachedObj -Force }
                $recompiled[$ixx] = $true
            }
            Get-ChildItem $PWD -Filter '*.ifc' -ErrorAction SilentlyContinue | ForEach-Object {
                Copy-Item $_.FullName (Join-Path $modCacheDir $_.Name) -Force
            }
        }
    }

    if ($skippedCount -gt 0) {
        
    }

    $modObjs = @()
    $modObjs += $stdModObjs
    foreach ($ixx in $ctx.IxxFiles) {
        $modObjs += [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFileName($ixx), '.obj')
    }
    $ctx.ModObjs = $modObjs
    $ctx.AnyModuleRecompiled = ($recompiled.Count -gt 0)
}

function Invoke-Compile([hashtable]$ctx) {
    $cppFiles = if ($ctx.IxxFiles.Count -gt 0) { $ctx.CppFiles } else { $ctx.Files }
    if (-not $cppFiles -or $cppFiles.Count -eq 0) { return }

    $projPath    = [System.IO.Path]::GetFullPath($PWD.Path).ToLower()
    $projHash    = [System.BitConverter]::ToString(
        [System.Security.Cryptography.MD5]::Create().ComputeHash(
            [System.Text.Encoding]::UTF8.GetBytes($projPath)
        )
    ).Replace('-','').Substring(0,8)
    $cppCacheDir = Join-Path $env:TEMP "msvc_cpp_cache\$projHash"
    if (-not (Test-Path $cppCacheDir)) { New-Item $cppCacheDir -ItemType Directory -Force | Out-Null }

    $objList     = @()
    $toCompile   = @()
    $skipped     = 0

    foreach ($cpp in $cppFiles) {
        $objName   = [System.IO.Path]::GetFileNameWithoutExtension($cpp) + '.obj'
        $cachedObj = Join-Path $cppCacheDir $objName

        $needRecompile = $false
        if (-not (Test-Path $cachedObj)) {
            $needRecompile = $true
        }
        elseif ((Get-Item $cpp).LastWriteTime -gt (Get-Item $cachedObj).LastWriteTime) {
            $needRecompile = $true
        }
        elseif ($ctx.AnyModuleRecompiled) {
            $needRecompile = $true
        }

        if ($needRecompile) { $toCompile += @{ Cpp = $cpp; ObjName = $objName; CachedObj = $cachedObj } }
        else {
            $skipped++
            Copy-Item $cachedObj (Join-Path $PWD $objName) -Force
        }
        $objList += $objName
    }

    if ($toCompile.Count -gt 0) {
        $names = ($toCompile | ForEach-Object { [System.IO.Path]::GetFileName($_.Cpp) }) -join ', '
        if (-not $script:compileShown) { Write-Host "1>  正在编译..."; $script:compileShown = $true }
        $cppList = ($toCompile | ForEach-Object { "`"$($_.Cpp)`"" }) -join ' '
        cmd.exe /c "cl /c $($ctx.BaseFlags) $cppList" 2>&1 | ForEach-Object { $s = $_.ToString().Trim(); if ($s -and $s -notmatch "^Microsoft|^Copyright|^cl :") { Write-Host "1>  $s" } }
        if ($LASTEXITCODE -ne 0) {
            Write-Host '1>error : 编译失败' -ForegroundColor Red
        $ctx.BuildSuccess = $false
            exit 1
        }
        foreach ($item in $toCompile) {
            $localObj = Join-Path $PWD $item.ObjName
            if (Test-Path $localObj) { Copy-Item $localObj $item.CachedObj -Force }
        }
    }

    if ($skipped -gt 0) {
        
    }

    $ctx.CppObjs = $objList
}

function Invoke-Link([hashtable]$ctx) {
    $allObjs = @()
    if ($ctx.ModObjs)  { $allObjs += $ctx.ModObjs }
    if ($ctx.CppObjs)  { $allObjs += $ctx.CppObjs }

    if (Test-Path $ctx.Exe) {
        $exeTime = (Get-Item $ctx.Exe).LastWriteTime
        $needRelink = $false
        foreach ($obj in $allObjs) {
            $objPath = Join-Path $PWD $obj
            if ((Test-Path $objPath) -and (Get-Item $objPath).LastWriteTime -gt $exeTime) {
                $needRelink = $true; break
            }
        }
        if (-not $needRelink) {
            
            Write-Host "1>  $($ctx.OutputName) -> $($ctx.Exe)" -ForegroundColor DarkGray
            Get-ChildItem $PWD -Filter '*.obj' -File -ErrorAction SilentlyContinue | Remove-Item -Force
            Get-ChildItem $PWD -Filter '*.ifc' -File -ErrorAction SilentlyContinue | Remove-Item -Force
            return
        }
    }

    $objList = ($allObjs | ForEach-Object { "`"$_`"" }) -join ' '

    $linkArgs = $ctx.LinkFlags -replace '^\s*/link\s*', ''

    
    cmd.exe /c "link.exe $objList /OUT:`"$($ctx.Exe)`" $linkArgs$($ctx.LibFiles)" 2>&1 | ForEach-Object { $s = $_.ToString().Trim(); if ($s -match "error|warning" -and $s -notmatch "^Microsoft|^Copyright") { Write-Host "1>$s" } }
    if ($LASTEXITCODE -ne 0) {
        Write-Host '1>error : 链接失败' -ForegroundColor Red
        $ctx.BuildSuccess = $false
        exit 1
    }
    Write-Host "1>  $($ctx.OutputName) -> $($ctx.Exe)"

    Get-ChildItem $PWD -Filter '*.obj' -File -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem $PWD -Filter '*.ifc' -File -ErrorAction SilentlyContinue | Remove-Item -Force
}

function Invoke-Run([hashtable]$ctx) {
    Write-Host ''
    Write-Host "
========== 正在启动: $($ctx.OutputName).exe $ProgramArgs ==========" -ForegroundColor DarkGray
    Write-Host ('=' * 40) -ForegroundColor DarkGray
    if ($ProgramArgs) {
        & $ctx.Exe ($ProgramArgs -split ' ')
    }
    else {
        & $ctx.Exe
    }
    $code = $LASTEXITCODE
    Write-Host ('=' * 40) -ForegroundColor DarkGray
    Write-Host "========== 进程已退出，返回代码 $code ==========" -ForegroundColor DarkGray
}

$envCfgFile = Join-Path $PSScriptRoot '.msvc_build_env'
if (-not (Test-Path $envCfgFile)) { $envCfgFile = Join-Path "$HOME\bin" '.msvc_build_env' }

if ($setenv) {
    $savePath = Join-Path "$HOME\bin" '.msvc_build_env'
    if ($setenv -eq 'auto') {
        Remove-Item $savePath -Force -ErrorAction SilentlyContinue
        Write-Host "编译环境已恢复为自动检测�? -ForegroundColor DarkGray
    } else {
        $setenv | Set-Content $savePath -Force
        Write-Host "编译环境已设�?""$setenv""�? -ForegroundColor DarkGray
    }
    exit 0
}

if ($help -or -not $Sources) { Show-Help; exit 0 }

$ctx = @{
    Sources       = $Sources
    OutputName    = $o
    Std           = $std
    Files         = @()
    IxxFiles      = @()
    CppFiles      = @()
    SmartAllFiles = @()
    Cfg           = $null
    CfgDir        = $null
    BaseFlags     = ''
    CppObjs       = @()
    LinkFlags     = ''
    LibFiles      = ''
    Exe           = ''
    Arch          = if ($x86) { 'x86' } else { 'x64' }
    HostArch      = if ($x86) { 'Hostx86' } else { 'Hostx64' }
    VcDir         = $null
    IsPortable    = $false
    HasCpp        = $false
    Bound         = $PSBoundParameters
    AnyModuleRecompiled = $false
    ModObjs       = @()
    BuildSuccess  = $true
}

$buildTimer = [System.Diagnostics.Stopwatch]::StartNew()
$buildStartTime = Get-Date -Format 'HH:mm'



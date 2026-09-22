# Pure syntax/helper contracts; never invoke either launch function or the study.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$path = Join-Path $PSScriptRoot 'collect_external_noop_boundary.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw ($errors | Out-String) }
$functions = @($ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] }, $true))
$expected = @('Write-NewJson', 'Get-Digest', 'Get-FileManifest', 'Initialize-RootTimes',
              'Invoke-LegacyBoundary', 'Invoke-ProcessBoundary', 'Invoke-RegisteredStudy')
if (@(Compare-Object ($expected | Sort-Object) ($functions.Name | Sort-Object)).Count -ne 0) { throw 'Unexpected function inventory.' }
foreach ($f in $functions) { . ([scriptblock]::Create($f.Extent.Text)) }
Initialize-RootTimes # Compile P/Invoke declaration; do NOT call the Windows API.
if (-not ('MqbNoopRootTimes' -as [type])) { throw 'Native timing helper not compiled.' }
$root = Join-Path ([IO.Path]::GetTempPath()) ('mqb-boundary-contract-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$fixture = Join-Path $root 'fixture'; $null = New-Item -ItemType Directory -Path $fixture
[IO.File]::WriteAllText((Join-Path $fixture 'a.txt'), 'a')
[IO.File]::WriteAllText((Join-Path $fixture 'b.txt'), 'bb')
$before = Get-FileManifest $fixture
if ($before.Count -ne 2 -or $before[0].size -ne 1 -or $before[1].size -ne 2) { throw 'Manifest cardinality/size.' }
$p = Join-Path $root 'new.json'
Write-NewJson $p $before
$hash = Get-Digest $p; $refused = $false
try { Write-NewJson $p @{} } catch { $refused = $true }
if (-not $refused -or (Get-Digest $p) -ne $hash) { throw 'Old evidence overwritten.' }
$decoded = Get-Content -LiteralPath $p -Raw | ConvertFrom-Json
if ($decoded.Count -ne 2 -or $decoded[0].sha256 -ne (Get-Digest (Join-Path $fixture 'a.txt'))) { throw 'Manifest serialization mismatch.' }
# An explicitly selected byte replacement must produce a changed observation.
[IO.File]::WriteAllText((Join-Path $fixture 'a.txt'), 'changed')
$after = Get-FileManifest $fixture
if ($after[0].sha256 -eq $before[0].sha256) { throw 'Changed file hidden.' }
Write-Host 'PASS: parser, 7-function inventory, native-helper compilation, manifest, no-overwrite, serialization and replacement controls.'
Write-Host "Synthetic helper files retained at $root. MQB calls=0; process launch methods not executed."

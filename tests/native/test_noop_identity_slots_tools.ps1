# Real PATH discovery and fixed harmless child commands; no original MQB inputs.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$FixtureRoot,[Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot 'noop_identity_slots_runtime.psm1') -Force
$originalPath=$env:PATH; $originalExtensions=$env:PATHEXT
$originalDirectory=(Get-Location).Path
$cases=[Collections.Generic.List[object]]::new(); $script:children=0
$first=Join-Path $FixtureRoot 'first tools'; $second=Join-Path $FixtureRoot 'second tools'
$empty=Join-Path $FixtureRoot 'empty'; $extension=if ($IsWindows) {'.cmd'} else {''}
function Check { param([bool]$Value,[string]$Message) if (-not $Value) {throw $Message} }
function Case {
    param([string]$CaseName,[scriptblock]$Test)
    try { & $Test; $cases.Add(@{name=$CaseName;passed=$true;error=$null}) }
    catch { $cases.Add(@{name=$CaseName;passed=$false;error=$_.ToString()}) }
}
function PathOrder { param([string[]]$Directories) $env:PATH=$Directories -join [IO.Path]::PathSeparator }
function SelectAndRun {
    param([string]$Name,[string]$Directory,[string]$Marker,[int]$ExpectedExit)
    $selected=Resolve-SlotApplication $Name
    $expected=Join-Path $Directory ($Name+$extension)
    Check ($selected -is [string] -and $selected -ceq $expected) 'Expected one exact scalar application path.'
    ++$script:children
    $lines=@(& $selected)
    $code=$LASTEXITCODE
    Check ($code -eq $ExpectedExit) 'Wrong child exit; do not fall back to another PATH match.'
    Check ($lines.Count -eq 1 -and $lines[0] -ceq $Marker) 'Wrong selected child output.'
}
try {
    if ($IsWindows) {$env:PATHEXT='.CMD'}
    foreach ($name in @('git','python')) {
        Case ($name+' multiple PATH matches select only the first') {
            PathOrder @($first,$second)
            $all=@(Get-Command -Name $name -CommandType Application -All -ErrorAction Stop)
            Check ($all.Count -eq 2) 'Fixture did not expose two real application matches.'
            # This is the original defective expression, retained for diagnosis.
            $legacy=@((Get-Command -Name $name -CommandType Application -ErrorAction Stop).Source)
            Write-NewJson (Join-Path $FixtureRoot ($name+'-discovery.json')) @{all=@($all.Source);legacy=@($legacy)}
            SelectAndRun $name $first ('FIRST-'+$name) 0
        }
        Case ($name+' reversed PATH preserves precedence and nonzero exit') {
            PathOrder @($second,$first)
            SelectAndRun $name $second ('SECOND-'+$name) 37
        }
        Case ($name+' single match stays a scalar') {
            PathOrder @($first)
            $selected=Resolve-SlotApplication $name
            Check ($selected -is [string] -and $selected -ceq (Join-Path $first ($name+$extension))) 'Single match changed.'
        }
        Case ($name+' missing application fails closed') {
            PathOrder @($empty)
            $refused=$false
            try { $null=Resolve-SlotApplication $name }
            catch {$refused=$_.FullyQualifiedErrorId -like '*CommandNotFoundException*'}
            Check $refused 'Missing application was accepted.'
        }
    }
    Case 'unsupported prerequisite name is refused without execution' {
        PathOrder @($first,$second)
        $refused=$false
        try {$null=Resolve-SlotApplication 'git --version'} catch {$refused=$true}
        Check $refused 'Arbitrary command string was accepted.'
    }
} finally {
    $env:PATH=$originalPath; $env:PATHEXT=$originalExtensions
}
Case 'discovery leaves caller PATH and working directory unchanged' {
    Check ($env:PATH -ceq $originalPath -and $env:PATHEXT -ceq $originalExtensions) 'Environment not restored.'
    Check ((Get-Location).Path -ceq $originalDirectory) 'Working directory changed.'
}
$failed=@($cases | Where-Object {-not $_.passed}).Count
Write-NewJson $OutputPath @{schema=1;tests=$cases.Count;failures=$failed;cases=@($cases.ToArray())
    harmless_child_launches=$children;study_mqb_calls=0;msvc_calls=0;workflow_dispatches=0}
if ($failed) {throw "$failed prerequisite discovery cases failed."}

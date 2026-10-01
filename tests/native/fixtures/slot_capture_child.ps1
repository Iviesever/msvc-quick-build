# Harmless IO fixture, not MQB: no filesystem mutation, subprocess or network.
param([ValidateSet('success','nonzero','empty','unicode','many')][string]$Mode='success',
      [string]$Value='literal')
if ($Mode -eq 'empty') { exit 0 }
if ($Mode -eq 'many') {
    for ($i=0;$i -lt 64;++$i) {
        [Console]::Out.WriteLine('out-'+$i)
        [Console]::Error.WriteLine('err-'+$i)
    }
    exit 0
}
if ($Mode -eq 'unicode') {
    [Console]::Out.WriteLine('日本語 / 中文 / café')
    exit 0
}
[Console]::Out.WriteLine('prefix-out')
[Console]::Error.WriteLine('prefix-err')
[Console]::Out.WriteLine('value='+$Value)
[Console]::Out.WriteLine('cwd='+(Get-Location).Path)
if ($Mode -eq 'nonzero') { exit 37 }
exit 0

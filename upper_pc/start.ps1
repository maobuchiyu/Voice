$ErrorActionPreference = 'Stop'
& py -3 (Join-Path $PSScriptRoot 'app.py')
if ($LASTEXITCODE -ne 0) { throw '上位机启动失败' }

$ErrorActionPreference = 'Stop'
$demoData = Join-Path $env:LOCALAPPDATA 'NetMon-Demo'
Start-Process -FilePath (Join-Path $PSScriptRoot 'netmon_service.exe') -ArgumentList ('--demo --data "'+$demoData+'" --seconds 600') -WindowStyle Hidden
Start-Sleep -Seconds 1
Start-Process -FilePath (Join-Path $PSScriptRoot 'netmon.exe') -ArgumentList '--demo'


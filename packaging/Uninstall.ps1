$ErrorActionPreference = 'Stop'
$administrator = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $administrator) {
    $process = Start-Process -FilePath powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'"') -PassThru -Wait
    exit $process.ExitCode
}
$destination = Join-Path $env:ProgramFiles 'NetMon'
$expected = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'NetMon'))
if ([IO.Path]::GetFullPath($destination) -ne $expected -or (Split-Path $destination -Leaf) -ne 'NetMon') { throw 'Invalid uninstall target' }
$service = Get-Service NetMon -ErrorAction SilentlyContinue
if ($service) { Stop-Service NetMon; $service.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(15)) }
& (Join-Path $destination 'netmon_service.exe') --restore-audit
if ($LASTEXITCODE -ne 0) { throw 'Audit restoration failed; files retained.' }
Unregister-ScheduledTask -TaskName 'NetMon Tray' -Confirm:$false -ErrorAction SilentlyContinue
& sc.exe delete NetMon | Out-Null
Get-Process -Name netmon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $destination 'netmon.exe') } | Stop-Process
$shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'NetMon.lnk'
if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut }
foreach ($name in @('netmon.exe','netmon_service.exe','README.md','THIRD_PARTY_NOTICES.md','PLAN.md','Install.ps1','Install.cmd','Uninstall.cmd')) {
    $file = Join-Path $destination $name
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
foreach ($name in @('geoip-country.csv','README.md')) {
    $file = Join-Path (Join-Path $destination 'data') $name
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
$countryDirectory = Join-Path $destination 'data'
if ((Test-Path -LiteralPath $countryDirectory) -and -not (Get-ChildItem -LiteralPath $countryDirectory -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $countryDirectory
}
Write-Output 'NetMon removed. History and rules remain in ProgramData\NetMon. Later administrator audit changes were preserved.'


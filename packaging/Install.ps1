param([string]$OwnerSid)
$ErrorActionPreference = 'Stop'
if (-not $OwnerSid) { $OwnerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value }
[void][Security.Principal.SecurityIdentifier]::new($OwnerSid)
$administrator = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $administrator) {
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -OwnerSid ' + $OwnerSid
    $process = Start-Process -FilePath powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru -Wait
    exit $process.ExitCode
}
$destination = Join-Path $env:ProgramFiles 'NetMon'
$dataPath = Join-Path $env:ProgramData 'NetMon'
$serviceExe = Join-Path $destination 'netmon_service.exe'
$createdService = $false
$enabledAudit = $false
$existing = Get-Service -Name NetMon -ErrorAction SilentlyContinue
try {
    foreach ($name in @('netmon.exe','netmon_service.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $name))) { throw "Missing package file: $name" }
    }
    foreach ($candidate in @($destination,$dataPath)) {
        if (Test-Path -LiteralPath $candidate) {
            if ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing installation through a reparse point.' }
            if (Get-ChildItem -LiteralPath $candidate -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1) { throw 'Refusing installation into a directory containing reparse points.' }
        }
    }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    New-Item -ItemType Directory -Path $dataPath -Force | Out-Null
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true,$false)
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    foreach ($sid in @('S-1-5-18','S-1-5-32-544')) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),'FullControl','ContainerInherit,ObjectInherit','None','Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $dataPath -AclObject $acl
    $existing = Get-Service -Name NetMon -ErrorAction SilentlyContinue
    if ($existing) { Stop-Service -Name NetMon -ErrorAction Stop; $existing.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(15)) }
    Get-Process -Name netmon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $destination 'netmon.exe') } | Stop-Process
    if ([IO.Path]::GetFullPath($PSScriptRoot) -ne [IO.Path]::GetFullPath($destination)) {
        foreach ($name in @('netmon.exe','netmon_service.exe','README.md','THIRD_PARTY_NOTICES.md','PLAN.md','Install.ps1','Install.cmd','Uninstall.ps1','Uninstall.cmd','Test-Live.ps1')) {
            $source = Join-Path $PSScriptRoot $name
            if (Test-Path -LiteralPath $source) {
                for ($attempt=0; $attempt -lt 10; $attempt++) {
                    try { Copy-Item -LiteralPath $source -Destination (Join-Path $destination $name) -Force; break }
                    catch { if ($attempt -eq 9) { throw }; Start-Sleep -Milliseconds 300 }
                }
            }
        }
    }
    $documentation = Join-Path $PSScriptRoot 'docs'
    if ((Test-Path -LiteralPath $documentation) -and [IO.Path]::GetFullPath($PSScriptRoot) -ne [IO.Path]::GetFullPath($destination)) {
        Copy-Item -LiteralPath $documentation -Destination $destination -Recurse -Force
    }
    $countryData = Join-Path $PSScriptRoot 'data'
    if ((Test-Path -LiteralPath $countryData) -and [IO.Path]::GetFullPath($PSScriptRoot) -ne [IO.Path]::GetFullPath($destination)) {
        Copy-Item -LiteralPath $countryData -Destination $destination -Recurse -Force
    }
    $ownerPath = Join-Path $dataPath 'owner.json'
    if (-not (Test-Path -LiteralPath $ownerPath)) { @{sid=$OwnerSid} | ConvertTo-Json | Set-Content -LiteralPath $ownerPath -Encoding UTF8 }
    & $serviceExe --enable-audit
    if ($LASTEXITCODE -ne 0) { throw 'Could not enable WFP connection auditing.' }
    $enabledAudit = $true
    if (-not $existing) {
        New-Service -Name NetMon -DisplayName 'NetMon connection monitor' -BinaryPathName ('"'+$serviceExe+'" --service') -StartupType Automatic -Description 'Observes outgoing WFP connection audit events and manages local notification rules.' | Out-Null
        $createdService = $true
    }
    & sc.exe failure NetMon reset= 86400 actions= restart/5000/restart/15000/restart/60000 | Out-Null
    Start-Service -Name NetMon
    $action = New-ScheduledTaskAction -Execute (Join-Path $destination 'netmon.exe') -Argument '--tray'
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $OwnerSid
    $principal = New-ScheduledTaskPrincipal -UserId $OwnerSid -LogonType Interactive -RunLevel Limited
    Register-ScheduledTask -TaskName 'NetMon Tray' -Action $action -Trigger $trigger -Principal $principal -Description 'NetMon tray notifications' -Force | Out-Null
    Start-ScheduledTask -TaskName 'NetMon Tray'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'NetMon.lnk'))
    $shortcut.TargetPath = Join-Path $destination 'netmon.exe'
    $shortcut.WorkingDirectory = $destination
    $shortcut.Save()
    'Installed NetMon. WFP success/failure auditing is enabled. Firewall decisions are unchanged.' |
        Set-Content -LiteralPath (Join-Path $dataPath 'install-result.txt')
    $check = Join-Path $PSScriptRoot 'Test-Live.ps1'
    if (Test-Path -LiteralPath $check) {
        $resultPath = Join-Path $PSScriptRoot 'live-validation.json'
        try { & $check -ServiceExe $serviceExe -OutputPath $resultPath | Out-Null }
        catch { @{passed=$false;error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8 }
    }
} catch {
    if ($createdService) {
        Stop-Service -Name NetMon -ErrorAction SilentlyContinue
        & sc.exe delete NetMon | Out-Null
    }
    if ($enabledAudit -and -not $existing) { & $serviceExe --restore-audit | Out-Null }
    $failure = $_ | Out-String
    $failure | Set-Content -LiteralPath (Join-Path $dataPath 'install-result.txt') -ErrorAction SilentlyContinue
    $failure | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'install-error.txt') -ErrorAction SilentlyContinue
    if ($existing) {
        Start-Service -Name NetMon -ErrorAction SilentlyContinue
        Start-ScheduledTask -TaskName 'NetMon Tray' -ErrorAction SilentlyContinue
    }
    exit 1
}


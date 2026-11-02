param([string]$BuildPath='build\Release')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path (Join-Path $root $BuildPath) 'netmon_service.exe'
$data=Join-Path $root ('build\events-'+[Guid]::NewGuid().ToString('N'))
$backend=Start-Process -FilePath $exe -ArgumentList ('--demo --data "'+$data+'" --seconds 40') -WindowStyle Hidden -PassThru
$clients=@()
$requestPath=Join-Path $data 'request.json'
function Invoke-Demo($request) {
    $request | ConvertTo-Json -Depth 10 -Compress | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $result=& $exe --demo-request-file $requestPath
    if($LASTEXITCODE -ne 0) { throw ($result -join [Environment]::NewLine) }
    ($result -join [Environment]::NewLine) | ConvertFrom-Json
}
try {
    for($i=0;$i -lt 40;$i++) {
        Start-Sleep -Milliseconds 100
        if(Test-Path -LiteralPath $data) {
            try { $status=Invoke-Demo @{op='status'}; break } catch {}
        }
    }
    if(-not $status) { throw 'Demo service did not start' }
    $revision=$status.revision
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    # Occupy three independent server instances with clients that send no frame.
    # The fourth instance must still serve a valid authenticated query.
    for($i=0;$i -lt 3;$i++) {
        $pipe=[IO.Pipes.NamedPipeClientStream]::new('.','NetMon.Demo.'+$sid,[IO.Pipes.PipeDirection]::InOut)
        $pipe.Connect(1000)
        $clients+=$pipe
    }
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $responsive=Invoke-Demo @{op='status'}
    $latency=$timer.Elapsed.TotalMilliseconds
    if($latency -ge 700) { throw ('Status blocked behind stalled pipe clients: '+$latency+' ms') }
    foreach($pipe in $clients) { $pipe.Dispose() }
    $clients=@()
    Start-Sleep -Milliseconds 100
    $settings=$responsive.settings
    $settings.review_hide_loopback=$true
    [void](Invoke-Demo @{op='settings';settings=$settings})
    $changed=Invoke-Demo @{op='watch';after=$revision;timeout_ms=1000}
    if($changed.revision -le $revision) { throw 'Change notification revision did not advance' }
    $settingsAfter=(Invoke-Demo @{op='config'}).settings
    if(-not $settingsAfter.review_hide_loopback) { throw 'Command acknowledgement preceded saved configuration' }
    [ordered]@{passed=$true;checks=3;stalled_clients=3;status_ms=$latency;revision_advanced=$true} | ConvertTo-Json
} finally {
    foreach($pipe in $clients) { $pipe.Dispose() }
    if(-not $backend.HasExited) { Stop-Process -Id $backend.Id }
}

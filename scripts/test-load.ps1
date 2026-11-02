param([string]$ServiceExe='C:\Program Files\NetMon\netmon_service.exe',
      [ValidateRange(1,120)][int]$Seconds=30,
      [ValidateRange(1,200)][int]$Rate=100,
      [string]$OutputPath)
$ErrorActionPreference='Stop'
$requestPath=Join-Path $env:TEMP ('netmon-load-'+[Guid]::NewGuid().ToString('N')+'.json')
function Get-Health {
    '{"op":"status"}' | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $reply=& $ServiceExe --request-file $requestPath
    if ($LASTEXITCODE -ne 0) { throw ($reply -join [Environment]::NewLine) }
    ($reply -join [Environment]::NewLine) | ConvertFrom-Json
}
Add-Type -TypeDefinition @'
using System;
using System.Net;
using System.Net.Sockets;
using System.Diagnostics;
using System.Threading;
public static class NetMonLocalTraffic {
    public static int Run(int seconds, int rate) {
        var listener=new TcpListener(IPAddress.Loopback,0);
        listener.Start(16);
        var watch=Stopwatch.StartNew();
        int count=0;
        try {
            int port=((IPEndPoint)listener.LocalEndpoint).Port;
            while(watch.Elapsed.TotalSeconds<seconds) {
                using(var client=new TcpClient(AddressFamily.InterNetwork)) {
                    client.Connect(IPAddress.Loopback,port);
                    using(var peer=listener.AcceptTcpClient()) {}
                }
                count++;
                long due=(long)count*1000/rate;
                while(watch.ElapsedMilliseconds<due) Thread.Sleep(1);
            }
        } finally { listener.Stop(); }
        return count;
    }
}
'@
try {
    $before=Get-Health
    if (-not $before.collector.subscribed) { throw 'Collector is not ready.' }
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $generated=[NetMonLocalTraffic]::Run($Seconds,$Rate)
    Start-Sleep -Seconds 3
    $after=Get-Health
    $timer.Stop()
    $cpuDelta=$after.cpu_milliseconds-$before.cpu_milliseconds
    $result=[ordered]@{
        timestamp=[DateTime]::UtcNow.ToString('o')
        test='Local IPv4 TCP load, current retention settings'
        requested_rate=$Rate
        generated=$generated
        generation_seconds=$Seconds
        observation_seconds=[Math]::Round($timer.Elapsed.TotalSeconds,3)
        received_delta=($after.received-$before.received)
        processed_delta=($after.processed-$before.processed)
        dropped_delta=($after.dropped-$before.dropped)
        collector_errors_delta=($after.collector.errors-$before.collector.errors)
        service_cpu_milliseconds=$cpuDelta
        service_cpu_percent_one_core=[Math]::Round($cpuDelta/($timer.Elapsed.TotalSeconds*10),3)
        service_private_bytes=$after.private_bytes
        service_working_set_bytes=$after.working_set_bytes
        queue_bytes=$after.queue_bytes
        storage_error=$after.storage_error
        passed=($after.received-$before.received -ge $generated -and $after.collector.subscribed -and $after.dropped -eq $before.dropped -and $after.collector.errors -eq $before.collector.errors -and -not $after.storage_error -and $after.queue_bytes -eq 0)
    }
    $json=$result | ConvertTo-Json -Depth 8
    $json
    if ($OutputPath) { $json | Set-Content -LiteralPath $OutputPath -Encoding UTF8 }
    if (-not $result.passed) { throw 'Local load test reported collector loss or another failure.' }
} finally {
    if (Test-Path -LiteralPath $requestPath) { Remove-Item -LiteralPath $requestPath }
}

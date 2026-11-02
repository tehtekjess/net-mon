param([string]$ServiceExe = (Join-Path $PSScriptRoot 'netmon_service.exe'),[string]$OutputPath)
$ErrorActionPreference = 'Stop'
$requestPath = Join-Path $env:TEMP ('netmon-check-'+[Guid]::NewGuid().ToString('N')+'.json')
function Invoke-NetMon($request) {
    $request | ConvertTo-Json -Depth 12 -Compress | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $output = & $ServiceExe --request-file $requestPath
    if ($LASTEXITCODE -ne 0) { throw ($output -join [Environment]::NewLine) }
    return ($output -join [Environment]::NewLine) | ConvertFrom-Json
}
try {
    $ready = $false
    for ($i=0;$i -lt 20;$i++) {
        try {
            $health = Invoke-NetMon @{op='status'}
            if ($health.collector.subscribed -and $health.audit_enabled) { $ready=$true; break }
        } catch {}
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw 'Collector/audit policy did not become ready.' }
    $expected = @()
    foreach ($address in @([Net.IPAddress]::Loopback,[Net.IPAddress]::IPv6Loopback)) {
        $listener = [Net.Sockets.TcpListener]::new($address,0)
        $listener.Start()
        $port = $listener.LocalEndpoint.Port
        $client = [Net.Sockets.TcpClient]::new($address.AddressFamily)
        try {
            $client.Connect($address,$port)
            $accepted = $listener.AcceptTcpClient()
            $accepted.Dispose()
            $expected += @{port=$port;protocol=6;address=$address.ToString()}
        } finally { $client.Dispose();$listener.Stop() }
    }
    foreach ($address in @([Net.IPAddress]::Loopback,[Net.IPAddress]::IPv6Loopback)) {
        $receiver = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new($address,0))
        $sender = [Net.Sockets.UdpClient]::new($address.AddressFamily)
        try {
            $port = $receiver.Client.LocalEndPoint.Port
            [void]$sender.Send([byte[]](78,101,116,77,111,110),6,[Net.IPEndPoint]::new($address,$port))
            $expected += @{port=$port;protocol=17;address=$address.ToString()}
        } finally { $sender.Dispose();$receiver.Dispose() }
    }
    $processPath = (Get-Process -Id $PID).Path
    $found = @{}
    for ($i=0;$i -lt 40;$i++) {
        $records = Invoke-NetMon @{op='query';view='live'}
        foreach ($test in $expected) {
            $matches = @($records | Where-Object { $_.pid -eq $PID -and $_.remote_port -eq $test.port -and $_.protocol -eq $test.protocol -and $_.remote_ip -eq $test.address -and $_.app -eq $processPath -and $_.process_verified -and $_.hash.Length -eq 64 })
            if ($matches.Count -gt 0) { $found["$($test.protocol):$($test.port)"]=$true }
        }
        if ($found.Count -eq $expected.Count) { break }
        Start-Sleep -Milliseconds 250
    }
    $health = Invoke-NetMon @{op='status'}
    $result = [ordered]@{timestamp=[DateTime]::UtcNow.ToString('o');test='Live WFP process/endpoint attribution';expected=$expected.Count;observed=$found.Count;passed=($found.Count -eq $expected.Count);collector=$health.collector;audit_enabled=$health.audit_enabled;dropped=$health.dropped;storage_error=$health.storage_error}
    $result | ConvertTo-Json -Depth 8
    if ($OutputPath) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8 }
    if (-not $result.passed) { throw 'One or more loopback test observations were not captured. Review collector diagnostics; do not claim complete coverage.' }
} finally {
    if (Test-Path -LiteralPath $requestPath) { Remove-Item -LiteralPath $requestPath }
}


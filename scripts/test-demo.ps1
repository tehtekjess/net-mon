param([string]$BuildPath='build\Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path (Join-Path $root $BuildPath) 'netmon_service.exe'
$data = Join-Path $root ('build\integration-'+[Guid]::NewGuid().ToString('N'))
$requestPath = Join-Path $env:TEMP ('netmon-demo-check-'+[Guid]::NewGuid().ToString('N')+'.json')
$backend = Start-Process -FilePath $exe -ArgumentList ('--demo --data "'+$data+'" --seconds 35') -WindowStyle Hidden -PassThru
function Invoke-Demo($request) {
    $request | ConvertTo-Json -Depth 15 -Compress | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $output = & $exe --demo-request-file $requestPath
    if ($LASTEXITCODE -ne 0) { throw ($output -join [Environment]::NewLine) }
    return ($output -join [Environment]::NewLine) | ConvertFrom-Json
}
try {
    $records = @()
    for ($i=0;$i -lt 30;$i++) {
        Start-Sleep -Milliseconds 250
        try { $records = @(Invoke-Demo @{op='query';view='live'}) } catch { continue }
        if ($records.Count -gt 0) { break }
    }
    if (-not $records.Count) { throw 'Demo did not produce any records.' }
    $record = $records[0]
    if (-not $record.process_verified -or $record.hash.Length -ne 64) { throw 'Process identity enrichment failed.' }
    $good = Invoke-Demo @{op='mark';id=$record.id;verdict='good';scope='connection'}
    $configuration = Invoke-Demo @{op='config'}
    if (@($configuration.rules | Where-Object { $_.id -eq $good.id }).Count -ne 1) { throw 'Rule was not persisted.' }
    $bad = Invoke-Demo @{op='mark';id=$record.id;verdict='bad';scope='connection'}
    if ($bad.verdict -ne 'bad') { throw 'Rule replacement failed.' }
    $configuration = Invoke-Demo @{op='config'}
    if (@($configuration.rules).Count -ne 1) { throw 'Conflicting equivalent rules were not replaced.' }
    [void](Invoke-Demo @{op='delete_rule';id=$bad.id})
    $configuration = Invoke-Demo @{op='config'}
    if (@($configuration.rules).Count -ne 0) { throw 'Rule deletion failed.' }
    $health = Invoke-Demo @{op='status'}
    if ($health.dropped -ne 0 -or $health.storage_error) { throw 'Pipeline reported loss or storage failure.' }
    $backend.Refresh()
    [ordered]@{passed=$true;checks=8;processed=$health.processed;dropped=$health.dropped;private_bytes=$backend.PrivateMemorySize64;data=$data} | ConvertTo-Json
} finally {
    if (Test-Path -LiteralPath $requestPath) { Remove-Item -LiteralPath $requestPath }
    # Stop only the specific child started by this test; it contains synthetic data.
    if (-not $backend.HasExited) { Stop-Process -Id $backend.Id }
}


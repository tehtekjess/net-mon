param([switch]$Live,[switch]$Stress,[string]$BuildPath='build\Release',[string]$OutputPath)
$ErrorActionPreference='Stop'
if($Live -and $Stress) { throw 'Choose -Live or -Stress, not both.' }
$root=Split-Path $PSScriptRoot -Parent
$bin=Join-Path $root $BuildPath
$backend=$null
try {
    if (-not $Live -and -not $Stress) {
        $data=Join-Path $root ('build\ui-demo-'+[Guid]::NewGuid().ToString('N'))
        $backend=Start-Process -FilePath (Join-Path $bin 'netmon_service.exe') -ArgumentList ('--demo --data "'+$data+'" --seconds 45') -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 750
    }
    $result=if($Stress) { & (Join-Path $bin 'netmon_responsiveness_tests.exe') --stress }
            elseif($Live) { & (Join-Path $bin 'netmon_responsiveness_tests.exe') --live }
            else { & (Join-Path $bin 'netmon_responsiveness_tests.exe') }
    $code=$LASTEXITCODE
    $json=$result -join [Environment]::NewLine
    if($OutputPath) { [IO.File]::WriteAllText($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath),$json,[Text.UTF8Encoding]::new($false)) }
    $json
    if($code -ne 0) { throw 'UI responsiveness checks failed; inspect the reported timings and service availability.' }
} finally {
    if($backend -and -not $backend.HasExited) { Stop-Process -Id $backend.Id }
}

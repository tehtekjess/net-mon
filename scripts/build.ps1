param([string]$Preset='vs2019')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
Push-Location $root
try {
    & $cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    $buildPath = if ($Preset -eq 'vs2022') {'build-vs2022'} else {'build'}
    & $cmake --build $buildPath --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & (Join-Path (Split-Path $cmake -Parent) 'ctest.exe') --test-dir $buildPath -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    & $cmake --install $buildPath --config Release --prefix (Join-Path $root 'dist\NetMon')
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
    Get-FileHash -LiteralPath (Join-Path $root 'dist\NetMon\netmon.exe'),(Join-Path $root 'dist\NetMon\netmon_service.exe') -Algorithm SHA256 |
        Format-Table -AutoSize
} finally { Pop-Location }


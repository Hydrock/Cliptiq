param([string]$BuildDir='out/windows-idle', [string[]]$Targets=@('cliptiq_app','idle_profile'), [switch]$Tests)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $projectRoot
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC Build Tools not found' }
$devcmd=Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
cmd /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
 if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$testing=if($Tests){'ON'}else{'OFF'}
& $cmake -S . -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=MinSizeRel "-DBUILD_TESTING=$testing" -DCLIPTIQ_RAVEN_EXPERIMENT=OFF -DCLIPTIQ_BUILD_IDLE_PROBE=ON
if($LASTEXITCODE){throw 'Configure failed'}
$targetNames=@($Targets | ForEach-Object { $_ -split ',' })
& $cmake --build $BuildDir --target @targetNames --parallel 6
if($LASTEXITCODE){throw 'Build failed'}

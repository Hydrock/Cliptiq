param([switch]$Fetch)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $projectRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC C++ Build Tools are required.' }
# Import the developer environment; all file operations stay in PowerShell.
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
cmd /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if ($Fetch) {
    if (!(Test-Path .toolchains/raven/CMakeLists.txt)) {
        git clone https://github.com/oscar-soirey/Raven.git .toolchains/raven
        if ($LASTEXITCODE) { throw 'Raven download failed' }
    }
    git -C .toolchains/raven checkout --detach 9f0cfa6fe3da33022a577e06a6ea843a5ef3f76e
    if ($LASTEXITCODE) { throw 'Raven pin failed' }
    python tools/fetch-footprint-deps.py
    if ($LASTEXITCODE) { throw 'SQLite download failed' }
}
& $cmake -S . -B out/raven-settings -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_TESTING=OFF -DCLIPTIQ_RAVEN_EXPERIMENT=ON
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build out/raven-settings --target settings_native settings_raven --parallel 6
if ($LASTEXITCODE) { throw 'Build failed' }
Write-Host "Binaries: $projectRoot\out\raven-settings\settings-bin"

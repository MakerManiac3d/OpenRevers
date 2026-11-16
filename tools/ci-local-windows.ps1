# SPDX-License-Identifier: GPL-3.0-or-later
# CI local para Windows: los mismos pasos que el job "windows" de
# .github/workflows/build.yml (sin publicar nada).
#   powershell -ExecutionPolicy Bypass -File tools\ci-local-windows.ps1 -Deps   (primera vez: instala herramientas)
#   powershell -ExecutionPolicy Bypass -File tools\ci-local-windows.ps1          (compila y prueba)
# Las dependencias pesadas (Qt, Open CASCADE) quedan en $Deps y solo se hacen una vez.
param([switch]$Deps, [string]$DepsDir = "C:\orv-deps")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$Repo = (Get-Location).Path
$Occt = Join-Path $DepsDir "occt"
$QtRoot = Join-Path $DepsDir "qt"
function Step($t) { Write-Host "`n==> $t" -ForegroundColor Cyan }
function Refresh-Path { $env:PATH = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User") }
Refresh-Path
# Un servicio (p. ej. el runner de GitHub) puede no ver el PATH del usuario que instaló las herramientas.
foreach ($d in "$env:ProgramFiles\CMake\bin", "$env:ProgramFiles\Git\cmd", "$env:ProgramFiles\NSIS", "C:\Program Files\CMake\bin", "C:\Program Files\Git\cmd") {
  if ((Test-Path $d) -and ($env:PATH -notlike "*$d*")) { $env:PATH += ";$d" }
}
foreach ($d in (Get-Item "C:\Users\*\AppData\Local\Microsoft\WinGet\Links" -ErrorAction SilentlyContinue)) {
  if ($env:PATH -notlike "*$($d.FullName)*") { $env:PATH += ";$($d.FullName)" }
}
foreach ($d in (Get-ChildItem "C:\Users\*\AppData\Local\Programs\Python\Python3*" -Directory -ErrorAction SilentlyContinue), (Get-ChildItem "$env:ProgramFiles\Python3*" -Directory -ErrorAction SilentlyContinue)) {
  if ($d -and ($env:PATH -notlike "*$($d.FullName)*")) { $env:PATH += ";$($d.FullName);$($d.FullName)\Scripts" }
}
function Run { param([string]$Cmd) & cmd /c $Cmd; if ($LASTEXITCODE -ne 0) { throw "Fallo: $Cmd" } }

if ($Deps) {
  Step "Herramientas (winget): Visual Studio Build Tools, CMake, Python, NSIS"
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  winget install --id Kitware.CMake -e
  winget install --id Python.Python.3.12 -e
  winget install --id NSIS.NSIS -e
  Refresh-Path
  Step "Qt 6.8 (aqtinstall)"
  python -m pip install --upgrade aqtinstall
  python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O $QtRoot
}
$QtDir = Get-ChildItem $QtRoot -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $QtDir) { throw "No hay Qt en ${QtRoot}: ejecuta con -Deps." }
$QtPrefix = Join-Path $QtDir.FullName "msvc2022_64"
$env:PATH = "$QtPrefix\bin;$Occt\win64\vc14\bin;$env:PATH"

foreach ($t in "cmake", "git") { if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { Write-Host "Usuario: $(whoami)  HOME: $env:USERPROFILE"; Write-Host "PATH: $env:PATH"; throw "Falta '$t' en el PATH: cierra y abre PowerShell (como administrador) y repite el script." } }
if (-not (Test-Path "$Occt\cmake")) {
  Step "Open CASCADE 7.9.3 (solo la primera vez; sobre una hora)"
  $src = Join-Path $DepsDir "occt-src"
  if (-not (Test-Path $src)) { git clone --depth 1 --branch V7_9_3 https://github.com/Open-Cascade-SAS/OCCT.git $src }
  $b = Join-Path $DepsDir "occt-build"
  cmake -S $src -B $b -A x64 -DBUILD_LIBRARY_TYPE=Shared "-DINSTALL_DIR=$Occt" `
    -DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingData=OFF -DBUILD_MODULE_ModelingAlgorithms=OFF `
    -DBUILD_MODULE_Visualization=OFF -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF `
    -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_DETools=OFF `
    "-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKBool;TKFillet;TKOffset;TKFeat;TKMesh;TKPrim;TKBO;TKShHealing" `
    -DUSE_FREETYPE=OFF -DUSE_TK=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF -DUSE_VTK=OFF -DUSE_RAPIDJSON=OFF `
    -DUSE_DRACO=OFF -DUSE_TBB=OFF -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF -DBUILD_DOC_Overview=OFF
  if ($LASTEXITCODE -ne 0) { throw "Fallo al configurar Open CASCADE" }
  cmake --build $b --config Release --parallel; if ($LASTEXITCODE -ne 0) { throw "Fallo al compilar Open CASCADE" }
  cmake --install $b --config Release; if ($LASTEXITCODE -ne 0) { throw "Fallo al instalar Open CASCADE" }
}

$stage = Join-Path $Repo "stage-ci"
Step "Configure"
cmake -S . -B build-ci -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCMAKE_INSTALL_PREFIX=$stage" "-DOpenCASCADE_DIR=$Occt/cmake"
if ($LASTEXITCODE -ne 0) { throw "Fallo al configurar" }
Step "Build"
cmake --build build-ci --config Release --parallel; if ($LASTEXITCODE -ne 0) { throw "Fallo al compilar" }
Step "Unit tests"
ctest --test-dir build-ci -C Release --output-on-failure; if ($LASTEXITCODE -ne 0) { throw "Fallan los tests" }
Step "Install into a staging folder"
cmake --install build-ci --config Release; if ($LASTEXITCODE -ne 0) { throw "Fallo al instalar" }
Step "End to end (the installed app driven through MCP)"
$env:QT_OPENGL = "software"
if (Get-Command python -ErrorAction SilentlyContinue) { python tests\e2e_mcp.py --app "$stage\bin\OpenRevers.exe" --cli "$stage\bin\openrevers-cli.exe" --picture view-ci.png; if ($LASTEXITCODE -ne 0) { throw "Fallo la prueba e2e" } } else { Write-Host "AVISO: no hay python para todo el equipo; se omite la prueba e2e." -ForegroundColor Yellow }
if ($LASTEXITCODE -ne 0) { throw "Falla el e2e" }
Step "Installer"
Push-Location build-ci
$env:PATH = "C:\Program Files (x86)\NSIS;$env:PATH"
cpack -C Release; if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Falla el instalador" }
Pop-Location
Write-Host "`nCI local Windows: todo en verde." -ForegroundColor Green

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
function Run { param([string]$Cmd) & cmd /c $Cmd; if ($LASTEXITCODE -ne 0) { throw "Fallo: $Cmd" } }

if ($Deps) {
  Step "Herramientas (winget): Visual Studio Build Tools, CMake, Python, NSIS"
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  winget install --id Kitware.CMake -e
  winget install --id Python.Python.3.12 -e
  winget install --id NSIS.NSIS -e
  Write-Host "Cierra y abre PowerShell para que el PATH se actualice y repite SIN -Deps si falta algo." -ForegroundColor Yellow
  Step "Qt 6.8 (aqtinstall)"
  python -m pip install --upgrade aqtinstall
  python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O $QtRoot
}
$QtDir = Get-ChildItem $QtRoot -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $QtDir) { throw "No hay Qt en ${QtRoot}: ejecuta con -Deps." }
$QtPrefix = Join-Path $QtDir.FullName "msvc2022_64"
$env:PATH = "$QtPrefix\bin;$Occt\win64\vc14\bin;$env:PATH"

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
python tests\e2e_mcp.py --app "$stage\bin\OpenRevers.exe" --cli "$stage\bin\openrevers-cli.exe" --picture view-ci.png
if ($LASTEXITCODE -ne 0) { throw "Falla el e2e" }
Step "Installer"
Push-Location build-ci
$env:PATH = "C:\Program Files (x86)\NSIS;$env:PATH"
cpack -C Release; if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Falla el instalador" }
Pop-Location
Write-Host "`nCI local Windows: todo en verde." -ForegroundColor Green

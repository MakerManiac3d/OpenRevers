# Compilar OpenRevers · Building OpenRevers

Requisitos / Requirements:

- CMake 3.24 o posterior;
- un compilador C++20: MSVC 2022, GCC 12 o Clang 15, o posteriores;
- **Qt 6.4 o posterior** (Widgets, OpenGL, Network, Concurrent, LinguistTools);
- Python 3, para los tests de punta a punta y las traducciones.

Las demás librerías (Eigen, nlohmann/json, PMP, GoogleTest) se usan del
sistema si están instaladas; si no, CMake las descarga en una versión fija
(`cmake/Dependencies.cmake`). / *Other libraries are taken from the system or
fetched at a pinned version.*

## Windows

1. Instala Visual Studio 2022 (o posterior) con *Desarrollo para el
   escritorio con C++*, y Qt 6.8 para MSVC 64 bits (instalador de Qt o
   [aqtinstall](https://github.com/miurahr/aqtinstall)).
2. Compila:

   ```bat
   cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
   cmake --build build --config Release
   ctest --test-dir build -C Release
   cmake --install build --config Release --prefix stage
   cd build && cpack -C Release
   ```

   `cpack` necesita [NSIS](https://nsis.sourceforge.io/) para el instalador
   `.exe`; el `.zip` se genera siempre.

## Linux (Ubuntu 24.04)

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools \
  qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libgl1-mesa-dev libeigen3-dev \
  nlohmann-json3-dev libgtest-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/src/app/OpenRevers
```

## macOS

```bash
brew install cmake ninja qt eigen nlohmann-json googletest
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

macOS todavía no se prueba en GitHub. / *macOS is not yet tested in CI.*

## Opciones de CMake

| Opción | Por defecto | Efecto |
| --- | --- | --- |
| `ORV_BUILD_APP` | `ON` | Compila la aplicación Qt. Con `OFF` se compilan solo el núcleo, `openrevers-cli` y los tests, sin necesidad de Qt |
| `ORV_BUILD_TESTS` | `ON` | Compila los tests |
| `ORV_WARNINGS_AS_ERRORS` | `OFF` | Trata los avisos como errores en el código de OpenRevers (activado en GitHub para Linux) |

## Tests

```bash
ctest --test-dir build --output-on-failure
xvfb-run -a python3 tests/e2e_mcp.py --app build/src/app/OpenRevers --cli build/src/openrevers-cli
```

`e2e_mcp.py` comprueba toda la cadena con una malla generada:

1. abre la aplicación;
2. conecta un agente por MCP;
3. importa, analiza, repara, rellena, remalla, exporta y deshace;
4. comprueba que el agente no puede escribir fuera de sus carpetas.

En Linux necesita una pantalla (`xvfb-run`). En Windows, `QT_OPENGL=software`
permite ejecutarlo sin tarjeta gráfica.

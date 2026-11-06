# Componentes de terceros · Third-party components

OpenRevers se distribuye bajo la GPL-3.0-or-later. Usa estos componentes, cada
uno con su licencia, todos compatibles con la GPL-3.0. / *Each keeps its
licence; all are GPL-3.0 compatible.*

| Componente | Versión | Licencia | Uso | Cómo llega |
| --- | --- | --- | --- | --- |
| [Qt](https://www.qt.io/) | 6.4 o posterior (6.8 en Windows) | LGPL-3.0 | Interfaz, OpenGL, hilos, red local | Enlazado dinámicamente; las DLL de Qt van junto al ejecutable y se pueden sustituir |
| [PMP Library](https://www.pmp-library.org) | 3.0.0 | MIT | Relleno de agujeros, suavizado, diezmado, remallado | Compilado de su código fuente |
| [Eigen](https://eigen.tuxfamily.org) | 3.4 | MPL-2.0 | Álgebra lineal, vectores | Solo cabeceras |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11 | MIT | Comandos, proyecto, MCP | Solo cabeceras |
| [miniz](https://github.com/richgel999/miniz) | 3.0.2 | MIT | ZIP de los paquetes 3MF | Compilado de su código fuente |
| [nanoflann](https://github.com/jlblancoc/nanoflann) | 1.6.1 | BSD-2-Clause | Vecinos más cercanos en nubes de puntos | Solo cabeceras |
| [PoissonRecon](https://github.com/mkazhdan/PoissonRecon) | 18.76 (commit `262b0f5`) | MIT | Reconstrucción de Poisson apantallada | Sus cabeceras, compiladas en un archivo propio |
| [QuadriFlow](https://github.com/hjwdzh/QuadriFlow) | commit `810b7a0` | BSD-3-Clause | Remallado en quads | Compilado de su código fuente, con los cambios de `cmake/PatchQuadriFlow.cmake` (sin Boost, sin llamadas a otros programas) |
| [LEMON](https://lemon.cs.elte.hu) | 1.3.1 (incluido en QuadriFlow) | Boost Software License 1.0 | Grafos y flujos para QuadriFlow | Solo cabeceras |
| [PCG32](https://www.pcg-random.org) (incluido en QuadriFlow) | — | Apache-2.0 | Números aleatorios de QuadriFlow | Solo cabeceras |
| [GoogleTest](https://github.com/google/googletest) | 1.14 | BSD-3-Clause | Tests (no se distribuye) | Solo para compilar los tests |
| Runtime de Microsoft Visual C++ | el de la compilación | Licencia redistribuible de Microsoft | Ejecución en Windows | Copiado junto al ejecutable |

El instalador copia los textos de licencia de los componentes compilados en
`share/doc/openrevers/licenses`.

Las referencias bibliográficas de los algoritmos están en
[docs/algoritmos](docs/algoritmos/mallas.md).

## Previstos para las próximas fases

| Componente | Licencia | Para qué |
| --- | --- | --- |
| [Open CASCADE Technology](https://dev.opencascade.org) | LGPL-2.1 con excepción | Núcleo CAD: B-rep, NURBS, STEP, IGES |
| [CGAL](https://www.cgal.org) | GPL-3.0+ / LGPL-3.0+ | Detección de primitivas (RANSAC eficiente), crecimiento de regiones |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | Apache-2.0 | Paralelismo |
| [Geogram](https://github.com/BrunoLevy/geogram) | BSD-3-Clause | Remallado y reconstrucción robustos |
| [libigl](https://libigl.github.io) | MPL-2.0 | Curvatura, parametrización |
| [Manifold](https://github.com/elalish/manifold) | Apache-2.0 | Booleanas de malla |
| [Open3D](https://www.open3d.org) | MIT | Registro ICP, nubes de puntos |
| [Instant Meshes](https://github.com/wjakob/instant-meshes) | BSD-3-Clause | Remallado en quads alineado con la curvatura |
| [nanoflann](https://github.com/jlblancoc/nanoflann) | BSD-2-Clause | Búsqueda de vecinos |

Cada componente se añadirá a la tabla de arriba, con su versión fijada, en la
fase que lo incorpore.

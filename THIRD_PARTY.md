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
| [GoogleTest](https://github.com/google/googletest) | 1.14 | BSD-3-Clause | Tests (no se distribuye) | Solo para compilar los tests |
| Runtime de Microsoft Visual C++ | el de la compilación | Licencia redistribuible de Microsoft | Ejecución en Windows | Copiado junto al ejecutable |

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
| [QuadriFlow](https://github.com/hjwdzh/QuadriFlow) | BSD-3-Clause | Remallado en quads |
| [Instant Meshes](https://github.com/wjakob/instant-meshes) | BSD-3-Clause | Remallado en quads alineado con la curvatura |
| [nanoflann](https://github.com/jlblancoc/nanoflann) | BSD-2-Clause | Búsqueda de vecinos |

Cada componente se añadirá a la tabla de arriba, con su versión fijada, en la
fase que lo incorpore.

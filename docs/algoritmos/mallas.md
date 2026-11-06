# Algoritmos de mallas (fase 1)

Cada operación, qué hace, en qué se basa y sus límites. El código está en
`src/core/mesh`; los tests, en `tests/test_mesh.cpp`.

| Operación | Método | Implementación | Referencia |
| --- | --- | --- | --- |
| Lectura STL | Unión exacta de esquinas coincidentes (hash de los bits del `float`) | Propia | — |
| Análisis | Mapa de aristas no dirigidas; recorrido de bordes con la cara a la izquierda; componentes por búsqueda en profundidad; volumen por el teorema de la divergencia | Propia | — |
| Unir vértices | Rejilla espacial de lado = tolerancia, búsqueda en las 27 celdas vecinas | Propia | — |
| Orientar | Propagación por aristas de dos caras; cada pieza cerrada se orienta con volumen positivo | Propia | — |
| Rellenar agujeros | Triangulación de peso mínimo, refinado y suavizado del parche | [PMP](https://www.pmp-library.org) | P. Liepa, *Filling Holes in Meshes*, SGP 2003 |
| Suavizado | Laplaciano explícito o implícito (Euler implícito, conserva el volumen) | PMP | M. Desbrun et al., *Implicit Fairing of Irregular Meshes*, SIGGRAPH 1999 |
| Diezmado | Colapso de aristas por métrica cuádrica, con límite de distancia de Hausdorff | PMP | M. Garland, P. Heckbert, *Surface Simplification Using Quadric Error Metrics*, SIGGRAPH 1997 |
| Remallado uniforme | Dividir, colapsar, girar y relajar aristas, con proyección a la malla original | PMP | M. Botsch, L. Kobbelt, *A Remeshing Approach to Multiresolution Modeling*, SGP 2004 |
| Remallado adaptativo | Longitud de arista según la curvatura y un error máximo | PMP | M. Dunyach et al., *Adaptive Remeshing for Real-Time Mesh Deformation*, Eurographics 2013 |
| Aristas vivas | Ángulo diedro mayor que `featureAngle`: la arista se marca y el remallado la conserva | PMP | — |
| Remallado en quads | Campo de orientaciones y de posiciones con simetría de 4 rotaciones; el mapa entero se arregla con un flujo máximo para que no haya quads invertidos | [QuadriFlow](https://github.com/hjwdzh/QuadriFlow), modificado (ver abajo) | J. Huang, Y. Zhou, M. Nießner, J. R. Shewchuk, L. J. Guibas, *QuadriFlow: A Scalable and Robust Method for Quadrangulation*, SGP 2018; W. Jakob et al., *Instant Field-Aligned Meshes*, SIGGRAPH Asia 2015 |
| Exportar T-Spline | Semiaristas de la malla de quads escritas en el formato TS0200 de Autodesk (`.tsm`), en centímetros, orientada hacia fuera | Propia, con el mismo formato que ya se probó en Fusion | — |

## QuadriFlow en OpenRevers

Se compila desde su código fuente, en una versión fija, con dos cambios que
aplica `cmake/PatchQuadriFlow.cmake` (si el código original cambia, la
configuración se detiene en vez de aplicar mal el cambio):

- **Sin Boost.** El flujo máximo de Boykov–Kolmogorov de Boost se sustituye
  por el de caminos aumentantes (Edmonds–Karp) que QuadriFlow ya trae. Da el
  mismo número de quads que el original (esfera de 20 480 triángulos: 9 872
  frente a 9 791 para 10 000 pedidos) y tarda entre 1,1 y 1,6 veces más. Se
  probó también el *push-relabel* de LEMON y se descartó: sus flujos dan
  rodeos y el número de quads se quedaba en la mitad del pedido.
- **Nunca ejecuta otro programa.** El paso opcional SAT llamaba a `minisat`
  con `system()`; esas llamadas se quitan y el paso queda desactivado.

La malla de quads guarda también su triangulación, así que todo lo que
trabaja con triángulos (análisis, exportar STL) sirve igual. Una operación
sobre los triángulos (reparar, suavizar, remallar…) deja la malla en
triángulos; invertir las normales conserva los quads.

## Límites conocidos

- **Unir vértices** puede juntar, con una tolerancia grande, dos superficies
  distintas muy próximas (las dos paredes de una chapa fina). Usa una
  tolerancia menor que el espesor.
- **Rellenar agujeros** crea un parche suave: no reproduce aristas vivas que
  crucen el agujero. Para eso, en fases siguientes, el relleno se hará con las
  primitivas ajustadas.
- **El suavizado** redondea las aristas vivas: no hay todavía un suavizado
  que las conserve.
- Las mallas con **aristas no manifold** se pasan a PMP sin las caras que lo
  impedirían. Repara antes de procesar. El remallado en quads las rechaza.
- **El remallado en quads** tarda más cuanto más densa es la malla de
  entrada: diezma o remalla antes un escaneo de millones de triángulos. El
  número de quads se acerca al pedido, no es exacto.
- **Para T-Spline** la malla de quads debe ser cerrada: rellena antes los
  agujeros. Fusion la convierte en un cuerpo de Forma (Crear forma → Utilidades
  → Importar en la API: `addByTSMFile`).

## Comprobación

Los tests usan mallas generadas con respuesta conocida:

- **cubo**: área, volumen y aristas vivas tras remallar;
- **esfera** de icosaedro subdividido: volumen, cierre, radio tras remallar y
  ruido tras suavizar;
- **lámina abierta**: un único borde de 40 mm;
- **esfera con dos agujeros**: queda cerrada tras rellenar;
- **quads** (`tests/test_quads.cpp`): esfera en quads cerrada, en el radio y
  cerca del número pedido; caja en quads con sus medidas y volumen; OBJ y PLY
  de ida y vuelta; el `.tsm` con sus recuentos, la característica de Euler
  y cada semiarista con su gemela.

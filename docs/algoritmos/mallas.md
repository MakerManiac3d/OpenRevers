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
  impedirían. Repara antes de procesar.

## Comprobación

Los tests usan mallas generadas con respuesta conocida:

- **cubo**: área, volumen y aristas vivas tras remallar;
- **esfera** de icosaedro subdividido: volumen, cierre, radio tras remallar y
  ruido tras suavizar;
- **lámina abierta**: un único borde de 40 mm;
- **esfera con dos agujeros**: queda cerrada tras rellenar.

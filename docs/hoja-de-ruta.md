# Hoja de ruta

Cinco fases. Cada una termina con un instalador usable y un criterio medible
sobre piezas reales.

| Fase | Contenido | Criterio para cerrarla | Estado |
| --- | --- | --- | --- |
| 1 · Base y mallas | Programa, visor, proyecto, deshacer; importar y exportar; analizar, reparar, rellenar, suavizar, diezmar, remallar; MCP | Abre 10 M de triángulos sin bloquearse y repara las piezas de referencia | **En curso** (v0.1: todo salvo remallado en quads, nubes de puntos y 3MF) |
| 2 · Alineación y primitivas | Alineación a ejes, 3-2-1 y mejor ajuste; segmentación automática; plano, cilindro, cono, esfera y toro con σ | Halla los planos y cilindros de una pieza de referencia al nivel de ruido del escáner | Pendiente |
| 3 · Sólido paramétrico | Bocetos desde secciones con restricciones automáticas; extruir, revolución, redondeos, taladros; exportar STEP (Open CASCADE) | Una pieza mecánica en STEP con el 90 % de la malla dentro de ±0,2 mm | Pendiente |
| 4 · Superficies libres | Superficie NURBS automática; híbrido sólido + libre; exportación T-Spline (.tsm) | Una piel libre con desviación mediana por debajo de 0,05 mm | Pendiente |
| 5 · Inspección y fabricación | Mapa de desviación, secciones comparadas, GD&T, informe PDF; análisis para impresión 3D y CNC | Informe PDF de una pieza real, con pasa o no pasa en cada cota | Pendiente |

En todas las fases: herramientas MCP, documentación, tests e instalador.

## Pendiente de la fase 1

- Remallado en quads, para superficies T-Spline y SubD (QuadriFlow o Instant
  Meshes, ambos BSD-3).
- Nubes de puntos a malla (Poisson apantallado) y lectura de E57, XYZ y 3MF.
- Prueba de rendimiento con 10 M de triángulos.
- Firma del instalador de Windows.
- Compilación en GitHub para macOS.

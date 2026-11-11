# Hoja de ruta

Cinco fases. Cada una termina con un instalador usable y un criterio medible
sobre piezas reales.

| Fase | Contenido | Criterio para cerrarla | Estado |
| --- | --- | --- | --- |
| 1 · Base y mallas | Programa, visor, proyecto, deshacer; importar y exportar; analizar, reparar, rellenar, suavizar, diezmar, remallar; MCP | Abre 10 M de triángulos sin bloquearse y repara las piezas de referencia | Hecha (v0.2), salvo medir el dibujo con tarjeta gráfica |
| 2 · Alineación y primitivas | Alineación a ejes, 3-2-1 y mejor ajuste; segmentación automática; plano, cilindro, cono, esfera y toro con σ | Halla los planos y cilindros de una pieza de referencia al nivel de ruido del escáner | **En curso** (v0.3: todo hecho y probado en piezas sintéticas; falta la pieza escaneada real) |
| 3 · Sólido paramétrico | Bocetos desde secciones con restricciones automáticas; extruir, revolución, redondeos, taladros; exportar STEP (Open CASCADE) | Una pieza mecánica en STEP con el 90 % de la malla dentro de ±0,2 mm | Pendiente |
| 4 · Superficies libres | Superficie NURBS automática; híbrido sólido + libre; T-Spline ajustado a la malla (la exportación `.tsm` desde quads ya existe) | Una piel libre con desviación mediana por debajo de 0,05 mm | Pendiente |
| 5 · Inspección y fabricación | Mapa de desviación, secciones comparadas, GD&T, informe PDF; análisis para impresión 3D y CNC | Informe PDF de una pieza real, con pasa o no pasa en cada cota | Pendiente |

En todas las fases: herramientas MCP, documentación, tests e instalador.

## Pendiente de la fase 1

- Rendimiento: medido con 10 M de triángulos ([rendimiento](desarrollo/rendimiento.md)); falta medir el dibujo con tarjeta gráfica y acelerar el diezmado.
- Firma del instalador de Windows.
- Compilación en GitHub para macOS.

## Pendiente de la fase 2

- Probar el criterio con un escaneo real de una pieza mecánica de medidas conocidas.
- Seleccionar puntos y formas con el ratón en el visor.
- Redondeos como formas propias.

## Después de la fase 2

- **Actualizaciones** con WinSparkle: canal estable y beta sobre las versiones de GitHub, firma EdDSA del paquete, frecuencia de comprobación elegida por el usuario en el primer arranque (diaria, semanal, mensual o bajo demanda), conservar ajustes y poder volver atrás.
- **Firma del instalador** con SignPath (gratis para código abierto) al hacer público el repositorio.

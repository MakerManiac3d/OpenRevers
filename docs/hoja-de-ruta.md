# Hoja de ruta

Seis fases. Cada una termina con un instalador usable y un criterio medible
sobre piezas reales.

| Fase | Contenido | Criterio para cerrarla | Estado |
| --- | --- | --- | --- |
| 1 · Base y mallas | Programa, visor, proyecto, deshacer; importar y exportar; analizar, reparar, rellenar, suavizar, diezmar, remallar; MCP | Abre 10 M de triángulos sin bloquearse y repara las piezas de referencia | Hecha (v0.2), salvo medir el dibujo con tarjeta gráfica |
| 2 · Alineación y primitivas | Alineación a ejes, 3-2-1 y mejor ajuste; segmentación automática; plano, cilindro, cono, esfera y toro con σ | Halla los planos y cilindros de una pieza de referencia al nivel de ruido del escáner | Casi hecha (v0.3.1: probada en una leva escaneada; taladros de Ø2,75 a ±0,04 mm del calibre). Falta repetirla con la 0.3.1 |
| 3 · Sólido paramétrico | Bocetos desde secciones con restricciones automáticas; extruir, revolución, redondeos, taladros; exportar STEP (Open CASCADE); **edición a mano** de todo lo que hace la IA | Una pieza mecánica en STEP con el 90 % de la malla dentro de ±0,2 mm | **Casi hecha** (v0.5): la leva en STEP, 81 % dentro de ±0,2 mm por el alabeo de la pieza escaneada (contorno a ≤0,13 mm); edición a mano en la v0.5. Falta una pieza sin alabeo y las restricciones |
| 4 · Superficies libres | Curvas spline en bocetos; superficies por secciones (loft) y barridos; ajuste de una superficie B-spline a una zona del escaneo; cortar, coser y engrosar; patrón circular; autosuperficie por parches para formas orgánicas; T-Spline ajustado a la malla (la exportación `.tsm` desde quads ya existe) | Una piel libre con desviación mediana por debajo de 0,05 mm; un álabe de turbina por secciones | **En curso** (v0.6: pasos 1 a 4; falta la autosuperficie y probarla en un escaneo real) |
| 5 · Inspección y fabricación | Mapa de desviación, secciones comparadas, GD&T, informe PDF; análisis para impresión 3D y CNC | Informe PDF de una pieza real, con pasa o no pasa en cada cota | Pendiente |
| 6 · Escaneo directo | Escanear desde OpenRevers con los escáneres de CrealityScan (Creality Raptor y familia Orbbec): lista de dispositivos, conectar, escanear con vista previa de la nube, detener y pasar el resultado a un cuerpo con deshacer; la biblioteca del escáner instalada por CrealityScan se carga en tiempo de ejecución si está, y el programa funciona sin ella | Un escaneo del Raptor entra en OpenRevers sin pasar por CrealityScan ni por un archivo intermedio, y sin conexiones a internet | Pendiente (análisis de la API en curso) |

En todas las fases: herramientas MCP, documentación, tests e instalador.

## Pendiente de la fase 1

- Rendimiento: medido con 10 M de triángulos ([rendimiento](desarrollo/rendimiento.md)); falta medir el dibujo con tarjeta gráfica y acelerar el diezmado.
- Firma del instalador de Windows.
- Compilación en GitHub para macOS.

## Pendiente de la fase 2

- Probar el criterio con un escaneo real de una pieza mecánica de medidas conocidas.
- Seleccionar puntos y formas con el ratón en el visor.
- Redondeos como formas propias.

## Fase 3, por pasos

1. **Motor de sólidos:** Open CASCADE (7.6 de Ubuntu en Linux, 7.9 precompilado en Windows); un cuerpo nuevo de tipo sólido que el visor dibuja, el proyecto guarda y se exporta a STEP.
2. **Bocetos desde secciones:** cortar la malla con un plano, convertir el contorno en rectas y arcos y proponer las restricciones (horizontal, tangente, concéntrico, radios iguales, paso), con cotas editables.
3. **Operaciones con historial:** extruir, cortar, revolución, taladro desde un cilindro detectado, redondeos; el árbol se recalcula al cambiar una cota.
4. **Comprobación:** desviación del sólido frente a la malla y exportación STEP.

Primer hito: la leva escaneada en STEP, con el pivote y los cuatro taladros paramétricos.

## Edición a mano (v0.5, dentro de la fase 3)

La IA hace la pieza entera, y la persona retoca lo que no llegó o quiere
cambiar sin salir del programa: selección de caras, aristas y elementos del
historial; propiedades editables; reordenar y suprimir; editor de bocetos
sobre la sección del escaneo; mapa de desviación con zonas. Pendiente:
restricciones y cotas en el editor de bocetos, y manipuladores 3D (arrastrar
la cara de una extrusión).

## Fase 4, por pasos

1. **Curvas spline** en los bocetos y en el ajuste desde secciones, donde una
   recta o un arco no bastan.
2. **Superficies por secciones y barridos:** una superficie o un sólido que
   pasa por varios bocetos (álabes, conductos, mangos).
3. **Ajuste directo de una superficie B-spline** a una zona del escaneo, con
   su desviación.
4. **Operaciones de superficie:** cortar el sólido por una superficie, coser,
   engrosar, patrón circular.
5. **Autosuperficie** para piezas orgánicas: red de parches sobre la malla en
   quads, una B-spline por parche con continuidad tangente, cosidas en un
   sólido.

Primer hito: un álabe por secciones repetido en patrón circular, y una cara
libre ajustada a un escaneo. Hecho en la v0.6 con piezas de prueba (una
turbina de seis álabes, una tapa abombada con 0,2 % de error en volumen);
falta un escaneo real.

## Fase 6, por pasos (escaneo directo)

Objetivo: escanear desde OpenRevers con cualquier escáner que maneje
CrealityScan (el del usuario es un Creality Raptor). Es interoperabilidad: se
llama a la biblioteca que ya instala CrealityScan; no se copia ni se
redistribuye código de Creality ni de Orbbec.

1. **Firmas de la API.** Sacar parámetros, tipos y estructuras de las
   funciones C `obscan_*` de `lib_orbbec_scan.dll` (224 de sus 447
   exportaciones) desde las declaraciones P/Invoke de `OBScan.SDK.dll`
   (metadatos IL2CPP de CrealityScan, versión 29), todo en local con REA, y
   contrastarlas con el código nativo de la DLL. Escribir cabeceras propias.
2. **Privacidad.** Comprobar si `lib_orbbec_scan.dll` se conecta a internet
   (registro del dispositivo, wifi, funciones de IA). OpenRevers promete que
   nada sale del equipo: si la biblioteca conecta, hay que impedirlo o
   avisar y pedir permiso.
3. **Licencia.** Añadir a la GPL-3.0-or-later una excepción de la sección 7
   que permita cargar en tiempo de ejecución las bibliotecas del escáner
   instaladas por CrealityScan. Comprobar antes que ningún otro autor de
   código deba consentir.
4. **Módulo `src/scan/`.** Carga dinámica (`LoadLibrary`/`GetProcAddress`)
   desde la carpeta de CrealityScan (`CrealityScan_Data\Plugins\x86_64`);
   sin ella, el programa compila y funciona igual. No se distribuye ninguna
   DLL.
5. **Comandos** (para la interfaz, los scripts y el agente): `scan.devices`,
   `scan.connect`, `scan.start`, `scan.stop`, `scan.export_to_body`. Ciclo de
   la API: contexto → gestión de dispositivos → configuración de modo →
   sesión de escaneo → fotogramas (nube, pose, seguimiento) → registro y
   mallado → exportación.
6. **Vista previa** de la nube en el visor mientras se escanea, con el
   estado del seguimiento.
7. **Prueba con el Raptor:** un escaneo completo hasta cuerpo, comparado con
   el mismo escaneo exportado desde CrealityScan.

Modelos que nombra la biblioteca: Otter, Otter Lite, Ferret, Raptor, Raptor
Pro, Raptor Pro Max, Raptor X, Raptor Mini, Raptor E y Sermoon P1.

## Al final (al hacer público el repositorio)

El sistema de actualizaciones se deja para cuando el repositorio sea público: el actualizador necesita una lista de versiones pública, y la firma del instalador con SignPath también lo exige. Hasta entonces, cada versión se instala a mano encima de la anterior; los ajustes y los proyectos se conservan.

- **Actualizaciones** con WinSparkle: canal estable y beta sobre las versiones de GitHub, firma EdDSA del paquete, frecuencia de comprobación elegida por el usuario en el primer arranque (diaria, semanal, mensual o bajo demanda), conservar ajustes y poder volver atrás.
- **Firma del instalador** con SignPath (gratis para código abierto) al hacer público el repositorio.

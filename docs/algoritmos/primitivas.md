# Primitivas, segmentación y alineación (fase 2)

Código: `src/core/fit/` (sin Qt), comandos en `src/commands/FeatureCommands.cpp`,
pruebas en `tests/test_fit.cpp`, `tests/test_segment.cpp` y `tests/test_align.cpp`.

## Ajuste de primitivas

| Forma | Primera estimación | Distancia que se minimiza |
| --- | --- | --- |
| Plano | Componentes principales (es ya el ajuste por mínimos cuadrados ortogonales) | n·(p − o) |
| Esfera | Ajuste algebraico lineal \|p\|² = 2c·p + k | \|p − c\| − r |
| Cilindro | Eje: autovector menor de Σ n nᵀ (las normales son perpendiculares al eje); círculo de Kåsa en el plano perpendicular | distancia al eje − r |
| Cono | Eje: autovector menor de la covarianza de las normales (forman un ángulo fijo con el eje); vértice: todos los planos tangentes pasan por él (n·A = n·p) | d·cos θ − h·sen θ |
| Toro | Los centros del tubo (p − r·n) están en un círculo plano: búsqueda de r en rejilla y sección áurea | √((ρ − R)² + h²) − r |

- **Refinado:** Levenberg–Marquardt sobre las distancias geométricas (ortogonales), con jacobiano por diferencias centrales. Se trabaja en un sistema normalizado (centroide en 0, radio cuadrático medio 1), para que tolerancias y pasos no dependan del tamaño de la pieza.
- **Puntos atípicos:** tres rondas; en cada una se descartan los puntos a más de 3σ robusta (1,4826 × mediana de |d|).
- **Conjuntos grandes:** el ajuste usa un subconjunto uniforme de hasta 3 000 puntos, y la calidad (σ, desviación máxima, atípicos) se mide sobre todos.
- **σ** es la raíz del error cuadrático medio de los puntos conservados.
- **Elección automática del tipo (`fitBest`):** se ajustan los cinco y se queda el más sencillo (plano; esfera o cilindro; cono; toro) cuya σ no supera en un 25 % la mejor, penalizando la fracción de atípicos. Se descartan formas absurdas para la zona: un radio de más de 20 veces su tamaño, o un cono de menos de 1° o más de 88°.

Referencias:
- G. Lukács, R. Martin, D. Marshall, *Faithful least-squares fitting of spheres, cylinders, cones and tori for reliable segmentation*, ECCV 1998.
- C. M. Shakarji, *Least-squares fitting algorithms of the NIST algorithm testing system*, J. Res. NIST 103(6), 1998.
- I. Kåsa, *A circle fitting procedure and its error analysis*, IEEE Trans. Instrum. Meas. 25, 1976.
- K. Levenberg (1944) y D. Marquardt (1963), método de mínimos cuadrados no lineales.

## Segmentación

Crecimiento de regiones guiado por la forma ajustada:

1. **Ruido del escáner:** en ~300 parches de 60 caras se ajusta un paraboloide (altura cuadrática sobre el plano del parche, que sigue cualquier superficie suave a ese tamaño, incluidas las sillas de un toro). Se toma el percentil 25 de la σ de sus vértices.
2. **Semillas:** las caras más lisas primero (menor cambio de normal con sus vecinas).
3. **Parche inicial** de 60 caras cruzando solo aristas suaves (< 20°) y la forma más sencilla que encaja.
4. **Crecimiento:** entra una cara vecina cuando su centro está a menos de *tolerancia × ruido* de la forma, sus vértices a menos de 1,5 veces eso y su normal a menos de `angle` grados de la de la forma.
5. **Reajuste** cada vez que la región dobla su tamaño y cuando deja de crecer. Si el tipo actual empieza a no encajar, se prueban todos: un trozo pequeño de un cono parece un cilindro, y uno de un toro, una esfera.
6. **Unión:** dos regiones del mismo tipo se juntan si el ajuste conjunto sigue dentro de la tolerancia (una cara cortada por una ranura, un cilindro partido).
7. **Pasada final:** cada región toma las caras libres vecinas que están sobre su forma, y una región pequeña la absorbe una vecina mayor cuya forma le sirve.

Las regiones demasiado pequeñas, los redondeos y las zonas libres quedan sin asignar.

Referencias:
- T. Rabbani, F. van den Heuvel, G. Vosselman, *Segmentation of point clouds using smoothness constraint*, ISPRS 2006.
- R. Schnabel, R. Wahl, R. Klein, *Efficient RANSAC for point-cloud shape detection*, Computer Graphics Forum 26(2), 2007. Es el enfoque alternativo, por muestreo; aquí se crece desde las semillas.

## Alineación

- **Ejes principales:** momentos de la superficie ponderados por área (puntos medios de las aristas de cada triángulo). Los signos se fijan por el momento de tercer orden, para que la misma pieza caiga siempre igual.
- **3-2-1 por datums:**
  - el primario fija Z: hacia dentro de la pieza desde una cara, contra su normal exterior, o a lo largo de un eje;
  - el secundario fija X: con un taladro paralelo a Z, X va del taladro al terciario;
  - el origen toma en orden lo que fija cada datum (una cara, una coordenada; un eje, dos; una esfera, tres). Se ignora una restricción en una dirección ya fijada: dos taladros paralelos dan el origen con el primero y la dirección con el segundo.
- **Mejor ajuste:** ICP punto-plano:
  - correspondencias por el vecino más cercano (k-d tree, nanoflann) y normales de la referencia;
  - pesos de Tukey con escala robusta, y distancia de corte que se estrecha al converger;
  - opcionalmente, un arranque grueso que hace coincidir los ejes principales y prueba las cuatro combinaciones de signos.
  - Informa RMS, mediana, percentil 95 y solape.

Referencias:
- Y. Chen, G. Medioni, *Object modelling by registration of multiple range images*, Image and Vision Computing 10(3), 1992.
- S. Rusinkiewicz, M. Levoy, *Efficient variants of the ICP algorithm*, 3DIM 2001.
- J. W. Tukey (función biponderada), en P. J. Huber, *Robust Statistics*, 1981.

## Resultados de las pruebas

Piezas sintéticas con ruido de 0,02 mm, un 3 % de atípicos y una posición arbitraria:

| Prueba | Resultado |
| --- | --- |
| Cilindro Ø12 visto en 120° | Diámetro ±0,04 mm, eje < 0,2° |
| Cono de 30° | Ángulo ±0,1° |
| Toro R30 / r5, un cuarto | ±0,05 mm |
| Bloque 60 × 40 × 20 con taladro Ø16, reconstruido por Poisson | Los 6 planos y el cilindro; Ø 15,998 mm, eje en su sitio a menos de 0,05 mm |
| Mejor ajuste de un segundo escaneo movido 6° y 3 mm | Error < 0,01° y < 0,01 mm |
| Mejor ajuste con 120° de giro y 300 mm de traslación, con arranque grueso | Error < 0,05° y < 0,05 mm |

Segmentar 300 000 triángulos tarda unos 15 s en 2 núcleos.

## Selección en el visor

Un clic lanza un rayo desde la cámara, con la inversa de la proyección por la
vista. Primero se descartan los cuerpos cuya caja no corta el rayo (método
de las placas) y después se prueban sus triángulos con Möller–Trumbore
(T. Möller, B. Trumbore, *Fast, minimum storage ray-triangle intersection*,
J. Graphics Tools 2(1), 1997), unos milisegundos por millón de caras. La
cara tocada da la región y, con ella, la forma.

## Límites conocidos

- Falta el criterio de la fase con una **pieza escaneada real** de medidas conocidas.
- La segmentación no detecta todavía redondeos (toros de transición) como formas propias: quedan sin asignar.
- En 3-2-1, el sentido de Y sigue la regla de la mano derecha desde Z y X: según qué cara se elija como terciaria, la pieza queda en +Y o en −Y.

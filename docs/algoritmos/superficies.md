# Curvas y superficies libres (fase 4)

Código: `src/core/solid/BSpline.cpp` (sin Open CASCADE), su traducción a
Open CASCADE en `src/core/solid/Solid.cpp`, el ajuste de contornos en
`src/core/solid/SketchFit.cpp` y los comandos en
`src/commands/PartCommands.cpp`. Pruebas en `tests/test_solid.cpp` y
`tests/test_document.cpp`.

## B-splines

- **Curvas:** B-spline uniforme y cerrada en los extremos (*clamped*) de grado
  `min(3, polos − 1)`: empieza en su primer polo y acaba en el último. Así una
  spline encadena con las rectas y arcos vecinos en sus juntas, igual que
  ellos.
- **Funciones base:** recursión de Cox–de Boor, algoritmos A2.1 (búsqueda del
  tramo) y A2.2 (funciones no nulas) de Piegl y Tiller.
- **La misma geometría en los dos lados:** el boceto, el editor y los tests
  evalúan la curva con este código; `Solid` construye un
  `Geom_BSplineCurve` con los mismos polos, nudos, multiplicidades y grado.
  Un test comprueba que el área de un lazo con splines coincide con la del
  sólido extruido.

## Ajuste de una curva a puntos

- **Parámetros:** por longitud de cuerda desde el extremo inicial al final.
- **Mínimos cuadrados** sobre los polos interiores, con los extremos fijos, y
  una penalización pequeña de las segundas diferencias de los polos (una
  placa delgada discreta) que evita oscilaciones cuando hay pocos puntos por
  polo.
- **Corrección de parámetros:** tres pasadas en las que cada punto toma el
  parámetro de su punto más cercano en la curva (proyección), como en Hoschek.
- **Número de polos:** desde 4, creciendo un 25 % cada vez, hasta que todos
  los puntos quedan dentro de la tolerancia o se llega al máximo (40).

## Contornos orgánicos en las secciones

Con `splines`, `fitLoop` sustituye por una sola spline cada tramo libre:
tres o más piezas seguidas, al menos dos de ellas arcos, unidas sin esquina
(menos de 25° de giro en la junta, medido en los puntos de la sección). Las
esquinas vivas, las rectas largas y los arcos aislados se quedan como están.
Un lazo que es todo él un tramo libre se parte en dos splines.

## Superficies como campo de alturas

Una superficie libre se describe vista desde un plano:
S(u, v) = origen + (x₀ + u·w)·x + (y₀ + v·h)·y + H(u, v)·n, con H una
B-spline bicúbica cerrada en los bordes.

- **Exacta en Open CASCADE:** en una B-spline cerrada y uniforme, la función
  lineal x(u) se representa exactamente con los polos en las abscisas de
  Greville (la media de los `p` nudos siguientes a cada polo). Los polos 3-D
  se colocan sobre esa rejilla a su altura, y el `Geom_BSplineSurface`
  resultante es la misma superficie, sin aproximación.
- **Ajuste:** los puntos se proyectan sobre el plano (por defecto, el de
  mejor ajuste de la zona, con la normal hacia fuera del escaneo); el
  rectángulo que cubren se amplía con un margen. Se resuelven por mínimos
  cuadrados las alturas de los polos, con una penalización de flexión
  (segundas diferencias en u y en v y torsión) proporcional al número de
  puntos. La matriz es densa y pequeña (como mucho 24 × 24 polos).
- **Límite:** la superficie no puede volver sobre sí misma vista desde su
  plano. Las formas que lo hacen (un álabe muy curvado, un mango) se hacen
  por secciones (loft) o, en una fase posterior, por parches.

## Operaciones

- **Loft:** `BRepOffsetAPI_ThruSections` sobre los lazos de los bocetos, en
  orden; suave o reglado.
- **Patrón circular:** copias giradas de la pieza que hizo otra operación,
  unidas, y combinadas con la pieza como ella.
- **Cortar por una superficie:** la cara de la superficie barrida lejos hacia
  el lado que se quita (`BRepPrimAPI_MakePrism`) y restada del sólido.
- **Engrosar:** `BRepOffsetAPI_MakeThickSolid::MakeThickSolidBySimple`, con el
  sólido resultante orientado hacia fuera (`BRepLib::OrientClosedSolid`).

## Volumen y área

`BRepGProp` integra con un número fijo de puntos de Gauss, y en caras
limitadas por B-splines con muchos nudos se equivoca hasta un 1 %. En sólidos
con curvas o superficies libres, el volumen y el área se calculan sobre una
teselación fina (desviación de una diezmilésima del tamaño de la pieza); en
los demás, con `BRepGProp`.

Referencias:

- L. Piegl, W. Tiller, *The NURBS Book*, 2.ª ed., Springer, 1997
  (algoritmos A2.1 y A2.2; capítulo 9, ajuste por mínimos cuadrados).
- J. Hoschek, *Intrinsic parametrization for approximation*, Computer Aided
  Geometric Design 5, 1988 (corrección de parámetros).
- T. N. E. Greville, *On the normalization of the B-splines and the location
  of the nodes for the case of unequally spaced knots*, 1967 (abscisas de
  Greville y precisión lineal).

# Cambios · Changelog

## 0.5.0 — 12 de noviembre de 2026 (edición a mano)

La IA hace la pieza; la persona retoca lo que no llegó o quiere cambiar, sin
salir del programa. Lo que se hace a mano pasa por los mismos comandos que usa
el agente, con su paso de deshacer, y el agente lo ve.

- **Vista 3D de las piezas:**
  - las aristas del sólido y sus bocetos (en azul; el elegido, en naranja;
    *Ver → Bocetos de las piezas*);
  - un clic elige una **cara** o una **arista** del sólido (Ctrl+clic añade o
    quita), resaltadas en ámbar, con su tipo y medida en la barra de estado;
  - **transparencia** por cuerpo (*Ver a través*; `body.set_opacity`);
  - los bordes de la teselación de una pieza ya no se marcan como abiertos.
- **Comparar con el escaneo:**
  - colorea el escaneo por su distancia a la pieza, con leyenda (verde
    dentro de la tolerancia; amarillo a rojo, falta material; cian a azul,
    sobra). No es un paso de deshacer ni se guarda; `body.clear_deviation`
    lo quita;
  - devuelve las **zonas** fuera de tolerancia: trozos continuos del
    escaneo con su centro, tamaño, porcentaje, distancia media y lado. En la
    leva habría señalado la cara alabeada al momento.
- **Panel *Modelo* y *Propiedades*:** cada parámetro, boceto y operación se
  elige; sus valores se editan en *Propiedades* (número o nombre de
  parámetro; modo con desplegable; casilla *Suprimida*), y el plano y el
  nombre de un boceto también. Menú contextual: editar boceto, suprimir,
  subir y bajar en el historial, borrar. La operación que falla, en rojo.
- **Editor de bocetos** (doble clic en un boceto): el boceto sobre la sección
  del escaneo en su plano, cada segmento coloreado por su desviación en los
  dos sentidos; arrastrar vértices, curvatura de arcos, centro y radio de
  círculos; recta ↔ arco, partir, borrar vértice o lazo; dibujar
  rectángulos, círculos y polígonos; valores exactos de lo elegido; ajuste a
  vértices y sección; deshacer propio. Aceptar es un solo paso de deshacer.
- **Boceto nuevo en una cara plana** y **redondear las aristas elegidas**,
  desde el clic derecho en la pieza.
- **Comandos nuevos o ampliados:** `part.move` (reordenar operaciones),
  `part.edit_sketch` (lazos, plano o nombre por separado), `body.set_opacity`,
  `body.clear_deviation`; `view.selection` da, en una pieza, el elemento del
  árbol y las caras y aristas elegidas con su tipo, medida y eje (y el punto
  medio de cada arista, para `part.fillet`).
- El núcleo da las aristas del sólido como polilíneas y la información de
  cada cara y arista (`Solid::faceInfo`, `Solid::edgeInfo`).

## 0.4.1 — 12 de noviembre de 2026

Correcciones tras pasar la leva escaneada a STEP con la 0.4.0:

- **Boceto desde una sección:**
  - un arco podía salir por el lado largo (337° donde había un tramo recto de
    4 mm): el sentido de cada arco sale ahora del giro de todos sus puntos, y
    un segmento que se aleja más de tres tolerancias de sus puntos se corrige
    o se cambia por una recta;
  - dos arcos seguidos dejaban la junta fuera del círculo de uno de ellos (hasta
    0,017 mm), y `part.edit_sketch` rechazaba después el boceto: el centro de
    cada arco se coloca en la mediatriz de su cuerda cuando las juntas ya son
    definitivas;
  - un taladro escaneado algo ovalado (0,1 mm) salía en arcos y rectas
    cortas: un lazo casi redondo (a menos de tres tolerancias de un círculo,
    con su área y sin rectas más largas que el radio) es un círculo.
- **Extruir un arco tras otro:** se rechazaba con «segment N does not join the
  previous one» si los centros venían redondeados. Cada arco se construye ahora
  por sus dos extremos y su punto medio, así que empieza exactamente donde
  acabó el anterior.
- Tests con la sección real de la leva.

## 0.4.0 — 12 de noviembre de 2026 (fase 3: sólido paramétrico)

- **Open CASCADE** para los sólidos exactos: los paquetes de Ubuntu en Linux;
  en Windows, una compilación propia y mínima de la 7.9.3 (solo modelado y
  STEP), hecha y guardada en caché por GitHub.
- **Piezas paramétricas** (menú *Pieza*; en el panel *Modelo*, con su
  historial debajo):
  - bocetos de rectas, arcos y círculos en cualquier plano; un lazo dentro de
    otro es un agujero;
  - extruir, cortar, intersecar, revolución y redondeos;
  - parámetros con nombre: al cambiarlos, la pieza se reconstruye. Un cambio
    que la rompe no se aplica y se dice por qué;
  - comparar con el escaneo: distancias con signo, mediana, percentil 95 y
    parte dentro de una tolerancia;
  - exportar e importar STEP; el proyecto (formato 5) guarda la pieza.
- **Boceto desde una sección:** corta el escaneo y convierte cada contorno
  en rectas y arcos dentro de una tolerancia (círculos enteros, esquinas vivas
  donde se cruzan rectas, arcos tangentes). En una placa de prueba escaneada:
  29,996 × 11,997 mm y taladros de Ø2,743 a 2,754 (reales 30 × 12 y 2,75);
  el sólido extruido deja el 100 % del escaneo dentro de ±0,05 mm.
- **Sección de una malla con un plano**, simplificada.
- **Ruido amplio** (corrección de la 0.3.1 tras probarla en la leva): en
  discos del 6 % del tamaño de la pieza que no se doblan por los cantos.
  En piezas finas la 0.3.1 medía la curvatura de los cantos (0,08 mm).
- Dos mitades de un taladro corto, con ejes hasta 10° distintos, se unen si
  encajan juntas.
- Los números que llegan como texto se aceptan en las entradas numéricas.

## 0.3.1 — 11 de noviembre de 2026

Correcciones tras la primera pieza real (una leva escaneada, 84 × 15 × 3,6 mm,
334 000 triángulos):

- **Ruido en mallas suavizadas por el escáner:** se medía a la escala de unas
  pocas caras, donde el software del escáner ya lo ha borrado (0,001 mm), y la
  pieza se rompía en cientos de formas. Ahora también se mide la planitud de
  las zonas más planas a un 3 % del tamaño de la pieza, y vale el mayor.
- **Taladros cortos y redondeos troceados:** las regiones vecinas cuya unión
  encaja en un cilindro, cono o esfera (y no en un plano) se unen en una sola
  forma. En una placa de prueba con cuatro taladros Ø2,75 de 2,5 mm de
  profundidad: de 101 formas a 17, y los taladros a ±0,01 mm.
- **Distancia entre ejes casi paralelos** (`features.measure`): ahora es la
  distancia entre centros, medida a media altura. Antes daba la distancia
  mínima entre las rectas, que en ejes casi paralelos no significa nada (2,23
  en vez de 4 mm). La mínima sigue en `shortestDistance`.
- **Ajustar aquí** (`features.fit`) se queda solo con las caras que están sobre
  la forma y miran como ella, en varias vueltas: los redondeos de la boca de
  un taladro y la cara de alrededor ya no agrandan el diámetro. Informa del
  área.
- `features.segment` lista como mucho `maxListed` formas (40 por defecto, de
  mayor a menor) e informa de `total` y `listed`: la respuesta ya no desborda
  al agente en piezas complejas.

## 0.3.0 — 11 de noviembre de 2026

- **Formas** (menú *Formas*, panel *Formas*):
  - segmentación automática en planos, esferas, cilindros, conos y toros, con su σ y el ruido del escáner estimado;
  - ajustar una forma alrededor de un punto, reajustar una región como otro tipo, medir ángulo y distancia entre formas, renombrar y borrar;
  - el visor colorea las regiones por tipo.
- **Alineación** (menú *Alinear*): ejes principales, 3-2-1 por datums (caras, ejes, centros), mejor ajuste ICP robusto con informe de desviación, y mover o girar. Las formas se mueven con la pieza.
- Proyecto en formato 4 (guarda las formas; lee los anteriores).
- **Selección con el ratón:**
  - un clic sobre la pieza marca el punto y elige la forma de debajo; Ctrl+clic añade más, en orden (primario, secundario, terciario);
  - las formas elegidas se resaltan en ámbar;
  - el botón derecho abre un menú para ajustar una forma ahí, reajustar, medir, alinear por datums, renombrar o borrar, con el formulario ya relleno.
- 42 herramientas MCP con la ventana abierta: `view_selection` (lo que la persona ha señalado) y `view_pick`; campos de punto [x, y, z] y de forma en los formularios.
- Documentación: `docs/algoritmos/primitivas.md`, manuales, guía de la IA.

## 0.2.0 — 6 de noviembre de 2026

- **Remallado en quads** (*Malla → Remallado en quads*, `mesh.remesh_quads`)
  con QuadriFlow: quads que siguen la forma y las aristas vivas, cerca del
  número pedido. QuadriFlow se compila sin Boost y sin llamadas a otros
  programas.
- **Exportar T-Spline** (`.tsm`, formato TS0200) para Autodesk Fusion
  (`mesh.export_tsm`), con comprobación de malla cerrada, manifold y sin
  vértices de valencia 2, y orientación hacia fuera.
- Las mallas guardan sus quads: OBJ y PLY los leen y escriben, el proyecto
  pasa al formato 2 (lee los de formato 1) y el visor dibuja las aristas de
  los quads.
- `openrevers-cli` reserva la salida estándar para el protocolo MCP y el JSON
  de `run`; lo que imprima una biblioteca va a la salida de errores.
- **3MF**: importar (unidad del archivo, transformaciones, componentes,
  varios objetos) y exportar.
- **E57**: lector propio de nubes de puntos E57 (todos los escaneos con su
  posición, cartesianas o esféricas, enteros escalados, normales), con las
  sumas de comprobación verificadas; probado con archivos de libE57Format.
- **Nubes de puntos**: importar XYZ, PTS, PLY y OBJ sin caras (con sus
  normales), aclarar, calcular y orientar normales, y reconstruir una malla
  cerrada con Poisson apantallado (PoissonRecon), con recorte por densidad
  para escaneos abiertos. Menú *Puntos*. Proyecto en formato 3.
- **Mallas grandes**: medido con 10 M de triángulos (ver
  `docs/desarrollo/rendimiento.md`). Mapa de aristas y unión de vértices más
  rápidos y con menos memoria; el diezmado limita la valencia; los bordes
  abiertos se calculan fuera del hilo de la interfaz.
- El instalador incluye las licencias de los componentes compilados.
- 29 herramientas MCP.

## 0.1.0 — 6 de noviembre de 2026

Primera versión: la fase 1, preparación de mallas.

- Aplicación de escritorio nativa (C++20, Qt 6):
  - visor 3D OpenGL con bordes abiertos marcados;
  - paneles de modelo, propiedades, historial y registro;
  - formularios de cada operación generados desde su esquema;
  - interfaz en español e inglés.
- Mallas:
  - importar y exportar STL, OBJ y PLY;
  - analizar;
  - reparar: unir vértices, caras degeneradas y duplicadas, piezas sueltas,
    orientación;
  - rellenar agujeros;
  - suavizar y diezmar;
  - remallar en modo uniforme o adaptativo, conservando las aristas vivas.
- Proyecto `.orvp` con todos los cuerpos y guardado seguro.
- Deshacer y rehacer de cada operación.
- Servidor MCP (`openrevers-cli mcp`) con 24 herramientas, conectado a la
  ventana o sin interfaz. Incluye carpetas permitidas para el agente y
  capturas de la vista.
- `openrevers-cli run` para encadenar operaciones desde la línea de comandos.
- Instalador de Windows (NSIS) y `.zip` portátil, generados por GitHub Actions.

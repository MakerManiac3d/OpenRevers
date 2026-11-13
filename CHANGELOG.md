# Cambios · Changelog

## 0.10.4 — 13 de noviembre de 2026

- Escáner: OpenRevers se cerraba al conectar con un modo que el escáner no
  admite (por ejemplo, láser azul con los valores por defecto). Se cerraba al
  destruir la sesión de la biblioteca justo después de crearla. Ahora:
  - la sesión y el contexto se mantienen abiertos hasta cerrar el programa;
    *Desconectar* solo detiene y suelta el escaneo, y el siguiente
    *Conectar* crea otro en la misma sesión;
  - al conectar se prueban las combinaciones de fusión, tipo de escaneo y
    líneas láser hasta que el escáner acepta una, conservando la luz, el
    modo, la precisión y el material elegidos. `scan.status` dice cuál se
    usa (`configuration`).

## 0.10.3 — 13 de noviembre de 2026

- Escáner: la vista previa fallaba (`data listener not found in preview`).
  La biblioteca necesita también un oyente de datos; ahora se registra, y
  las imágenes y fotogramas que envía se cuentan (`frames` en
  `scan.status`) y se le devuelven al momento. También un oyente del proceso
  de escaneo (`processMessage`: búfer lleno, escaneo fallido…).
- Los códigos de error y de mensaje de la biblioteca no son 1, 2, 3…: los
  errores van de 0x32620100 en adelante y los mensajes de dispositivo de
  0x12620100. Ahora OpenRevers da su nombre real (antes daba un nombre
  equivocado o «DEVICE_MESSAGE»).

## 0.10.2 — 13 de noviembre de 2026

- Escáner: OpenRevers no veía el Raptor aunque estuviera conectado. La
  biblioteca de CrealityScan solo llena su lista de escáneres cuando hay un
  oyente de mensajes de dispositivo; ahora se registra al abrir el contexto,
  y *Dispositivos* espera unos segundos a que aparezca el escáner.
  `scan.status` dice el último mensaje (conectado, desconectado, necesita
  calibración…).

## 0.10.1 — 13 de noviembre de 2026

- MCP: dos herramientas fijas, `command_list` y `command_run`, que llegan a
  cualquier comando, también a los que se añadan en versiones futuras. Así un
  cliente que guarda la lista de herramientas de una versión anterior (como
  un chat ya abierto) puede usar los comandos nuevos sin abrir otro chat.
- Instalador de Windows: antes de copiar los archivos cierra los servidores
  MCP de OpenRevers (`openrevers-cli.exe`) que dejan abiertos los clientes de
  IA, y si la aplicación está abierta pide cerrarla (Reintentar/Cancelar).
  Ya no hace falta terminarlos a mano para que la instalación no falle por
  DLL ocupadas. El desinstalador hace lo mismo.

## 0.10.0 — 12 de noviembre de 2026 (vista previa y tiradores)

- **Ver en vivo** en el panel de comandos: el resultado se calcula sobre una
  copia del documento mientras cambias los valores y se dibuja en ámbar, con
  el cuerpo original semitransparente. Las secciones se ven como contornos
  magenta. Nada cambia ni entra en el historial hasta *Aplicar*.
- **`dryRun`** para el agente y la línea de órdenes: cualquier comando que
  cambia el documento responde con lo que haría (`preview`: cuerpos
  cambiados, nuevos y quitados) sin tocarlo.
- **Tiradores y guías en la vista:** puntos del panel que se arrastran con el
  ratón (centro de esfera, esquinas de caja, origen de plano); la caja, la
  esfera y el plano se dibujan; *Recortar* muestra solo los de su forma.
- La rueda del ratón ya no cambia un valor del panel al desplazarlo; solo el
  campo con el foco.
- *Buscar un comando* ordena por relevancia: el nombre exacto va primero.
- Prueba nueva: la vista previa no cambia el documento ni el historial.

## 0.9.0 — 12 de noviembre de 2026 (control manual al nivel del agente, primer bloque)

- **Panel de comandos acoplado y no modal**, en lugar del formulario. Se gira
  la vista y se hace clic en la pieza con el panel abierto; *Aplicar* deja el
  panel abierto para repetir.
- **Un control para cada tipo de dato** (`x-kind` en el esquema):
  - desplegables de bocetos, operaciones, superficies, formas y parámetros de
    la pieza;
  - lista ordenable de bocetos para el loft;
  - casillas de cuerpos;
  - puntos, direcciones, cuerpos y planos tomados con un clic en la vista
    (⌖, *Desde una cara de la vista*);
  - listas de puntos por clics;
  - la selección del lazo;
  - el editor de bocetos para los lazos.
  Ya no hay ningún campo que pida JSON.
- **Prueba de paridad** en el CI: falla si un campo de cualquier comando no
  tiene control, o si una referencia se pide como texto libre.
- **Buscar un comando** (Ctrl+K), en español o en inglés.
- **Panel de resultados:** cada comando, del usuario o del agente, con su
  entrada y su respuesta completas.
- Nuevo boceto en el editor sobre cualquier plano dado desde el panel.

## 0.8.0 — 12 de noviembre de 2026 (limpiar y alinear escaneos)

- **Quitar la mesa** (`mesh.remove_plane`): el plano con más puntos (RANSAC
  afinado por mínimos cuadrados), o uno dado; quita su franja y, si se pide,
  lo que quede debajo. Para mallas y nubes.
- **Quitar puntos sueltos** (`mesh.remove_outliers`): eliminación estadística
  de atípicos por la distancia media a los vecinos.
- **Recortar** (`mesh.crop`) por esfera, caja o semiespacio.
- **Lazo en la vista** (*Malla → Seleccionar con lazo*, tecla L): rodea lo que
  quieras quitar, que queda en rojo (solo las caras que miran hacia ti), y
  **Supr** lo borra (`mesh.delete_faces`).
- **Alineación automática** (`align.global`): descriptores FPFH emparejados
  con RANSAC y después el ICP. No necesita posición de partida. Una escuadra
  vista solo en parte, girada 130° y desplazada 150 mm, vuelve a su sitio
  con menos de 0,2 mm de error.
- **Alinear por pares de puntos** (`align.point_pairs` y su ventana): tres
  pares o más con marcas numeradas en la vista, por mínimos cuadrados (Kabsch)
  y después el ICP. Da la distancia que queda en cada par.
- **Unir escaneos** (`mesh.merge`), como malla o como nube para
  reconstruir.
- **ICP:** la distancia de corte ya no baja de dos aristas medias de la
  referencia. Cuando dos escaneos coincidían casi del todo, el solape salía
  bajísimo (5 %) porque las parejas se buscan de vértice a vértice.

## 0.7.1 — 12 de noviembre de 2026

Correcciones de la prueba de la 0.6.0 con la caja de aire de un kart:

- **Splines de una sección dentro de la tolerancia:** antes se quedaban en 40
  polos aunque no llegaran (0,46–0,93 mm pedida a 0,1). Ahora usan hasta 200,
  según la longitud del tramo, y un tramo que aún no llega se divide en dos.
- **Loft hueco:** cada sección puede tener un contorno y los mismos huecos;
  el loft es el del contorno menos el de los huecos (una pared). En
  *Boceto desde una sección*, `keep` (todos, el exterior o los interiores) y
  `keepLoops` (por índice) eligen los lazos; cada lazo devuelve su índice,
  del mayor al menor.
- **Números como texto:** `"-4.5"` o `"-4,5"` son números, no nombres de
  parámetro; `"-pared"` es menos el parámetro `pared`.
- **Cortar por una superficie** comprueba que la superficie cubra toda la
  pieza vista desde su plano, y si no se niega diciendo cuánto le falta (antes
  cortaba dentro de su huella y dejaba restos sin avisar).
- **Signo de la comparación junto a aristas y vértices:** se usa la normal
  conjunta de las caras igual de cercanas, no la de una sola (daba «sobra
  material» de −15 mm dentro de un hueco).
- **Escala del mapa de desviación:** por defecto, cinco veces la tolerancia,
  para que se vea la banda de ±tolerancia; `range` la fija.
- **Elegir en la vista:** sobre una cara, una arista solo gana si el clic
  cae a 3 píxeles o menos de ella (antes, 6).
- **`part_list` con `sketch`** devuelve la geometría completa de un boceto
  (con los polos de las splines), en la forma que aceptan `part_sketch` y
  `part_edit_sketch`.

## 0.7.0 — 12 de noviembre de 2026 (fase 6: escaneo directo, primer bloque)

- **Menú Escanear y comandos `scan.*`:** conectar un escáner de CrealityScan
  (Raptor, Otter, Ferret…), vista previa, escanear, pausar, terminar y pasar
  el escaneo a un cuerpo (puntos con normales, o la malla del escáner). Los
  mismos comandos para la interfaz, los scripts y el agente de IA.
- **Vista en vivo:** mientras se escanea, los puntos aparecen en ámbar en la
  vista. No entran en el historial hasta que se pasan a un cuerpo.
- **Sin código de terceros:** OpenRevers carga en tiempo de ejecución
  `lib_orbbec_scan.dll` desde la instalación de CrealityScan del usuario; no
  la distribuye. Las declaraciones de la interfaz son propias, sacadas de los
  metadatos de CrealityScan. Solo se resuelven 22 funciones del ciclo de
  escaneo; nunca las de firmware, wifi, registro o IA (lo comprueba una
  prueba).
- **Licencia:** permiso adicional de la sección 7 de la GPL para cargar esas
  bibliotecas ([LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md)).
- **Simulador de escaneo** (`ORV_SCAN_SIMULATOR=1` o *Usar el simulador de
  escaneo*) para probar sin escáner.
- Pendiente: la prueba con el Raptor, que confirmará valores de
  enumeraciones, unidades y el registro del dispositivo
  ([escaneo-obscan.md](docs/desarrollo/escaneo-obscan.md)).

## 0.6.1 — 12 de noviembre de 2026

- El mapa de desviación y los colores de las formas se ven también desde el
  otro lado de la superficie (un escaneo de una sola cara visto por detrás
  salía en un tono rojizo fijo).
- Compila en Windows: `far` y `near` son macros allí.

## 0.6.0 — 12 de noviembre de 2026 (fase 4: superficies libres, primer bloque)

- **Splines en los bocetos:** B-splines cúbicas cerradas en sus extremos
  (`{type: spline, poles}`), evaluadas por el programa y construidas igual en
  Open CASCADE. En el editor, *A spline* convierte una recta o un arco, y sus
  polos se arrastran o se escriben.
- **Boceto desde una sección con `splines`:** los tramos libres de un
  contorno orgánico (tres o más piezas unidas sin esquina, dos arcos al
  menos) salen como una spline cada uno, dentro de la tolerancia. Un perfil
  alar de 40 mm pasa de decenas de arcos a cuatro segmentos.
- **Loft por secciones** (`part.loft`), suave o reglado.
- **Patrón circular** (`part.pattern`): copias de lo que hizo otra
  operación alrededor de un eje; el número y el ángulo pueden ser parámetros.
- **Superficies libres** (`part.surface_from_scan`): B-spline bicúbica
  ajustada a una forma segmentada o a una esfera de puntos del escaneo, vista
  desde su plano de mejor ajuste; se guarda en la pieza y se dibuja como una
  rejilla. `part.cut_by_surface` quita lo que queda por encima o por debajo
  de ella (un bloque toma la cara curva del escaneo: 0,2 % de error en el
  volumen de prueba) y `part.thicken` la engruesa hasta un sólido.
- **Volumen y área exactos con formas libres:** la integración de Open
  CASCADE se equivoca hasta un 1 % en caras limitadas por B-splines con
  muchos nudos; en esos sólidos se calculan sobre una teselación fina.
- Una turbina de prueba hecha por comandos: cubo por revolución, álabe por
  loft de tres secciones alares retorcidas y patrón de seis.
- Documentación de los algoritmos en `docs/algoritmos/superficies.md`.

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

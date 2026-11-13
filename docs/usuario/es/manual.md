# Manual de OpenRevers (versión 0.9)

OpenRevers prepara mallas escaneadas para la ingeniería inversa. Esta versión
cubre la **fase 1**: abrir el escaneo, ver qué le pasa, repararlo y dejarlo
listo para modelar o para imprimir.

## La ventana

| Zona | Para qué |
| --- | --- |
| Barra de herramientas | Las acciones más usadas: guardar, importar, exportar, deshacer, y las operaciones de malla |
| **Modelo** (izquierda) | Los cuerpos del proyecto. La casilla los muestra u oculta; doble clic para renombrar |
| **Propiedades** | Datos del cuerpo seleccionado: triángulos, vértices, tamaño, área, archivo de origen |
| Vista 3D (centro) | La pieza. Los **bordes abiertos** se marcan en rojo; las aristas con tres o más caras, en magenta |
| **Historial** (derecha) | Cada operación hecha; en gris, las deshechas que se pueden rehacer |
| **Registro** (abajo) | Qué ha hecho cada operación, tuya o del agente de IA, con sus números |
| Barra de estado | La operación en curso y el estado del agente de IA |

Los paneles se mueven, se cierran y se recuperan en el menú **Ver**.

### Ratón y teclado

| Acción | Cómo |
| --- | --- |
| Girar | Botón izquierdo y arrastrar |
| Desplazar | Botón central o derecho y arrastrar (o Mayús + izquierdo) |
| Acercar y alejar | Rueda |
| Encuadrar todo | Doble clic, o tecla **F** |
| Vistas estándar | Menú **Ver**: isométrica, frontal, posterior, izquierda, derecha, superior, inferior |
| Deshacer / rehacer | Ctrl+Z / Ctrl+Y |

El eje Z es el vertical, como en la mayoría de programas de CAD.

## Hablar con el agente sin salir de OpenRevers (0.11.2)

*Agente IA → Hablar con el agente* (Ctrl+J) abre un chat junto al panel de
comandos. Escribe lo que quieres, o díctalo con **Win+H** (el dictado de
Windows). El agente trabaja sobre el proyecto abierto y ves cada cambio.

- Usa **Claude Code** instalado en tu equipo, con **tu cuenta de Claude**.
  La primera vez, abre una terminal, ejecuta `claude` e inicia sesión.
  OpenRevers no ve ni guarda tus credenciales.
- Solo puede usar las herramientas de OpenRevers: ni consola ni archivos.
- Con *Incluir la selección* marcado, el mensaje dice qué tienes elegido,
  así que «redondea estas aristas» se refiere a las que has elegido.
- *Detener* corta lo que está haciendo; el siguiente mensaje sigue la misma
  conversación. *Nueva conversación* empieza de cero.
- No se conecta a nada hasta que envías el primer mensaje.

## Señalar al agente lo que quieres (0.11.2)

Abajo a la derecha se ve siempre qué está elegido: un cuerpo, caras o
aristas de una pieza, formas, o la zona de una malla marcada con el lazo.
**Guardar selección** le pone un nombre (`sel1`, `sel2`…). Luego puedes
pedirle al agente «redondea a 2 mm las aristas de sel1» o «borra sel2»: el
agente lee exactamente esa selección.

## Línea de tiempo y cubo de vista (0.11.1)

- **Línea de tiempo** (abajo): los pasos de la pieza en orden. Clic para
  elegir, doble clic para editar, botón derecho para suprimir, volver a
  incluir o borrar.
- **Cubo de vista** (arriba a la derecha de la vista): clic en una cara para
  mirar desde ese lado; junto al cubo, vista isométrica.

## La cinta de pestañas (0.11)

Bajo la barra rápida (guardar, importar, exportar, deshacer, rehacer) está la
**cinta**, como en Fusion: una pestaña por tarea (*Sólido*, *Boceto*,
*Superficie*, *Malla*, *Alinear*, *Inspeccionar*, *Agente IA*) y, en cada
una, grupos de herramientas. Cada botón hace lo mismo que su entrada de menú
y abre el mismo panel. Si una pestaña no cabe en la ventana, se desplaza de
lado. Ctrl+K sigue buscando cualquier comando por su nombre.

## Trabajar a mano: panel de comandos, paleta y resultados

Todo lo que hace el agente de IA lo puedes hacer tú con los mismos comandos y
los mismos datos, sin escribir JSON:

- **Panel de comandos** (a la derecha). Se abre desde cualquier menú o desde
  la paleta y no bloquea la ventana: puedes girar la vista y hacer clic en la
  pieza mientras está abierto. *Aplicar* ejecuta el comando y deja el panel
  abierto para repetir con otros valores.
- **Tomar datos de la vista:** cada campo que nombra un sitio tiene su botón:
  - **⌖** junto a un punto toma el punto pulsado;
  - junto a una dirección, la normal de la cara pulsada;
  - junto a un cuerpo, el cuerpo pulsado;
  - **Desde una cara de la vista** da un plano;
  - **Añadir clics** va sumando puntos en orden (redondear aristas, pares de
    puntos);
  - **Tomar la selección del lazo** pasa las caras seleccionadas.
- **Listas en lugar de referencias:**
  - los bocetos, operaciones, superficies, formas y parámetros salen en
    desplegables de la pieza elegida;
  - los bocetos de un loft se marcan y se arrastran para ordenarlos;
  - los cuerpos de *Unir escaneos* se marcan;
  - los lazos de un boceto se dibujan con **Dibujar en el editor de
    bocetos…**.
- **Ver en vivo** (casilla del panel): mientras cambias los valores, el
  resultado se muestra en ámbar sobre el cuerpo original, que se vuelve
  semitransparente; una sección se dibuja en magenta. No cambia nada ni entra
  en el historial hasta *Aplicar*.
- **Tiradores en la vista:** los puntos del panel (centro de una esfera,
  esquinas de una caja, origen de un plano) aparecen como puntos naranjas que
  se arrastran con el ratón; la caja, la esfera y el plano se dibujan como
  guías.
- **Buscar un comando** (Ctrl+K): escribe lo que quieres hacer, en español o
  en inglés («sección», «rellenar», «alinear»), y Intro abre el panel.
- **Resultados** (pestaña junto al registro): cada comando, tuyo o del
  agente, con su entrada y su respuesta completas (desviaciones, solapes,
  residuos…), para consultarlos después.

## Flujo de trabajo recomendado

1. **Importar.** *Archivo → Importar malla* (Ctrl+I) o arrastra el archivo a la
   ventana. Formatos: STL (binario y texto), OBJ y PLY, en milímetros; nubes
   de puntos E57 (todos los escaneos, colocados según su posición y pasados de
   metros a milímetros), XYZ y PTS; 3MF,
   convertido desde la unidad que declara, con todos los objetos de su
   construcción.
2. **Analizar.** *Malla → Analizar malla*. El informe da:
   - **agujeros** y su longitud. Un escaneo de una sola cara tiene un borde
     exterior largo; los lazos pequeños son agujeros;
   - **aristas no manifold**: aristas compartidas por tres o más caras. Hay
     que repararlas antes que nada;
   - **conflictos de orientación**: caras orientadas al revés que sus vecinas;
   - **piezas**: si hay más de una, suelen ser restos del escáner;
   - **longitud media de arista**: la resolución del escaneo.
3. **Reparar.** *Malla → Reparar malla* hace en un paso:
   - une los vértices coincidentes (o los más cercanos que la tolerancia);
   - quita las caras degeneradas y duplicadas;
   - elimina las piezas sueltas pequeñas;
   - orienta todas las caras de forma coherente y hacia fuera.
4. **Rellenar agujeros.** *Malla → Rellenar agujeros* cierra los agujeros
   cuyo borde tiene como mucho *maxEdges* aristas, con un parche suave que se
   funde con la superficie. Ajusta *maxEdges* para no cerrar el borde exterior
   de un escaneo abierto.
5. **Mejorar la malla**, según para qué la quieras:
   - **Suavizar**: reduce el ruido, pero también redondea las aristas vivas.
     Usa poca intensidad.
   - **Remallar**: triángulos regulares. *uniform* usa una longitud de arista;
     *adaptive* usa triángulos más finos donde la superficie se curva. Las
     aristas más vivas que *featureAngle* (30° por defecto) se conservan.
   - **Diezmar**: menos triángulos para una malla más ligera, con *maxError*
     como límite de desviación.
   - **Remallado en quads**: reconstruye la superficie con quads que siguen
     la forma y las aristas vivas, para modelar con superficies de
     subdivisión o T-Splines. *targetFaces* es el número aproximado de quads.
     Con *Ver → Aristas de la malla* se ven los quads. Cualquier operación
     posterior sobre la malla (reparar, suavizar, remallar…) la devuelve a
     triángulos.
6. **Guardar o exportar.** *Archivo → Guardar proyecto* (`.orvp`, con todos
   los cuerpos), o *Exportar malla* a STL, OBJ, PLY o 3MF. Una malla de quads sale
   en quads en OBJ y PLY.
7. **Pasar a Fusion como T-Spline** (opcional). Con una malla de quads
   **cerrada**, *Archivo → Exportar T-Spline para Fusion (.tsm)*. En Fusion, el
   archivo se abre como cuerpo de Forma desde un script
   (`TSplineBodies.addByTSMFile`). Si la malla está abierta, rellena los
   agujeros antes del remallado en quads: el programa te dice qué impide la
   exportación.

### Si partes de una nube de puntos

Un archivo E57, XYZ o PTS, o un PLY u OBJ sin caras, entra como **nube de puntos**
(se dibuja con puntos). Las operaciones de malla no se aplican a una nube;
primero hay que reconstruir la superficie, desde el menú *Puntos*:

1. **Aclarar puntos** (opcional): un punto por cubo de lado *spacing*. Una
   nube uniforme y más ligera reconstruye mejor y más rápido.
2. **Calcular normales**: cada punto recibe la normal del plano de sus
   vecinos. *propagate* orienta toda la nube de forma coherente (escaneos
   abiertos o cerrados); *outward*, hacia fuera del centro (piezas cerradas
   y más o menos convexas); *keep*, conserva el lado de las normales que ya
   traía el escáner.
3. **Malla desde puntos (Poisson)**: crea un cuerpo nuevo con una malla
   cerrada; la nube se conserva. *depth* fija el detalle (8 suele bastar;
   cada nivel más multiplica los triángulos por cuatro). En un escaneo
   abierto, Poisson cierra lo que no se escaneó: *trim* (0,05–0,1) lo quita.

Después, la malla sigue el flujo normal desde el paso 2.

Prueba una operación sobre una copia con *Cuerpo → Duplicar cuerpo*, y compara.

## Formas y alineación

Para piezas mecánicas (caras planas, taladros, ejes, conos):

1. **Formas → Detectar formas.** La malla se divide en regiones y a cada una
   se le ajusta un plano, esfera, cilindro, cono o toro. El visor las pinta
   por colores:
   - azul: planos;
   - verde: cilindros;
   - naranja: conos;
   - violeta: esferas;
   - amarillo: toros;
   - gris: lo que no es ninguna forma (redondeos, zonas libres).

   El panel *Formas* lista cada una con su medida (Ø, ángulo, área) y su
   **σ**, la desviación típica de la malla respecto a la forma. Una σ
   parecida al ruido del escáner, que también se muestra, indica que la
   forma es real.
   - *noise*: el ruido del escáner, si lo conoces (0 para estimarlo).
   - *tolerance*: cuántas veces el ruido puede separarse una cara de la
     forma y seguir dentro.
   - *angle*: desviación máxima de la normal.
2. **Formas → Ajustar una forma aquí**, para un taladro o una cara que no
   salió. Indica un punto de su superficie y un radio.
3. **Formas → Reajustar forma como…**, si una región salió con otro tipo (un
   cilindro corto tomado por plano, por ejemplo).
4. **Formas → Medir entre formas**: ángulo y distancia entre dos formas, por
   ejemplo:
   - el grosor entre dos caras paralelas;
   - la distancia entre dos taladros;
   - la distancia de un eje a una cara;
   - el punto donde un eje corta a una cara.
5. **Alinear** coloca la pieza en el origen:
   - **Alinear por datums (3-2-1):** como en una máquina de medir.
     - **Primario:** una cara grande o un eje. La pieza se apoya en esa cara y
       Z entra en ella.
     - **Secundario:** una cara lateral o un eje; fija X.
     - **Terciario:** otra cara o un taladro; fija el origen que falta.

     Con una cara y dos taladros, el origen queda en el primer taladro y X
     va hacia el segundo.
   - **Alinear a los ejes principales:** rápido; centra la pieza y pone su
     lado mayor en X.
   - **Alineación de mejor ajuste:** lleva un escaneo sobre otro cuerpo (otro
     escaneo o la malla nominal) y da la desviación que queda. Si están lejos
     o girados, activa *coarse*.
   - **Mover o girar:** a mano.

   Las formas se mueven con la pieza.

**Con el ratón:**
- Un **clic** sobre la pieza marca el punto (una cruz naranja) y elige la
  forma de debajo, que se resalta en ámbar. La barra de estado muestra las
  coordenadas, la forma y su σ.
- **Ctrl+clic** añade otra forma. El orden cuenta: en *Alinear por datums*,
  la primera elegida es el primario, la segunda el secundario y la tercera el
  terciario.
- Un clic en el fondo lo deselecciona todo.
- El **botón derecho**, sin arrastrar, abre un menú con lo que se puede hacer
  ahí: ajustar una forma en ese punto, reajustar, medir entre las dos formas
  elegidas, alinear, renombrar o borrar. Los formularios salen rellenos con
  el punto y las formas elegidas.
- Arrastrar sigue igual: izquierdo para girar, derecho o central para
  desplazar, y la rueda para acercar.
- El agente de IA puede leer lo que has señalado, así que puedes pedirle
  cosas como «mide la distancia entre estas dos caras».

## Pieza paramétrica (fase 3)

Una **pieza** es un sólido exacto con su historial, que se exporta a STEP para
Fusion, FreeCAD o SolidWorks. En el panel *Modelo* aparece con sus
parámetros, bocetos y operaciones debajo. La IA puede hacerla entera, y tú
puedes retocar cualquier parte sin salir del programa.

1. **Pieza → Nueva pieza.**
2. **Pieza → Sección con un plano**, sobre el escaneo ya alineado: da el
   contorno de la pieza a esa altura, en coordenadas del plano. Es la base
   del boceto.
3. **Pieza → Boceto desde una sección:** corta el escaneo y convierte cada
   contorno cerrado en rectas y arcos (tolerancia 0,05 mm por defecto); los
   taladros salen como círculos, las esquinas vivas donde se cruzan rectas y
   los arcos tangentes a las rectas que tocan. Da la medida de cada segmento
   para compararla con el calibre. Un lazo dentro de otro es un agujero.
4. **Pieza → Extruir** (unir, cortar, intersecar), **Revolución** y
   **Redondear aristas**. Los valores pueden ser números o nombres de
   **parámetros** (*Pieza → Fijar un parámetro*): al cambiar un parámetro, la
   pieza se reconstruye. Si un cambio la rompe, no se aplica y se dice por
   qué.
5. **Pieza → Comparar con el escaneo:** distancias del escaneo al sólido,
   mediana, percentil 95, qué parte queda dentro de una tolerancia (±0,2 mm
   por defecto) y **las zonas fuera de ella** (dónde están, cuánto escaneo
   ocupan y hacia qué lado se desvían). El escaneo se **colorea** por su
   distancia a la pieza, con la leyenda a la derecha: verde dentro de la
   tolerancia, de amarillo a rojo donde al modelo le falta material y de
   cian a azul donde le sobra. *Cuerpo → Quitar colores de desviación* lo
   devuelve a su color.
6. **Archivo → Exportar STEP.**

Los triángulos de una pieza solo la muestran: las herramientas de malla no
la cambian.

### Formas libres (fase 4)

- **Contornos orgánicos:** en *Boceto desde una sección*, marca *splines*: los
  tramos suaves del contorno (que sin ella salen como muchos arcos cortos)
  pasan a ser splines dentro de la tolerancia. Las esquinas vivas, las rectas
  largas y los arcos claros se mantienen.
- **Loft por secciones** (*Pieza → Loft por secciones*): un sólido que pasa
  por varios bocetos en orden. Para un álabe, dibuja o saca del escaneo su
  sección a varios radios (cada una, un lazo cerrado que empieza en el borde
  de ataque, y los planos mirando hacia el mismo lado). *Reglado* lo hace
  plano entre secciones.
- **Patrón circular** (*Pieza → Patrón circular*): copias de lo que hizo otra
  operación (un álabe, un taladro) alrededor de un eje. El número de copias y
  el ángulo pueden ser parámetros.
- **Superficie desde el escaneo** (*Pieza → Superficie desde el escaneo*):
  una superficie libre y suave ajustada a una zona del escaneo, una forma
  segmentada o los puntos alrededor de un punto. Da su rms y su desviación
  máxima. En la vista se ve como una rejilla violeta; en el panel *Modelo*,
  con sus datos. Su menú contextual permite:
  - **Cortar la pieza por ella** (dejando lo de debajo o lo de encima):
    extruye un bloque que sobrepase la cara curva, y el corte le da la forma
    del escaneo;
  - **Engrosar** hasta un sólido: una carcasa o un panel.
- **En el editor de bocetos,** *A spline* convierte una recta o un arco en
  una spline, que se dobla arrastrando sus polos (los círculos unidos por una
  línea discontinua).

Una superficie se ve desde un plano: si vista desde él vuelve sobre sí misma,
hay que hacerla por secciones y loft.

### Retocar la pieza a mano

- **En la vista 3D**, la pieza muestra sus aristas y sus bocetos (en azul; el
  elegido, en naranja; *Ver → Bocetos de las piezas* los oculta). Un clic
  sobre la pieza elige la **cara** que hay debajo, o la **arista** si el clic
  cae a pocos píxeles de una; Ctrl+clic añade o quita. La barra de estado da
  su tipo y medida (plano y área, cilindro y diámetro, recta y longitud…).
- **Clic derecho sobre la pieza:** *Boceto nuevo en esta cara* (en una cara
  plana), *Redondear las aristas elegidas*, *Comparar con un escaneo* y *Ver a
  través* (la pieza semitransparente, para ver el escaneo dentro; también en
  el escaneo).
- **En el panel *Modelo*,** elige un parámetro, un boceto o una operación:
  sus valores aparecen en *Propiedades* y se cambian con doble clic (un
  número o el nombre de un parámetro); el modo, con su desplegable, y
  *Suprimida*, con su casilla. El clic derecho sobre una operación permite
  editar su boceto, suprimirla, **subirla o bajarla** en el historial y
  borrarla. Una operación que falla sale en rojo.
- **Editor de bocetos** (doble clic en un boceto, o *Pieza → Editar el
  boceto elegido*): el boceto sobre la **sección del escaneo** en su plano
  (línea de puntos gris). Cada segmento se colorea según cuánto se separa de
  la sección: verde dentro de la tolerancia, naranja hasta cuatro veces y
  rojo más.
  - *Seleccionar:* arrastra vértices, el rombo del medio de un arco (cambia
    su curvatura), el centro de un círculo o el rombo de su radio. Doble clic
    en una recta la convierte en arco, y en un arco, en recta.
  - *Rectángulo* (dos esquinas), *Círculo* (centro y un punto) y *Polígono*
    (clic en cada esquina; doble clic o Intro lo cierra) dibujan lazos nuevos.
  - *Recta ↔ arco*, *Partir*, *Borrar vértice* (Supr), *Borrar lazo*,
    *Deshacer*, *Rehacer* y *Encajar* (F).
  - A la derecha, los **valores** de lo elegido (vértice X/Y, longitud de una
    recta, radio de un arco, centro y diámetro de un círculo) para escribir
    la medida exacta del calibre.
  - Se ajusta a los vértices y a la sección mientras arrastras (casilla
    *Ajustar…*). Rueda para acercar; botón central o derecho para desplazar.
  - **Restricciones y cotas** (segunda barra): elige lo que restringir
    (Mayús o Ctrl+clic elige varias cosas: vértices, rectas, arcos, círculos)
    y pulsa *Coincidente*, *Horizontal*, *Vertical*, *Paralela*,
    *Perpendicular*, *Tangente*, *Igual*, *Fijar*, *Sobre objeto*, *Punto
    medio* o *Simétrico*; o una cota: *Distancia* (la longitud de una recta, o
    entre dos puntos, o de un punto a una recta), *Distancia horizontal* y
    *vertical*, *Radio*, *Diámetro* o *Ángulo*. La cota propone la medida
    actual; escribe la del calibre, o el **nombre de un parámetro** de la pieza
    para que la mande (al cambiar el parámetro, el boceto se vuelve a
    resolver).
  - Cada restricción se ve como una etiqueta junto a lo que sujeta (H, V, ∥,
    ⊥, =, ⚓… y las cotas con su valor). Un clic en la etiqueta la elige
    (se resalta lo que sujeta); doble clic en una cota cambia su valor; Supr
    la quita. La lista de la derecha muestra todas, y los **grados de
    libertad** que quedan (0: totalmente restringido). Las que se contradicen
    no se añaden, y se dice con cuáles chocan.
  - Al arrastrar, el boceto se **resuelve en directo**: el punto sigue al
    ratón hasta donde lo dejan las restricciones.
  - Los rectángulos, y las rectas de un polígono casi horizontales o
    verticales, se quedan **horizontales y verticales** al dibujarlos (casilla
    en el panel). Partir o borrar segmentos quita solo las restricciones que
    ya no tienen sentido.
  - *Aceptar* cambia el boceto y reconstruye la pieza en un solo paso de
    deshacer. No se puede aceptar un boceto que no cierra o cuyas
    restricciones no se cumplen.

Todo lo que haces a mano lo ve el agente de IA (`view_selection` le dice qué
parámetro, boceto u operación, qué caras y qué aristas has elegido), así que
puedes señalar algo y pedirle que siga desde ahí.

## Limpiar y alinear escaneos

**Limpiar** (menú *Malla*; sirve para mallas y nubes de puntos):

- **Quitar la mesa:** encuentra el plano con más puntos (la mesa o la base
  giratoria) y quita la franja de `distance` a cada lado y, con *beyond*,
  todo lo que quede debajo. También se le puede dar el plano (un punto y su
  normal hacia la pieza).
- **Quitar puntos sueltos:** quita los puntos lejos de sus vecinos (ruido,
  reflejos). Con un *ratio* menor quita más.
- **Recortar:** conserva lo que queda dentro (o fuera) de una esfera, una caja
  o un semiespacio.
- **Seleccionar con lazo** (tecla L): arrastrando con el botón izquierdo
  rodeas lo que quieres quitar y queda en rojo.
  - Ctrl añade a la selección; Alt+arrastrar gira la vista.
  - Solo se toman las caras que miran hacia ti, así que lo de detrás no se
    toca.
  - **Supr** lo borra (y se puede deshacer).
- **Quitar piezas sueltas** (ya existía) para las islas que quedan después.

**Alinear varios escaneos** (menú *Alinear*):

- **Alineación automática:** coloca un escaneo sobre otro solo por su forma,
  aunque estén lejos y girados. Necesitan solaparse (un tercio o más). Da el
  solape y la desviación que queda.
- **Alinear por pares de puntos:** se abre una ventana al lado de la vista.
  1. Haz clic en el mismo detalle (una esquina, un taladro, una marca) en el
     escaneo que se mueve (marcas naranjas) y en el de referencia (marcas
     cian), en el mismo orden.
  2. Con tres pares o más, *Alinear*. Basta con hacer clic cerca: el mejor
     ajuste lo termina.
- **Alineación de mejor ajuste** (ya existía) para afinar dos escaneos que
  ya están cerca.
- **Unir escaneos:** junta escaneos alineados en un cuerpo nuevo y oculta los
  originales. Con *asPoints*, como nube de puntos para reconstruir una sola
  malla con *Puntos → Malla desde puntos*.

## Escanear desde OpenRevers (fase 6, experimental)

> **Oculto por defecto desde la 0.10.5.** Con el Raptor conecta y da vista
> previa, pero todavía no registra el escaneo y puede cerrar el programa. Lo
> recomendado es escanear en CrealityScan e importar el archivo (OBJ, STL o
> PLY). Para probarlo: *Ajustes → Experimental → Escaneo directo*, y
> reiniciar OpenRevers.

Con **CrealityScan instalado** (Windows) y el escáner conectado por USB
(Raptor, Otter, Ferret…), el menú **Escanear** maneja el escáner sin salir
de OpenRevers:

1. **Estado del escáner:** dice si se ha encontrado la biblioteca de escaneo
   de CrealityScan. OpenRevers no la incluye; usa la de tu instalación
   (`C:\Program Files\CrealityScan\…\Plugins\x86_64`, o la carpeta que
   indique la variable `ORV_CREALITYSCAN_DIR`).
2. **Conectar escáner:** elige qué escaneas (objeto pequeño, mediano, grande,
   cara o persona), la precisión, la superficie (normal, negra, blanca,
   reflectante) y la luz (infrarroja o láser azul).
3. **Vista previa**, para apuntar, y **Empezar a escanear**. Los puntos
   aparecen en ámbar en la vista según llegan. **Pausar el escaneo** deja de
   sumar puntos y **Empezar a escanear** continúa.
4. **Terminar el escaneo** y **Escaneo a cuerpo:** los puntos (con normales)
   o la malla que calcula el escáner pasan al proyecto como un cuerpo nuevo,
   en milímetros. Desde ahí se trabaja como con cualquier escaneo importado.
5. **Desconectar escáner** al acabar.

Antes de abrir el escáner, cierra CrealityScan: solo un programa puede
usarlo a la vez. Para probar sin escáner, **Usar el simulador de escaneo**
da una placa de prueba.

Es una función nueva: hasta probarla con un escáner real, algunos detalles
(el nombre del dispositivo, las unidades) pueden salir mal. Si algo falla,
el registro dice qué devolvió la biblioteca.

## Ajustes

*Archivo → Ajustes*:

- **Idioma**: el del sistema, español o inglés. Se aplica al volver a abrir el
  programa.
- **Agente IA**: permite o impide que se conecte un agente. También fija el
  puerto local y las carpetas donde el agente puede leer y escribir. Si no
  indicas ninguna, puede usar tu carpeta personal.

## Agente de IA

Consulta [docs/mcp/README.md](../../mcp/README.md). En resumen:

1. *Agente IA → Copiar configuración para Claude Desktop*.
2. Pégala en la configuración de Claude Desktop y reinícialo.
3. Pídele lo que necesites.

El agente usa los mismos comandos que tú, cada paso aparece en el registro y en
el historial, y puedes deshacerlo.

## Línea de comandos

`openrevers-cli` está junto al programa:

```text
openrevers-cli run mesh.import '{"path":"pieza.stl"}' mesh.repair '{"body":"b1"}' mesh.fill_holes '{"body":"b1"}' mesh.export '{"body":"b1","path":"limpia.stl"}'
openrevers-cli list        # todos los comandos con sus parámetros
openrevers-cli mcp         # servidor MCP para un agente
```

## Problemas frecuentes

| Problema | Solución |
| --- | --- |
| «The file has no points or triangles» | El archivo está vacío o no es del formato que dice su extensión |
| El relleno deja agujeros abiertos | Son más grandes que *maxEdges*; súbelo, o pon 0 para rellenarlos todos |
| El remallado redondea una arista | Baja *featureAngle* para que esa arista cuente como viva |
| «… es una nube de puntos; reconstruye antes una malla» | Usa el menú *Puntos*: calcular normales y malla desde puntos |
| La malla de Poisson tiene una «tapa» donde no había puntos | Repite con *trim* 0,05–0,1 |
| «La malla de quads no puede ser un T-Spline: está abierta» | Rellena los agujeros (maxEdges 0) y repite el remallado en quads |
| «AI agent: port … busy» | Otro programa usa el puerto; cámbialo en *Ajustes → Agente IA* |

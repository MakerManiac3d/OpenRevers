# Manual de OpenRevers (versión 0.3)

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

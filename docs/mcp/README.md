# OpenRevers y los agentes de IA (MCP) · OpenRevers and AI agents

*Español primero; [English below](#english).*

OpenRevers incluye un servidor del
[Model Context Protocol](https://modelcontextprotocol.io) (revisión
2025-06-18; acepta también 2025-03-26 y 2024-11-05). Con él, un agente como
Claude puede usar todas las funciones del programa.

## Cómo funciona

```text
Claude Desktop / Claude Code
      │  stdio (JSON-RPC)
      ▼
openrevers-cli mcp ──── TCP 127.0.0.1:47823 + token ───► OpenRevers (ventana)
      │                                                    los mismos comandos,
      └─ sin la ventana abierta: trabaja «headless»         el mismo deshacer
         sobre un documento propio
```

- El agente arranca `openrevers-cli mcp`.
- Si la aplicación está abierta, el puente se conecta a ella y **cada paso
  ocurre en la ventana**: aparece en el registro y en el historial, y se puede
  deshacer.
- Si no está abierta, el puente trabaja sin interfaz, sobre un documento
  propio. Las herramientas de vista (`view_set` y `view_capture`) no están
  disponibles en ese modo.

## Configurar Claude Desktop

1. En OpenRevers, abre **Agente IA → Copiar configuración para Claude Desktop**.
2. En Claude Desktop, ve a **Configuración → Desarrollador → Editar configuración**.
3. Pega la entrada `openrevers` dentro de `mcpServers`. Queda así:

   ```json
   {
     "mcpServers": {
       "openrevers": {
         "command": "C:\\Program Files\\OpenRevers\\bin\\openrevers-cli.exe",
         "args": ["mcp"]
       }
     }
   }
   ```

4. Cierra Claude Desktop del todo (Archivo → Salir) y vuelve a abrirlo.

**Claude Code:** usa **Agente IA → Copiar comando para Claude Code** y pégalo
en una terminal:

```text
claude mcp add --scope user openrevers -- "C:\Program Files\OpenRevers\bin\openrevers-cli.exe" mcp
```

`--lang es` o `--lang en` fija el idioma de las descripciones; por defecto se
usa el del sistema. `--headless` obliga a trabajar sin la ventana.

## Herramientas (versión 0.6)

| Herramienta | Qué hace | Cambia el proyecto |
| --- | --- | --- |
| `session_state` | Qué hay abierto, cambios sin guardar, historial, carpetas permitidas | No |
| `guide_read` | La guía de trabajo para el agente | No |
| `mesh_import` | Importa STL, OBJ, PLY o 3MF (con su unidad y transformaciones), o una nube de puntos E57, XYZ, PTS, PLY u OBJ, a un cuerpo nuevo | Sí |
| `mesh_analyze` | Agujeros, aristas no manifold, orientación, piezas, área, volumen | No |
| `mesh_repair` | Une, limpia, quita piezas sueltas y orienta | Sí |
| `mesh_fill_holes` | Rellena los agujeros de hasta `maxEdges` aristas | Sí |
| `mesh_remove_pieces` | Quita piezas sueltas | Sí |
| `mesh_flip_normals` | Invierte las caras | Sí |
| `mesh_smooth` | Suavizado laplaciano (implícito o explícito) | Sí |
| `mesh_decimate` | Reduce triángulos con un error máximo | Sí |
| `mesh_remesh` | Remallado uniforme o adaptativo que conserva las aristas vivas | Sí |
| `mesh_remesh_quads` | Remallado en quads que siguen la forma y las aristas vivas (QuadriFlow); dice si sirve para T-Spline | Sí |
| `mesh_export` | Escribe STL, OBJ, PLY o 3MF; una nube, en PLY, XYZ u OBJ con sus normales (no sobrescribe sin `overwrite`); una malla de quads sale en quads en OBJ y PLY | No |
| `features_segment` | Divide la malla en regiones y ajusta a cada una plano, esfera, cilindro, cono o toro, con su σ. Lista las 40 mayores (`maxListed`) y dice cuántas hay (`total`) | Sí |
| `features_fit` | Ajusta una forma a las caras alrededor de un punto | Sí |
| `features_refit` | Vuelve a ajustar una región como otro tipo de forma | Sí |
| `features_list` | Formas del cuerpo con medidas, posición y σ | No |
| `features_measure` | Ángulo y distancia entre dos formas | No |
| `features_rename` / `features_delete` | Renombrar o borrar formas | Sí |
| `mesh_cast` | Lanza rayos contra la malla y devuelve cruces, cuerdas (grosor de pared) y huecos; admite barridos de varios rayos | No |
| `mesh_pieces` | Lista los trozos separados de un escaneo (caras, área, volumen, tamaño, centro) y, con `split`, hace de cada uno un cuerpo propio | Sí |
| `mesh_profile` | Perfil radial de una pieza de revolución alrededor de un eje (el de un cilindro ajustado o uno dado): piel exterior o interior, tramos y zonas cilindro/cono/escalón | No |
| `mesh_unwrap` | Mapa desenrollado ángulo × posición de la piel y parches de rebaje o resalte respecto al radio de cada fila; puede escribir un CSV | No |
| `view_selection` | Lo que la persona ha señalado con el ratón: punto, cuerpo y formas en orden; en una pieza, el parámetro, boceto u operación elegido en el árbol y las caras y aristas del sólido (tipo, medida, eje; en las aristas, el punto medio para `part_fillet`). Solo con la ventana abierta | No |
| `view_pick` | Hacer clic en la vista como la persona (x, y de 0 a 1; `add` para añadir) | No |
| `align_principal` | Alinea a los ejes principales | Sí |
| `align_datums` | Alineación 3-2-1 por formas (caras, ejes, centros) | Sí |
| `align_best_fit` | Mejor ajuste (ICP robusto) sobre otro cuerpo, con informe de desviación | Sí |
| `align_move` | Girar y trasladar | Sí |
| `points_thin` | Aclara una nube: un punto por cubo de lado `spacing` | Sí |
| `points_estimate_normals` | Normales de una nube (plano de los vecinos) y su orientación | Sí |
| `points_to_mesh` | Malla cerrada desde la nube (Poisson apantallado), con recorte opcional de lo inventado | Sí |
| `mesh_section` | Dónde corta un plano a una malla: polilíneas en coordenadas del plano, simplificadas. El contorno del que sacar un boceto | No |
| `part_new` | Pieza paramétrica vacía | Sí |
| `part_sketch_from_section` | Corta el escaneo con un plano y convierte cada contorno cerrado en rectas y arcos dentro de una tolerancia (círculos enteros, esquinas vivas, arcos tangentes); con `splines`, los tramos libres de un contorno orgánico salen como splines. Devuelve la medida de cada segmento | Sí |
| `part_sketch` · `part_edit_sketch` | Boceto: lazos cerrados de rectas, arcos, círculos y splines (`{type: spline, poles: [[x, y], ...]}`, del primer polo al último) en un plano (un lazo dentro de otro es un agujero). `part_edit_sketch` cambia los lazos, el plano o el nombre, cada uno por separado | Sí |
| `part_constrain` | Restricciones y cotas de un boceto (`add`: {kind, refs: [{loop, segment, part}], value}; `remove`: índices o nombres), resueltas con PlaneGCS. Una cota con el nombre de un parámetro sigue a `part_parameter`. Devuelve `dof` y el valor medido de cada una; rechaza las contradictorias | Sí |
| `part_chamfer` | Achaflana aristas (puntos sobre ellas) con una distancia. |
| `part_hole` | Taladra un agujero en cada círculo de un boceto: simple, con cajeado o avellanado; con profundidad o a través de todo. |
| `part_mirror` | Refleja una operación (o toda la pieza) en un plano y combina la copia. |
| `part_export_mesh` | Escribe la pieza como malla (STL, 3MF, OBJ, PLY). |
| `part_loft` | Sólido que pasa por varios bocetos en orden (secciones de un álabe, extremos de un conducto), suave o reglado | Sí |
| `part_pattern` | Copias de lo que hizo otra operación alrededor de un eje (`count` y `angle` pueden ser parámetros) | Sí |
| `part_surface_from_scan` | Superficie libre (B-spline bicúbica) ajustada a una zona del escaneo: una forma segmentada (`shape`) o una esfera (`centre`, `radius`); devuelve rms y máximo | Sí |
| `mesh_remove_plane` · `mesh_remove_outliers` · `mesh_crop` · `mesh_delete_faces` | Limpiar un escaneo: la mesa, los puntos sueltos, una región, caras elegidas | Sí |
| `align_global` · `align_point_pairs` | Alinear un escaneo sobre otro por su forma, sin posición de partida, o por pares de puntos | Sí |
| `mesh_merge` | Unir escaneos alineados en un cuerpo nuevo | Sí |
| `scan_status` · `scan_devices` · `scan_connect` · `scan_preview` · `scan_start` · `scan_pause` · `scan_stop` · `scan_disconnect` · `scan_use_simulator` | Manejar el escáner a través de la biblioteca de CrealityScan (Windows) o el simulador | No |
| `scan_export_to_body` | Pasar el escaneo (puntos o malla del escáner) a un cuerpo nuevo | Sí |
| `part_cut_by_surface` · `part_thicken` | Quitar lo que queda por encima o por debajo de una superficie libre; engrosarla hasta un sólido | Sí |
| `part_extrude` · `part_revolve` · `part_fillet` | Operaciones; se combinan con la pieza (`new`, `join`, `cut`, `intersect`). Los valores pueden ser números o nombres de parámetros | Sí |
| `part_parameter` | Crea o cambia un parámetro con nombre; la pieza se reconstruye | Sí |
| `part_edit` · `part_delete` · `part_move` | Cambiar valores, modo o supresión de una operación (también `count`, `thickness`, `keep`, `ruled`); borrar operaciones, bocetos o superficies; mover una operación en el historial. Un cambio que rompe la pieza no se aplica | Sí |
| `part_list` | Parámetros, bocetos, operaciones y el sólido (volumen, área, caras, tamaño) | No |
| `part_compare` | Distancias con signo del escaneo al sólido: mediana, percentiles 90 y 95, parte dentro de ±tolerancia y **zonas** fuera de ella (centro, tamaño, % del escaneo, distancia media y lado). Con `colorMap` colorea el escaneo en la vista (no es un paso de deshacer) | No |
| `part_export_step` · `part_import_step` | STEP (AP214, mm) | No · Sí |
| `mesh_export_tsm` | Escribe una malla de quads cerrada como T-Spline `.tsm` para Fusion | No |
| `project_save` · `project_open` · `project_new` | Proyecto `.orvp` | — |
| `body_list` · `body_rename` · `body_duplicate` · `body_delete` · `body_set_visible` · `body_set_opacity` · `body_clear_deviation` | Cuerpos; opacidad para ver uno dentro de otro; quitar los colores de `part_compare` | — |
| `history_undo` · `history_redo` | Deshacer y rehacer | — |
| `view_set` · `view_capture` | Gira la vista y devuelve una imagen PNG (solo con la ventana) | No |

`openrevers-cli list` imprime cada comando con su esquema JSON completo.

## Seguridad

- **Solo en local.** El puente habla con el agente por stdio y con la ventana
  por `127.0.0.1`, con un token de emparejamiento. Ninguna de las dos partes se
  conecta a internet.
- **Carpetas permitidas.** El agente solo lee y escribe dentro de las carpetas
  de *Ajustes → Agente IA*; por defecto, tu carpeta personal. Una ruta fuera de
  ellas se rechaza con un mensaje que lo explica.
- **Nada se pierde sin querer.** Un archivo no se sobrescribe sin
  `overwrite: true`, ni se descarta un proyecto con cambios sin
  `discard: true`. La guía pide al agente que te pregunte antes de usarlos.
- **Todo se deshace.** Cada operación del agente es un paso del historial.
- **Lo que lee el agente va a su modelo de IA.** Es la única información que
  sale del equipo.

Los ajustes del agente están en `agent.json`:

- Windows: `%APPDATA%\OpenRevers`
- macOS: `~/Library/Application Support/OpenRevers`
- Linux: `~/.config/OpenRevers`

`OPENREVERS_CONFIG_DIR` cambia esa carpeta.

---

## English

OpenRevers ships a [Model Context Protocol](https://modelcontextprotocol.io)
server (revision 2025-06-18; 2025-03-26 and 2024-11-05 also accepted).

**How it works:**

- The agent starts `openrevers-cli mcp`.
- With the OpenRevers window open, the bridge connects to it on
  `127.0.0.1:47823`, with a pairing token. Every step then happens in the
  window, appears in its log and history, and can be undone.
- Without the window, the bridge works headless on a document of its own. The
  view tools are not available then.

**Claude Desktop:**

1. In OpenRevers: *AI agent → Copy configuration for Claude Desktop*.
2. In Claude Desktop: *Settings → Developer → Edit Config*, and paste it inside
   `mcpServers`.
3. Quit Claude Desktop completely and open it again.

**Claude Code:** *AI agent → Copy command for Claude Code*, and run the copied
command in a terminal.

**Tools:** see the table above (names are the same). `openrevers-cli list`
prints every command with its full JSON Schema. `--lang es|en` sets the
language of the descriptions, and `--headless` forces the headless mode.

**Safety:**

- Local only: the bridge talks to the agent over stdio and to the window over
  `127.0.0.1` with a token. Neither connects to the internet.
- The agent may read and write only inside the folders set in *Settings → AI
  agent*; your home folder by default.
- Nothing is lost by accident: files are not overwritten without
  `overwrite: true`, and unsaved work is not discarded without
  `discard: true`.
- Every agent step is an undo step.
- What the agent reads goes to its model provider. That is the only
  information that leaves the computer.

## Paridad con el usuario

Cada campo de los esquemas lleva su tipo de control (`x-kind`) para que el
usuario pueda dar lo mismo desde el panel de comandos de la aplicación. Ver
[paridad.md](../desarrollo/paridad.md).

## Probar antes de cambiar (dryRun) · Try before changing

Toda herramienta que cambia el documento admite `"dryRun": true`. Se calcula
sobre una copia del documento (barata: las mallas se comparten) y responde con
su resultado normal más `"dryRun": true` y
`"preview": {"changed": [...], "added": [...], "removed": [...]}` con las
referencias de los cuerpos. El documento y el historial quedan igual. Es lo
mismo que usa la casilla *Ver en vivo* del panel.

Every tool that changes the document takes `"dryRun": true`: it runs on a
copy and answers with its usual result plus `dryRun` and `preview`
{changed, added, removed}; the document and history stay as they were. It is
what the panel's *See it live* checkbox uses.

## Comandos nuevos en un chat ya abierto · New commands in an open chat

Algunos clientes leen la lista de herramientas una sola vez por sesión. Para
que un chat abierto con una versión anterior llegue a los comandos nuevos hay
dos herramientas fijas: `command_list` (todos los comandos; con `command`, su
esquema) y `command_run` (`{"command": "scan.status", "arguments": {...}}`).

Some clients read the tool list once per session. `command_list` and
`command_run` reach every command, including ones added after that.

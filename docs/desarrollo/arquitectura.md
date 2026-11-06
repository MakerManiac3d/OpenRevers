# Arquitectura de OpenRevers

La interfaz, los scripts y el agente de IA pasan por **la misma capa de
comandos**. Así, lo que hace un clic es exactamente lo que hace la IA, con el
mismo deshacer.

```text
 Interfaz Qt (src/app)     openrevers-cli run (src/cli)     Agente IA ── openrevers-cli mcp (src/cli, src/mcp)
          │                          │                                   │ TCP 127.0.0.1 + token
          └──────────────┬───────────┘                                   │ (AgentServer, src/app)
                         ▼                                               ▼
                 Comandos (src/commands) ◄───────────────────────────────┘
                  entrada y salida JSON, esquema JSON, un paso de deshacer
                         │
                 Documento (src/doc): cuerpos, historial, proyecto .orvp
                         │
          Núcleo geométrico (src/core): mallas, análisis, reparación, procesado
                         │
          Librerías: PMP, Eigen, nlohmann/json  (fases siguientes: Open CASCADE, CGAL…)
```

## Carpetas

| Carpeta | Contenido | Depende de |
| --- | --- | --- |
| `src/core/mesh` | `TriMesh`, lectura y escritura, análisis, reparación y procesado | Eigen, PMP |
| `src/core/util` | Ajustes, carpeta de configuración, política de rutas del agente | nlohmann/json |
| `src/doc` | `Document` (cuerpos, deshacer, rehacer) y el formato `.orvp` | core |
| `src/commands` | Registro de comandos, validación de entradas, comandos del núcleo, guía | doc |
| `src/mcp` | Protocolo MCP, backend local, cliente del enlace con la aplicación | commands |
| `src/cli` | `openrevers-cli`: `mcp`, `run`, `list` | mcp |
| `src/app` | Aplicación Qt: ventana, visor OpenGL, ejecutor de comandos, servidor del agente | Qt 6, todo lo anterior |
| `tests` | Tests unitarios (GoogleTest) y prueba de punta a punta (`e2e_mcp.py`) | |

`orv_core` (todo menos `src/app` y `src/cli`) no depende de Qt: se prueba solo
y se puede usar desde otras herramientas.

## Principios

1. **Un comando = una acción.** Tiene un nombre (`mesh.fill_holes`; el agente
   lo ve como `mesh_fill_holes`), un título y una descripción en español e
   inglés, un esquema JSON de entrada y un resultado JSON. Si cambia el
   documento (`mutates`), es un paso de deshacer. Si falla, el documento vuelve
   a como estaba (`abortChange`).
2. **Las mallas no se modifican en sitio.** Un cuerpo apunta a una malla
   compartida (`shared_ptr<const TriMesh>`). Cambiarla es reemplazar el
   puntero, así que un paso de deshacer no copia triángulos.
3. **La interfaz no se congela.** `CommandRunner` ejecuta los comandos de uno
   en uno, los pesados en un hilo de trabajo. El documento está protegido con
   un mutex y avisa de los cambios a la interfaz.
4. **Nada sale del equipo.** No hay código de red salvo el servidor del agente
   en `127.0.0.1`, que comprueba el token y que la conexión sea local.
5. **Los formularios salen del esquema.** `CommandDialog` construye el
   formulario de cada comando a partir de su esquema JSON. Un comando nuevo
   tiene interfaz sin escribir una línea de Qt.

## Añadir un comando

1. En `src/commands/CoreCommands.cpp` (o `src/app/AppCommands.cpp` si necesita
   la vista), llama a `R.add({...})` con:
   - nombre y grupo;
   - título y descripción en `{inglés, español}`;
   - esquema (usa los constructores de `schema::`);
   - `mutates` y `appOnly`;
   - la función.
2. Dentro de la función:
   - lee los cuerpos con `ctx.doc.body(ref)`;
   - pasa las rutas por `ctx.paths.check(...)`, que aplica las carpetas
     permitidas al agente;
   - cambia una malla con `editMesh(...)`;
   - devuelve un `json` con números y un `summary` en el idioma de
     `ctx.say(en, es)`.
3. Añade un test en `tests/test_document.cpp`.
4. Añádelo a un menú en `MainWindow::buildActions` si debe tener uno; el
   agente lo ve sin hacer nada más.
5. Documenta el comando en `docs/mcp/README.md` y, si cambia el flujo de
   trabajo, en `src/commands/Guide.cpp`.

## Traducciones

- Los textos de la interfaz están en inglés en el código (`tr("…")`). Los de
  los comandos, en sus dos idiomas, en `Text{en, es}`.
- `cmake --build build --target update_translations` añade los textos nuevos
  a `translations/openrevers_es.ts`.
- `python tools/translate_es.py` los rellena desde su tabla y avisa de los que
  falten. La comprobación de GitHub falla si queda alguno sin traducir.

## Formato de proyecto

Ver [formato-proyecto.md](formato-proyecto.md).

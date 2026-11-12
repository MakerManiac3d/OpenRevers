# Paridad: lo que hace el agente, lo puede hacer el usuario

Regla: cada dato que un comando pide por MCP debe poder darse en la
interfaz eligiéndolo o haciendo clic, nunca escribiendo JSON ni una
referencia (`s3`, `o5`) a mano. Así OpenRevers se puede usar igual de forma
manual que automática.

## Cómo se cumple

- Cada campo del esquema de un comando tiene un tipo de control, que se lee
  con `fieldKind()` (`src/commands/FieldKind.h`) a partir de `type`, `enum` y
  `x-kind`.
- `x-of` dice de qué campo de cuerpo dependen las listas (por ejemplo, las
  formas de `scan` en `part.surface_from_scan`). Por defecto es `body`.
- El panel de comandos (`src/app/CommandPanel.cpp`) da a cada tipo su
  selector.
- La prueba `Parity.EveryFieldOfEveryCommandHasAControl` recorre todos los
  comandos y falla si un campo no tiene control (`unsupported`) o si un campo
  que nombra algo del documento es texto libre.

## Tipos de control (`x-kind`)

| x-kind | Control en el panel |
| --- | --- |
| `body` | Desplegable de cuerpos y ⌖ (clic en la vista) |
| `body-list` | Lista con casillas y ⌖ |
| `feature` | Desplegable de las formas del cuerpo (`x-of`) |
| `sketch`, `operation`, `surface`, `part-item`, `parameter` | Desplegable de la pieza |
| `sketch-list` | Lista con casillas, ordenable arrastrando |
| `point` | X, Y, Z y ⌖: el punto pulsado; si no tiene unidad (una dirección), la normal de la cara |
| `point2` | X, Y en el plano del boceto |
| `point-list` | Lista de puntos con *Añadir clics*, *Quitar el último* y *Vaciar* |
| `plane` | Origen, normal y dirección X, y *Desde una cara de la vista* |
| `mesh-faces` | *Tomar la selección del lazo* |
| `index-list` | Números separados por comas |
| `loops` | *Dibujar en el editor de bocetos…* |
| `open-path`, `save-path`, `folder` | Texto y examinar |
| (sin x-kind) | Según `type`: número, entero, casilla, desplegable (`enum`), texto, número o parámetro |

## Al añadir un comando

1. Usa los constructores de `schema::` y `schema::kind(…, "<x-kind>")` para
   cualquier campo que no sea un número, un texto o una elección.
2. Si hace falta un control nuevo, añádelo a `FieldKind` y al panel en el
   mismo cambio.
3. La prueba de paridad no deja pasar un campo sin control.

## Vista previa (0.10)

`CommandRegistry::preview()` ejecuta un comando que cambia el documento sobre
un `Document` de usar y tirar, creado con `reset()` a partir de los cuerpos
actuales (copia barata: los cuerpos comparten sus mallas y modelos por
`shared_ptr`). Los cuerpos con otra malla u otro modelo, los nuevos y los
quitados forman el `Preview`. Lo usan:

- el panel, con *Ver en vivo* (en otro hilo, con `QtConcurrent`; solo cuenta
  el último cálculo pedido);
- el agente y la línea de órdenes, con `"dryRun": true` en `run()`.

`tests/test_preview.cpp` comprueba que el documento y el historial no cambian.

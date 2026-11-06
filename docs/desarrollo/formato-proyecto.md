# Formato de proyecto `.orvp` (versión 3)

Un proyecto es un único archivo binario, en orden little endian:

| Desplazamiento | Tamaño | Contenido |
| --- | --- | --- |
| 0 | 8 | `ORVPROJ\0` |
| 8 | 4 | Versión del formato, `uint32` = 3 |
| 12 | 8 | Longitud *N* del manifiesto, `uint64` |
| 20 | *N* | Manifiesto JSON (UTF-8) |
| 20 + *N* | … | Bloques de datos de las mallas |

## Manifiesto

```json
{
  "application": "OpenRevers",
  "applicationVersion": "0.2.0",
  "units": "mm",
  "nextId": 3,
  "bodies": [
    {
      "id": 1, "name": "soporte", "kind": "mesh", "visible": true,
      "color": [0.62, 0.70, 0.78], "source": "C:/escaneos/soporte.stl",
      "vertices": 120345, "faces": 240686, "quads": 0, "normals": 0, "offset": 0
    }
  ]
}
```

## Datos

Cada malla empieza en su `offset`, contado desde el final del manifiesto:

- `vertices × 3 × float32`: las coordenadas x, y, z en milímetros;
- `faces × 3 × uint32`: los índices de vértice de cada triángulo, en sentido
  antihorario visto desde fuera;
- `quads × 4 × uint32`: los quads de una malla de quads, en el mismo sentido.
  Los triángulos son entonces su triangulación (dos por quad). `quads` es 0
  en una malla de triángulos;
- `normals × 3 × float32`: las normales de una nube de puntos, una por
  vértice, o ninguna (`normals` = 0).

Una **nube de puntos** es un cuerpo con `faces` = 0 y `"kind": "points"`.

| Versión | Cambio |
| --- | --- |
| 1 | Vértices y triángulos |
| 2 | Añade `quads`. Un archivo de la versión 1 se lee igual, sin quads |
| 3 | Añade `normals` y las nubes de puntos. Las versiones 1 y 2 se leen igual |

## Garantías

- **Guardado seguro.** Se escribe en `<archivo>.tmp` y después se renombra:
  un fallo al guardar nunca destruye el proyecto anterior.
- **Lectura defensiva.** Antes de usar los datos se comprueban el
  identificador, la versión, que ningún bloque sobrepase el final del archivo y
  que ningún índice apunte a un vértice inexistente.
- **Nada se ejecuta.** Un proyecto solo contiene datos.
- **Compatibilidad.** Un formato más nuevo que el que conoce el programa se
  rechaza con un mensaje claro. Cada versión lee las anteriores.

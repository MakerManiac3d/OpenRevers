# Formato de proyecto `.orvp` (versión 1)

Un proyecto es un único archivo binario, en orden little endian:

| Desplazamiento | Tamaño | Contenido |
| --- | --- | --- |
| 0 | 8 | `ORVPROJ\0` |
| 8 | 4 | Versión del formato, `uint32` = 1 |
| 12 | 8 | Longitud *N* del manifiesto, `uint64` |
| 20 | *N* | Manifiesto JSON (UTF-8) |
| 20 + *N* | … | Bloques de datos de las mallas |

## Manifiesto

```json
{
  "application": "OpenRevers",
  "applicationVersion": "0.1.0",
  "units": "mm",
  "nextId": 3,
  "bodies": [
    {
      "id": 1, "name": "soporte", "kind": "mesh", "visible": true,
      "color": [0.62, 0.70, 0.78], "source": "C:/escaneos/soporte.stl",
      "vertices": 120345, "faces": 240686, "offset": 0
    }
  ]
}
```

## Datos

Cada malla empieza en su `offset`, contado desde el final del manifiesto:

- `vertices × 3 × float32`: las coordenadas x, y, z en milímetros;
- `faces × 3 × uint32`: los índices de vértice de cada triángulo, en sentido
  antihorario visto desde fuera.

## Garantías

- **Guardado seguro.** Se escribe en `<archivo>.tmp` y después se renombra:
  un fallo al guardar nunca destruye el proyecto anterior.
- **Lectura defensiva.** Antes de usar los datos se comprueban el
  identificador, la versión, que ningún bloque sobrepase el final del archivo y
  que ningún índice apunte a un vértice inexistente.
- **Nada se ejecuta.** Un proyecto solo contiene datos.
- **Compatibilidad.** Un formato más nuevo que el que conoce el programa se
  rechaza con un mensaje claro. Las versiones futuras sabrán leer la 1.

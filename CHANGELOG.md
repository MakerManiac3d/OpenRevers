# Cambios · Changelog

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

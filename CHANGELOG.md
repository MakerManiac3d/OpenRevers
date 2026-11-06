# Cambios · Changelog

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
- **Mallas grandes**: medido con 10 M de triángulos (ver
  `docs/desarrollo/rendimiento.md`). Mapa de aristas y unión de vértices más
  rápidos y con menos memoria; el diezmado limita la valencia; los bordes
  abiertos se calculan fuera del hilo de la interfaz.
- El instalador incluye las licencias de los componentes compilados.
- 26 herramientas MCP.

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

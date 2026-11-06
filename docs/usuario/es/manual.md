# Manual de OpenRevers (versión 0.1)

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
   ventana. Formatos: STL (binario y texto), OBJ y PLY. Las unidades se toman
   como milímetros.
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
6. **Guardar o exportar.** *Archivo → Guardar proyecto* (`.orvp`, con todos
   los cuerpos), o *Exportar malla* a STL, OBJ o PLY.

Prueba una operación sobre una copia con *Cuerpo → Duplicar cuerpo*, y compara.

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
| «The file has no triangles» | El archivo es una nube de puntos o está vacío. La reconstrucción de nubes llega en una versión próxima |
| El relleno deja agujeros abiertos | Son más grandes que *maxEdges*; súbelo, o pon 0 para rellenarlos todos |
| El remallado redondea una arista | Baja *featureAngle* para que esa arista cuente como viva |
| «AI agent: port … busy» | Otro programa usa el puerto; cámbialo en *Ajustes → Agente IA* |

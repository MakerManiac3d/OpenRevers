# Rendimiento con mallas grandes

Criterio de la fase 1: **abrir 10 M de triángulos sin que la ventana se
bloquee**. Medido el 6 de noviembre de 2026 en un contenedor Linux de 2
núcleos y 7 GB, sin tarjeta gráfica (OpenGL por software, llvmpipe), con la
compilación Release.

## Núcleo (`orv_bench`)

`orv_bench 10` crea una losa cerrada y ondulada de 10 010 892 triángulos
(5 005 448 vértices), la escribe en STL binario y mide cada paso:

| Paso | Tiempo | Memoria máxima |
| --- | --- | --- |
| Escribir STL binario (500 MB) | 1,3 s | 272 MB |
| Leer STL binario y unir vértices | 6,9 s | 845 MB |
| Analizar | 10,6 s | 1 285 MB |
| Quitar caras degeneradas | 0,2 s | 1 285 MB |
| Quitar caras duplicadas | 3,4 s | 1 285 MB |
| Unir vértices (0,001 mm) | 9,3 s | 1 285 MB |
| Orientar | 6,9 s | 1 514 MB |
| Guardar proyecto `.orvp` | 0,4 s | 1 514 MB |
| Abrir proyecto `.orvp` | 0,1 s | 1 514 MB |
| Diezmar a 1 M de triángulos | 145 s | 1 552 MB |

Los tiempos varían un ±30 % entre ejecuciones en esta máquina compartida.

Cambios que hicieron falta para llegar aquí:

- **Mapa de aristas** en arrays planos ordenados por radix, en vez de una
  tabla hash con un vector por arista: memoria máxima del análisis de
  3,4 GB a 1,3 GB, y el análisis lo construye una sola vez.
- **Unir vértices** con celdas del doble de la tolerancia: 8 celdas vecinas
  en vez de 27, y listas en arrays planos.
- **Diezmado** con valencia máxima 12. Sin límite, las zonas planas (donde
  todo colapso cuesta cero) formaban abanicos de miles de triángulos y el
  tiempo crecía con su cuadrado: una losa de 40 000 triángulos pasó de 16 s
  a 0,3 s.

## Aplicación

Prueba: la aplicación abierta, un agente importa el STL de 10 M y lo analiza
por MCP, mientras otro le pide la lista de herramientas cada 0,2 s (la
respuesta la da el hilo de la interfaz, así que mide si está libre).

| Qué | Resultado |
| --- | --- |
| Importar (en un hilo de trabajo) | 4,8 s |
| Analizar (en un hilo de trabajo) | 11,2 s |
| Respuesta de la interfaz, mediana | 3 ms |
| Preparar la malla para la tarjeta gráfica (hilo de la interfaz) | 0,36 s |
| Dibujar un fotograma con OpenGL **por software** | 1,8–2,0 s |

Las operaciones nunca bloquean la ventana: corren en un hilo de trabajo, y
los bordes abiertos también se calculan aparte. Lo único que ocupa el hilo
de la interfaz es preparar los búferes (0,36 s) y dibujar. En esta máquina
sin tarjeta gráfica cada fotograma de 10 M de triángulos tarda unos 2 s;
con una tarjeta gráfica normal es cuestión de milisegundos, pero no se ha
podido medir aquí.

## Cómo repetirlo

```sh
cmake --build build --target orv_bench
build/tests/orv_bench 10            # millones de triángulos; carpeta temporal opcional
```

## Pendiente

- Diezmado de 10 M más rápido (2,5 min ahora): diezmado previo por
  agrupamiento de vértices o en paralelo por bloques.
- Unir vértices y analizar en paralelo.
- Medir el dibujo con tarjeta gráfica en Windows.

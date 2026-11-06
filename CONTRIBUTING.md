# Contribuir a OpenRevers

Las contribuciones se aceptan bajo la **GPL-3.0-or-later**, la licencia del
proyecto. Firma cada commit (`git commit -s`, el
[Developer Certificate of Origin](https://developercertificate.org/)).

## Normas

1. **Nada sale a la red.** Ningún código nuevo puede conectarse fuera del
   equipo: ni telemetría, ni actualizaciones, ni descargas en ejecución.
2. **Cada acción es un comando.** Una función nueva se añade como comando, con
   su esquema y sus textos en español e inglés. Así la tienen la interfaz, los
   scripts y la IA ([arquitectura](docs/desarrollo/arquitectura.md#añadir-un-comando)).
3. **Con test.** Cada operación geométrica lleva un test con una malla de
   respuesta conocida.
4. **Con referencia.** Un algoritmo tomado de un artículo o de otro proyecto se
   cita en `docs/algoritmos`. Una librería nueva se añade a `THIRD_PARTY.md` con
   su versión fijada y su licencia, que debe ser compatible con la GPL-3.0.
5. **Sin avisos.** El código compila sin avisos con `-DORV_WARNINGS_AS_ERRORS=ON`.
6. **Traducido.** Los textos nuevos de la interfaz se traducen con
   `tools/translate_es.py`.
7. **Documentado.** Los cambios visibles van al manual (en los dos idiomas) y a
   `CHANGELOG.md`.

## Estilo

- C++20, cabeceras con `#pragma once`.
- Espacio de nombres `orv`.
- Formato de `.clang-format`: 4 espacios y 150 columnas.
- Los comentarios explican el porqué, no el qué.
- Cada archivo empieza con `// SPDX-License-Identifier: GPL-3.0-or-later`.

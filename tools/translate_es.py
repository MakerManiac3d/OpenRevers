#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fills translations/openrevers_es.ts with the Spanish texts below.

Run after `cmake --build build --target update_translations` has added new
source texts. Texts already translated in the .ts file are kept; a source
text missing here is left "unfinished" and reported.
"""
import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape, unescape

ES = {
    "Run": "Ejecutar",
    "Cancel": "Cancelar",
    "Agent: %1…": "Agente: %1…",
    "%1…": "%1…",
    "Agent": "Agente",
    "You": "Tú",
    "Failed: %1": "Ha fallado: %1",
    "OpenRevers": "OpenRevers",
    "Version %1. Everything runs on this computer.": "Versión %1. Todo se ejecuta en este equipo.",
    "&File": "&Archivo",
    "&Edit": "&Edición",
    "&Mesh": "&Malla",
    "&Body": "&Cuerpo",
    "&View": "&Ver",
    "&AI agent": "Agente &IA",
    "&Help": "A&yuda",
    "Main": "Principal",
    "&New project": "&Nuevo proyecto",
    "&Open project…": "&Abrir proyecto…",
    "&Save project": "&Guardar proyecto",
    "Save project &as…": "Guardar proyecto &como…",
    "&Import mesh…": "&Importar malla…",
    "&Export mesh…": "&Exportar malla…",
    "&Quit": "&Salir",
    "&Undo": "&Deshacer",
    "&Redo": "&Rehacer",
    "Isometric": "Isométrica",
    "Front": "Frontal",
    "Back": "Posterior",
    "Left": "Izquierda",
    "Right": "Derecha",
    "Top": "Superior",
    "Bottom": "Inferior",
    "&Fit all": "&Encuadrar todo",
    "Mesh edges": "Aristas de la malla",
    "Export &T-Spline for Fusion (.tsm)…": "Exportar &T-Spline para Fusion (.tsm)…",
    "Export T-Spline": "Exportar T-Spline",
    "T-Spline (*.tsm)": "T-Spline (*.tsm)",
    "Quads": "Quads",
    "Open edges": "Bordes abiertos",
    "Flat shading": "Sombreado plano",
    "Copy configuration for Claude &Desktop": "Copiar configuración para Claude &Desktop",
    "Copy command for Claude &Code": "Copiar comando para Claude &Code",
    "&Settings…": "&Ajustes…",
    "&Guide": "&Guía",
    "Source code (GitHub)": "Código fuente (GitHub)",
    "&About OpenRevers": "&Acerca de OpenRevers",
    "Se&ttings…": "A&justes…",
    "Body": "Cuerpo",
    "Triangles": "Triángulos",
    "Model": "Modelo",
    "Property": "Propiedad",
    "Value": "Valor",
    "Properties": "Propiedades",
    "History": "Historial",
    "Log": "Registro",
    "Name": "Nombre",
    "Reference": "Referencia",
    "Vertices": "Vértices",
    "Size X × Y × Z (mm)": "Tamaño X × Y × Z (mm)",
    "Minimum (mm)": "Mínimo (mm)",
    "Maximum (mm)": "Máximo (mm)",
    "Area (mm²)": "Área (mm²)",
    "Source": "Origen",
    "Untitled": "Sin título",
    "AI agent connected": "Agente IA conectado",
    "AI agent: ready on 127.0.0.1:%1": "Agente IA: listo en 127.0.0.1:%1",
    "What the agent reads goes to its AI model. Nothing else leaves this computer.": "Lo que lee el agente va a su modelo de IA. Nada más sale de este equipo.",
    "AI agent: off": "Agente IA: desactivado",
    "AI agent: port %1 busy": "Agente IA: puerto %1 ocupado",
    "ERROR ": "ERROR ",
    "Import a mesh first (File → Import mesh).": "Importa primero una malla (Archivo → Importar malla).",
    "Mesh analysis": "Análisis de la malla",
    "Import mesh": "Importar malla",
    "Meshes (*.stl *.obj *.ply *.3mf);;All files (*)": "Mallas (*.stl *.obj *.ply *.3mf);;Todos los archivos (*)",
    "Select a body to export.": "Selecciona un cuerpo para exportarlo.",
    "Export mesh": "Exportar malla",
    "STL (*.stl);;OBJ (*.obj);;PLY (*.ply);;3MF (*.3mf)": "STL (*.stl);;OBJ (*.obj);;PLY (*.ply);;3MF (*.3mf)",
    "Open project": "Abrir proyecto",
    "OpenRevers project (*.orvp)": "Proyecto de OpenRevers (*.orvp)",
    "Save project": "Guardar proyecto",
    "Unsaved changes": "Cambios sin guardar",
    "The project has unsaved changes. Save them first?": "El proyecto tiene cambios sin guardar. ¿Quieres guardarlos antes?",
    "Copied": "Copiado",
    "Paste this command in a terminal:\n\n%1": "Pega este comando en una terminal:\n\n%1",
    'In Claude Desktop: Settings → Developer → Edit Config, paste the "openrevers" entry inside "mcpServers", then quit Claude Desktop completely and open it again.\n\n%1':
        'En Claude Desktop: Configuración → Desarrollador → Editar configuración, pega la entrada "openrevers" dentro de "mcpServers" y después cierra Claude Desktop del todo y vuelve a abrirlo.\n\n%1',
    "Language": "Idioma",
    "The new language is used the next time OpenRevers starts.": "El nuevo idioma se aplicará la próxima vez que abras OpenRevers.",
    "OpenRevers guide": "Guía de OpenRevers",
    "About OpenRevers": "Acerca de OpenRevers",
    '<h3>OpenRevers %1</h3><p>Reverse engineering of scanned meshes into parametric solids, entirely on this computer.</p><p>© 2026 MakerManiac3d. Free software under the GNU General Public License, version 3 or later.</p><p>Built on Qt, PMP Library, Eigen and nlohmann/json; see THIRD_PARTY.md.</p><p><a href="%2">%2</a></p>':
        '<h3>OpenRevers %1</h3><p>Ingeniería inversa de mallas escaneadas a sólidos paramétricos, completamente en este equipo.</p><p>© 2026 MakerManiac3d. Software libre bajo la Licencia Pública General de GNU, versión 3 o posterior.</p><p>Hecho con Qt, PMP Library, Eigen y nlohmann/json; consulta THIRD_PARTY.md.</p><p><a href="%2">%2</a></p>',
    "Settings": "Ajustes",
    "General": "General",
    "System language": "Idioma del sistema",
    "AI agent (MCP)": "Agente IA (MCP)",
    "An AI agent such as Claude can drive OpenRevers through the openrevers-cli bridge. It connects on 127.0.0.1 only, with a pairing token. What the agent reads goes to its AI model; nothing else leaves this computer.":
        "Un agente de IA como Claude puede manejar OpenRevers a través del puente openrevers-cli. Solo se conecta en 127.0.0.1, con un token de emparejamiento. Lo que lee el agente va a su modelo de IA; nada más sale de este equipo.",
    "Let an AI agent connect": "Permitir que se conecte un agente de IA",
    "Local port": "Puerto local",
    "Folders the agent may read and write (empty: your home folder):": "Carpetas en las que el agente puede leer y escribir (vacío: tu carpeta personal):",
    "Add folder…": "Añadir carpeta…",
    "Remove": "Quitar",
    "Folder the agent may use": "Carpeta que puede usar el agente",
    "The 3D view needs OpenGL 3.3, and this computer offers %1.%2. Update the graphics driver; on a computer without a graphics card, start OpenRevers with QT_OPENGL=software.":
        "La vista 3D necesita OpenGL 3.3 y este equipo ofrece el %1.%2. Actualiza el controlador de la tarjeta gráfica; en un equipo sin tarjeta gráfica, abre OpenRevers con QT_OPENGL=software.",
}


def main() -> int:
    path = Path(__file__).resolve().parent.parent / "translations" / "openrevers_es.ts"
    text = path.read_text(encoding="utf-8")
    missing = []

    def fill(m: re.Match) -> str:
        source_xml, translation = m.group(1), m.group(2)
        source = unescape(source_xml, {"&quot;": '"', "&apos;": "'"})
        if source in ES:
            return f"<source>{source_xml}</source>\n        <translation>{escape(ES[source], {chr(34): '&quot;'})}</translation>"
        if 'type="unfinished"' in translation or 'type="vanished"' in translation:
            missing.append(source)
        return m.group(0)

    text = re.sub(r"<source>(.*?)</source>\s*(<translation[^>]*>.*?</translation>|<translation[^>]*/>)", fill, text, flags=re.S)
    path.write_text(text, encoding="utf-8")
    for s in missing:
        print("untranslated:", s, file=sys.stderr)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())

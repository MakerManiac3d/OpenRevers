// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/Guide.h"

namespace orv {

namespace {

const char* kWorkflowEn = R"(## Workflow (version 0.3: mesh preparation, shapes and alignment)
1. `session_state` — what is open. Never discard someone's unsaved work: save it first.
2. `mesh_import` with the full path of the scan. Units are millimetres.
3. `mesh_analyze` — read holes, pieces, non-manifold edges, flipped faces and the mean edge length before changing anything. Tell the person what you found.
4. `mesh_repair` — weld, clean and orient in one step. Then `mesh_analyze` again.
5. `mesh_fill_holes` with `maxEdges` a little above the size of the holes you mean to close; the scan's outer rim is usually the longest loop: leave it open unless the part is closed.
6. Optional: `mesh_smooth` (little: it rounds sharp edges), `mesh_remesh` (even triangles, needed later for surfacing), `mesh_decimate` (lighter mesh for CAD) with `maxError` to stay close to the scan.
7. `view_capture` (desktop app) to look at the result; `mesh_export` or `project_save` to hand it over.
Point clouds (E57, XYZ, PTS, a PLY or OBJ without faces) come in as clouds: mesh tools refuse them. `points_thin` (optional, spacing about the scan's resolution), `points_estimate_normals` (propagate; outward for closed convex parts), then `points_to_mesh` (depth 8; trim 0.05–0.1 for an open scan) makes a new mesh body; continue from step 3 on it.
Shapes and alignment (for parts made of planes, holes, bosses): `features_segment` finds the planes, cylinders, cones, spheres and tori with their σ (σ near the noise means a true shape). Read sizes with `features_list`, relations with `features_measure` (distance between parallel faces, hole spacing). Then place the part: `align_datums` with a large face as primary, a side face or a hole as secondary, and a third face or hole as tertiary; or `align_principal` for a quick start; or `align_best_fit` onto a reference body. A hole the segmentation missed: `features_fit` at a point on its wall. When the person says "this face" or "these holes", read `view_selection` (desktop app): the point they clicked and the shapes they picked, in order.
8. For a freeform part going to Fusion as a T-Spline: close every hole (`mesh_fill_holes` with `maxEdges` 0), bring a dense scan down to a few hundred thousand triangles (`mesh_decimate` or `mesh_remesh`), then `mesh_remesh_quads` (a few thousand quads is usual for a skin) and `mesh_export_tsm`. Its answer says `tsmReady` and, if not, why. A later triangle operation drops the quads.
Every changing step is one undo step: `history_undo` takes it back. Report numbers, not impressions.
)";

const char* kWorkflowEs = R"(## Flujo de trabajo (versión 0.3: preparación de mallas, formas y alineación)
1. `session_state`: qué hay abierto. Nunca descartes trabajo sin guardar de otra persona: guárdalo antes.
2. `mesh_import` con la ruta completa del escaneo. Las unidades son milímetros.
3. `mesh_analyze`: lee agujeros, piezas, aristas no manifold, caras invertidas y la longitud media de arista antes de cambiar nada. Cuéntale a la persona lo que has encontrado.
4. `mesh_repair`: une, limpia y orienta en un paso. Después, `mesh_analyze` otra vez.
5. `mesh_fill_holes` con `maxEdges` algo mayor que el tamaño de los agujeros que quieres cerrar; el borde exterior del escaneo suele ser el lazo más largo: déjalo abierto salvo que la pieza sea cerrada.
6. Opcional: `mesh_smooth` (poco: redondea aristas vivas), `mesh_remesh` (triángulos regulares, necesarios después para superficies), `mesh_decimate` (malla más ligera para CAD) con `maxError` para no alejarse del escaneo.
7. `view_capture` (aplicación de escritorio) para ver el resultado; `mesh_export` o `project_save` para entregarlo.
Las nubes de puntos (E57, XYZ, PTS, un PLY u OBJ sin caras) entran como nubes: las herramientas de malla las rechazan. `points_thin` (opcional, con spacing parecido a la resolución del escaneo), `points_estimate_normals` (propagate; outward para piezas cerradas convexas) y después `points_to_mesh` (depth 8; trim 0,05–0,1 en un escaneo abierto) crea un cuerpo de malla nuevo; sigue desde el paso 3 con él.
Formas y alineación (piezas de planos, taladros, salientes): `features_segment` encuentra planos, cilindros, conos, esferas y toros con su σ (una σ cercana al ruido indica una forma real). Las medidas se leen con `features_list` y las relaciones con `features_measure` (distancia entre caras paralelas, entre taladros). Después se coloca la pieza: `align_datums` con una cara grande como primario, una cara lateral o un taladro como secundario y otra cara o taladro como terciario; o `align_principal` para empezar rápido; o `align_best_fit` sobre un cuerpo de referencia. Un taladro que la segmentación no encontró: `features_fit` en un punto de su pared. Cuando la persona dice "esta cara" o "estos taladros", lee `view_selection` (aplicación de escritorio): el punto en que hizo clic y las formas que eligió, en orden.
8. Para una pieza de forma libre que irá a Fusion como T-Spline: cierra todos los agujeros (`mesh_fill_holes` con `maxEdges` 0), baja un escaneo denso a unos cientos de miles de triángulos (`mesh_decimate` o `mesh_remesh`), después `mesh_remesh_quads` (unos miles de quads suelen bastar para una piel) y `mesh_export_tsm`. La respuesta dice `tsmReady` y, si no, por qué. Una operación posterior sobre los triángulos quita los quads.
Cada paso que cambia algo es un paso de deshacer: `history_undo` lo revierte. Da números, no impresiones.
)";

const char* kMeshEn = R"(## Reading a mesh report
- `holes`: open rims. A scan of one side of a part has one long outer rim; small loops are holes.
- `nonManifoldEdges` > 0: an edge with three or more faces — repair before anything else.
- `orientationConflicts` > 0: faces wound against their neighbours; `mesh_repair` with orient fixes it.
- `components` > 1: loose pieces (often scanner noise); `minPieceFaces` or `keepLargestOnly` removes them.
- `meanEdgeLength`: the resolution of the scan; nothing finer than about three times the scan's noise is trustworthy.
- `volume` is only given for a closed mesh.
)";

const char* kMeshEs = R"(## Cómo leer el informe de una malla
- `holes`: bordes abiertos. El escaneo de una cara de una pieza tiene un borde exterior largo; los lazos pequeños son agujeros.
- `nonManifoldEdges` > 0: una arista con tres o más caras; repárala antes que nada.
- `orientationConflicts` > 0: caras orientadas al revés que sus vecinas; `mesh_repair` con orient lo corrige.
- `components` > 1: piezas sueltas (a menudo ruido del escáner); `minPieceFaces` o `keepLargestOnly` las quitan.
- `meanEdgeLength`: la resolución del escaneo; nada más fino que unas tres veces el ruido del escáner es fiable.
- `volume` solo se da para una malla cerrada.
)";

const char* kSafetyEn = R"(## Safety
- An agent reads and writes only inside the folders allowed in Settings → AI agent (by default the user's home folder).
- Files are never overwritten unless `overwrite: true` is passed; a project with unsaved changes is never replaced unless `discard: true` is passed. Ask the person before passing either.
- OpenRevers makes no connection to the internet. What you read from it goes to your model provider: say so if the person seems unaware.
)";

const char* kSafetyEs = R"(## Seguridad
- Un agente solo lee y escribe dentro de las carpetas permitidas en Ajustes → Agente IA (por defecto, la carpeta personal del usuario).
- Nunca se sobrescribe un archivo sin `overwrite: true`, ni se reemplaza un proyecto con cambios sin guardar sin `discard: true`. Pregunta a la persona antes de usar cualquiera de los dos.
- OpenRevers no se conecta a internet. Lo que leas de él va al proveedor de tu modelo: dilo si la persona no parece saberlo.
)";

}  // namespace

std::string guideText(const std::string& lang, const std::string& topic) {
    const bool es = lang == "es";
    std::string out = es ? "# Guía de OpenRevers\n\n" : "# OpenRevers guide\n\n";
    if (topic == "all" || topic == "workflow") out += es ? kWorkflowEs : kWorkflowEn;
    if (topic == "all" || topic == "mesh") out += std::string("\n") + (es ? kMeshEs : kMeshEn);
    if (topic == "all" || topic == "safety") out += std::string("\n") + (es ? kSafetyEs : kSafetyEn);
    return out;
}

}  // namespace orv

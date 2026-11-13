// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppCommands.h"

#include "app/Viewport.h"

#include <QBuffer>
#include <QImage>

namespace orv {

void registerAppCommands(CommandRegistry& R, Viewport& vp, SelectionAccess sel) {
    using namespace schema;
    Viewport* v = &vp;

    R.add({"view.selection", "view", {"What is picked", "Qué está seleccionado"},
           {"What the person is pointing at: the point they last clicked on a surface, its body, the shapes they picked (in order), and the mesh region they marked with the lasso (meshRegion: its faces, area, centre, mean normal and box; pass faces to mesh.delete_faces). On a part: the parameter, sketch or operation picked in the Model panel (partItem), and the faces and edges of its solid they picked (type, size, axis; an edge's middle is the point for part.fillet). Use it to act on \"this hole\" or \"these edges\" as they point at them.",
            "El último punto en que la persona hizo clic sobre una superficie, su cuerpo y las formas que ha elegido (en orden). En una pieza: el parámetro, boceto u operación elegido en el panel Modelo (partItem) y las caras y aristas de su sólido que ha elegido (tipo, medida, eje; el middle de una arista es el punto para part.fillet). Sirve para actuar sobre \"este taladro\" o \"estas aristas\" cuando las señala."},
           object({{"name", string({"A selection kept under this name (sel1...); empty: what is picked now", "Una selección guardada con este nombre (sel1...); vacío: lo elegido ahora"}, "")}}), false, true,
           [sel](CommandContext&, const json& in) {
               const std::string name = in.value("name", "");
               return name.empty() ? sel.selection() : sel.saved(name);
           }});

    R.add({"view.save_selection", "view", {"Keep the selection", "Guardar la selección"},
           {"Keeps what is picked now (body, faces, edges, shapes, the mesh region marked with the lasso) under a name, so the person and the agent can refer to it later (\"sel2\"). view.selection with name gives it back.",
            "Guarda lo elegido ahora (cuerpo, caras, aristas, formas, la zona de malla marcada con el lazo) con un nombre, para que la persona y el agente se refieran a ello después («sel2»). view.selection con name lo devuelve."},
           object({{"name", string({"Name (empty: sel1, sel2...)", "Nombre (vacío: sel1, sel2...)"}, "")}}), false, true,
           [sel](CommandContext&, const json& in) { return sel.save(in.value("name", "")); }});

    R.add({"view.pick", "view", {"Pick in the view", "Seleccionar en la vista"},
           {"Click on the view as the person would: x and y from 0 to 1 across and down the view. With add, the shape under it is added to the picked ones.",
            "Hace clic en la vista como lo haría la persona: x e y de 0 a 1, a lo ancho y hacia abajo. Con add, la forma de debajo se añade a las elegidas."},
           object({{"x", number({"Across the view (0 left, 1 right)", "A lo ancho (0 izquierda, 1 derecha)"}, 0.5, 0.0, 1.0)},
                   {"y", number({"Down the view (0 top, 1 bottom)", "Hacia abajo (0 arriba, 1 abajo)"}, 0.5, 0.0, 1.0)},
                   {"add", boolean({"Add to the picked shapes", "Añadir a las formas elegidas"}, false)}}),
           false, true,
           [sel](CommandContext&, const json& in) { return sel.pick(in["x"].get<double>(), in["y"].get<double>(), in["add"].get<bool>()); }});

    R.add({"view.set", "view", {"Set view", "Cambiar vista"},
           {"Turn the view to a standard direction and fit the visible bodies.", "Gira la vista a una dirección estándar y encuadra los cuerpos visibles."},
           object({{"view", choice({"Direction", "Dirección"}, {"iso", "front", "back", "left", "right", "top", "bottom"}, "iso")},
                   {"wireframe", boolean({"Show the mesh edges (the quads' on a quad mesh)", "Mostrar las aristas de la malla (las de los quads en una malla de quads)"}, false)},
                   {"openEdges", boolean({"Show open and non-manifold edges", "Mostrar bordes abiertos y no manifold"}, true)},
                   {"shapes", boolean({"Colour found shapes by region", "Colorear las formas encontradas por región"}, true)}}),
           false, true,
           [v](CommandContext&, const json& in) {
               bool ok = false;
               v->setView(Viewport::viewFromName(QString::fromStdString(in["view"].get<std::string>()), &ok));
               v->setWireframe(in["wireframe"].get<bool>());
               v->setOpenEdges(in["openEdges"].get<bool>());
               v->setShowShapes(in["shapes"].get<bool>());
               return json{{"view", in["view"]}};
           }});

    R.add({"view.capture", "view", {"Capture view", "Capturar vista"},
           {"A picture of the view as it is now (PNG), to look at the part. Use view.set first for a standard direction.",
            "Una imagen de la vista tal como está (PNG), para ver la pieza. Usa antes view.set para una dirección estándar."},
           object({{"width", integer({"Width in pixels (0 = as shown)", "Ancho en píxeles (0 = como se ve)"}, 1024, 0, 4096)},
                   {"height", integer({"Height in pixels (0 = as shown)", "Alto en píxeles (0 = como se ve)"}, 768, 0, 4096)}}),
           false, true,
           [v](CommandContext&, const json& in) {
               const QImage img = v->capture(static_cast<int>(in["width"].get<long long>()), static_cast<int>(in["height"].get<long long>()));
               if (img.isNull()) throw CommandError("unavailable", "The view could not be captured (is the window minimised?).");
               QByteArray png;
               QBuffer buf(&png);
               buf.open(QIODevice::WriteOnly);
               img.save(&buf, "PNG");
               return json{{"image", png.toBase64().toStdString()}, {"mimeType", "image/png"}, {"width", img.width()}, {"height", img.height()}};
           }});
}

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppCommands.h"

#include "app/Viewport.h"

#include <QBuffer>
#include <QImage>

namespace orv {

void registerAppCommands(CommandRegistry& R, Viewport& vp) {
    using namespace schema;
    Viewport* v = &vp;

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

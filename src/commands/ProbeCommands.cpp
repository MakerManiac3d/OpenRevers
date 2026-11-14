// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — commands that measure a scan without building anything from
// it: mesh.cast (chords along rays), mesh.profile (radius along an axis, in
// zones) and mesh.unwrap (what is not round, on a map of angle × position).
#include "commands/ProbeCommands.h"

#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshProbe.h"
#include "doc/Document.h"

#include <cmath>
#include <algorithm>
#include <fstream>
#include <map>

namespace orv {
namespace {

using namespace schema;
namespace fs = std::filesystem;
const Text kBody{"The body: its reference (b1) or its name", "El cuerpo: su referencia (b1) o su nombre"};

double r4(double v) { return std::round(v * 1e4) / 1e4; }
json j3(const Vec3d& v) { return json::array({r4(v.x()), r4(v.y()), r4(v.z())}); }
Vec3d v3(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }

Body scanBody(CommandContext& ctx, const json& ref, bool needFaces) {
    Body b = ctx.doc.body(ref);
    if (b.isPart())
        throw CommandError("invalid_input", ctx.say(b.name + " is a parametric part; this measures scans (use part.compare for a part).",
                                                    b.name + " es una pieza paramétrica; esto mide escaneos (usa part.compare para una pieza)."));
    if (b.mesh->vertices.empty()) throw CommandError("invalid_input", ctx.say(b.name + " is empty.", b.name + " está vacío."));
    if (needFaces && b.mesh->faces.empty())
        throw CommandError("invalid_input", ctx.say(b.name + " has no triangles (a point cloud: reconstruct a mesh first with points.to_mesh).",
                                                    b.name + " no tiene triángulos (es una nube de puntos: reconstruye antes una malla con points.to_mesh)."));
    return b;
}

/// The axis a command measures round: a fitted cylinder or cone (shape), or
/// a point and a direction.
Axis axisOf(CommandContext& ctx, const Body& b, const json& in) {
    Axis a;
    if (in.contains("shape")) {
        const Feature* f = b.features ? b.features->find(in["shape"].get<std::string>()) : nullptr;
        if (!f) throw CommandError("not_found", ctx.say("No shape called " + in["shape"].get<std::string>() + " in " + b.name + ".", "No hay ninguna forma " + in["shape"].get<std::string>() + " en " + b.name + "."));
        if (f->shape.type != PrimitiveType::Cylinder && f->shape.type != PrimitiveType::Cone && f->shape.type != PrimitiveType::Torus)
            throw CommandError("invalid_input", ctx.say(f->name + " has no axis (use a cylinder or a cone).", f->name + " no tiene eje (usa un cilindro o un cono)."));
        a.point = f->shape.origin;
        a.direction = f->shape.axis;
    } else if (in.contains("axisPoint") && in.contains("axisDirection")) {
        a.point = v3(in["axisPoint"]);
        a.direction = v3(in["axisDirection"]);
        if (a.direction.norm() < 1e-12) throw CommandError("invalid_input", ctx.say("The axis needs a direction.", "El eje necesita una dirección."));
    } else {
        throw CommandError("invalid_input", ctx.say("Give the axis: shape (a fitted cylinder or cone) or axisPoint and axisDirection.",
                                                    "Da el eje: shape (un cilindro o cono ajustado) o axisPoint y axisDirection."));
    }
    if (in.contains("reference")) {
        const Vec3d ref = v3(in["reference"]);
        a.normalize(&ref);
    } else {
        a.normalize();
    }
    return a;
}

/// from and to are used only when they make a range (both 0 by default: the scan's whole length).
bool ranged(const json& in) { return in.value("to", 0.0) > in.value("from", 0.0); }

Skin skinOf(const json& in) { return in.value("skin", std::string("outer")) == "inner" ? Skin::Inner : Skin::Outer; }

}  // namespace

void registerProbeCommands(CommandRegistry& R) {
    R.add({"mesh.cast", "mesh", {"Measure along rays", "Medir con rayos"},
           {"Shoot rays through a scan's triangles and list where they cross the surface and the stretches inside material (chords): the thickness of a wall, the diameter of a hole (a ray across it: the gap between two chords), the width of a slot. One ray from origin along direction; with count > 1, more rays parallel to it, each moved step mm along stepDirection from the last (a sweep across a bore or a slot). Needs a closed, outward-facing mesh for the chords; the crossings are listed regardless.",
            "Lanza rayos a través de los triángulos de un escaneo y lista dónde cruzan la superficie y los tramos dentro del material (cuerdas): el espesor de una pared, el diámetro de un agujero (un rayo que lo cruza: el hueco entre dos cuerdas), el ancho de una ranura. Un rayo desde origin en la dirección direction; con count > 1, más rayos paralelos, cada uno desplazado step mm en stepDirection respecto al anterior (un barrido a través de un taladro o una ranura). Para las cuerdas hace falta una malla cerrada con las caras hacia fuera; los cruces se listan en todo caso."},
           object({{"body", body(kBody)}, {"origin", point({"Where the ray starts", "Dónde empieza el rayo"})}, {"direction", point({"Its direction", "Su dirección"}, nullptr)},
                   {"count", integer({"Rays (parallel, one step apart)", "Rayos (paralelos, a un paso uno de otro)"}, 1, 1, 400)},
                   {"step", number({"Distance between rays", "Distancia entre rayos"}, 1.0, 0.0001, 10000.0, "mm")},
                   {"stepDirection", point({"The direction the rays move in", "La dirección en que se mueven los rayos"}, nullptr)},
                   {"maxDistance", number({"How far each ray goes (0: all the way)", "Hasta dónde llega cada rayo (0: todo)"}, 0.0, 0.0, 1e6, "mm")}},
                  {"body", "origin", "direction"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = scanBody(ctx, in["body"], true);
               const Vec3d o = v3(in["origin"]), d = v3(in["direction"]);
               if (d.norm() < 1e-12) throw CommandError("invalid_input", ctx.say("The direction cannot be zero.", "La dirección no puede ser cero."));
               const long long count = in["count"].get<long long>();
               Vec3d sd = Vec3d::Zero();
               if (count > 1) {
                   if (!in.contains("stepDirection")) throw CommandError("invalid_input", ctx.say("count > 1 needs stepDirection.", "count > 1 necesita stepDirection."));
                   sd = v3(in["stepDirection"]).normalized();
               }
               if (static_cast<double>(count) * static_cast<double>(b.mesh->faces.size()) > 3e9)
                   throw CommandError("invalid_input", ctx.say("Too many rays for this mesh: decimate a copy first (body.duplicate, mesh.decimate).", "Demasiados rayos para esta malla: diezma antes una copia (body.duplicate, mesh.decimate)."));
               const double maxT = in["maxDistance"].get<double>() > 0 ? in["maxDistance"].get<double>() : 1e9;
               json rays = json::array();
               for (long long k = 0; k < count; ++k) {
                   const Vec3d ok = o + sd * (in["step"].get<double>() * static_cast<double>(k));
                   const auto hits = castRay(*b.mesh, ok, d, 0.0, maxT);
                   json jh = json::array(), jc = json::array();
                   for (const auto& h : hits) jh.push_back({{"t", r4(h.t)}, {"point", j3(h.point)}, {"entering", h.entering}});
                   for (const auto& c : chordsOf(hits)) jc.push_back({{"from", j3(c.from)}, {"to", j3(c.to)}, {"length", r4(c.length())}});
                   json jr{{"origin", j3(ok)}, {"crossings", jh}, {"chords", jc}};
                   // The gaps between consecutive chords: holes, slots, bores.
                   json gaps = json::array();
                   const auto ch = chordsOf(hits);
                   for (std::size_t i = 0; i + 1 < ch.size(); ++i) gaps.push_back({{"from", j3(ch[i].to)}, {"to", j3(ch[i + 1].from)}, {"length", r4(ch[i + 1].t0 - ch[i].t1)}});
                   jr["gaps"] = gaps;
                   rays.push_back(jr);
               }
               return json{{"body", b.ref()}, {"direction", j3(d.normalized())}, {"rays", rays},
                           {"summary", ctx.say(std::to_string(count) + " ray(s) shot.", std::to_string(count) + " rayo(s) lanzado(s).")}};
           }});

    R.add({"mesh.profile", "mesh", {"Radial profile", "Perfil radial"},
           {"The radius of a scan along an axis (a turned part's profile): bins of `step` mm along the axis with the inner, median and outer radius in each, and, more usefully, the zones: stretches that are one cylinder (constant radius) or one cone (radius changing evenly) within `tolerance`, with their radii and half angle, from the outer (skin outer) or inner (skin inner) surface. The axis is a fitted cylinder or cone (shape) or axisPoint and axisDirection (align.principal first puts the long axis along X: axisPoint [0,0,0], axisDirection [1,0,0]). Positions along the axis are measured from axisPoint. Steps, grooves, flanges and tapers show up as short zones between longer ones; what is not round (slots, flats) does not: see mesh.unwrap.",
            "El radio de un escaneo a lo largo de un eje (el perfil de una pieza torneada): tramos de `step` mm a lo largo del eje con el radio interior, medio y exterior de cada uno y, sobre todo, las zonas: tramos que son un cilindro (radio constante) o un cono (radio que cambia por igual) dentro de `tolerance`, con sus radios y semiángulo, de la superficie exterior (skin outer) o interior (skin inner). El eje es un cilindro o cono ajustado (shape) o axisPoint y axisDirection (align.principal pone antes el eje largo en X: axisPoint [0,0,0], axisDirection [1,0,0]). Las posiciones a lo largo del eje se miden desde axisPoint. Escalones, gargantas, bridas y conicidades salen como zonas cortas entre otras más largas; lo que no es redondo (ranuras, planos) no sale: mira mesh.unwrap."},
           object({{"body", body(kBody)}, {"shape", kind(string({"A fitted cylinder or cone for the axis (f3)", "Un cilindro o cono ajustado para el eje (f3)"}), "feature", "body")},
                   {"axisPoint", point({"A point of the axis", "Un punto del eje"}, nullptr)}, {"axisDirection", point({"The axis direction", "La dirección del eje"}, nullptr)},
                   {"skin", choice({"Which surface: outer or inner (a bore)", "Qué superficie: exterior o interior (un taladro)"}, {"outer", "inner"}, "outer")},
                   {"step", number({"Bin length along the axis", "Longitud de cada tramo a lo largo del eje"}, 0.5, 0.01, 1000.0, "mm")},
                   {"tolerance", number({"How far a zone may stray from a straight line", "Cuánto puede alejarse una zona de una recta"}, 0.15, 0.001, 100.0, "mm")},
                   {"from", number({"Start along the axis (default: the scan's)", "Inicio sobre el eje (por defecto, el del escaneo)"}, 0.0, -1e6, 1e6, "mm")},
                   {"to", number({"End along the axis (default: the scan's)", "Fin sobre el eje (por defecto, el del escaneo)"}, 0.0, -1e6, 1e6, "mm")},
                   {"maxRadius", number({"Ignore points farther from the axis (0: none)", "Ignorar los puntos más lejos del eje (0: ninguno)"}, 0.0, 0.0, 1e6, "mm")},
                   {"includeBins", boolean({"List every bin too", "Listar también cada tramo"}, false)}},
                  {"body"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = scanBody(ctx, in["body"], false);
               const Axis axis = axisOf(ctx, b, in);
               const auto pts = samplePoints(*b.mesh, in["step"].get<double>());
               const double nan = std::numeric_limits<double>::quiet_NaN();
               const auto bins = radialProfile(pts, axis, in["step"].get<double>(), ranged(in) ? in["from"].get<double>() : nan,
                                               ranged(in) ? in["to"].get<double>() : nan, in["maxRadius"].get<double>());
               if (bins.empty()) throw CommandError("failed", ctx.say("Nothing to profile: check the axis, from, to and step.", "No hay nada que perfilar: revisa el eje, from, to y step."));
               const Skin skin = skinOf(in);
               json zones = json::array();
               for (const auto& z : profileZones(bins, skin, in["tolerance"].get<double>()))
                   zones.push_back({{"kind", z.kind}, {"from", r4(z.z0)}, {"to", r4(z.z1)}, {"length", r4(z.z1 - z.z0)}, {"radiusStart", r4(z.rStart)}, {"radiusEnd", r4(z.rEnd)},
                                    {"diameterStart", r4(2 * z.rStart)}, {"diameterEnd", r4(2 * z.rEnd)}, {"halfAngle", r4(z.halfAngleDeg)}, {"maxResidual", r4(z.maxResidual)}});
               json out{{"body", b.ref()}, {"axis", {{"point", j3(axis.point)}, {"direction", j3(axis.direction)}}}, {"skin", skin == Skin::Outer ? "outer" : "inner"},
                        {"bins", bins.size()}, {"zones", zones}};
               if (in["includeBins"].get<bool>()) {
                   json jb = json::array();
                   for (const auto& x : bins)
                       if (x.count) jb.push_back({{"z", r4(0.5 * (x.z0 + x.z1))}, {"count", x.count}, {"inner", r4(x.rInner)}, {"median", r4(x.rMedian)}, {"outer", r4(x.rOuter)}});
                   out["profile"] = jb;
               }
               out["summary"] = ctx.say(std::to_string(zones.size()) + " zones.", std::to_string(zones.size()) + " zonas.");
               return out;
           }});

    R.add({"mesh.unwrap", "mesh", {"Unwrapped map", "Mapa desenrollado"},
           {"What is not round about an axis: the scan's radius on a map of angle × position along the axis (the surface cut along a line and laid flat), and the patches where the radius differs from the rest of its row by more than `threshold`: a slot or flat (the radius is smaller, deviation < 0 on the outer skin), a boss or a rib (larger). Each patch gives its position along the axis, its angular span (degrees, from `reference`, counterclockwise seen from the tip of the axis; it may pass 360), its mean and worst deviation and the radius of the rest of those rows. The axis is as in mesh.profile. With `path`, the whole map is written as CSV (a row per position, a column per angle, radius in mm) to look at in a spreadsheet.",
            "Lo que no es redondo alrededor de un eje: el radio del escaneo en un mapa de ángulo × posición a lo largo del eje (la superficie cortada por una línea y puesta plana), y las zonas donde el radio difiere del resto de su fila en más de `threshold`: una ranura o un plano (el radio es menor, deviation < 0 en la superficie exterior), un saliente o una nervadura (mayor). Cada zona da su posición a lo largo del eje, su extensión angular (grados, desde `reference`, en sentido antihorario visto desde la punta del eje; puede pasar de 360), su desviación media y peor y el radio del resto de esas filas. El eje es como en mesh.profile. Con `path`, el mapa entero se escribe como CSV (una fila por posición, una columna por ángulo, radio en mm) para verlo en una hoja de cálculo."},
           object({{"body", body(kBody)}, {"shape", kind(string({"A fitted cylinder or cone for the axis (f3)", "Un cilindro o cono ajustado para el eje (f3)"}), "feature", "body")},
                   {"axisPoint", point({"A point of the axis", "Un punto del eje"}, nullptr)}, {"axisDirection", point({"The axis direction", "La dirección del eje"}, nullptr)},
                   {"reference", point({"Where angle 0 is (a direction across the axis)", "Dónde está el ángulo 0 (una dirección perpendicular al eje)"}, nullptr)},
                   {"skin", choice({"Which surface: outer or inner (a bore)", "Qué superficie: exterior o interior (un taladro)"}, {"outer", "inner"}, "outer")},
                   {"angles", integer({"Columns round the axis", "Columnas alrededor del eje"}, 180, 8, 1440)},
                   {"step", number({"Row length along the axis", "Longitud de cada fila a lo largo del eje"}, 0.5, 0.01, 1000.0, "mm")},
                   {"threshold", number({"Deviation that counts as a feature", "Desviación que cuenta como un rasgo"}, 0.3, 0.001, 100.0, "mm")},
                   {"minCells", integer({"Smallest patch to report, in cells", "Zona más pequeña que informar, en celdas"}, 6, 1, 100000)},
                   {"from", number({"Start along the axis (default: the scan's)", "Inicio sobre el eje (por defecto, el del escaneo)"}, 0.0, -1e6, 1e6, "mm")},
                   {"to", number({"End along the axis (default: the scan's)", "Fin sobre el eje (por defecto, el del escaneo)"}, 0.0, -1e6, 1e6, "mm")},
                   {"maxRadius", number({"Ignore points farther from the axis (0: none)", "Ignorar los puntos más lejos del eje (0: ninguno)"}, 0.0, 0.0, 1e6, "mm")},
                   {"path", path({"Write the map here as CSV (optional)", "Escribir aquí el mapa como CSV (opcional)"}, true, "CSV (*.csv)")},
                   {"overwrite", boolean({"Replace the file if it exists", "Reemplazar el archivo si existe"}, false)}},
                  {"body"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = scanBody(ctx, in["body"], false);
               const Axis axis = axisOf(ctx, b, in);
               const auto pts = samplePoints(*b.mesh, in["step"].get<double>());
               const double nan = std::numeric_limits<double>::quiet_NaN();
               const Unwrapped u = unwrap(pts, axis, skinOf(in), static_cast<int>(in["angles"].get<long long>()), in["step"].get<double>(),
                                          ranged(in) ? in["from"].get<double>() : nan, ranged(in) ? in["to"].get<double>() : nan, in["maxRadius"].get<double>());
               if (u.nAngle == 0) throw CommandError("failed", ctx.say("Nothing to unwrap: check the axis, from, to, step and angles (at most 4000 rows, 4 million cells).",
                                                                       "No hay nada que desenrollar: revisa el eje, from, to, step y angles (como mucho 4000 filas y 4 millones de celdas)."));
               const auto devs = deviations(u, in["threshold"].get<double>(), static_cast<std::size_t>(in["minCells"].get<long long>()));
               json list = json::array();
               for (std::size_t i = 0; i < devs.size() && i < 60; ++i) {
                   const auto& d = devs[i];
                   list.push_back({{"from", r4(d.z0)}, {"to", r4(d.z1)}, {"angleFrom", r4(d.angle0)}, {"angleTo", r4(d.angle1)}, {"angularSpan", r4(d.angle1 - d.angle0)},
                                   {"meanDeviation", r4(d.meanDeviation)}, {"worstDeviation", r4(d.worstDeviation)}, {"rowRadius", r4(d.rowRadius)}, {"cells", d.cells},
                                   {"kind", d.meanDeviation < 0 ? (skinOf(in) == Skin::Outer ? "recess" : "boss") : (skinOf(in) == Skin::Outer ? "boss" : "recess")}});
               }
               std::size_t round = 0, valid = 0;
               for (int r = 0; r < u.nAlong; ++r)
                   for (int c = 0; c < u.nAngle; ++c) {
                       const double x = u.at(r, c);
                       if (std::isnan(x)) continue;
                       ++valid;
                       if (!std::isnan(u.rowMedian[static_cast<std::size_t>(r)]) && std::abs(x - u.rowMedian[static_cast<std::size_t>(r)]) <= in["threshold"].get<double>()) ++round;
                   }
               json out{{"body", b.ref()}, {"axis", {{"point", j3(axis.point)}, {"direction", j3(axis.direction)}, {"reference", j3(axis.reference)}}},
                        {"rows", u.nAlong}, {"columns", u.nAngle}, {"emptyCells", u.empty}, {"roundFraction", r4(valid ? static_cast<double>(round) / static_cast<double>(valid) : 0)},
                        {"patches", list}, {"patchesFound", devs.size()}};
               if (in.contains("path")) {
                   const fs::path p = ctx.paths.check(in["path"].get<std::string>(), true);
                   if (fs::exists(p) && !in["overwrite"].get<bool>())
                       throw CommandError("refused", ctx.say(p.string() + " already exists; pass overwrite: true to replace it.", p.string() + " ya existe; usa overwrite: true para reemplazarlo."));
                   std::ofstream f(p);
                   if (!f) throw CommandError("failed", ctx.say("Could not write " + p.string() + ".", "No se pudo escribir " + p.string() + "."));
                   f << "position";
                   for (int c = 0; c < u.nAngle; ++c) f << ',' << r4((c + 0.5) * 360.0 / u.nAngle);
                   f << '\n';
                   for (int r = 0; r < u.nAlong; ++r) {
                       f << r4(u.z0 + (r + 0.5) * u.dz);
                       for (int c = 0; c < u.nAngle; ++c) {
                           const double x = u.at(r, c);
                           f << ',';
                           if (!std::isnan(x)) f << r4(x);
                       }
                       f << '\n';
                   }
                   out["path"] = p.string();
               }
               out["summary"] = ctx.say(std::to_string(devs.size()) + " patches off the round.", std::to_string(devs.size()) + " zonas fuera de lo redondo.");
               return out;
           }});

    R.add({"mesh.pieces", "mesh", {"Find the pieces", "Encontrar los trozos"},
           {"Lists the separate pieces of a scan (parts of the mesh not joined by any triangle: several parts scanned together, a fixture, a loose chunk): faces, area, volume when closed, size and centre, largest first. "
            "With split, each piece of at least minFaces faces becomes its own body (named after the scan), so each can be aligned and compared on its own.",
            "Lista los trozos separados de un escaneo (partes de la malla que ningún triángulo une: varias piezas escaneadas juntas, una sujeción, un pedazo suelto): caras, área, volumen si está cerrado, tamaño y centro, de mayor a menor. "
            "Con split, cada trozo de al menos minFaces caras pasa a ser un cuerpo propio (con el nombre del escaneo), para alinear y comparar cada uno por separado."},
           object({{"body", body(kBody)},
                   {"minFaces", integer({"Ignore pieces with fewer faces", "Ignorar los trozos con menos caras"}, 100, 1, 100000000)},
                   {"split", boolean({"Make each piece its own body", "Hacer de cada trozo un cuerpo propio"}, false)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = scanBody(ctx, in["body"], true);
               const TriMesh& m = *b.mesh;
               std::vector<std::uint32_t> comp;
               const std::size_t n = faceComponents(m, comp);
               const auto minFaces = static_cast<std::size_t>(in["minFaces"].get<long long>());
               std::vector<std::size_t> count(n, 0);
               std::vector<double> areaOf(n, 0.0);
               for (std::size_t f = 0; f < comp.size(); ++f) {
                   ++count[comp[f]];
                   const auto& t = m.faces[f];
                   areaOf[comp[f]] += 0.5 * (m.vertices[t[1]] - m.vertices[t[0]]).cross(m.vertices[t[2]] - m.vertices[t[0]]).cast<double>().norm();
               }
               std::vector<std::uint32_t> order;
               for (std::uint32_t c = 0; c < n; ++c)
                   if (count[c] >= minFaces) order.push_back(c);
               std::sort(order.begin(), order.end(), [&](auto a, auto c) { return areaOf[a] > areaOf[c]; });
               if (order.size() > 200) order.resize(200);
               json list = json::array();
               int made = 0;
               for (std::size_t k = 0; k < order.size(); ++k) {
                   const std::uint32_t c = order[k];
                   auto sub = std::make_shared<TriMesh>();
                   std::map<std::uint32_t, std::uint32_t> remap;
                   for (std::size_t f = 0; f < m.faces.size(); ++f) {
                       if (comp[f] != c) continue;
                       Tri t = m.faces[f];
                       for (auto& v : t) {
                           auto [it, fresh] = remap.emplace(v, static_cast<std::uint32_t>(sub->vertices.size()));
                           if (fresh) sub->vertices.push_back(m.vertices[v]);
                           v = it->second;
                       }
                       sub->faces.push_back(t);
                   }
                   const Box3 box = sub->bounds();
                   json j{{"index", k + 1}, {"faces", count[c]}, {"area", r4(sub->area())}, {"size", j3(box.size().cast<double>())}, {"centre", j3(box.center().cast<double>())}};
                   if (const double vol = sub->signedVolume(); vol > 0) j["volume"] = r4(vol);
                   if (in["split"].get<bool>() && order.size() > 1) {
                       const Body nb = ctx.doc.addBody(b.name + " " + std::to_string(k + 1), sub, b.source);
                       j["body"] = nb.ref();
                       ++made;
                   }
                   list.push_back(j);
               }
               json out{{"body", b.ref()}, {"pieces", list}, {"total", n}};
               out["summary"] = ctx.say(std::to_string(order.size()) + " pieces" + (made ? ", " + std::to_string(made) + " bodies made." : "."),
                                        std::to_string(order.size()) + " trozos" + (made ? ", " + std::to_string(made) + " cuerpos creados." : "."));
               return out;
           }});
}

}  // namespace orv

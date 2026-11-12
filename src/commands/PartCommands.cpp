// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — commands on parametric parts: a new part, sketches, extrude,
// revolve, fillet, parameters, editing and deleting in the history, STEP in
// and out, comparing a part with the scan; and cutting a mesh with a plane
// (the outline a sketch is drawn from).
#include "commands/Command.h"
#include "commands/PartCommands.h"

#include "core/mesh/Distance.h"
#include "core/solid/BSpline.h"
#include "core/solid/SketchFit.h"
#include "doc/Document.h"

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>

namespace orv {
namespace {

namespace fs = std::filesystem;
using namespace schema;

const Text kBody{"The body: its reference (b1) or its name", "El cuerpo: su referencia (b1) o su nombre"};
const Text kPart{"The part: its reference (b1) or its name", "La pieza: su referencia (b1) o su nombre"};

double r4(double v) { return std::round(v * 1e4) / 1e4; }
json j3(const Vec3d& v) { return json::array({r4(v.x()), r4(v.y()), r4(v.z())}); }
json j2(const Vec2d& v) { return json::array({r4(v.x()), r4(v.y())}); }
Vec3d v3(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }

/// A number or a parameter's name.
json valueSchema(const Text& d, double def) {
    return json{{"type", json::array({"number", "string"})}, {"default", def}, {"x-unit", "mm"}, {"description", d.en}, {"x-es", d.es}};
}
json listSchema(const Text& d) { return json{{"type", "array"}, {"description", d.en}, {"x-es", d.es}}; }
json objectSchema(const Text& d) { return json{{"type", "object"}, {"description", d.en}, {"x-es", d.es}}; }

Body partBody(CommandContext& ctx, const json& ref) {
    Body b = ctx.doc.body(ref);
    if (!b.isPart())
        throw CommandError("invalid_input", ctx.say(b.name + " is a mesh, not a parametric part; start one with part.new.",
                                                    b.name + " es una malla, no una pieza paramétrica; empieza una con part.new."));
    return b;
}

json partJson(const Body& b) {
    const Model& m = *b.model;
    json sketches = json::array(), ops = json::array();
    for (const auto& s : m.sketches) {
        std::size_t segs = 0;
        for (const auto& l : s.profile.loops) segs += l.segments.size();
        sketches.push_back({{"ref", s.ref()}, {"name", s.name}, {"loops", s.profile.loops.size()}, {"segments", segs},
                            {"plane", {{"origin", j3(s.profile.plane.origin)}, {"normal", j3(s.profile.plane.normal)}, {"xDir", j3(s.profile.plane.xDir)}}}});
    }
    for (const auto& o : m.operations) {
        json jo{{"ref", o.ref()}, {"name", o.name}, {"kind", opKindName(o.kind)}, {"mode", opModeName(o.mode)}, {"suppressed", o.suppressed}};
        auto sketchRef = [&](std::uint64_t id) { return "s" + std::to_string(id); };
        switch (o.kind) {
            case OpKind::Extrude: jo["sketch"] = sketchRef(o.sketch); jo["start"] = valueToJson(o.start); jo["end"] = valueToJson(o.end); break;
            case OpKind::Revolve: jo["sketch"] = sketchRef(o.sketch); jo["angle"] = valueToJson(o.angle); jo["axisPoint"] = j2(o.axisPoint); jo["axisDir"] = j2(o.axisDir); break;
            case OpKind::Fillet: jo["edges"] = o.edgePoints.size(); jo["radius"] = valueToJson(o.radius); break;
            case OpKind::Import: break;
            case OpKind::Loft: {
                json secs = json::array();
                for (auto id : o.sections) secs.push_back(sketchRef(id));
                jo["sections"] = secs;
                jo["ruled"] = o.ruled;
                break;
            }
            case OpKind::Pattern:
                jo["source"] = "o" + std::to_string(o.source);
                jo["axisOrigin"] = j3(o.axisOrigin);
                jo["axisDirection"] = j3(o.axisDirection);
                jo["count"] = valueToJson(o.count);
                jo["angle"] = valueToJson(o.angle);
                break;
            case OpKind::SurfaceCut: jo["surface"] = "sf" + std::to_string(o.surface); jo["keep"] = o.keepBelow ? "below" : "above"; break;
            case OpKind::Thicken: jo["surface"] = "sf" + std::to_string(o.surface); jo["thickness"] = valueToJson(o.thickness); break;
        }
        ops.push_back(jo);
    }
    json surfaces = json::array();
    for (const auto& f : m.surfaces)
        surfaces.push_back({{"ref", f.ref()}, {"name", f.name}, {"poles", {f.surface.nu, f.surface.nv}}, {"rms", r4(f.rms)}, {"maxDeviation", r4(f.maxDeviation)},
                            {"normal", j3(f.surface.normal)}, {"size", {r4(f.surface.width), r4(f.surface.height)}}});
    json out{{"body", b.ref()}, {"name", b.name}, {"parameters", m.parameters}, {"sketches", sketches}, {"operations", ops}};
    if (!m.surfaces.empty()) out["surfaces"] = surfaces;
    if (!b.solid.empty()) {
        const auto bb = b.solid.bounds();
        out["solid"] = {{"volume", r4(b.solid.volume())}, {"area", r4(b.solid.area())}, {"faces", b.solid.faceCount()}, {"solids", b.solid.solidCount()},
                        {"bounds", {{"min", j3(bb.min())}, {"max", j3(bb.max())}, {"size", j3(bb.max() - bb.min())}}}};
    }
    if (!b.buildError.empty()) out["buildError"] = b.buildError;
    return out;
}

/// Rebuilds the model and puts it on the body. An edit that breaks the part
/// is refused with the reason, and nothing changes.
Body applyModel(CommandContext& ctx, const Body& b, std::shared_ptr<Model> m) {
    BuildResult r = rebuild(*m);
    if (!r.error.empty())
        throw CommandError("failed", ctx.say("The part cannot be rebuilt: " + r.error + ". Nothing was changed.",
                                             "La pieza no se puede reconstruir: " + r.error + ". No se ha cambiado nada."));
    ctx.doc.setPart(b.id, m, r.solid, partTriangles(r.solid));
    return ctx.doc.body(b.ref());
}

const Sketch& findSketch(CommandContext& ctx, const Model& m, const json& ref) {
    const Sketch* s = m.sketch(ref.get<std::string>());
    if (!s) throw CommandError("not_found", ctx.say("No sketch \"" + ref.get<std::string>() + "\" in the part.", "No hay ningún boceto \"" + ref.get<std::string>() + "\" en la pieza."));
    return *s;
}

Operation& findOperation(CommandContext& ctx, Model& m, const json& ref) {
    for (auto& o : m.operations)
        if (o.ref() == ref.get<std::string>() || o.name == ref.get<std::string>()) return o;
    throw CommandError("not_found", ctx.say("No operation \"" + ref.get<std::string>() + "\" in the part.", "No hay ninguna operación \"" + ref.get<std::string>() + "\" en la pieza."));
}

std::string uniqueName(const Model& m, const std::string& base) {
    for (int i = 1;; ++i) {
        const std::string n = base + " " + std::to_string(i);
        bool used = false;
        for (const auto& s : m.sketches) used |= s.name == n;
        for (const auto& o : m.operations) used |= o.name == n;
        for (const auto& f : m.surfaces) used |= f.name == n;
        if (!used) return n;
    }
}

Profile readProfile(CommandContext& ctx, const json& in) {
    try {
        json j = {{"loops", in.at("loops")}};
        if (in.contains("plane")) j["plane"] = in["plane"];
        Profile p = profileFromJson(j);
        if (const auto why = p.problem(); !why.empty()) throw std::invalid_argument(why);
        return p;
    } catch (const std::exception& e) {
        throw CommandError("invalid_input", ctx.say(std::string("The sketch is not usable: ") + e.what() + ".", std::string("El boceto no sirve: ") + e.what() + "."));
    }
}

const Text kLoops{
    "Closed loops in the plane's coordinates (x along xDir, y along normal × xDir): each a list of segments, {type: line, from: [x, y], to: [x, y]}, "
    "{type: arc, from, to, centre: [x, y], ccw: true}, or one {type: circle, centre, radius}. Each segment starts where the previous one ended. "
    "A loop inside another is a hole.",
    "Lazos cerrados en las coordenadas del plano (x a lo largo de xDir, y de normal × xDir): cada uno, una lista de segmentos, {type: line, from: [x, y], to: [x, y]}, "
    "{type: arc, from, to, centre: [x, y], ccw: true}, o un único {type: circle, centre, radius}. Cada segmento empieza donde acabó el anterior. "
    "Un lazo dentro de otro es un agujero."};
const Text kPlane{"The sketch plane: {origin: [x, y, z], normal: [x, y, z], xDir: [x, y, z]} (default: XY at the origin)",
                  "El plano del boceto: {origin: [x, y, z], normal: [x, y, z], xDir: [x, y, z]} (por defecto, XY en el origen)"};
const Text kMode{"How it combines with the part: new (the first one), join, cut or intersect",
                 "Cómo se combina con la pieza: new (la primera), join (unir), cut (cortar) o intersect (intersecar)"};

}  // namespace

void registerPartCommands(CommandRegistry& R) {
    R.add({"part.new", "part", {"New part", "Nueva pieza"},
           {"Start an empty parametric part: then add sketches and operations (extrude, revolve, fillet) to it.",
            "Empieza una pieza paramétrica vacía; después se le añaden bocetos y operaciones (extruir, revolución, redondeo)."},
           object({{"name", string({"Name", "Nombre"})}}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.addPart(in.contains("name") ? in["name"].get<std::string>() : ctx.say("Part", "Pieza"), std::make_shared<Model>(), Solid{},
                                              partTriangles(Solid{}));
               json out = partJson(b);
               out["summary"] = ctx.say("New part " + b.name + " (" + b.ref() + ").", "Nueva pieza " + b.name + " (" + b.ref() + ").");
               return out;
           }});

    R.add({"part.sketch", "part", {"Add a sketch", "Añadir boceto"},
           {"Add a sketch to a part: closed loops of lines, arcs and circles on a plane. It makes no solid by itself; extrude or revolve it.",
            "Añade un boceto a una pieza: lazos cerrados de rectas, arcos y círculos en un plano. Por sí solo no hace sólido; hay que extruirlo o revolucionarlo."},
           object({{"body", body(kPart)}, {"name", string({"Name", "Nombre"})}, {"plane", objectSchema(kPlane)}, {"loops", listSchema(kLoops)}}, {"body", "loops"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               Sketch s;
               s.id = m->nextId++;
               s.profile = readProfile(ctx, in);
               s.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Sketch", "Boceto"));
               m->sketches.push_back(s);
               ctx.doc.setPart(b.id, m, b.solid, PartTriangles{b.mesh, b.view}, b.buildError);
               double area = 0;
               for (const auto& l : s.profile.loops) area += l.area();
               return json{{"body", b.ref()}, {"ref", s.ref()}, {"name", s.name}, {"loops", s.profile.loops.size()},
                           {"summary", ctx.say("Added " + s.name + " (" + s.ref() + ") with " + std::to_string(s.profile.loops.size()) + " loops.",
                                               "Añadido " + s.name + " (" + s.ref() + ") con " + std::to_string(s.profile.loops.size()) + " lazos.")}};
           }});

    R.add({"part.sketch_from_section", "part", {"Sketch from a section", "Boceto desde una sección"},
           {"Cut a scan (a mesh body) with a plane and turn each closed outline into a loop of lines and arcs within 'tolerance' (a whole circle when it is one; sharp corners where lines meet; arcs tangent to the lines they touch), added to a part as a sketch. Returns every segment's size, to check against the drawing or the person's measurements before extruding.",
            "Corta un escaneo (un cuerpo de malla) con un plano y convierte cada contorno cerrado en un lazo de rectas y arcos a menos de 'tolerance' (un círculo entero cuando lo es; esquinas vivas donde se cruzan rectas; arcos tangentes a las rectas que tocan), y lo añade a una pieza como boceto. Devuelve la medida de cada segmento, para compararla con el plano o con las medidas de la persona antes de extruir."},
           object({{"body", body(kPart)}, {"scan", body({"The scan (a mesh body)", "El escaneo (un cuerpo de malla)"})},
                   {"origin", point({"A point of the plane", "Un punto del plano"})}, {"normal", point({"The plane's normal", "La normal del plano"}, nullptr)},
                   {"xDir", point({"The sketch's x direction (default: any in the plane)", "La dirección x del boceto (por defecto, cualquiera del plano)"}, nullptr)},
                   {"tolerance", number({"Largest distance from the section", "Distancia máxima a la sección"}, 0.05, 0.001, 5.0, "mm")},
                   {"minSegment", number({"Shortest segment", "Segmento más corto"}, 0.3, 0.0, 100.0, "mm")},
                   {"minLoop", number({"Ignore closed outlines shorter than", "Ignorar contornos cerrados más cortos que"}, 1.0, 0.0, 10000.0, "mm")},
                   {"splines", boolean({"Free-form runs (an organic outline the fit breaks into many smooth arcs) as splines", "Tramos libres (un contorno orgánico que el ajuste rompe en muchos arcos suaves) como splines"}, false)},
                   {"name", string({"Name", "Nombre"})}},
                  {"body", "scan", "origin", "normal"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               const Body scan = ctx.doc.body(in["scan"]);
               if (scan.mesh->faces.empty()) throw CommandError("invalid_input", ctx.say(scan.name + " has no triangles.", scan.name + " no tiene triángulos."));
               const Vec3d o = v3(in["origin"]), n = v3(in["normal"]);
               if (n.norm() < 1e-12) throw CommandError("invalid_input", ctx.say("The normal cannot be zero.", "La normal no puede ser cero."));
               Profile prof;
               prof.plane.origin = o;
               prof.plane.normal = n;
               prof.plane.xDir = in.contains("xDir") ? v3(in["xDir"]) : n.unitOrthogonal();
               prof.plane.normalize();
               SketchFitOptions fo;
               fo.tolerance = in["tolerance"].get<double>();
               fo.minSegment = in["minSegment"].get<double>();
               fo.splines = in["splines"].get<bool>();
               const auto polys = sectionMesh(*scan.mesh, prof.plane.origin, prof.plane.normal, prof.plane.xDir);
               json loops = json::array();
               std::size_t open = 0;
               for (const auto& pl : polys) {
                   if (!pl.closed) { ++open; continue; }
                   double len = 0;
                   for (std::size_t i = 0; i < pl.points.size(); ++i) len += (pl.points[(i + 1) % pl.points.size()] - pl.points[i]).norm();
                   if (len < in["minLoop"].get<double>()) continue;
                   const LoopFit f = fitLoop(pl.points, fo);
                   if (f.loop.segments.empty()) continue;
                   json segs = json::array();
                   for (const auto& sg : f.loop.segments) {
                       json js{{"type", segmentKindName(sg.kind)}};
                       if (sg.kind == Segment::Kind::Circle) { js["centre"] = j2(sg.centre); js["diameter"] = r4(2 * sg.radius); }
                       else if (sg.kind == Segment::Kind::Spline) {
                           js["from"] = j2(sg.a);
                           js["to"] = j2(sg.b);
                           js["length"] = r4(sg.length());
                           js["poles"] = sg.poles.size();
                       }
                       else {
                           js["from"] = j2(sg.a);
                           js["to"] = j2(sg.b);
                           js["length"] = r4(sg.length());
                           if (sg.kind == Segment::Kind::Arc) { js["centre"] = j2(sg.centre); js["radius"] = r4(sg.radius); js["ccw"] = sg.ccw; }
                       }
                       segs.push_back(js);
                   }
                   loops.push_back({{"segments", segs}, {"maxDeviation", r4(f.maxDeviation)}, {"area", r4(std::abs(f.loop.area()))}});
                   prof.loops.push_back(f.loop);
               }
               if (prof.loops.empty())
                   throw CommandError("failed", ctx.say("The plane cuts no closed outline of " + scan.name + " (" + std::to_string(open) + " open ones: is the scan closed at that height?).",
                                                        "El plano no corta ningún contorno cerrado de " + scan.name + " (" + std::to_string(open) + " abiertos: ¿está cerrado el escaneo a esa altura?)."));
               auto m = std::make_shared<Model>(*b.model);
               Sketch sk;
               sk.id = m->nextId++;
               sk.profile = prof;
               sk.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Sketch", "Boceto"));
               m->sketches.push_back(sk);
               ctx.doc.setPart(b.id, m, b.solid, PartTriangles{b.mesh, b.view}, b.buildError);
               return json{{"body", b.ref()}, {"ref", sk.ref()}, {"name", sk.name}, {"plane", {{"origin", j3(prof.plane.origin)}, {"normal", j3(prof.plane.normal)}, {"xDir", j3(prof.plane.xDir)}}},
                           {"loops", loops}, {"openOutlinesSkipped", open},
                           {"summary", ctx.say("Added " + sk.name + " (" + sk.ref() + ") with " + std::to_string(prof.loops.size()) + " loops from the section.",
                                               "Añadido " + sk.name + " (" + sk.ref() + ") con " + std::to_string(prof.loops.size()) + " lazos de la sección.")}};
           }});

    R.add({"part.edit_sketch", "part", {"Edit a sketch", "Editar boceto"},
           {"Replace a sketch's loops, move its plane or rename it (each optional). The operations that use it are rebuilt; if the part then fails, nothing changes.",
            "Reemplaza los lazos de un boceto, mueve su plano o le cambia el nombre (cada cosa, opcional). Se reconstruyen las operaciones que lo usan; si la pieza falla, no cambia nada."},
           object({{"body", body(kPart)}, {"sketch", string({"The sketch: s1 or its name", "El boceto: s1 o su nombre"})}, {"plane", objectSchema(kPlane)}, {"loops", listSchema(kLoops)},
                   {"name", string({"New name", "Nombre nuevo"})}},
                  {"body", "sketch"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const auto id = findSketch(ctx, *m, in["sketch"]).id;
               for (auto& s : m->sketches) {
                   if (s.id != id) continue;
                   if (in.contains("loops")) {
                       Profile p = readProfile(ctx, in);
                       if (!in.contains("plane")) p.plane = s.profile.plane;
                       s.profile = p;
                   } else if (in.contains("plane")) {
                       Profile p = readProfile(ctx, json{{"loops", profileToJson(s.profile)["loops"]}, {"plane", in["plane"]}});
                       s.profile = p;
                   }
                   if (in.contains("name")) {
                       const std::string n = in["name"];
                       if (n.empty()) throw CommandError("invalid_input", ctx.say("A sketch needs a name.", "Un boceto necesita nombre."));
                       s.name = n;
                   }
               }
               bool used = false;
               for (const auto& o : m->operations) used |= (o.kind == OpKind::Extrude || o.kind == OpKind::Revolve) && o.sketch == id;
               json out = partJson(used ? applyModel(ctx, b, m) : (ctx.doc.setPart(b.id, m, b.solid, PartTriangles{b.mesh, b.view}, b.buildError), ctx.doc.body(b.ref())));
               out["summary"] = ctx.say(used ? "Sketch changed and the part rebuilt." : "Sketch changed.", used ? "Boceto cambiado y pieza reconstruida." : "Boceto cambiado.");
               return out;
           }});

    R.add({"part.extrude", "part", {"Extrude", "Extruir"},
           {"Push a sketch's loops along its plane's normal, from start to end (distances along the normal; negative goes the other way), and combine the result with the part. Values may be numbers or parameter names.",
            "Empuja los lazos de un boceto a lo largo de la normal de su plano, de start a end (distancias sobre la normal; en negativo, al otro lado), y combina el resultado con la pieza. Los valores pueden ser números o nombres de parámetros."},
           object({{"body", body(kPart)}, {"sketch", string({"The sketch: s1 or its name", "El boceto: s1 o su nombre"})},
                   {"start", valueSchema({"From", "Desde"}, 0.0)}, {"end", valueSchema({"To", "Hasta"}, 10.0)},
                   {"mode", choice(kMode, {"new", "join", "cut", "intersect"}, "join")}, {"name", string({"Name", "Nombre"})}},
                  {"body", "sketch", "end"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Extrude;
               o.sketch = findSketch(ctx, *m, in["sketch"]).id;
               o.start = valueFromJson(in["start"]);
               o.end = valueFromJson(in["end"]);
               o.mode = *opModeFromName(in["mode"].get<std::string>());
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, o.mode == OpMode::Cut ? ctx.say("Cut", "Corte") : ctx.say("Extrude", "Extrusión"));
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadida " + o.name + ".");
               return out;
           }});

    R.add({"part.revolve", "part", {"Revolve", "Revolución"},
           {"Turn a sketch's loops round an axis in its plane (axisPoint and axisDir, as [x, y] in the sketch) by angle degrees, and combine the result with the part.",
            "Gira los lazos de un boceto alrededor de un eje de su plano (axisPoint y axisDir, como [x, y] del boceto) angle grados, y combina el resultado con la pieza."},
           object({{"body", body(kPart)}, {"sketch", string({"The sketch: s1 or its name", "El boceto: s1 o su nombre"})},
                   {"axisPoint", listSchema({"A point of the axis, [x, y] in the sketch", "Un punto del eje, [x, y] en el boceto"})},
                   {"axisDir", listSchema({"The axis direction, [x, y] in the sketch", "La dirección del eje, [x, y] en el boceto"})},
                   {"angle", valueSchema({"Angle (degrees)", "Ángulo (grados)"}, 360.0)},
                   {"mode", choice(kMode, {"new", "join", "cut", "intersect"}, "join")}, {"name", string({"Name", "Nombre"})}},
                  {"body", "sketch", "axisPoint", "axisDir"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               auto v2 = [&](const json& j, const char* what) {
                   if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number())
                       throw CommandError("invalid_input", std::string(what) + ctx.say(" must be [x, y]", " debe ser [x, y]"));
                   return Vec2d(j[0].get<double>(), j[1].get<double>());
               };
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Revolve;
               o.sketch = findSketch(ctx, *m, in["sketch"]).id;
               o.axisPoint = v2(in["axisPoint"], "axisPoint");
               o.axisDir = v2(in["axisDir"], "axisDir");
               o.angle = valueFromJson(in["angle"]);
               o.mode = *opModeFromName(in["mode"].get<std::string>());
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Revolve", "Revolución"));
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadida " + o.name + ".");
               return out;
           }});

    R.add({"part.fillet", "part", {"Fillet edges", "Redondear aristas"},
           {"Round edges of the part with a radius. Each edge is picked by a point on it, [x, y, z] (within 0.05 mm).",
            "Redondea aristas de la pieza con un radio. Cada arista se elige con un punto sobre ella, [x, y, z] (a menos de 0,05 mm)."},
           object({{"body", body(kPart)}, {"points", listSchema({"Points on the edges, [[x, y, z], ...]", "Puntos sobre las aristas, [[x, y, z], ...]"})},
                   {"radius", valueSchema({"Radius", "Radio"}, 1.0)}, {"name", string({"Name", "Nombre"})}},
                  {"body", "points", "radius"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Fillet;
               for (const auto& p : in["points"]) {
                   if (!p.is_array() || p.size() != 3) throw CommandError("invalid_input", ctx.say("Each point must be [x, y, z].", "Cada punto debe ser [x, y, z]."));
                   o.edgePoints.push_back(v3(p));
               }
               o.radius = valueFromJson(in["radius"]);
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Fillet", "Redondeo"));
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadido " + o.name + ".");
               return out;
           }});

    R.add({"part.parameter", "part", {"Set a parameter", "Fijar un parámetro"},
           {"Create or change a named value of the part (a thickness, a hole's diameter...). Operations that use it by name are rebuilt.",
            "Crea o cambia un valor con nombre de la pieza (un espesor, el diámetro de un taladro...). Se reconstruyen las operaciones que lo usan por su nombre."},
           object({{"body", body(kPart)}, {"name", string({"Parameter name", "Nombre del parámetro"})}, {"value", number({"Value", "Valor"}, 0.0, -1e6, 1e6, "mm")}},
                  {"body", "name", "value"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const std::string name = in["name"];
               if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])))
                   throw CommandError("invalid_input", ctx.say("A parameter's name must start with a letter.", "El nombre de un parámetro debe empezar por una letra."));
               m->parameters[name] = in["value"].get<double>();
               json out = partJson(m->operations.empty() ? (ctx.doc.setPart(b.id, m, b.solid, PartTriangles{b.mesh, b.view}, b.buildError), ctx.doc.body(b.ref())) : applyModel(ctx, b, m));
               out["summary"] = ctx.say(name + " = " + std::to_string(in["value"].get<double>()) + ".", name + " = " + std::to_string(in["value"].get<double>()) + ".");
               return out;
           }});

    R.add({"part.edit", "part", {"Edit an operation", "Editar una operación"},
           {"Change an operation's values (start, end, angle, radius: numbers or parameter names), its mode, or suppress it. The part is rebuilt; if it then fails, nothing changes.",
            "Cambia los valores de una operación (start, end, angle, radius: números o nombres de parámetros), su modo, o la suprime. La pieza se reconstruye; si falla, no cambia nada."},
           object({{"body", body(kPart)}, {"operation", string({"The operation: o1 or its name", "La operación: o1 o su nombre"})},
                   {"start", json{{"type", json::array({"number", "string"})}, {"description", "From"}, {"x-es", "Desde"}}},
                   {"end", json{{"type", json::array({"number", "string"})}, {"description", "To"}, {"x-es", "Hasta"}}},
                   {"angle", json{{"type", json::array({"number", "string"})}, {"description", "Angle (degrees)"}, {"x-es", "Ángulo (grados)"}}},
                   {"radius", json{{"type", json::array({"number", "string"})}, {"description", "Radius"}, {"x-es", "Radio"}}},
                   {"mode", json{{"type", "string"}, {"enum", json::array({"new", "join", "cut", "intersect"})}, {"description", kMode.en}, {"x-es", kMode.es}}},
                   {"suppressed", json{{"type", "boolean"}, {"description", "Leave it out of the part"}, {"x-es", "Dejarla fuera de la pieza"}}},
                   {"count", json{{"type", json::array({"number", "string"})}, {"description", "Pattern: copies"}, {"x-es", "Patrón: copias"}}},
                   {"thickness", json{{"type", json::array({"number", "string"})}, {"description", "Thicken: thickness"}, {"x-es", "Engrosar: espesor"}}},
                   {"keep", json{{"type", "string"}, {"enum", json::array({"below", "above"})}, {"description", "Surface cut: the side kept"}, {"x-es", "Corte por superficie: el lado que queda"}}},
                   {"ruled", json{{"type", "boolean"}, {"description", "Loft: flat between sections"}, {"x-es", "Loft: plano entre secciones"}}},
                   {"name", string({"New name", "Nombre nuevo"})}},
                  {"body", "operation"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               Operation& o = findOperation(ctx, *m, in["operation"]);
               if (in.contains("start")) o.start = valueFromJson(in["start"]);
               if (in.contains("end")) o.end = valueFromJson(in["end"]);
               if (in.contains("angle")) o.angle = valueFromJson(in["angle"]);
               if (in.contains("radius")) o.radius = valueFromJson(in["radius"]);
               if (in.contains("mode")) o.mode = *opModeFromName(in["mode"].get<std::string>());
               if (in.contains("suppressed")) o.suppressed = in["suppressed"].get<bool>();
               if (in.contains("count")) o.count = valueFromJson(in["count"]);
               if (in.contains("thickness")) o.thickness = valueFromJson(in["thickness"]);
               if (in.contains("keep")) o.keepBelow = in["keep"].get<std::string>() == "below";
               if (in.contains("ruled")) o.ruled = in["ruled"].get<bool>();
               if (in.contains("name")) o.name = in["name"].get<std::string>();
               json out = partJson(applyModel(ctx, b, m));
               out["summary"] = ctx.say("Changed " + o.name + " and rebuilt the part.", "Cambiada " + o.name + " y reconstruida la pieza.");
               return out;
           }});

    R.add({"part.delete", "part", {"Delete from the part", "Borrar de la pieza"},
           {"Delete an operation (o1), or a sketch (s1) or surface (sf1) that no operation uses. The part is rebuilt.",
            "Borra una operación (o1), o un boceto (s1) o una superficie (sf1) que ninguna operación use. La pieza se reconstruye."},
           object({{"body", body(kPart)}, {"item", string({"o1, s1, sf1 or a name", "o1, s1, sf1 o un nombre"})}}, {"body", "item"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const std::string item = in["item"];
               if (const Sketch* s = m->sketch(item)) {
                   for (const auto& o : m->operations)
                       if (((o.kind == OpKind::Extrude || o.kind == OpKind::Revolve) && o.sketch == s->id) ||
                           (o.kind == OpKind::Loft && std::find(o.sections.begin(), o.sections.end(), s->id) != o.sections.end()))
                           throw CommandError("refused", ctx.say(o.name + " uses " + s->name + "; delete it first.", o.name + " usa " + s->name + "; bórrala antes."));
                   const auto id = s->id;
                   std::erase_if(m->sketches, [&](const Sketch& x) { return x.id == id; });
               } else if (const FreeSurface* f = m->surface(item)) {
                   for (const auto& o : m->operations)
                       if ((o.kind == OpKind::SurfaceCut || o.kind == OpKind::Thicken) && o.surface == f->id)
                           throw CommandError("refused", ctx.say(o.name + " uses " + f->name + "; delete it first.", o.name + " usa " + f->name + "; bórrala antes."));
                   const auto id = f->id;
                   std::erase_if(m->surfaces, [&](const FreeSurface& x) { return x.id == id; });
               } else {
                   const auto id = findOperation(ctx, *m, in["item"]).id;
                   std::erase_if(m->operations, [&](const Operation& x) { return x.id == id; });
               }
               Body nb = b;
               if (m->operations.empty()) {
                   ctx.doc.setPart(b.id, m, Solid{}, partTriangles(Solid{}));
                   nb = ctx.doc.body(b.ref());
               } else {
                   nb = applyModel(ctx, b, m);
               }
               json out = partJson(nb);
               out["summary"] = ctx.say("Deleted " + item + ".", "Borrado " + item + ".");
               return out;
           }});

    R.add({"part.move", "part", {"Move in the history", "Mover en el historial"},
           {"Move an operation to another place in the part's history (position 1 is the first). The part is rebuilt; if it then fails, nothing changes. The first operation must make a solid (mode new).",
            "Mueve una operación a otro lugar del historial de la pieza (la posición 1 es la primera). La pieza se reconstruye; si falla, no cambia nada. La primera operación debe crear un sólido (modo new)."},
           object({{"body", body(kPart)}, {"operation", string({"The operation: o1 or its name", "La operación: o1 o su nombre"})},
                   {"position", integer({"New position (1: first)", "Posición nueva (1: la primera)"}, 1, 1, 100000)}},
                  {"body", "operation", "position"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const auto id = findOperation(ctx, *m, in["operation"]).id;
               auto it = std::find_if(m->operations.begin(), m->operations.end(), [&](const Operation& o) { return o.id == id; });
               Operation o = *it;
               m->operations.erase(it);
               const auto pos = std::min<std::size_t>(static_cast<std::size_t>(in["position"].get<long long>()) - 1, m->operations.size());
               m->operations.insert(m->operations.begin() + static_cast<std::ptrdiff_t>(pos), o);
               json out = partJson(applyModel(ctx, b, m));
               out["summary"] = ctx.say("Moved " + o.name + " to position " + std::to_string(pos + 1) + ".", "Movida " + o.name + " a la posición " + std::to_string(pos + 1) + ".");
               return out;
           }});

    R.add({"part.loft", "part", {"Loft through sections", "Loft por secciones"},
           {"A solid through two or more sketches, in order (each one closed loop: a blade's sections at several radii, a duct's ends), smooth through them or flat between (ruled), combined with the part. Sections whose starts are far apart twist the loft: start each loop at the matching place (the leading edge, say).",
            "Un sólido que pasa por dos o más bocetos, en orden (cada uno, un lazo cerrado: las secciones de un álabe a varios radios, los extremos de un conducto), suave a través de ellos o plano entre ellos (ruled), combinado con la pieza. Si los inicios de las secciones quedan lejos, el loft se retuerce: empieza cada lazo en el mismo sitio (el borde de ataque, por ejemplo)."},
           object({{"body", body(kPart)}, {"sections", listSchema({"The sketches, in order: [\"s1\", \"s2\", ...]", "Los bocetos, en orden: [\"s1\", \"s2\", ...]"})},
                   {"ruled", boolean({"Flat between sections", "Plano entre secciones"}, false)},
                   {"mode", choice(kMode, {"new", "join", "cut", "intersect"}, "join")}, {"name", string({"Name", "Nombre"})}},
                  {"body", "sections"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Loft;
               if (!in["sections"].is_array() || in["sections"].size() < 2)
                   throw CommandError("invalid_input", ctx.say("A loft needs two sections at least.", "Un loft necesita dos secciones como mínimo."));
               for (const auto& r : in["sections"]) o.sections.push_back(findSketch(ctx, *m, r).id);
               o.ruled = in["ruled"].get<bool>();
               o.mode = *opModeFromName(in["mode"].get<std::string>());
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, "Loft");
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadido " + o.name + ".");
               return out;
           }});

    R.add({"part.pattern", "part", {"Circular pattern", "Patrón circular"},
           {"Copies of what another operation made (a hole, a blade, a boss) round an axis: count copies in all, over angle degrees (360 spreads them evenly). They combine with the part as that operation did, unless mode is given. Values may be numbers or parameter names.",
            "Copias de lo que hizo otra operación (un taladro, un álabe, un saliente) alrededor de un eje: count copias en total, en angle grados (360 las reparte por igual). Se combinan con la pieza como lo hizo esa operación, salvo que se dé mode. Los valores pueden ser números o nombres de parámetros."},
           object({{"body", body(kPart)}, {"operation", string({"The operation to repeat: o1 or its name", "La operación que repetir: o1 o su nombre"})},
                   {"axisOrigin", point({"A point of the axis", "Un punto del eje"})}, {"axisDirection", point({"The axis direction", "La dirección del eje"}, nullptr)},
                   {"count", valueSchema({"Copies in all (with the first)", "Copias en total (con la primera)"}, 6.0)},
                   {"angle", valueSchema({"Span (degrees; 360: a whole turn)", "Ángulo total (grados; 360: una vuelta)"}, 360.0)},
                   {"mode", json{{"type", "string"}, {"enum", json::array({"new", "join", "cut", "intersect"})}, {"description", kMode.en}, {"x-es", kMode.es}}},
                   {"name", string({"Name", "Nombre"})}},
                  {"body", "operation", "axisOrigin", "axisDirection"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const Operation& src = findOperation(ctx, *m, in["operation"]);
               if (src.kind == OpKind::Fillet || src.kind == OpKind::SurfaceCut)
                   throw CommandError("invalid_input", ctx.say("A pattern repeats what an operation adds or cuts; " + src.name + " changes the part in place.",
                                                               "Un patrón repite lo que una operación añade o corta; " + src.name + " cambia la pieza donde está."));
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Pattern;
               o.source = src.id;
               o.mode = in.contains("mode") ? *opModeFromName(in["mode"].get<std::string>()) : (src.mode == OpMode::New ? OpMode::Join : src.mode);
               o.axisOrigin = v3(in["axisOrigin"]);
               o.axisDirection = v3(in["axisDirection"]);
               o.count = valueFromJson(in["count"]);
               o.angle = valueFromJson(in["angle"]);
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Pattern", "Patrón"));
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadido " + o.name + ".");
               return out;
           }});

    R.add({"part.surface_from_scan", "part", {"Surface from the scan", "Superficie desde el escaneo"},
           {"Fit a smooth free-form surface (a bicubic B-spline) to a zone of a scan: a shape found by features.segment (shape: f3), or the points within radius of centre, or both. It is seen from a plane (default: the zone's best-fit plane, facing out of the scan) and must not fold over itself from it. The surface reaches margin past the zone, to cut the part with part.cut_by_surface or to thicken with part.thicken. Returns how far the points lie from it.",
            "Ajusta una superficie libre y suave (una B-spline bicúbica) a una zona de un escaneo: una forma hallada con features.segment (shape: f3), o los puntos a menos de radius de centre, o las dos cosas. Se ve desde un plano (por defecto, el que mejor se ajusta a la zona, mirando hacia fuera del escaneo) y no debe volver sobre sí misma vista desde él. La superficie se extiende margin más allá de la zona, para cortar la pieza con part.cut_by_surface o engrosarla con part.thicken. Devuelve cuánto se alejan los puntos de ella."},
           object({{"body", body(kPart)}, {"scan", body({"The scan (a mesh body)", "El escaneo (un cuerpo de malla)"})},
                   {"shape", string({"A shape of the scan's segmentation (f3)", "Una forma de la segmentación del escaneo (f3)"})},
                   {"centre", point({"Centre of a sphere of points", "Centro de una esfera de puntos"})},
                   {"radius", number({"Radius of that sphere", "Radio de esa esfera"}, 5.0, 0.01, 100000.0, "mm")},
                   {"plane", objectSchema({"The plane it is seen from: {origin, normal, xDir} (default: the zone's best fit)", "El plano desde el que se ve: {origin, normal, xDir} (por defecto, el que mejor se ajusta a la zona)"})},
                   {"poles", integer({"Poles along each side (0: chosen from the points)", "Polos en cada lado (0: según los puntos)"}, 0, 0, 60)},
                   {"margin", number({"How far it reaches past the zone", "Cuánto se extiende más allá de la zona"}, 2.0, 0.0, 10000.0, "mm")},
                   {"smoothing", number({"Weight of bending against closeness", "Peso de la flexión frente a la cercanía"}, 1e-6, 0.0, 10.0)},
                   {"name", string({"Name", "Nombre"})}},
                  {"body", "scan"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               const Body scan = ctx.doc.body(in["scan"]);
               if (scan.isPart() || scan.mesh->faces.empty())
                   throw CommandError("invalid_input", ctx.say(scan.name + " is not a scan with triangles.", scan.name + " no es un escaneo con triángulos."));
               const TriMesh& M = *scan.mesh;
               // The zone: vertices of the shape's faces, and/or within the sphere.
               std::vector<char> take(M.vertices.size(), in.contains("shape") ? 0 : 1);
               if (in.contains("shape")) {
                   const Feature* f = scan.features ? scan.features->find(in["shape"].get<std::string>()) : nullptr;
                   if (!f || f->region < 0)
                       throw CommandError("not_found", ctx.say("No segmented shape \"" + in["shape"].get<std::string>() + "\" on " + scan.name + " (run features.segment).",
                                                               "No hay ninguna forma segmentada \"" + in["shape"].get<std::string>() + "\" en " + scan.name + " (usa features.segment)."));
                   for (std::size_t t = 0; t < M.faces.size(); ++t)
                       if (scan.features->faceRegion[t] == f->region)
                           for (auto v : M.faces[t]) take[v] = 1;
               }
               if (in.contains("centre")) {
                   const Vec3d c = v3(in["centre"]);
                   const double r2 = in["radius"].get<double>() * in["radius"].get<double>();
                   for (std::size_t v = 0; v < M.vertices.size(); ++v)
                       if ((M.vertices[v].cast<double>() - c).squaredNorm() > r2) take[v] = 0;
               }
               std::vector<Vec3d> pts;
               Vec3d nsum = Vec3d::Zero();
               const auto normals = M.vertexNormals();
               for (std::size_t v = 0; v < M.vertices.size(); ++v)
                   if (take[v]) {
                       pts.push_back(M.vertices[v].cast<double>());
                       nsum += normals[v].cast<double>();
                   }
               if (pts.size() < 16) throw CommandError("failed", ctx.say("The zone has " + std::to_string(pts.size()) + " points; a surface needs 16 at least.",
                                                                       "La zona tiene " + std::to_string(pts.size()) + " puntos; una superficie necesita 16 como mínimo."));
               // The plane: given, or the zone's best fit, facing the way the scan faces.
               Vec3d o = Vec3d::Zero(), n, x;
               for (const auto& p : pts) o += p;
               o /= static_cast<double>(pts.size());
               if (in.contains("plane")) {
                   const json& pl = in["plane"];
                   if (pl.contains("origin")) o = v3(pl["origin"]);
                   n = v3(pl.at("normal"));
                   x = pl.contains("xDir") ? v3(pl["xDir"]) : n.unitOrthogonal();
               } else {
                   Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
                   for (const auto& p : pts) C += (p - o) * (p - o).transpose();
                   Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
                   n = es.eigenvectors().col(0);
                   x = es.eigenvectors().col(2);
                   if (n.dot(nsum) < 0) n = -n;
               }
               if (n.norm() < 1e-12) throw CommandError("invalid_input", ctx.say("The normal cannot be zero.", "La normal no puede ser cero."));
               // Poles: about one per six points along each side, 4 to 24, more along the longer side.
               int nu = static_cast<int>(in["poles"].get<long long>()), nv = nu;
               if (nu == 0) {
                   double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
                   const Vec3d nn = n.normalized(), xx = (x - x.dot(nn) * nn).normalized(), yy = nn.cross(xx);
                   for (const auto& p : pts) {
                       xmin = std::min(xmin, (p - o).dot(xx)); xmax = std::max(xmax, (p - o).dot(xx));
                       ymin = std::min(ymin, (p - o).dot(yy)); ymax = std::max(ymax, (p - o).dot(yy));
                   }
                   const double side = std::sqrt(static_cast<double>(pts.size())) / 6;
                   const double aspect = std::sqrt(std::max(1e-6, xmax - xmin) / std::max(1e-6, ymax - ymin));
                   nu = std::clamp(static_cast<int>(std::round(side * aspect)), 4, 24);
                   nv = std::clamp(static_cast<int>(std::round(side / aspect)), 4, 24);
               }
               const SurfaceFit fit = fitHeightSurface(pts, o, n, x, nu, nv, in["margin"].get<double>(), in["smoothing"].get<double>());
               auto m = std::make_shared<Model>(*b.model);
               FreeSurface f;
               f.id = m->nextId++;
               f.surface = fit.surface;
               f.rms = fit.rms;
               f.maxDeviation = fit.maxDeviation;
               f.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Surface", "Superficie"));
               m->surfaces.push_back(f);
               ctx.doc.setPart(b.id, m, b.solid, PartTriangles{b.mesh, b.view}, b.buildError);
               return json{{"body", b.ref()}, {"ref", f.ref()}, {"name", f.name}, {"points", fit.points}, {"poles", {f.surface.nu, f.surface.nv}},
                           {"rms", r4(fit.rms)}, {"median", r4(fit.median)}, {"maxDeviation", r4(fit.maxDeviation)}, {"normal", j3(f.surface.normal)},
                           {"origin", j3(f.surface.origin)}, {"size", {r4(f.surface.width), r4(f.surface.height)}},
                           {"summary", ctx.say("Added " + f.name + " (" + f.ref() + ") through " + std::to_string(fit.points) + " points: rms " + std::to_string(r4(fit.rms)).substr(0, 6) + " mm, worst " +
                                                   std::to_string(r4(fit.maxDeviation)).substr(0, 6) + " mm.",
                                               "Añadida " + f.name + " (" + f.ref() + ") por " + std::to_string(fit.points) + " puntos: rms " + std::to_string(r4(fit.rms)).substr(0, 6) + " mm, máximo " +
                                                   std::to_string(r4(fit.maxDeviation)).substr(0, 6) + " mm.")}};
           }});

    R.add({"part.cut_by_surface", "part", {"Cut by a surface", "Cortar por una superficie"},
           {"Remove from the part what lies beyond one of its free surfaces: above it (along its normal) when keep is below, below it when keep is above. A block cut by the surface fitted to a scan's curved top gets that top. The surface must reach past the part on every side.",
            "Quita de la pieza lo que queda más allá de una de sus superficies libres: por encima (según su normal) si keep es below, por debajo si keep es above. Un bloque cortado por la superficie ajustada a la cara curva de un escaneo toma esa cara. La superficie debe extenderse más allá de la pieza por todos los lados."},
           object({{"body", body(kPart)}, {"surface", string({"The surface: sf1 or its name", "La superficie: sf1 o su nombre"})},
                   {"keep", choice({"The side kept", "El lado que queda"}, {"below", "above"}, "below")}, {"name", string({"Name", "Nombre"})}},
                  {"body", "surface"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const FreeSurface* f = m->surface(in["surface"].get<std::string>());
               if (!f) throw CommandError("not_found", ctx.say("No surface \"" + in["surface"].get<std::string>() + "\" in the part.", "No hay ninguna superficie \"" + in["surface"].get<std::string>() + "\" en la pieza."));
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::SurfaceCut;
               o.surface = f->id;
               o.keepBelow = in["keep"].get<std::string>() == "below";
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Surface cut", "Corte por superficie"));
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadido " + o.name + ".");
               return out;
           }});

    R.add({"part.thicken", "part", {"Thicken a surface", "Engrosar una superficie"},
           {"Make a solid of one of the part's free surfaces, offset by thickness along its normal (negative: the other way): a shell, a panel, a blade from its mid surface. Combined with the part.",
            "Hace un sólido con una de las superficies libres de la pieza, desplazada thickness según su normal (en negativo, al otro lado): una carcasa, un panel, un álabe desde su superficie media. Se combina con la pieza."},
           object({{"body", body(kPart)}, {"surface", string({"The surface: sf1 or its name", "La superficie: sf1 o su nombre"})},
                   {"thickness", valueSchema({"Thickness", "Espesor"}, 1.0)},
                   {"mode", choice(kMode, {"new", "join", "cut", "intersect"}, "join")}, {"name", string({"Name", "Nombre"})}},
                  {"body", "surface"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const FreeSurface* f = m->surface(in["surface"].get<std::string>());
               if (!f) throw CommandError("not_found", ctx.say("No surface \"" + in["surface"].get<std::string>() + "\" in the part.", "No hay ninguna superficie \"" + in["surface"].get<std::string>() + "\" en la pieza."));
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Thicken;
               o.surface = f->id;
               o.thickness = valueFromJson(in["thickness"]);
               o.mode = *opModeFromName(in["mode"].get<std::string>());
               o.name = in.contains("name") ? in["name"].get<std::string>() : uniqueName(*m, ctx.say("Thicken", "Engrosado"));
               m->operations.push_back(o);
               json out = partJson(applyModel(ctx, b, m));
               out["ref"] = o.ref();
               out["summary"] = ctx.say("Added " + o.name + ".", "Añadido " + o.name + ".");
               return out;
           }});

    R.add({"part.list", "part", {"Part history", "Historial de la pieza"},
           {"A part's parameters, sketches and operations in order, and its solid's volume, area, faces and size.",
            "Los parámetros, bocetos y operaciones de una pieza, en orden, y el volumen, área, caras y tamaño de su sólido."},
           object({{"body", body(kPart)}}, {"body"}), false, false,
           [](CommandContext& ctx, const json& in) { return partJson(partBody(ctx, in["body"])); }});

    R.add({"part.export_step", "file", {"Export STEP", "Exportar STEP"},
           {"Write a part's solid as STEP (AP214, millimetres), for Fusion, FreeCAD, SolidWorks... Refuses to overwrite unless overwrite is true.",
            "Escribe el sólido de una pieza en STEP (AP214, milímetros), para Fusion, FreeCAD, SolidWorks... No sobrescribe salvo con overwrite."},
           object({{"body", body(kPart)}, {"path", path({"File to write", "Archivo a escribir"}, true, "STEP (*.step *.stp)")},
                   {"overwrite", boolean({"Replace the file if it exists", "Reemplazar el archivo si existe"}, false)}},
                  {"body", "path"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               if (b.solid.empty()) throw CommandError("failed", ctx.say(b.name + " has no solid yet.", b.name + " aún no tiene sólido."));
               const fs::path p = ctx.paths.check(in["path"].get<std::string>(), true);
               if (fs::exists(p) && !in["overwrite"].get<bool>())
                   throw CommandError("refused", ctx.say(p.string() + " already exists; pass overwrite: true to replace it.", p.string() + " ya existe; usa overwrite: true para reemplazarlo."));
               try {
                   b.solid.writeStep(p, b.name);
               } catch (const SolidError& e) {
                   throw CommandError("failed", e.what());
               }
               return json{{"path", p.string()}, {"bytes", fs::file_size(p)}, {"summary", ctx.say("Wrote " + p.filename().string() + ".", "Escrito " + p.filename().string() + ".")}};
           }});

    R.add({"part.import_step", "file", {"Import STEP", "Importar STEP"},
           {"Read a STEP file into a new part (one operation holding the solid; more can be added on top).",
            "Lee un archivo STEP en una pieza nueva (una operación con el sólido; se le pueden añadir más encima)."},
           object({{"path", path({"File to read", "Archivo a leer"}, false, "STEP (*.step *.stp)")}, {"name", string({"Name", "Nombre"})}}, {"path"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const fs::path p = ctx.paths.check(in["path"].get<std::string>(), false);
               auto m = std::make_shared<Model>();
               Operation o;
               o.id = m->nextId++;
               o.kind = OpKind::Import;
               o.name = p.filename().string();
               try {
                   o.brep = Solid::readStep(p).toBrep();
               } catch (const SolidError& e) {
                   throw CommandError("failed", e.what());
               }
               m->operations.push_back(o);
               const BuildResult r = rebuild(*m);
               const Body b = ctx.doc.addPart(in.contains("name") ? in["name"].get<std::string>() : p.stem().string(), m, r.solid, partTriangles(r.solid), r.error);
               json out = partJson(b);
               out["summary"] = ctx.say("Imported " + p.filename().string() + ".", "Importado " + p.filename().string() + ".");
               return out;
           }});

    R.add({"part.compare", "part", {"Compare with the scan", "Comparar con el escaneo"},
           {"How far a scan (a mesh body) lies from a part's solid: signed distances of the scan's points (positive: material outside the solid), their median, 90th and 95th percentiles, the share within ±tolerance, "
            "and where the points outside it gather (zones: centre, size, share, mean distance), largest first. With colorMap, the scan is coloured by its distance in the view. Both must be in the same place (align the scan first).",
            "Cuánto se aleja un escaneo (un cuerpo de malla) del sólido de una pieza: distancias con signo de sus puntos (positivas: material fuera del sólido), mediana, percentiles 90 y 95, la parte dentro de ±tolerancia "
            "y dónde se agrupan los puntos fuera de ella (zonas: centro, tamaño, parte del escaneo, distancia media), de mayor a menor. Con colorMap, el escaneo se colorea por su distancia en la vista. Los dos deben estar en el mismo sitio (alinea antes el escaneo)."},
           object({{"body", body(kPart)}, {"scan", body({"The scan (a mesh body)", "El escaneo (un cuerpo de malla)"})},
                   {"tolerance", number({"Tolerance", "Tolerancia"}, 0.2, 0.0001, 100.0, "mm")},
                   {"colorMap", boolean({"Colour the scan by its distance in the view", "Colorear el escaneo por su distancia en la vista"}, true)}},
                  {"body", "scan"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               const Body s = ctx.doc.body(in["scan"]);
               if (b.solid.empty()) throw CommandError("failed", ctx.say(b.name + " has no solid yet.", b.name + " aún no tiene sólido."));
               if (s.isPart()) throw CommandError("invalid_input", ctx.say(s.name + " is a part; compare a scan (a mesh body).", s.name + " es una pieza; compara un escaneo (un cuerpo de malla)."));
               const double tol = in["tolerance"].get<double>();
               const auto fine = b.solid.tessellate(std::min(0.01, tol / 10));
               std::vector<double> dist;
               const auto& V = s.mesh->vertices;
               const DeviationStats d = deviation(V, fine.mesh, tol, V.size(), &dist);
               const double pct = 100.0 * d.withinFraction;
               json out{{"body", b.ref()}, {"scan", s.ref()}, {"points", d.points}, {"mean", r4(d.mean)}, {"rms", r4(d.rms)}, {"median", r4(d.median)},
                        {"p90", r4(d.p90)}, {"p95", r4(d.p95)}, {"maxAbs", r4(d.maxAbs)}, {"minSigned", r4(d.minSigned)}, {"maxSigned", r4(d.maxSigned)},
                        {"tolerance", tol}, {"withinPercent", r4(pct)}};
               // Where the points outside the tolerance gather: pieces of the scan's
               // surface outside it on one side, joined along its triangles.
               if (dist.size() == V.size()) {
                   std::vector<std::uint32_t> parent(V.size());
                   for (std::uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
                   auto root = [&](std::uint32_t x) {
                       while (parent[x] != x) x = parent[x] = parent[parent[x]];
                       return x;
                   };
                   auto side = [&](std::uint32_t v) { return dist[v] > tol ? 1 : dist[v] < -tol ? -1 : 0; };
                   for (const auto& f : s.mesh->faces)
                       for (int k = 0; k < 3; ++k) {
                           const auto a = f[static_cast<std::size_t>(k)], c = f[static_cast<std::size_t>((k + 1) % 3)];
                           if (side(a) != 0 && side(a) == side(c)) parent[root(a)] = root(c);
                       }
                   std::map<std::uint32_t, std::size_t> zoneOf;
                   std::vector<std::vector<std::size_t>> zones;
                   for (std::uint32_t v = 0; v < V.size(); ++v) {
                       if (side(v) == 0) continue;
                       auto [it, fresh] = zoneOf.emplace(root(v), zones.size());
                       if (fresh) zones.emplace_back();
                       zones[it->second].push_back(v);
                   }
                   std::sort(zones.begin(), zones.end(), [](const auto& a, const auto& c) { return a.size() > c.size(); });
                   json jz = json::array();
                   for (std::size_t k = 0; k < zones.size() && k < 8; ++k) {
                       Box3 zb;
                       double mean = 0;
                       for (auto i : zones[k]) { zb.add(V[i]); mean += dist[i]; }
                       mean /= static_cast<double>(zones[k].size());
                       const double share = 100.0 * static_cast<double>(zones[k].size()) / static_cast<double>(V.size());
                       if (share < 0.2) break;
                       jz.push_back({{"centre", j3(zb.center().cast<double>())}, {"size", j3(zb.size().cast<double>())}, {"percent", r4(share)}, {"meanDistance", r4(mean)},
                                     {"side", mean > 0 ? ctx.say("scan outside the part (material missing in the model)", "escaneo fuera de la pieza (falta material en el modelo)")
                                                       : ctx.say("scan inside the part (too much material in the model)", "escaneo dentro de la pieza (sobra material en el modelo)")}});
                   }
                   out["zones"] = jz;
               }
               if (in["colorMap"].get<bool>() && dist.size() == V.size()) {
                   auto map = std::make_shared<DeviationMap>();
                   map->part = b.ref();
                   map->mesh = s.mesh.get();
                   map->tolerance = static_cast<float>(tol);
                   map->range = static_cast<float>(std::max(3 * tol, d.p95));
                   map->distance.assign(dist.begin(), dist.end());
                   ctx.doc.setDeviation(s.id, map);
               }
               out["summary"] = ctx.say(std::to_string(static_cast<int>(std::round(pct))) + "% of the scan within ±" + std::to_string(tol).substr(0, 5) + " mm; median " +
                                            std::to_string(r4(d.median)).substr(0, 6) + " mm, 95% under " + std::to_string(r4(d.p95)).substr(0, 6) + " mm.",
                                        std::to_string(static_cast<int>(std::round(pct))) + " % del escaneo dentro de ±" + std::to_string(tol).substr(0, 5) + " mm; mediana " +
                                            std::to_string(r4(d.median)).substr(0, 6) + " mm, el 95 % por debajo de " + std::to_string(r4(d.p95)).substr(0, 6) + " mm.");
               return out;
           }});

    R.add({"mesh.section", "mesh", {"Section with a plane", "Sección con un plano"},
           {"Where a plane cuts a mesh: polylines in the plane's coordinates (x along xDir, y along normal × xDir), simplified to within 'tolerance'. The outline to draw a sketch from.",
            "Dónde corta un plano a una malla: polilíneas en las coordenadas del plano (x a lo largo de xDir, y de normal × xDir), simplificadas a menos de 'tolerance'. El contorno del que sacar un boceto."},
           object({{"body", body(kBody)}, {"origin", point({"A point of the plane", "Un punto del plano"})}, {"normal", point({"The plane's normal", "La normal del plano"}, nullptr)},
                   {"xDir", point({"The sketch's x direction (default: any in the plane)", "La dirección x del boceto (por defecto, cualquiera del plano)"}, nullptr)},
                   {"tolerance", number({"Simplify to within", "Simplificar a menos de"}, 0.01, 0.0, 10.0, "mm")},
                   {"maxPoints", integer({"Points to list per polyline, at most", "Puntos por polilínea, como mucho"}, 400, 10, 100000)}},
                  {"body", "origin", "normal"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               if (b.mesh->faces.empty()) throw CommandError("invalid_input", ctx.say(b.name + " has no triangles.", b.name + " no tiene triángulos."));
               const Vec3d o = v3(in["origin"]), n = v3(in["normal"]);
               if (n.norm() < 1e-12) throw CommandError("invalid_input", ctx.say("The normal cannot be zero.", "La normal no puede ser cero."));
               Vec3d x = in.contains("xDir") ? v3(in["xDir"]) : n.unitOrthogonal();
               x = x - x.dot(n.normalized()) * n.normalized();
               if (x.norm() < 1e-9) x = n.unitOrthogonal();
               const auto polys = sectionMesh(*b.mesh, o, n, x);
               const double tol = in["tolerance"].get<double>();
               const auto maxPts = static_cast<std::size_t>(in["maxPoints"].get<long long>());
               json list = json::array();
               for (const auto& pl : polys) {
                   auto pts = tol > 0 ? simplifyPolyline(pl.points, tol, pl.closed) : pl.points;
                   double len = 0, area = 0;
                   for (std::size_t i = 0; i + 1 < pl.points.size(); ++i) len += (pl.points[i + 1] - pl.points[i]).norm();
                   if (pl.closed)
                       for (std::size_t i = 0; i < pl.points.size(); ++i) {
                           const auto& u = pl.points[i];
                           const auto& v = pl.points[(i + 1) % pl.points.size()];
                           area += 0.5 * (u.x() * v.y() - v.x() * u.y());
                       }
                   json jp = json::array();
                   const std::size_t step = std::max<std::size_t>(1, (pts.size() + maxPts - 1) / maxPts);
                   for (std::size_t i = 0; i < pts.size(); i += step) jp.push_back(j2(pts[i]));
                   list.push_back({{"closed", pl.closed}, {"length", r4(len)}, {"area", r4(area)}, {"points", jp}, {"listedEvery", step}});
               }
               return json{{"body", b.ref()}, {"plane", {{"origin", j3(o)}, {"normal", j3(n.normalized())}, {"xDir", j3(x.normalized())}}}, {"polylines", list},
                           {"summary", ctx.say(std::to_string(polys.size()) + " polylines.", std::to_string(polys.size()) + " polilíneas.")}};
           }});
}

}  // namespace orv

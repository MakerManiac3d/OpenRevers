// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — commands on parametric parts: a new part, sketches, extrude,
// revolve, fillet, parameters, editing and deleting in the history, STEP in
// and out, comparing a part with the scan; and cutting a mesh with a plane
// (the outline a sketch is drawn from).
#include "commands/Command.h"
#include "commands/PartCommands.h"

#include "core/mesh/Distance.h"
#include "doc/Document.h"

#include <cmath>
#include <filesystem>

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

double deflectionFor(const Solid& s) {
    if (s.empty()) return 0.01;
    const auto b = s.bounds();
    return std::clamp(0.0005 * (b.max() - b.min()).norm(), 0.002, 0.05);
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
        }
        ops.push_back(jo);
    }
    json out{{"body", b.ref()}, {"name", b.name}, {"parameters", m.parameters}, {"sketches", sketches}, {"operations", ops}};
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
    auto mesh = std::make_shared<TriMesh>(r.solid.tessellate(deflectionFor(r.solid)).mesh);
    ctx.doc.setPart(b.id, m, r.solid, mesh);
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
                                              std::make_shared<TriMesh>());
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
               ctx.doc.setPart(b.id, m, b.solid, b.mesh, b.buildError);
               double area = 0;
               for (const auto& l : s.profile.loops) area += l.area();
               return json{{"body", b.ref()}, {"ref", s.ref()}, {"name", s.name}, {"loops", s.profile.loops.size()},
                           {"summary", ctx.say("Added " + s.name + " (" + s.ref() + ") with " + std::to_string(s.profile.loops.size()) + " loops.",
                                               "Añadido " + s.name + " (" + s.ref() + ") con " + std::to_string(s.profile.loops.size()) + " lazos.")}};
           }});

    R.add({"part.edit_sketch", "part", {"Edit a sketch", "Editar boceto"},
           {"Replace a sketch's loops (and its plane, if given). The operations that use it are rebuilt; if the part then fails, nothing changes.",
            "Reemplaza los lazos de un boceto (y su plano, si se da). Se reconstruyen las operaciones que lo usan; si la pieza falla, no cambia nada."},
           object({{"body", body(kPart)}, {"sketch", string({"The sketch: s1 or its name", "El boceto: s1 o su nombre"})}, {"plane", objectSchema(kPlane)}, {"loops", listSchema(kLoops)}},
                  {"body", "sketch", "loops"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const auto id = findSketch(ctx, *m, in["sketch"]).id;
               Profile p = readProfile(ctx, in);
               for (auto& s : m->sketches)
                   if (s.id == id) {
                       if (!in.contains("plane")) p.plane = s.profile.plane;
                       s.profile = p;
                   }
               json out = partJson(applyModel(ctx, b, m));
               out["summary"] = ctx.say("Sketch changed and the part rebuilt.", "Boceto cambiado y pieza reconstruida.");
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
               json out = partJson(m->operations.empty() ? (ctx.doc.setPart(b.id, m, b.solid, b.mesh, b.buildError), ctx.doc.body(b.ref())) : applyModel(ctx, b, m));
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
               if (in.contains("name")) o.name = in["name"].get<std::string>();
               json out = partJson(applyModel(ctx, b, m));
               out["summary"] = ctx.say("Changed " + o.name + " and rebuilt the part.", "Cambiada " + o.name + " y reconstruida la pieza.");
               return out;
           }});

    R.add({"part.delete", "part", {"Delete from the part", "Borrar de la pieza"},
           {"Delete an operation (o1) or a sketch (s1) that no operation uses. The part is rebuilt.",
            "Borra una operación (o1) o un boceto (s1) que ninguna operación use. La pieza se reconstruye."},
           object({{"body", body(kPart)}, {"item", string({"o1, s1 or a name", "o1, s1 o un nombre"})}}, {"body", "item"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               auto m = std::make_shared<Model>(*b.model);
               const std::string item = in["item"];
               if (const Sketch* s = m->sketch(item)) {
                   for (const auto& o : m->operations)
                       if ((o.kind == OpKind::Extrude || o.kind == OpKind::Revolve) && o.sketch == s->id)
                           throw CommandError("refused", ctx.say(o.name + " uses " + s->name + "; delete it first.", o.name + " usa " + s->name + "; bórrala antes."));
                   const auto id = s->id;
                   std::erase_if(m->sketches, [&](const Sketch& x) { return x.id == id; });
               } else {
                   const auto id = findOperation(ctx, *m, in["item"]).id;
                   std::erase_if(m->operations, [&](const Operation& x) { return x.id == id; });
               }
               Body nb = b;
               if (m->operations.empty()) {
                   ctx.doc.setPart(b.id, m, Solid{}, std::make_shared<TriMesh>());
                   nb = ctx.doc.body(b.ref());
               } else {
                   nb = applyModel(ctx, b, m);
               }
               json out = partJson(nb);
               out["summary"] = ctx.say("Deleted " + item + ".", "Borrado " + item + ".");
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
               const Body b = ctx.doc.addPart(in.contains("name") ? in["name"].get<std::string>() : p.stem().string(), m, r.solid,
                                              std::make_shared<TriMesh>(r.solid.tessellate(deflectionFor(r.solid)).mesh), r.error);
               json out = partJson(b);
               out["summary"] = ctx.say("Imported " + p.filename().string() + ".", "Importado " + p.filename().string() + ".");
               return out;
           }});

    R.add({"part.compare", "part", {"Compare with the scan", "Comparar con el escaneo"},
           {"How far a scan (a mesh body) lies from a part's solid: signed distances of the scan's points (positive: material outside the solid), their median, 90th and 95th percentiles, and the share within ±tolerance. Both must be in the same place (align the scan first).",
            "Cuánto se aleja un escaneo (un cuerpo de malla) del sólido de una pieza: distancias con signo de sus puntos (positivas: material fuera del sólido), mediana, percentiles 90 y 95, y la parte dentro de ±tolerancia. Los dos deben estar en el mismo sitio (alinea antes el escaneo)."},
           object({{"body", body(kPart)}, {"scan", body({"The scan (a mesh body)", "El escaneo (un cuerpo de malla)"})},
                   {"tolerance", number({"Tolerance", "Tolerancia"}, 0.2, 0.0001, 100.0, "mm")}},
                  {"body", "scan"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = partBody(ctx, in["body"]);
               const Body s = ctx.doc.body(in["scan"]);
               if (b.solid.empty()) throw CommandError("failed", ctx.say(b.name + " has no solid yet.", b.name + " aún no tiene sólido."));
               const double tol = in["tolerance"].get<double>();
               const auto fine = b.solid.tessellate(std::min(0.01, tol / 10));
               const DeviationStats d = deviation(s.mesh->vertices, fine.mesh, tol);
               const double pct = 100.0 * d.withinFraction;
               return json{{"body", b.ref()}, {"scan", s.ref()}, {"points", d.points}, {"mean", r4(d.mean)}, {"rms", r4(d.rms)}, {"median", r4(d.median)},
                           {"p90", r4(d.p90)}, {"p95", r4(d.p95)}, {"maxAbs", r4(d.maxAbs)}, {"minSigned", r4(d.minSigned)}, {"maxSigned", r4(d.maxSigned)},
                           {"tolerance", tol}, {"withinPercent", r4(pct)},
                           {"summary", ctx.say(std::to_string(static_cast<int>(std::round(pct))) + "% of the scan within ±" + std::to_string(tol).substr(0, 5) + " mm; median " +
                                                   std::to_string(r4(d.median)).substr(0, 6) + " mm, 95% under " + std::to_string(r4(d.p95)).substr(0, 6) + " mm.",
                                               std::to_string(static_cast<int>(std::round(pct))) + " % del escaneo dentro de ±" + std::to_string(tol).substr(0, 5) + " mm; mediana " +
                                                   std::to_string(r4(d.median)).substr(0, 6) + " mm, el 95 % por debajo de " + std::to_string(r4(d.p95)).substr(0, 6) + " mm.")}};
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

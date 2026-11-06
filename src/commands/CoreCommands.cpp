// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the commands that need no interface. The desktop app adds its
// own (views, pictures) in src/app/AppCommands.cpp.
#include "commands/Command.h"
#include "commands/Guide.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshRepair.h"
#include "core/util/Version.h"
#include "doc/Project.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace orv {
namespace fs = std::filesystem;

namespace {

const Text kBody{"The body: its reference (b1) or its name", "El cuerpo: su referencia (b1) o su nombre"};

json boxJson(const Box3& b) {
    if (b.empty()) return nullptr;
    auto r = [](float v) { return std::round(v * 1e4) / 1e4; };
    return {{"min", {r(b.min.x()), r(b.min.y()), r(b.min.z())}},
            {"max", {r(b.max.x()), r(b.max.y()), r(b.max.z())}},
            {"size", {r(b.size().x()), r(b.size().y()), r(b.size().z())}}};
}

double round4(double v) { return std::round(v * 1e4) / 1e4; }

json bodyJson(const Body& b) {
    return {{"ref", b.ref()}, {"name", b.name}, {"vertices", b.mesh->vertexCount()}, {"faces", b.mesh->faceCount()},
            {"visible", b.visible}, {"bounds", boxJson(b.mesh->bounds())}, {"source", b.source}};
}

json reportJson(const MeshReport& r, std::size_t maxLoops = 20) {
    json loops = json::array();
    for (std::size_t i = 0; i < r.boundaryLoops.size() && i < maxLoops; ++i)
        loops.push_back({{"edges", r.boundaryLoops[i].vertices.size()}, {"length", round4(r.boundaryLoops[i].length)}});
    return {{"vertices", r.vertices},
            {"faces", r.faces},
            {"edges", r.edges},
            {"closed", r.closed},
            {"holes", r.boundaryLoops.size()},
            {"largestHoles", loops},
            {"boundaryEdges", r.boundaryEdges},
            {"nonManifoldEdges", r.nonManifoldEdges},
            {"degenerateFaces", r.degenerateFaces},
            {"duplicateFaces", r.duplicateFaces},
            {"orientationConflicts", r.orientationConflicts},
            {"components", r.components},
            {"largestComponentFaces", r.largestComponentFaces},
            {"area", round4(r.area)},
            {"volume", r.closed ? json(round4(r.volume)) : json(nullptr)},
            {"meanEdgeLength", round4(r.meanEdgeLength)},
            {"bounds", boxJson(r.bounds)},
            {"units", "mm"}};
}

/// Run `op` on a copy of the body's mesh and put the result in its place.
template <class Op>
std::shared_ptr<TriMesh> editMesh(CommandContext& ctx, const Body& b, Op&& op) {
    auto m = std::make_shared<TriMesh>(*b.mesh);
    op(*m);
    if (m->faces.empty()) throw CommandError("failed", ctx.say("The operation left no triangles; nothing was changed.", "La operación no dejó ningún triángulo; no se ha cambiado nada."));
    ctx.doc.setMesh(b.id, m);
    return m;
}

json changeSummary(const Body& b, const TriMesh& before, const TriMesh& after, json extra) {
    extra["body"] = b.ref();
    extra["facesBefore"] = before.faceCount();
    extra["facesAfter"] = after.faceCount();
    extra["verticesAfter"] = after.vertexCount();
    return extra;
}

std::string extensionOf(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

}  // namespace

void registerCoreCommands(CommandRegistry& R) {
    using namespace schema;

    // ------------------------------------------------------------ files --
    R.add({"mesh.import", "file", {"Import mesh", "Importar malla"},
           {"Read a mesh file (STL, OBJ or PLY, in millimetres) into a new body.",
            "Lee un archivo de malla (STL, OBJ o PLY, en milímetros) en un cuerpo nuevo."},
           object({{"path", path({"File to read", "Archivo a leer"}, false, "Mesh (*.stl *.obj *.ply)")},
                   {"name", string({"Name for the body (default: the file name)", "Nombre del cuerpo (por defecto, el del archivo)"})}},
                  {"path"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const fs::path p = ctx.paths.check(in["path"].get<std::string>(), false);
               const auto t0 = std::chrono::steady_clock::now();
               auto mesh = std::make_shared<TriMesh>(readMesh(p));
               const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
               const std::string name = in.contains("name") ? in["name"].get<std::string>() : p.stem().string();
               const Body b = ctx.doc.addBody(name, mesh, p.string());
               json out = bodyJson(b);
               out["seconds"] = round4(secs);
               out["summary"] = ctx.say("Imported " + p.filename().string() + ": " + std::to_string(mesh->faceCount()) + " triangles.",
                                        "Importado " + p.filename().string() + ": " + std::to_string(mesh->faceCount()) + " triángulos.");
               return out;
           }});

    R.add({"mesh.export", "file", {"Export mesh", "Exportar malla"},
           {"Write a body's mesh to a file: .stl (binary), .obj or .ply (binary). Refuses to overwrite unless overwrite is true.",
            "Escribe la malla de un cuerpo en un archivo: .stl (binario), .obj o .ply (binario). No sobrescribe salvo con overwrite."},
           object({{"body", body(kBody)},
                   {"path", path({"File to write", "Archivo a escribir"}, true, "STL (*.stl);;OBJ (*.obj);;PLY (*.ply)")},
                   {"ascii", boolean({"Text format (STL or PLY)", "Formato de texto (STL o PLY)"}, false)},
                   {"overwrite", boolean({"Replace the file if it exists", "Reemplazar el archivo si existe"}, false)}},
                  {"body", "path"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const fs::path p = ctx.paths.check(in["path"].get<std::string>(), true);
               if (fs::exists(p) && !in["overwrite"].get<bool>())
                   throw CommandError("refused", ctx.say(p.string() + " already exists; pass overwrite: true to replace it.", p.string() + " ya existe; usa overwrite: true para reemplazarlo."));
               MeshFormat f = formatFromPath(p);
               if (in["ascii"].get<bool>()) f = f == MeshFormat::Stl ? MeshFormat::StlAscii : f == MeshFormat::Ply ? MeshFormat::PlyAscii : f;
               writeMesh(*b.mesh, p, f);
               return json{{"path", p.string()}, {"faces", b.mesh->faceCount()}, {"bytes", fs::file_size(p)},
                           {"summary", ctx.say("Wrote " + p.filename().string() + ".", "Escrito " + p.filename().string() + ".")}};
           }});

    R.add({"project.save", "file", {"Save project", "Guardar proyecto"},
           {"Save the project (.orvp). Without a path, saves to the file it was opened from.",
            "Guarda el proyecto (.orvp). Sin ruta, lo guarda en el archivo del que se abrió."},
           object({{"path", path({"Project file", "Archivo de proyecto"}, true, "OpenRevers project (*.orvp)")},
                   {"overwrite", boolean({"Replace a different existing file", "Reemplazar otro archivo existente"}, false)}}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               std::string target = in.contains("path") ? in["path"].get<std::string>() : ctx.doc.path();
               if (target.empty()) throw CommandError("invalid_input", ctx.say("The project has no file yet: give a path.", "El proyecto aún no tiene archivo: indica una ruta."));
               fs::path p = ctx.paths.check(target, true);
               if (extensionOf(p) != ".orvp") p += ".orvp";
               const bool same = !ctx.doc.path().empty() && fs::path(ctx.doc.path()) == p;
               if (fs::exists(p) && !same && !in["overwrite"].get<bool>())
                   throw CommandError("refused", ctx.say(p.string() + " already exists; pass overwrite: true to replace it.", p.string() + " ya existe; usa overwrite: true para reemplazarlo."));
               saveProject(ctx.doc, p);
               ctx.doc.setPath(p.string());
               ctx.doc.setSaved();
               return json{{"path", p.string()}, {"summary", ctx.say("Project saved.", "Proyecto guardado.")}};
           }});

    R.add({"project.open", "file", {"Open project", "Abrir proyecto"},
           {"Open a project (.orvp), replacing the current one. Refuses when there are unsaved changes unless discard is true.",
            "Abre un proyecto (.orvp) en lugar del actual. No lo hace si hay cambios sin guardar, salvo con discard."},
           object({{"path", path({"Project file", "Archivo de proyecto"}, false, "OpenRevers project (*.orvp)")},
                   {"discard", boolean({"Discard unsaved changes", "Descartar cambios sin guardar"}, false)}},
                  {"path"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               if (ctx.doc.modified() && !in["discard"].get<bool>())
                   throw CommandError("refused", ctx.say("The current project has unsaved changes: save it first, or pass discard: true.", "El proyecto actual tiene cambios sin guardar: guárdalo antes o usa discard: true."));
               const fs::path p = ctx.paths.check(in["path"].get<std::string>(), false);
               loadProject(ctx.doc, p);
               json bodies = json::array();
               for (const auto& b : ctx.doc.bodies()) bodies.push_back(bodyJson(b));
               return json{{"path", p.string()}, {"bodies", bodies}, {"summary", ctx.say("Project opened.", "Proyecto abierto.")}};
           }});

    R.add({"project.new", "file", {"New project", "Proyecto nuevo"},
           {"Start an empty project. Refuses when there are unsaved changes unless discard is true.",
            "Empieza un proyecto vacío. No lo hace si hay cambios sin guardar, salvo con discard."},
           object({{"discard", boolean({"Discard unsaved changes", "Descartar cambios sin guardar"}, false)}}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               if (ctx.doc.modified() && !in["discard"].get<bool>())
                   throw CommandError("refused", ctx.say("The current project has unsaved changes: save it first, or pass discard: true.", "El proyecto actual tiene cambios sin guardar: guárdalo antes o usa discard: true."));
               ctx.doc.reset({}, 1);
               return json{{"summary", ctx.say("New project.", "Proyecto nuevo.")}};
           }});

    // ------------------------------------------------------- inspection --
    R.add({"mesh.analyze", "mesh", {"Analyse mesh", "Analizar malla"},
           {"Check a mesh before modelling: holes and their size, open and non-manifold edges, flipped, degenerate and duplicate faces, loose pieces, area, volume, mean edge length. Changes nothing.",
            "Revisa una malla antes de modelar: agujeros y su tamaño, bordes abiertos y no manifold, caras invertidas, degeneradas y duplicadas, piezas sueltas, área, volumen y longitud media de arista. No cambia nada."},
           object({{"body", body(kBody)}}, {"body"}), false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const MeshReport r = analyze(*b.mesh);
               json out = reportJson(r);
               out["body"] = b.ref();
               std::string en = std::to_string(r.faces) + " triangles, " + std::to_string(r.boundaryLoops.size()) + " holes, " +
                                std::to_string(r.components) + " pieces, " + std::to_string(r.nonManifoldEdges) + " non-manifold edges" +
                                (r.closed ? ", closed." : ".");
               std::string es = std::to_string(r.faces) + " triángulos, " + std::to_string(r.boundaryLoops.size()) + " agujeros, " +
                                std::to_string(r.components) + " piezas, " + std::to_string(r.nonManifoldEdges) + " aristas no manifold" +
                                (r.closed ? ", cerrada." : ".");
               out["summary"] = ctx.say(en, es);
               return out;
           }});

    // ----------------------------------------------------------- repair --
    R.add({"mesh.repair", "mesh", {"Repair mesh", "Reparar malla"},
           {"Clean a scanned mesh in one step: weld coincident vertices, remove degenerate and duplicate faces, remove small loose pieces, and make the faces' winding consistent and outward.",
            "Limpia una malla escaneada en un paso: une vértices coincidentes, quita caras degeneradas y duplicadas, elimina piezas sueltas pequeñas y orienta las caras de forma coherente hacia fuera."},
           object({{"body", body(kBody)},
                   {"weldTolerance", number({"Weld vertices closer than this (0 = only identical)", "Unir vértices más cercanos que esto (0 = solo idénticos)"}, 0.0, 0.0, 10.0, "mm")},
                   {"minPieceFaces", integer({"Remove loose pieces with fewer faces than this (0 = keep all)", "Quitar piezas sueltas con menos caras que esto (0 = conservar todas)"}, 50, 0, 100000000)},
                   {"keepLargestOnly", boolean({"Keep only the largest piece", "Conservar solo la pieza más grande"}, false)},
                   {"orient", boolean({"Make the winding consistent and outward", "Orientar las caras de forma coherente y hacia fuera"}, true)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               json done;
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) {
                   done["verticesWelded"] = weldVertices(m, static_cast<float>(in["weldTolerance"].get<double>()));
                   done["degenerateRemoved"] = removeDegenerateFaces(m);
                   done["duplicatesRemoved"] = removeDuplicateFaces(m);
                   const auto minFaces = static_cast<std::size_t>(in["minPieceFaces"].get<long long>());
                   const bool largest = in["keepLargestOnly"].get<bool>();
                   done["pieceFacesRemoved"] = (largest || minFaces > 0) ? removeSmallComponents(m, minFaces, largest) : 0;
                   done["facesFlipped"] = in["orient"].get<bool>() ? orientConsistently(m) : 0;
               });
               json out = changeSummary(b, *before, *after, done);
               out["after"] = reportJson(analyze(*after), 5);
               auto n = [&](const char* k) { return std::to_string(done[k].get<std::size_t>()); };
               out["summary"] = ctx.say(n("verticesWelded") + " vertices welded, " + n("degenerateRemoved") + " degenerate and " + n("duplicatesRemoved") +
                                            " duplicate faces removed, " + n("pieceFacesRemoved") + " faces of loose pieces removed, " + n("facesFlipped") + " faces flipped.",
                                        n("verticesWelded") + " vértices unidos, " + n("degenerateRemoved") + " caras degeneradas y " + n("duplicatesRemoved") +
                                            " duplicadas quitadas, " + n("pieceFacesRemoved") + " caras de piezas sueltas quitadas, " + n("facesFlipped") + " caras invertidas.");
               return out;
           }});

    R.add({"mesh.fill_holes", "mesh", {"Fill holes", "Rellenar agujeros"},
           {"Close holes with a smooth patch that blends into the surface. Holes whose rim has more than maxEdges edges are left open (0 = fill all, including the scan's outer rim).",
            "Cierra agujeros con un parche suave que se funde con la superficie. Los agujeros con más de maxEdges aristas en su borde se dejan abiertos (0 = todos, incluido el borde exterior del escaneo)."},
           object({{"body", body(kBody)},
                   {"maxEdges", integer({"Largest hole to fill, in rim edges (0 = all)", "Agujero más grande a rellenar, en aristas del borde (0 = todos)"}, 500, 0, 10000000)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               FillHolesResult r;
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) { r = fillHoles(m, static_cast<std::size_t>(in["maxEdges"].get<long long>())); });
               json out = changeSummary(b, *before, *after, {{"filled", r.filled}, {"leftOpen", r.skipped}, {"facesAdded", r.facesAdded}});
               out["summary"] = ctx.say(std::to_string(r.filled) + " holes filled, " + std::to_string(r.skipped) + " left open.",
                                        std::to_string(r.filled) + " agujeros rellenados, " + std::to_string(r.skipped) + " abiertos.");
               return out;
           }});

    R.add({"mesh.remove_pieces", "mesh", {"Remove loose pieces", "Quitar piezas sueltas"},
           {"Remove connected pieces smaller than minFaces faces, or every piece but the largest.",
            "Elimina las piezas conectadas con menos de minFaces caras, o todas salvo la más grande."},
           object({{"body", body(kBody)},
                   {"minFaces", integer({"Smallest piece to keep, in faces", "Pieza más pequeña a conservar, en caras"}, 100, 1, 100000000)},
                   {"keepLargestOnly", boolean({"Keep only the largest piece", "Conservar solo la pieza más grande"}, false)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               std::size_t removed = 0;
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) {
                   removed = removeSmallComponents(m, static_cast<std::size_t>(in["minFaces"].get<long long>()), in["keepLargestOnly"].get<bool>());
               });
               json out = changeSummary(b, *before, *after, {{"facesRemoved", removed}});
               out["summary"] = ctx.say(std::to_string(removed) + " faces removed.", std::to_string(removed) + " caras eliminadas.");
               return out;
           }});

    R.add({"mesh.flip_normals", "mesh", {"Flip normals", "Invertir normales"},
           {"Reverse every face so the normals point the other way.", "Invierte todas las caras para que las normales apunten al otro lado."},
           object({{"body", body(kBody)}}, {"body"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) { flipNormals(m); });
               json out = changeSummary(b, *before, *after, json::object());
               out["summary"] = ctx.say("Normals flipped.", "Normales invertidas.");
               return out;
           }});

    // ------------------------------------------------------- processing --
    R.add({"mesh.smooth", "mesh", {"Smooth", "Suavizar"},
           {"Reduce scan noise with Laplacian smoothing. 'implicit' is stable and keeps the volume; 'explicit' is a few local steps. Smoothing also rounds sharp edges: use little.",
            "Reduce el ruido del escaneo con suavizado laplaciano. 'implicit' es estable y conserva el volumen; 'explicit' son pasos locales. También redondea las aristas vivas: úsalo con moderación."},
           object({{"body", body(kBody)},
                   {"method", choice({"Scheme", "Método"}, {"implicit", "explicit"}, "implicit")},
                   {"iterations", integer({"Steps", "Pasos"}, 1, 1, 100)},
                   {"timestep", number({"Strength per step (implicit)", "Intensidad por paso (implícito)"}, 0.001, 1e-6, 0.1)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) {
                   smooth(m, in["method"] == "implicit", static_cast<unsigned>(in["iterations"].get<long long>()), static_cast<float>(in["timestep"].get<double>()));
               });
               json out = changeSummary(b, *before, *after, json::object());
               out["summary"] = ctx.say("Smoothed.", "Suavizada.");
               return out;
           }});

    R.add({"mesh.decimate", "mesh", {"Decimate", "Diezmar"},
           {"Reduce the number of triangles while staying close to the surface (quadric edge collapse). Give targetFaces or ratio; maxError bounds the deviation.",
            "Reduce el número de triángulos sin alejarse de la superficie (colapso de aristas por cuádricas). Indica targetFaces o ratio; maxError limita la desviación."},
           object({{"body", body(kBody)},
                   {"ratio", number({"Fraction of the triangles to keep", "Fracción de triángulos a conservar"}, 0.5, 0.001, 1.0)},
                   {"targetFaces", integer({"Triangles to keep (overrides ratio)", "Triángulos a conservar (sustituye a ratio)"}, 0, 0, 1000000000)},
                   {"maxError", number({"Largest allowed deviation (0 = no limit)", "Desviación máxima permitida (0 = sin límite)"}, 0.0, 0.0, 100.0, "mm")}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const auto faces = b.mesh->faceCount();
               const auto target = in["targetFaces"].get<long long>() > 0 ? static_cast<std::size_t>(in["targetFaces"].get<long long>())
                                                                           : static_cast<std::size_t>(std::llround(static_cast<double>(faces) * in["ratio"].get<double>()));
               DecimateOptions o;
               // A closed triangle mesh has about half as many vertices as faces.
               o.targetVertices = std::max<std::size_t>(4, target / 2);
               o.maxError = static_cast<float>(in["maxError"].get<double>());
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) { decimate(m, o); });
               json out = changeSummary(b, *before, *after, json::object());
               out["summary"] = ctx.say("Decimated to " + std::to_string(after->faceCount()) + " triangles.", "Diezmada a " + std::to_string(after->faceCount()) + " triángulos.");
               return out;
           }});

    R.add({"mesh.remesh", "mesh", {"Remesh", "Remallar"},
           {"Rebuild the triangles evenly: 'uniform' to one edge length, 'adaptive' finer where the surface curves. The new mesh is projected back onto the old one, and edges sharper than featureAngle stay sharp.",
            "Reconstruye los triángulos de forma regular: 'uniform' con una longitud de arista, 'adaptive' más fina donde la superficie se curva. La malla nueva se proyecta sobre la original, y las aristas más vivas que featureAngle se conservan."},
           object({{"body", body(kBody)},
                   {"mode", choice({"Mode", "Modo"}, {"uniform", "adaptive"}, "uniform")},
                   {"edgeLength", number({"Edge length (uniform; 0 = current mean)", "Longitud de arista (uniforme; 0 = la media actual)"}, 0.0, 0.0, 1000.0, "mm")},
                   {"minEdge", number({"Shortest edge (adaptive)", "Arista mínima (adaptativo)"}, 0.1, 0.001, 1000.0, "mm")},
                   {"maxEdge", number({"Longest edge (adaptive)", "Arista máxima (adaptativo)"}, 2.0, 0.001, 1000.0, "mm")},
                   {"maxError", number({"Allowed deviation (adaptive)", "Desviación permitida (adaptativo)"}, 0.02, 0.0001, 10.0, "mm")},
                   {"featureAngle", number({"Keep edges sharper than this angle (0 = none)", "Conservar las aristas más vivas que este ángulo (0 = ninguna)"}, 30.0, 0.0, 179.0, "°")},
                   {"iterations", integer({"Iterations", "Iteraciones"}, 10, 1, 100)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               auto before = b.mesh;
               const auto it = static_cast<unsigned>(in["iterations"].get<long long>());
               const auto feat = static_cast<float>(in["featureAngle"].get<double>());
               double used = 0;
               auto after = editMesh(ctx, b, [&](TriMesh& m) {
                   if (in["mode"] == "uniform") {
                       double l = in["edgeLength"].get<double>();
                       if (l <= 0) l = analyze(m).meanEdgeLength;
                       used = l;
                       remeshUniform(m, static_cast<float>(l), it, feat);
                   } else {
                       if (in["minEdge"].get<double>() > in["maxEdge"].get<double>())
                           throw CommandError("invalid_input", ctx.say("minEdge must not exceed maxEdge.", "minEdge no puede ser mayor que maxEdge."));
                       remeshAdaptive(m, static_cast<float>(in["minEdge"].get<double>()), static_cast<float>(in["maxEdge"].get<double>()),
                                      static_cast<float>(in["maxError"].get<double>()), it, feat);
                   }
               });
               json out = changeSummary(b, *before, *after, json::object());
               if (used > 0) out["edgeLength"] = round4(used);
               out["summary"] = ctx.say("Remeshed: " + std::to_string(after->faceCount()) + " triangles.", "Remallada: " + std::to_string(after->faceCount()) + " triángulos.");
               return out;
           }});

    // ----------------------------------------------------------- bodies --
    R.add({"body.list", "body", {"List bodies", "Listar cuerpos"}, {"The bodies of the project.", "Los cuerpos del proyecto."},
           object({}), false, false,
           [](CommandContext& ctx, const json&) {
               json list = json::array();
               for (const auto& b : ctx.doc.bodies()) list.push_back(bodyJson(b));
               return json{{"bodies", list}};
           }});

    R.add({"body.rename", "body", {"Rename body", "Renombrar cuerpo"}, {"Give a body a new name.", "Cambia el nombre de un cuerpo."},
           object({{"body", body(kBody)}, {"name", string({"New name", "Nombre nuevo"})}}, {"body", "name"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               ctx.doc.renameBody(b.id, in["name"]);
               return json{{"body", b.ref()}, {"name", in["name"]}};
           }});

    R.add({"body.delete", "body", {"Delete body", "Borrar cuerpo"}, {"Remove a body from the project (undo brings it back).", "Quita un cuerpo del proyecto (deshacer lo recupera)."},
           object({{"body", body(kBody)}}, {"body"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               ctx.doc.removeBody(b.id);
               return json{{"deleted", b.ref()}, {"summary", ctx.say("Deleted " + b.name + ".", "Borrado " + b.name + ".")}};
           }});

    R.add({"body.duplicate", "body", {"Duplicate body", "Duplicar cuerpo"}, {"Copy a body, to try an operation on the copy.", "Copia un cuerpo, para probar una operación en la copia."},
           object({{"body", body(kBody)}, {"name", string({"Name of the copy", "Nombre de la copia"})}}, {"body"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Body c = ctx.doc.addBody(in.contains("name") ? in["name"].get<std::string>() : b.name + " copy", b.mesh, b.source);
               return bodyJson(c);
           }});

    R.add({"body.set_visible", "body", {"Show or hide body", "Mostrar u ocultar cuerpo"}, {"Show or hide a body in the view.", "Muestra u oculta un cuerpo en la vista."},
           object({{"body", body(kBody)}, {"visible", boolean({"Visible", "Visible"}, true)}}, {"body"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               ctx.doc.setVisible(b.id, in["visible"]);
               return json{{"body", b.ref()}, {"visible", in["visible"]}};
           }});

    // ---------------------------------------------------------- history --
    R.add({"history.undo", "history", {"Undo", "Deshacer"}, {"Undo the last change.", "Deshace el último cambio."}, object({}), false, false,
           [](CommandContext& ctx, const json&) {
               const std::string l = ctx.doc.undo();
               return json{{"undone", l.empty() ? json(nullptr) : json(l)}, {"canUndo", ctx.doc.canUndo()}, {"canRedo", ctx.doc.canRedo()}};
           }});
    R.add({"history.redo", "history", {"Redo", "Rehacer"}, {"Redo the last undone change.", "Rehace el último cambio deshecho."}, object({}), false, false,
           [](CommandContext& ctx, const json&) {
               const std::string l = ctx.doc.redo();
               return json{{"redone", l.empty() ? json(nullptr) : json(l)}, {"canUndo", ctx.doc.canUndo()}, {"canRedo", ctx.doc.canRedo()}};
           }});

    // ----------------------------------------------------------- session --
    R.add({"session.state", "help", {"Session state", "Estado de la sesión"},
           {"What is open: the bodies, the project file, unsaved changes, the undo history and where an agent may read and write.",
            "Qué hay abierto: los cuerpos, el archivo del proyecto, cambios sin guardar, el historial y dónde puede leer y escribir un agente."},
           object({}), false, false,
           [](CommandContext& ctx, const json&) {
               json bodies = json::array();
               for (const auto& b : ctx.doc.bodies()) bodies.push_back(bodyJson(b));
               json undo = json::array(), redo = json::array();
               for (const auto& h : ctx.doc.undoList()) undo.push_back(h.label);
               for (const auto& h : ctx.doc.redoList()) redo.push_back(h.label);
               json roots = json::array();
               for (const auto& r : ctx.paths.roots()) roots.push_back(r.string());
               return json{{"application", kAppName}, {"version", kVersionString}, {"units", "mm"}, {"project", ctx.doc.path()},
                           {"modified", ctx.doc.modified()}, {"bodies", bodies}, {"undo", undo}, {"redo", redo},
                           {"caller", ctx.caller}, {"allowedFolders", ctx.paths.restricted() ? roots : json("any")}};
           }});

    R.add({"guide.read", "help", {"Read the guide", "Leer la guía"},
           {"How to reverse engineer a scan with OpenRevers, step by step, for an agent or a person. Read it before the first task.",
            "Cómo hacer la ingeniería inversa de un escaneo con OpenRevers, paso a paso, para un agente o una persona. Léela antes de la primera tarea."},
           object({{"topic", choice({"Section", "Sección"}, {"all", "workflow", "mesh", "safety"}, "all")}}), false, false,
           [](CommandContext& ctx, const json& in) { return json{{"guide", guideText(ctx.lang, in["topic"].get<std::string>())}}; }});
}

}  // namespace orv

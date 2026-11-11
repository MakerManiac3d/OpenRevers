// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the commands that need no interface. The desktop app adds its
// own (views, pictures) in src/app/AppCommands.cpp.
#include "commands/Command.h"
#include "commands/Guide.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshQuad.h"
#include "core/mesh/MeshRepair.h"
#include "core/points/PointCloud.h"
#include "commands/FeatureCommands.h"
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
            {"quads", b.mesh->quads.size()},
            {"pointCloud", b.mesh->isPointCloud()}, {"normals", b.mesh->hasNormals()}, {"visible", b.visible}, {"bounds", boxJson(b.mesh->bounds())}, {"source", b.source}};
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
/// An operation on the triangles makes the quads out of date: they are
/// dropped unless `keepQuads` (the operation keeps them right itself).
template <class Op>
std::shared_ptr<TriMesh> editMesh(CommandContext& ctx, const Body& b, Op&& op, bool keepQuads = false) {
    auto m = std::make_shared<TriMesh>(*b.mesh);
    op(*m);
    if (!keepQuads) m->dropQuads();
    if (b.mesh->isPointCloud() ? m->vertices.empty() : m->faces.empty())
        throw CommandError("failed", ctx.say("The operation left nothing; nothing was changed.", "La operación no dejó nada; no se ha cambiado nada."));
    ctx.doc.setMesh(b.id, m);
    return m;
}

/// The body, which must be a triangle mesh (not a point cloud).
Body meshBody(CommandContext& ctx, const json& ref) {
    Body b = ctx.doc.body(ref);
    if (b.mesh->isPointCloud())
        throw CommandError("invalid_input", ctx.say(b.name + " is a point cloud; reconstruct a mesh from it first (points.to_mesh).",
                                                    b.name + " es una nube de puntos; reconstruye antes una malla (points.to_mesh)."));
    return b;
}

/// The body, which must be a point cloud.
Body cloudBody(CommandContext& ctx, const json& ref) {
    Body b = ctx.doc.body(ref);
    if (!b.mesh->isPointCloud())
        throw CommandError("invalid_input", ctx.say(b.name + " is a mesh, not a point cloud.", b.name + " es una malla, no una nube de puntos."));
    return b;
}

json changeSummary(const Body& b, const TriMesh& before, const TriMesh& after, json extra) {
    extra["body"] = b.ref();
    extra["facesBefore"] = before.faceCount();
    extra["facesAfter"] = after.faceCount();
    extra["verticesAfter"] = after.vertexCount();
    return extra;
}

/// tsmProblem() in the user's language.
std::string tsmProblemText(CommandContext& ctx, const std::string& p) {
    if (p == "not a quad mesh") return ctx.say("it is not a quad mesh", "no es una malla de quads");
    if (p == "open: the mesh has boundary edges") return ctx.say("it is open (it has boundary edges); close it before quad remeshing", "está abierta (tiene aristas de borde); ciérrala antes del remallado en quads");
    if (p == "a vertex has only two edges") return ctx.say("a vertex has only two edges", "un vértice tiene solo dos aristas");
    if (p == "non-manifold or inconsistently oriented edges") return ctx.say("it has non-manifold or inconsistently oriented edges", "tiene aristas no manifold o mal orientadas");
    return p;
}

std::string extensionOf(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

}  // namespace

void registerCoreCommands(CommandRegistry& R) {
    registerFeatureCommands(R);
    using namespace schema;

    // ------------------------------------------------------------ files --
    R.add({"mesh.import", "file", {"Import mesh", "Importar malla"},
           {"Read a mesh or point cloud file into a new body: STL, OBJ or PLY (read as millimetres), 3MF (converted from its own unit; every build item with its transforms), or points: E57 (every scan with its pose, metres converted to mm), XYZ or PTS. A PLY or OBJ without faces is a point cloud, with its normals if it has them.",
            "Lee un archivo de malla o de nube de puntos en un cuerpo nuevo: STL, OBJ o PLY (en milímetros), 3MF (convertido desde su unidad; todos los elementos de su construcción con sus transformaciones) o puntos: E57 (todos los escaneos con su posición, de metros a mm), XYZ o PTS. Un PLY u OBJ sin caras es una nube de puntos, con sus normales si las tiene."},
           object({{"path", path({"File to read", "Archivo a leer"}, false, "Mesh or points (*.stl *.obj *.ply *.3mf *.e57 *.xyz *.pts)")},
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
               if (mesh->isPointCloud()) {
                   const std::string n = std::to_string(mesh->vertexCount());
                   out["summary"] = ctx.say("Imported " + p.filename().string() + ": a point cloud of " + n + " points" + (mesh->hasNormals() ? " with normals." : ", no normals."),
                                            "Importado " + p.filename().string() + ": una nube de " + n + " puntos" + (mesh->hasNormals() ? " con normales." : ", sin normales."));
               } else {
                   out["summary"] = ctx.say("Imported " + p.filename().string() + ": " + std::to_string(mesh->faceCount()) + " triangles.",
                                            "Importado " + p.filename().string() + ": " + std::to_string(mesh->faceCount()) + " triángulos.");
               }
               return out;
           }});

    R.add({"mesh.export", "file", {"Export mesh", "Exportar malla"},
           {"Write a body to a file: a mesh as .stl (binary), .obj, .ply (binary) or .3mf (millimetres); a point cloud as .ply, .xyz or .obj, with its normals. Refuses to overwrite unless overwrite is true.",
            "Escribe un cuerpo en un archivo: una malla en .stl (binario), .obj, .ply (binario) o .3mf (en milímetros); una nube de puntos en .ply, .xyz u .obj, con sus normales. No sobrescribe salvo con overwrite."},
           object({{"body", body(kBody)},
                   {"path", path({"File to write", "Archivo a escribir"}, true, "STL (*.stl);;OBJ (*.obj);;PLY (*.ply);;3MF (*.3mf);;XYZ (*.xyz)")},
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
               const Body b = meshBody(ctx, in["body"]);
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
               const Body b = meshBody(ctx, in["body"]);
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
               const Body b = meshBody(ctx, in["body"]);
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
               const Body b = meshBody(ctx, in["body"]);
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
               const Body b = meshBody(ctx, in["body"]);
               auto before = b.mesh;
               auto after = editMesh(ctx, b, [&](TriMesh& m) { flipNormals(m); }, true);
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
               const Body b = meshBody(ctx, in["body"]);
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
               const Body b = meshBody(ctx, in["body"]);
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
               const Body b = meshBody(ctx, in["body"]);
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

    R.add({"mesh.remesh_quads", "mesh", {"Quad remesh", "Remallado en quads"},
           {"Rebuild the surface as quads that follow the part's shape and sharp edges (QuadriFlow). The result is ready for subdivision surfaces and T-Splines (see mesh.export_tsm). Decimate or remesh a very dense scan first: the time grows with its size.",
            "Reconstruye la superficie con quads que siguen la forma de la pieza y sus aristas vivas (QuadriFlow). El resultado sirve para superficies de subdivisión y T-Splines (ver mesh.export_tsm). Diezma o remalla antes un escaneo muy denso: el tiempo crece con su tamaño."},
           object({{"body", body(kBody)},
                   {"targetFaces", integer({"Quads wanted (approximate)", "Quads deseados (aproximado)"}, 2000, 6, 2000000)},
                   {"preserveSharp", boolean({"Follow sharp edges", "Seguir las aristas vivas"}, true)},
                   {"preserveBoundary", boolean({"Follow open rims", "Seguir los bordes abiertos"}, true)},
                   {"adaptive", boolean({"Smaller quads where the surface curves", "Quads más pequeños donde la superficie se curva"}, false)},
                   {"minimumCostFlow", boolean({"Slower solver, sometimes cleaner", "Resolución más lenta, a veces más limpia"}, false)},
                   {"seed", integer({"Random seed (same seed, same result)", "Semilla (misma semilla, mismo resultado)"}, 0, 0, 1000000)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = meshBody(ctx, in["body"]);
               auto before = b.mesh;
               QuadRemeshOptions o;
               o.targetFaces = static_cast<std::size_t>(in["targetFaces"].get<long long>());
               o.preserveSharp = in["preserveSharp"].get<bool>();
               o.preserveBoundary = in["preserveBoundary"].get<bool>();
               o.adaptive = in["adaptive"].get<bool>();
               o.minimumCostFlow = in["minimumCostFlow"].get<bool>();
               o.seed = static_cast<int>(in["seed"].get<long long>());
               QuadRemeshResult r;
               const auto t0 = std::chrono::steady_clock::now();
               auto after = editMesh(ctx, b, [&](TriMesh& m) {
                   try {
                       r = remeshQuads(m, o);
                   } catch (const std::runtime_error& e) {
                       if (std::string(e.what()) == "non-manifold edges")
                           throw CommandError("failed", ctx.say("The mesh has non-manifold edges; repair it first (mesh.repair).",
                                                                "La malla tiene aristas no manifold; repárala antes (mesh.repair)."));
                       throw CommandError("failed", ctx.say("Quad remeshing gave no result for this mesh (" + std::string(e.what()) + ").",
                                                            "El remallado en quads no ha dado resultado con esta malla (" + std::string(e.what()) + ")."));
                   }
               }, true);
               const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
               json out = changeSummary(b, *before, *after, {{"quads", r.quads}, {"droppedDegenerate", r.droppedDegenerate}, {"seconds", round4(secs)}});
               const std::string problem = tsmProblem(*after);
               out["tsmReady"] = problem.empty();
               if (!problem.empty()) out["tsmProblem"] = tsmProblemText(ctx, problem);
               out["summary"] = ctx.say("Remeshed into " + std::to_string(r.quads) + " quads.", "Remallada en " + std::to_string(r.quads) + " quads.");
               return out;
           }});

    R.add({"mesh.export_tsm", "file", {"Export T-Spline (.tsm)", "Exportar T-Spline (.tsm)"},
           {"Write a quad body as an Autodesk Fusion T-Spline file (.tsm) to open as a Form body. The quad mesh must be closed, manifold and have no vertex with only two edges; it is turned outward if needed. Refuses to overwrite unless overwrite is true.",
            "Escribe un cuerpo de quads como archivo T-Spline de Autodesk Fusion (.tsm) para abrirlo como cuerpo de Forma. La malla de quads debe ser cerrada, manifold y sin vértices de solo dos aristas; se orienta hacia fuera si hace falta. No sobrescribe salvo con overwrite."},
           object({{"body", body(kBody)},
                   {"path", path({"File to write", "Archivo a escribir"}, true, "T-Spline (*.tsm)")},
                   {"overwrite", boolean({"Replace the file if it exists", "Reemplazar el archivo si existe"}, false)}},
                  {"body", "path"}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = meshBody(ctx, in["body"]);
               const fs::path p = ctx.paths.check(in["path"].get<std::string>(), true);
               if (fs::exists(p) && !in["overwrite"].get<bool>())
                   throw CommandError("refused", ctx.say(p.string() + " already exists; pass overwrite: true to replace it.", p.string() + " ya existe; usa overwrite: true para reemplazarlo."));
               if (!b.mesh->hasQuads())
                   throw CommandError("failed", ctx.say("This body has no quads; run mesh.remesh_quads first.", "Este cuerpo no tiene quads; usa antes mesh.remesh_quads."));
               if (const std::string problem = tsmProblem(*b.mesh); !problem.empty())
                   throw CommandError("failed", ctx.say("The quad mesh cannot be a T-Spline: " + tsmProblemText(ctx, problem) + ".", "La malla de quads no puede ser un T-Spline: " + tsmProblemText(ctx, problem) + "."));
               const TsmResult r = writeTsm(*b.mesh, p);
               return json{{"path", p.string()}, {"faces", r.faces}, {"edges", r.edges}, {"vertices", r.vertices}, {"flipped", r.flipped},
                           {"bytes", fs::file_size(p)},
                           {"summary", ctx.say("Wrote " + p.filename().string() + ": " + std::to_string(r.faces) + " T-Spline faces.",
                                               "Escrito " + p.filename().string() + ": " + std::to_string(r.faces) + " caras T-Spline.")}};
           }});

    // ----------------------------------------------------------- points --
    R.add({"points.thin", "points", {"Thin points", "Aclarar puntos"},
           {"Keep one point per cube of side 'spacing' (the one nearest the cube's centre of mass): an even, lighter cloud for reconstruction.",
            "Conserva un punto por cubo de lado 'spacing' (el más cercano al centro de masas del cubo): una nube uniforme y más ligera para reconstruir."},
           object({{"body", body(kBody)},
                   {"spacing", number({"Cube side", "Lado del cubo"}, 0.5, 0.0001, 1000.0, "mm")}},
                  {"body", "spacing"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = cloudBody(ctx, in["body"]);
               std::size_t removed = 0;
               auto after = editMesh(ctx, b, [&](TriMesh& m) { removed = thinPoints(m, static_cast<float>(in["spacing"].get<double>())); });
               const std::string n = std::to_string(after->vertexCount());
               return json{{"body", b.ref()}, {"removed", removed}, {"points", after->vertexCount()},
                           {"summary", ctx.say(std::to_string(removed) + " points removed; " + n + " left.", std::to_string(removed) + " puntos quitados; quedan " + n + ".")}};
           }});

    R.add({"points.estimate_normals", "points", {"Estimate normals", "Calcular normales"},
           {"Give every point a normal from the plane through its nearest neighbours, then orient them: 'propagate' spreads one side over the cloud (open or closed scans), 'outward' points away from the centre (closed, roughly convex parts), 'keep' keeps the side of the normals the points already have. Needed before points.to_mesh.",
            "Da a cada punto una normal a partir del plano de sus vecinos más cercanos y después las orienta: 'propagate' extiende un mismo lado por toda la nube (escaneos abiertos o cerrados), 'outward' las apunta hacia fuera del centro (piezas cerradas y más o menos convexas), 'keep' conserva el lado de las normales que ya tienen. Hace falta antes de points.to_mesh."},
           object({{"body", body(kBody)},
                   {"neighbours", integer({"Neighbours per point", "Vecinos por punto"}, 16, 3, 100)},
                   {"orientation", choice({"Orientation", "Orientación"}, {"propagate", "outward", "keep"}, "propagate")}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = cloudBody(ctx, in["body"]);
               const std::string o = in["orientation"];
               const NormalOrientation orient = o == "outward" ? NormalOrientation::Outward : o == "keep" ? NormalOrientation::Keep : NormalOrientation::Propagate;
               if (orient == NormalOrientation::Keep && !b.mesh->hasNormals())
                   throw CommandError("invalid_input", ctx.say("The points have no normals to keep the side of; use 'propagate' or 'outward'.",
                                                               "Los puntos no tienen normales cuyo lado conservar; usa 'propagate' u 'outward'."));
               NormalsResult r;
               editMesh(ctx, b, [&](TriMesh& m) { r = estimateNormals(m, static_cast<int>(in["neighbours"].get<long long>()), orient); });
               return json{{"body", b.ref()}, {"points", r.points}, {"flipped", r.flipped}, {"isolated", r.isolated}, {"meanSpacing", round4(r.meanSpacing)},
                           {"summary", ctx.say("Normals for " + std::to_string(r.points) + " points (mean spacing " + std::to_string(round4(r.meanSpacing)).substr(0, 6) + " mm).",
                                               "Normales de " + std::to_string(r.points) + " puntos (separación media " + std::to_string(round4(r.meanSpacing)).substr(0, 6) + " mm).")}};
           }});

    R.add({"points.to_mesh", "points", {"Mesh from points (Poisson)", "Malla desde puntos (Poisson)"},
           {"Reconstruct a watertight surface from a point cloud with normals (screened Poisson) into a new body; the cloud is kept. 'depth' sets the detail (the smallest feature is about size / 2^depth). For an open scan, 'trim' removes the surface Poisson invents where there were no points (e.g. 0.05-0.1).",
            "Reconstruye una superficie cerrada a partir de una nube de puntos con normales (Poisson apantallado) en un cuerpo nuevo; la nube se conserva. 'depth' fija el detalle (el detalle más pequeño es unos tamaño / 2^depth). Para un escaneo abierto, 'trim' quita la superficie que Poisson inventa donde no había puntos (p. ej. 0,05-0,1)."},
           object({{"body", body(kBody)},
                   {"depth", integer({"Octree depth (detail)", "Profundidad del octree (detalle)"}, 8, 4, 12)},
                   {"pointWeight", number({"How closely the surface follows the points", "Cuánto se ciñe la superficie a los puntos"}, 4.0, 0.0, 100.0)},
                   {"samplesPerNode", number({"Points per cell (raise for noisy scans)", "Puntos por celda (súbelo en escaneos con ruido)"}, 1.5, 1.0, 20.0)},
                   {"trim", number({"Remove the lowest-density fraction of the surface", "Quitar la fracción de superficie con menos densidad"}, 0.0, 0.0, 0.5)},
                   {"keepLargest", boolean({"After trimming, keep only the largest piece", "Tras recortar, conservar solo la pieza mayor"}, true)},
                   {"name", string({"Name of the new body", "Nombre del cuerpo nuevo"})}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = cloudBody(ctx, in["body"]);
               if (!b.mesh->hasNormals())
                   throw CommandError("invalid_input", ctx.say("The points have no normals; run points.estimate_normals first.",
                                                               "Los puntos no tienen normales; usa antes points.estimate_normals."));
               PoissonOptions o;
               o.depth = static_cast<int>(in["depth"].get<long long>());
               o.pointWeight = static_cast<float>(in["pointWeight"].get<double>());
               o.samplesPerNode = static_cast<float>(in["samplesPerNode"].get<double>());
               o.trimQuantile = static_cast<float>(in["trim"].get<double>());
               o.keepLargest = in["keepLargest"].get<bool>();
               auto mesh = std::make_shared<TriMesh>();
               PoissonResult r;
               const auto t0 = std::chrono::steady_clock::now();
               try {
                   r = reconstructPoisson(*b.mesh, o, *mesh);
               } catch (const std::runtime_error& e) {
                   throw CommandError("failed", ctx.say("Reconstruction failed: " + std::string(e.what()) + ".", "La reconstrucción ha fallado: " + std::string(e.what()) + "."));
               }
               const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
               const std::string name = in.contains("name") ? in["name"].get<std::string>() : b.name + " mesh";
               const Body nb = ctx.doc.addBody(name, mesh, b.source);
               json out = bodyJson(nb);
               out["from"] = b.ref();
               out["facesTrimmed"] = r.facesTrimmed;
               out["closed"] = analyze(*mesh).closed;
               out["seconds"] = round4(secs);
               out["summary"] = ctx.say("Reconstructed " + nb.ref() + ": " + std::to_string(mesh->faceCount()) + " triangles.",
                                        "Reconstruida " + nb.ref() + ": " + std::to_string(mesh->faceCount()) + " triángulos.");
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

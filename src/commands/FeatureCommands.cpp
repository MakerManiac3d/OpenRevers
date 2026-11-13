// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — commands on the shapes of a body: segmentation, fitting a
// shape on a spot, listing, measuring between shapes, renaming, deleting.
#include "commands/Command.h"
#include "core/util/Cancel.h"
#include "commands/FeatureCommands.h"

#include "core/fit/Align.h"
#include "core/fit/Register.h"
#include "core/fit/Segmentation.h"
#include "core/mesh/EdgeMap.h"
#include "doc/Document.h"

#include <cmath>
#include <map>
#include <numeric>

namespace orv {
namespace {

using namespace schema;
constexpr double kPi = 3.14159265358979323846;
const Text kBody{"The body: its reference (b1) or its name", "El cuerpo: su referencia (b1) o su nombre"};
const Text kFeature{"The shape: its reference (f1) or its name", "La forma: su referencia (f1) o su nombre"};

double r4(double v) { return std::round(v * 1e4) / 1e4; }
json v3(const Vec3d& v) { return json::array({r4(v.x()), r4(v.y()), r4(v.z())}); }
Vec3d v3(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }

Body meshBody(CommandContext& ctx, const json& ref) {
    Body b = ctx.doc.body(ref);
    if (b.mesh->isPointCloud())
        throw CommandError("invalid_input", ctx.say(b.name + " is a point cloud; reconstruct a mesh from it first (points.to_mesh).",
                                                    b.name + " es una nube de puntos; reconstruye antes una malla (points.to_mesh)."));
    if (b.isPart())
        throw CommandError("invalid_input", ctx.say(b.name + " is a parametric part: its triangles only show its solid. Change it with the part commands.",
                                                    b.name + " es una pieza paramétrica: sus triángulos solo muestran su sólido. Se cambia con los comandos de pieza."));
    return b;
}

std::string typeName(CommandContext& ctx, PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Plane: return ctx.say("Plane", "Plano");
        case PrimitiveType::Sphere: return ctx.say("Sphere", "Esfera");
        case PrimitiveType::Cylinder: return ctx.say("Cylinder", "Cilindro");
        case PrimitiveType::Cone: return ctx.say("Cone", "Cono");
        case PrimitiveType::Torus: return ctx.say("Torus", "Toro");
    }
    return "?";
}

std::string pluralName(CommandContext& ctx, PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Plane: return ctx.say("planes", "planos");
        case PrimitiveType::Sphere: return ctx.say("spheres", "esferas");
        case PrimitiveType::Cylinder: return ctx.say("cylinders", "cilindros");
        case PrimitiveType::Cone: return ctx.say("cones", "conos");
        case PrimitiveType::Torus: return ctx.say("tori", "toros");
    }
    return "?";
}

json matrixJson(const Transform& t) {
    json rows = json::array();
    const Eigen::Matrix4d m = t.matrix();
    for (int i = 0; i < 4; ++i)
        rows.push_back({std::round(m(i, 0) * 1e9) / 1e9, std::round(m(i, 1) * 1e9) / 1e9, std::round(m(i, 2) * 1e9) / 1e9, std::round(m(i, 3) * 1e6) / 1e6});
    return rows;
}

/// Move a body (its mesh and its shapes) and say how.
json applyTransform(CommandContext& ctx, const Body& b, const Transform& t) {
    auto mesh = std::make_shared<TriMesh>(*b.mesh);
    transformMesh(*mesh, t);
    std::shared_ptr<Features> fs;
    if (b.features) {
        fs = std::make_shared<Features>(*b.features);
        for (auto& f : fs->list) f.shape = transformPrimitive(f.shape, t);
    }
    ctx.doc.setMesh(b.id, mesh, fs);
    const Eigen::AngleAxisd aa(t.linear());
    return json{{"body", b.ref()}, {"matrix", matrixJson(t)}, {"rotationDeg", r4(aa.angle() * 180 / kPi)},
                {"translation", v3(Vec3d(t.translation()))}, {"bounds", {{"min", v3(Vec3d(mesh->bounds().min.cast<double>()))},
                                                                        {"max", v3(Vec3d(mesh->bounds().max.cast<double>()))}}}};
}

}  // namespace

json featureJson(const Feature& f) {
    const Primitive& s = f.shape;
    json j = {{"ref", f.ref()}, {"name", f.name}, {"type", primitiveName(s.type)}, {"sigma", r4(s.rms)}, {"maxDeviation", r4(s.maxAbs)},
              {"points", s.points}, {"outliers", s.outliers}, {"faces", f.faces}, {"area", r4(f.area)}, {"units", "mm"}};
    switch (s.type) {
        case PrimitiveType::Plane: j["point"] = v3(s.origin); j["normal"] = v3(s.axis); break;
        case PrimitiveType::Sphere: j["center"] = v3(s.origin); j["diameter"] = r4(2 * s.radius); break;
        case PrimitiveType::Cylinder: j["axisPoint"] = v3(s.origin); j["axis"] = v3(s.axis); j["diameter"] = r4(2 * s.radius); break;
        case PrimitiveType::Cone: j["apex"] = v3(s.origin); j["axis"] = v3(s.axis); j["angleDeg"] = r4(2 * s.angle * 180 / kPi); j["halfAngleDeg"] = r4(s.angle * 180 / kPi); break;
        case PrimitiveType::Torus: j["center"] = v3(s.origin); j["axis"] = v3(s.axis); j["majorRadius"] = r4(s.radius); j["minorRadius"] = r4(s.radius2); break;
    }
    if (f.region >= 0) j["region"] = f.region;
    return j;
}

/// Name like "Plane 3": one more than the highest number of that type so far.
std::string nextName(CommandContext& ctx, const Features& fs, PrimitiveType t) {
    const std::string base = typeName(ctx, t);
    int n = 0;
    for (const auto& f : fs.list)
        if (f.name.rfind(base + " ", 0) == 0) {
            try { n = std::max(n, std::stoi(f.name.substr(base.size() + 1))); } catch (...) {}
        }
    return base + " " + std::to_string(n + 1);
}

const Feature& findFeature(CommandContext& ctx, const Body& b, const json& ref) {
    if (!b.features || b.features->list.empty())
        throw CommandError("invalid_input", ctx.say(b.name + " has no shapes yet; run features.segment or features.fit.", b.name + " aún no tiene formas; usa features.segment o features.fit."));
    const Feature* f = b.features->find(ref.get<std::string>());
    if (!f) throw CommandError("not_found", ctx.say("No shape \"" + ref.get<std::string>() + "\" on " + b.name + ".", "No hay ninguna forma \"" + ref.get<std::string>() + "\" en " + b.name + "."));
    return *f;
}

/// How two shapes stand to each other: angle between their directions and
/// the distance that makes sense for the pair.
json measurePair(const Primitive& a, const Primitive& b) {
    json j;
    const bool aDir = a.type != PrimitiveType::Sphere, bDir = b.type != PrimitiveType::Sphere;
    if (aDir && bDir) {
        const double c = std::clamp(std::abs(a.axis.dot(b.axis)), 0.0, 1.0);
        j["angleDeg"] = r4(std::acos(c) * 180 / kPi);
    }
    auto isAxis = [](const Primitive& p) { return p.type == PrimitiveType::Cylinder || p.type == PrimitiveType::Cone || p.type == PrimitiveType::Torus; };
    auto centre = [](const Primitive& p) { return p.origin; };
    if (a.type == PrimitiveType::Plane && b.type == PrimitiveType::Plane) {
        if (j["angleDeg"].get<double>() < 1.0) j["distance"] = r4(std::abs(a.distance(b.origin) + b.distance(a.origin)) / 2);
    } else if (a.type == PrimitiveType::Plane || b.type == PrimitiveType::Plane) {
        const Primitive& pl = a.type == PrimitiveType::Plane ? a : b;
        const Primitive& o = a.type == PrimitiveType::Plane ? b : a;
        if (o.type == PrimitiveType::Sphere || o.type == PrimitiveType::Torus) j["distance"] = r4(std::abs(pl.distance(centre(o))));
        else if (isAxis(o) && std::abs(pl.axis.dot(o.axis)) < std::sin(1.0 * kPi / 180)) j["distance"] = r4(std::abs(pl.distance(o.origin)));  // axis parallel to the plane
        if (isAxis(o)) {
            const double den = pl.axis.dot(o.axis);
            if (std::abs(den) > 1e-9) j["axisIntersection"] = v3(Vec3d(o.origin + o.axis * (pl.axis.dot(pl.origin - o.origin) / den)));
        }
    } else if (isAxis(a) && isAxis(b)) {
        const Vec3d w = b.origin - a.origin, c = a.axis.cross(b.axis);
        if (std::abs(a.axis.dot(b.axis)) > std::cos(10.0 * kPi / 180)) {
            // Nominally parallel axes (holes of a pattern, a shaft and its
            // bore): the centre distance, measured across the mean direction
            // half way between the two shapes. A fitted axis is never exactly
            // parallel, and the shortest distance between two lines that
            // almost are means nothing (it falls anywhere along them).
            Vec3d m = a.axis + (a.axis.dot(b.axis) < 0 ? -b.axis : b.axis);
            m.normalize();
            const double t = 0.5 * (a.origin.dot(m) + b.origin.dot(m));
            auto at = [&](const Primitive& p) { return Vec3d(p.origin + p.axis * ((t - p.origin.dot(m)) / p.axis.dot(m))); };
            const Vec3d d = at(b) - at(a);
            j["distance"] = r4((d - d.dot(m) * m).norm());
            if (c.norm() > 1e-9) j["shortestDistance"] = r4(std::abs(w.dot(c.normalized())));
        } else {
            j["distance"] = r4(std::abs(w.dot(c.normalized())));  // crossing axes: shortest distance
        }
    } else {
        j["distance"] = r4((centre(a) - centre(b)).norm());
    }
    return j;
}

void registerFeatureCommands(CommandRegistry& R) {
    // ------------------------------------------------------------ align --
    R.add({"align.principal", "align", {"Align to principal axes", "Alinear a los ejes principales"},
           {"Move the body so that its surface's centroid is at the origin and its longest, middle and shortest extents lie along X, Y and Z. A quick, repeatable start before datums or best fit.",
            "Mueve el cuerpo para que el centroide de su superficie quede en el origen y sus dimensiones mayor, media y menor sobre X, Y y Z. Un punto de partida rápido y repetible antes de los datums o el mejor ajuste."},
           object({{"body", body(kBody)}}, {"body"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               json out = applyTransform(ctx, b, principalAlignment(*b.mesh));
               out["summary"] = ctx.say("Aligned " + b.name + " to its principal axes.", b.name + " alineado a sus ejes principales.");
               return out;
           }});

    R.add({"align.datums", "align", {"Align by datums (3-2-1)", "Alinear por datums (3-2-1)"},
           {"Place the part by its shapes, as on a measuring machine. Primary (a face or an axis) sets Z: the part sits on the face, Z points into it. Secondary sets X (into the part from a face, or along an axis); with a hole along Z, X points from it to the tertiary. The origin: where the datums meet (a face fixes one coordinate, an axis two, a sphere three).",
            "Coloca la pieza por sus formas, como en una máquina de medir. El primario (una cara o un eje) fija Z: la pieza se apoya en la cara y Z entra en ella. El secundario fija X (hacia dentro desde una cara, o a lo largo de un eje); con un taladro a lo largo de Z, X va de él al terciario. El origen: donde se cortan los datums (una cara fija una coordenada, un eje dos, una esfera tres)."},
           object({{"body", body(kBody)}, {"primary", string(kFeature, "feature")}, {"secondary", string(kFeature, "feature")}, {"tertiary", string(kFeature, "feature")}},
                  {"body", "primary"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Feature& a = findFeature(ctx, b, in["primary"]);
               std::optional<Primitive> sb, sc;
               std::string names = a.name;
               if (in.contains("secondary")) { const Feature& f = findFeature(ctx, b, in["secondary"]); sb = f.shape; names += ", " + f.name; }
               if (in.contains("tertiary")) { const Feature& f = findFeature(ctx, b, in["tertiary"]); sc = f.shape; names += ", " + f.name; }
               std::string why;
               const auto t = datumAlignment(a.shape, sb, sc, &why);
               if (!t) throw CommandError("invalid_input", ctx.say("These datums do not fix the part: " + why + ".", "Estos datums no fijan la pieza: " + why + "."));
               json out = applyTransform(ctx, b, *t);
               out["summary"] = ctx.say("Aligned " + b.name + " on " + names + ".", b.name + " alineado sobre " + names + ".");
               return out;
           }});

    R.add({"align.best_fit", "align", {"Best-fit alignment", "Alineación de mejor ajuste"},
           {"Move a body onto a reference body (another scan of the part, or its nominal mesh) so that their surfaces match best (robust point-to-plane ICP: stray points and missing areas do not pull it). For bodies far apart or turned, set coarse: it first matches their principal axes. Reports the remaining deviation.",
            "Mueve un cuerpo sobre un cuerpo de referencia (otro escaneo de la pieza, o su malla nominal) para que sus superficies coincidan lo mejor posible (ICP punto-plano robusto: los puntos sueltos y las zonas que faltan no tiran de él). Si están lejos o girados, activa coarse: primero hace coincidir sus ejes principales. Informa de la desviación que queda."},
           object({{"body", body(kBody)}, {"reference", body({"The reference body", "El cuerpo de referencia"})},
                   {"coarse", boolean({"Start by matching principal axes", "Empezar haciendo coincidir los ejes principales"}, false)},
                   {"maxDistance", number({"Ignore matches farther than (0: automatic)", "Ignorar parejas a más de (0: automático)"}, 0.0, 0.0, 10000.0, "mm")}},
                  {"body", "reference"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Body ref = ctx.doc.body(in["reference"]);
               if (b.id == ref.id) throw CommandError("invalid_input", ctx.say("A body cannot be aligned to itself.", "Un cuerpo no se puede alinear consigo mismo."));
               BestFitOptions o;
               o.coarseStart = in["coarse"].get<bool>();
               o.maxDistance = in["maxDistance"].get<double>();
               BestFitResult r;
               try {
                   r = bestFit(*b.mesh, *ref.mesh, o);
               } catch (const std::runtime_error& e) {
                   throw CommandError("failed", ctx.say("Best fit failed: " + std::string(e.what()) + ". Try coarse: true, or align both first.",
                                                        "El mejor ajuste ha fallado: " + std::string(e.what()) + ". Prueba con coarse: true, o alinea antes los dos."));
               }
               json out = applyTransform(ctx, b, r.transform);
               out["reference"] = ref.ref();
               out["rms"] = r4(r.rms);
               out["median"] = r4(r.median);
               out["p95"] = r4(r.p95);
               out["overlapPercent"] = r4(100 * r.overlap);
               out["iterations"] = r.iterations;
               out["summary"] = ctx.say("Aligned " + b.name + " to " + ref.name + ": RMS " + std::to_string(r4(r.rms)).substr(0, 6) + " mm, overlap " +
                                            std::to_string(static_cast<int>(100 * r.overlap)) + " %.",
                                        b.name + " alineado con " + ref.name + ": RMS " + std::to_string(r4(r.rms)).substr(0, 6) + " mm, solape " +
                                            std::to_string(static_cast<int>(100 * r.overlap)) + " %.");
               return out;
           }});


    R.add({"align.global", "align", {"Automatic alignment", "Alineación automática"},
           {"Put a scan onto another from their shape alone, wherever each one is and however it is turned: shape descriptors (FPFH) matched under RANSAC, then the best-fit ICP. For scans of the same part from different sides (they must overlap: a third or more of the moving scan seen in the reference). Reports the overlap and the deviation left; check it in the view.",
            "Coloca un escaneo sobre otro solo por su forma, estén donde estén y como estén girados: descriptores de forma (FPFH) emparejados con RANSAC y después el ICP de mejor ajuste. Para escaneos de la misma pieza desde lados distintos (deben solaparse: un tercio o más del escaneo que se mueve debe verse en el de referencia). Informa del solape y de la desviación que queda; compruébalo en la vista."},
           object({{"body", body(kBody)}, {"reference", body({"The reference body (stays put)", "El cuerpo de referencia (no se mueve)"})},
                   {"voxel", number({"Sampling cell (0: automatic, 1/60 of the size); larger is faster, smaller finds small details", "Celda de muestreo (0: automática, 1/60 del tamaño); mayor es más rápido, menor encuentra detalles pequeños"}, 0.0, 0.0, 1000.0, "mm")},
                   {"refine", boolean({"Finish with the best-fit ICP", "Terminar con el ICP de mejor ajuste"}, true)}},
                  {"body", "reference"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Body ref = ctx.doc.body(in["reference"]);
               if (b.id == ref.id) throw CommandError("invalid_input", ctx.say("A body cannot be aligned to itself.", "Un cuerpo no se puede alinear consigo mismo."));
               GlobalRegOptions o;
               o.voxel = in["voxel"].get<double>();
               o.refine = in["refine"].get<bool>();
               GlobalRegResult r;
               try {
                   r = globalRegistration(*b.mesh, *ref.mesh, o);
               } catch (const Cancelled&) {
                   throw;
               } catch (const std::exception& e) {
                   throw CommandError("failed", ctx.say("Automatic alignment failed: " + std::string(e.what()) + ". Try align.point_pairs (three points picked on each).",
                                                        "La alineación automática ha fallado: " + std::string(e.what()) + ". Prueba con align.point_pairs (tres puntos elegidos en cada uno)."));
               }
               json out = applyTransform(ctx, b, r.transform);
               out["reference"] = ref.ref();
               out["voxel"] = r4(r.voxel);
               out["matches"] = r.correspondences;
               out["agreeing"] = r.inliers;
               out["coarseOverlapPercent"] = r4(100 * r.coarseOverlap);
               if (o.refine) {
                   out["rms"] = r4(r.refined.rms);
                   out["median"] = r4(r.refined.median);
                   out["p95"] = r4(r.refined.p95);
                   out["overlapPercent"] = r4(100 * r.refined.overlap);
               }
               const int ov = static_cast<int>(100 * (o.refine ? r.refined.overlap : r.coarseOverlap));
               out["summary"] = ctx.say("Aligned " + b.name + " to " + ref.name + " by shape: overlap " + std::to_string(ov) + " %" +
                                            (o.refine ? ", median " + std::to_string(r4(r.refined.median)).substr(0, 6) + " mm." : "."),
                                        b.name + " alineado con " + ref.name + " por su forma: solape " + std::to_string(ov) + " %" +
                                            (o.refine ? ", mediana " + std::to_string(r4(r.refined.median)).substr(0, 6) + " mm." : "."));
               return out;
           }});

    R.add({"align.point_pairs", "align", {"Align by pairs of points", "Alinear por pares de puntos"},
           {"Put a body onto a reference from three or more pairs of points picked on both (the same feature on each: a corner, a hole's centre, a mark), in the same order; then, with refine, the best-fit ICP. Gives each pair's remaining distance: a large one is a pair picked wrong.",
            "Coloca un cuerpo sobre otro de referencia a partir de tres o más pares de puntos elegidos en los dos (el mismo detalle en cada uno: una esquina, el centro de un taladro, una marca), en el mismo orden; después, con refine, el ICP de mejor ajuste. Da la distancia que queda en cada par: una grande es un par mal elegido."},
           object({{"body", body(kBody)}, {"reference", body({"The reference body (stays put)", "El cuerpo de referencia (no se mueve)"})},
                   {"points", json{{"x-kind", "point-list"}, {"type", "array"}, {"minItems", 3}, {"items", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                                   {"description", "Points on the body, [[x, y, z], ...]"}}},
                   {"referencePoints", json{{"x-kind", "point-list"}, {"x-of", "reference"}, {"type", "array"}, {"minItems", 3}, {"items", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                                            {"description", "The same points on the reference, in the same order"}}},
                   {"refine", boolean({"Finish with the best-fit ICP", "Terminar con el ICP de mejor ajuste"}, true)}},
                  {"body", "reference", "points", "referencePoints"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Body ref = ctx.doc.body(in["reference"]);
               if (b.id == ref.id) throw CommandError("invalid_input", ctx.say("A body cannot be aligned to itself.", "Un cuerpo no se puede alinear consigo mismo."));
               std::vector<Vec3d> a, c;
               for (const auto& p : in["points"]) a.push_back(v3(p));
               for (const auto& p : in["referencePoints"]) c.push_back(v3(p));
               std::vector<double> res;
               Transform t;
               try {
                   t = rigidFromPairs(a, c, &res);
               } catch (const std::invalid_argument& e) {
                   throw CommandError("invalid_input", e.what());
               }
               json pairs = json::array();
               for (double d : res) pairs.push_back(r4(d));
               BestFitResult fine;
               const bool refine = in["refine"].get<bool>();
               if (refine) {
                   TriMesh moved = *b.mesh;
                   transformMesh(moved, t);
                   double worst = 0;
                   for (double d : res) worst = std::max(worst, d);
                   BestFitOptions bo;
                   bo.maxDistance = std::max(1.0, 3 * worst);
                   try {
                       fine = bestFit(moved, *ref.mesh, bo);
                       t = fine.transform * t;
                   } catch (const std::runtime_error&) {
                       // The pairs alone, then.
                   }
               }
               json out = applyTransform(ctx, b, t);
               out["reference"] = ref.ref();
               out["pairResiduals"] = pairs;
               if (refine && fine.iterations > 0) {
                   out["rms"] = r4(fine.rms);
                   out["median"] = r4(fine.median);
                   out["overlapPercent"] = r4(100 * fine.overlap);
               }
               double worst = 0;
               for (double d : res) worst = std::max(worst, d);
               out["summary"] = ctx.say("Aligned " + b.name + " to " + ref.name + " by " + std::to_string(res.size()) + " pairs (worst pair " + std::to_string(r4(worst)).substr(0, 5) + " mm" +
                                            (refine && fine.iterations > 0 ? ", then refined: median " + std::to_string(r4(fine.median)).substr(0, 6) + " mm)." : ")."),
                                        b.name + " alineado con " + ref.name + " por " + std::to_string(res.size()) + " pares (peor par " + std::to_string(r4(worst)).substr(0, 5) + " mm" +
                                            (refine && fine.iterations > 0 ? "; afinado después: mediana " + std::to_string(r4(fine.median)).substr(0, 6) + " mm)." : ")."));
               return out;
           }});

    R.add({"align.move", "align", {"Move or rotate", "Mover o girar"},
           {"Rotate a body by 'angle' degrees about an axis through 'about', then translate it.",
            "Gira un cuerpo 'angle' grados alrededor de un eje que pasa por 'about' y después lo traslada."},
           object({{"body", body(kBody)}, {"translate", point({"Translation", "Traslación"})}, {"axis", point({"Rotation axis", "Eje de giro"}, nullptr)},
                   {"angle", number({"Rotation angle", "Ángulo de giro"}, 0.0, -360.0, 360.0, "deg")}, {"about", point({"Point on the rotation axis", "Punto del eje de giro"})}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               Transform t = Transform::Identity();
               const double ang = in["angle"].get<double>();
               if (ang != 0.0) {
                   const Vec3d ax = in.contains("axis") ? v3(in["axis"]) : Vec3d::UnitZ();
                   if (ax.norm() < 1e-12) throw CommandError("invalid_input", ctx.say("The rotation axis cannot be zero.", "El eje de giro no puede ser cero."));
                   const Vec3d c = in.contains("about") ? v3(in["about"]) : Vec3d::Zero();
                   t.translate(c);
                   t.rotate(Eigen::AngleAxisd(ang * kPi / 180, ax.normalized()));
                   t.translate(-c);
               }
               if (in.contains("translate")) t.pretranslate(v3(in["translate"]));
               return applyTransform(ctx, b, t);
           }});

    R.add({"features.segment", "features", {"Find shapes (segment)", "Detectar formas (segmentar)"},
           {"Split a mesh into regions that each follow a plane, sphere, cylinder, cone or torus, and fit that shape to each (with its σ). Replaces the body's shapes. Leaves fillets, blends and freeform areas unassigned. 'noise' is the scanner's noise in mm (0: estimated); a face joins a region within tolerance × noise and with its normal within 'angle' degrees.",
            "Divide una malla en regiones que siguen cada una un plano, esfera, cilindro, cono o toro, y ajusta esa forma a cada una (con su σ). Reemplaza las formas del cuerpo. Deja sin asignar redondeos, transiciones y zonas libres. 'noise' es el ruido del escáner en mm (0: se estima); una cara entra en una región a menos de tolerance × noise y con su normal a menos de 'angle' grados."},
           object({{"body", body(kBody)},
                   {"noise", number({"Scanner noise (0: estimate)", "Ruido del escáner (0: estimarlo)"}, 0.0, 0.0, 10.0, "mm")},
                   {"tolerance", number({"Tolerance, in multiples of the noise", "Tolerancia, en múltiplos del ruido"}, 3.0, 1.0, 20.0)},
                   {"angle", number({"Largest normal deviation", "Desviación máxima de la normal"}, 12.0, 1.0, 45.0, "deg")},
                   {"minFaces", integer({"Smallest region, in faces (0: automatic)", "Región más pequeña, en caras (0: automática)"}, 0, 0, 1000000)},
                   {"maxListed", integer({"Shapes to list in the answer, largest first (features.list gives all)", "Formas a listar en la respuesta, de mayor a menor (features.list da todas)"}, 40, 0, 100000)}},
                  {"body"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = meshBody(ctx, in["body"]);
               SegmentOptions o;
               o.noise = in["noise"].get<double>();
               o.tolerance = in["tolerance"].get<double>();
               o.angleDeg = in["angle"].get<double>();
               o.minFaces = static_cast<std::size_t>(in["minFaces"].get<long long>());
               const auto t0 = std::chrono::steady_clock::now();
               const Segmentation s = segment(*b.mesh, o);
               auto fs = std::make_shared<Features>();
               fs->faceRegion = s.faceRegion;
               fs->noise = s.noise;
               for (std::size_t i = 0; i < s.regions.size(); ++i) {
                   Feature f;
                   f.id = fs->nextId++;
                   f.shape = s.regions[i].shape;
                   f.region = static_cast<std::int32_t>(i);
                   f.faces = s.regions[i].faces;
                   f.area = s.regions[i].area;
                   f.name = nextName(ctx, *fs, f.shape.type);
                   fs->list.push_back(f);
               }
               ctx.doc.setFeatures(b.id, fs);
               std::map<std::string, int> count;
               json list = json::array();
               const auto maxListed = static_cast<std::size_t>(in["maxListed"].get<long long>());
               for (const auto& f : fs->list) {
                   ++count[pluralName(ctx, f.shape.type)];
                   if (list.size() < maxListed) list.push_back(featureJson(f));
               }
               const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
               std::string sum;
               for (const auto& [k, n] : count) sum += (sum.empty() ? "" : ", ") + std::to_string(n) + " " + k;
               const double un = b.mesh->faces.empty() ? 0.0 : 100.0 * static_cast<double>(s.unassignedFaces) / static_cast<double>(b.mesh->faces.size());
               return json{{"body", b.ref()}, {"noise", r4(s.noise)}, {"features", list}, {"total", fs->list.size()}, {"listed", list.size()}, {"unassignedFacesPercent", r4(un)}, {"seconds", r4(secs)},
                           {"summary", ctx.say("Found " + std::to_string(fs->list.size()) + " shapes (" + sum + "); noise " + std::to_string(r4(s.noise)).substr(0, 6) + " mm.",
                                               "Encontradas " + std::to_string(fs->list.size()) + " formas (" + sum + "); ruido " + std::to_string(r4(s.noise)).substr(0, 6) + " mm.")}};
           }});

    R.add({"features.fit", "features", {"Fit a shape here", "Ajustar una forma aquí"},
           {"Fit a shape to the faces within 'radius' of a point (for a hole, a boss, a face the segmentation left out). 'type' auto picks the simplest that fits. Adds it to the body's shapes.",
            "Ajusta una forma a las caras a menos de 'radius' de un punto (un taladro, un saliente, una cara que la segmentación dejó fuera). Con 'type' auto se elige la más sencilla que encaja. La añade a las formas del cuerpo."},
           object({{"body", body(kBody)},
                   {"at", point({"Point on the surface", "Punto sobre la superficie"})},
                   {"radius", number({"Radius around the point", "Radio alrededor del punto"}, 5.0, 0.01, 10000.0, "mm")},
                   {"type", choice({"Shape", "Forma"}, {"auto", "plane", "sphere", "cylinder", "cone", "torus"}, "auto")},
                   {"name", string({"Name", "Nombre"})}},
                  {"body", "at"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = meshBody(ctx, in["body"]);
               const Vec3d at = v3(in["at"]);
               const double rad = in["radius"].get<double>();
               std::vector<Vec3d> p, n;
               std::vector<double> area;
               std::vector<std::uint32_t> faceOf;
               const TriMesh& m = *b.mesh;
               std::vector<std::int32_t> slot(m.faces.size(), -1);
               for (std::size_t f = 0; f < m.faces.size(); ++f) {
                   const Tri& t = m.faces[f];
                   const Vec3d a = m.vertices[t[0]].cast<double>(), bb = m.vertices[t[1]].cast<double>(), c = m.vertices[t[2]].cast<double>();
                   const Vec3d cen = (a + bb + c) / 3.0;
                   if ((cen - at).norm() > rad) continue;
                   const Vec3d nn = (bb - a).cross(c - a);
                   if (nn.norm() <= 0) continue;
                   slot[f] = static_cast<std::int32_t>(p.size());
                   p.push_back(cen);
                   n.push_back(nn.normalized());
                   area.push_back(0.5 * nn.norm());
                   faceOf.push_back(static_cast<std::uint32_t>(f));
               }
               if (p.size() < 12)
                   throw CommandError("failed", ctx.say("Too few faces near that point (" + std::to_string(p.size()) + "); use a larger radius or a point on the surface.",
                                                        "Muy pocas caras cerca de ese punto (" + std::to_string(p.size()) + "); usa un radio mayor o un punto sobre la superficie."));
               const std::string type = in["type"];
               const double noise = b.features ? b.features->noise : 0.0;
               auto fitAll = [&](const std::vector<Vec3d>& pp, const std::vector<Vec3d>& nn) {
                   return type == "auto" ? fitBest(pp, nn, noise) : fitPrimitive(*primitiveFromName(type), pp, nn);
               };
               // First guess on the faces joined smoothly to the one under the
               // point: across a sharp edge lies another face of the part.
               std::vector<Vec3d> p0, n0;
               {
                   std::size_t nearest = 0;
                   for (std::size_t i = 1; i < p.size(); ++i)
                       if ((p[i] - at).squaredNorm() < (p[nearest] - at).squaredNorm()) nearest = i;
                   const EdgeMap em(m);
                   std::vector<char> seen(p.size(), 0);
                   std::vector<std::size_t> queue{nearest};
                   seen[nearest] = 1;
                   for (std::size_t q = 0; q < queue.size(); ++q) {
                       const std::size_t i = queue[q];
                       for (std::size_t k = 0; k < 3; ++k) {
                           const auto e = em.edgeOf(faceOf[i], k);
                           if (e == EdgeMap::kNone) continue;
                           for (auto g : em.faces(em.edge(e))) {
                               const auto j = slot[g];
                               if (j < 0 || seen[static_cast<std::size_t>(j)] || n[i].dot(n[static_cast<std::size_t>(j)]) < std::cos(30.0 * kPi / 180)) continue;
                               seen[static_cast<std::size_t>(j)] = 1;
                               queue.push_back(static_cast<std::size_t>(j));
                           }
                       }
                   }
                   for (auto i : queue) { p0.push_back(p[i]); n0.push_back(n[i]); }
               }
               std::optional<Primitive> s = p0.size() >= 12 ? fitAll(p0, n0) : fitAll(p, n);
               if (!s) throw CommandError("failed", ctx.say("No " + type + " fits the faces there.", "Ninguna forma (" + type + ") encaja con las caras de ahí."));
               // Keep only the faces that belong to the shape: on it and facing
               // its way. A ball around a hole also takes the rounded mouths and
               // a ring of the face around it, which pull the diameter.
               double kept = std::accumulate(area.begin(), area.end(), 0.0);
               std::size_t keptFaces = p.size();
               for (int round = 0; round < 4; ++round) {
                   const double tol = std::max({2.5 * s->rms, 2.0 * noise, 0.005});
                   std::vector<Vec3d> p2, n2;
                   double a2 = 0;
                   for (std::size_t i = 0; i < p.size(); ++i) {
                       if (std::abs(s->distance(p[i])) > tol || std::abs(s->normal(p[i]).dot(n[i])) < std::cos(20.0 * kPi / 180)) continue;
                       p2.push_back(p[i]);
                       n2.push_back(n[i]);
                       a2 += area[i];
                   }
                   if (p2.size() < 12) break;
                   auto s2 = type == "auto" ? fitPrimitive(s->type, p2, n2) : fitAll(p2, n2);
                   if (!s2) break;
                   const bool settled = p2.size() == keptFaces;
                   s = s2;
                   kept = a2;
                   keptFaces = p2.size();
                   if (settled) break;
               }
               auto fs = std::make_shared<Features>(b.features ? *b.features : Features{});
               Feature f;
               f.id = fs->nextId++;
               f.shape = *s;
               f.faces = keptFaces;
               f.area = kept;
               f.name = in.contains("name") ? in["name"].get<std::string>() : nextName(ctx, *fs, s->type);
               fs->list.push_back(f);
               ctx.doc.setFeatures(b.id, fs);
               json out = featureJson(f);
               out["body"] = b.ref();
               out["summary"] = ctx.say("Fitted " + f.name + " (σ " + std::to_string(r4(s->rms)).substr(0, 6) + " mm).",
                                        "Ajustado " + f.name + " (σ " + std::to_string(r4(s->rms)).substr(0, 6) + " mm).");
               return out;
           }});

    R.add({"features.refit", "features", {"Refit shape as…", "Reajustar forma como…"},
           {"Fit another type of shape to the faces of a found region (a short cylinder taken for a plane, a cone you know is a cylinder…).",
            "Ajusta otro tipo de forma a las caras de una región encontrada (un cilindro corto tomado por plano, un cono que sabes que es un cilindro…)."},
           object({{"body", body(kBody)}, {"feature", string(kFeature, "feature")},
                   {"type", choice({"Shape", "Forma"}, {"plane", "sphere", "cylinder", "cone", "torus"}, "cylinder")}},
                  {"body", "feature", "type"}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = meshBody(ctx, in["body"]);
               const Feature& old = findFeature(ctx, b, in["feature"]);
               if (old.region < 0 || b.features->faceRegion.size() != b.mesh->faces.size())
                   throw CommandError("invalid_input", ctx.say(old.name + " was fitted on a spot, not found as a region: fit it again with features.fit.",
                                                               old.name + " se ajustó en un punto, no es una región encontrada: vuelve a ajustarla con features.fit."));
               Segmentation s;
               s.faceRegion = b.features->faceRegion;
               std::vector<Vec3d> p, n;
               regionSamples(*b.mesh, s, old.region, p, n);
               const auto type = *primitiveFromName(in["type"].get<std::string>());
               auto shape = fitPrimitive(type, p, n);
               if (!shape) throw CommandError("failed", ctx.say("That region is not a " + in["type"].get<std::string>() + ".", "Esa región no es un " + typeName(ctx, type) + "."));
               auto fs = std::make_shared<Features>(*b.features);
               for (auto& f : fs->list)
                   if (f.id == old.id) {
                       f.shape = *shape;
                       if (f.name.rfind(typeName(ctx, old.shape.type) + " ", 0) == 0) f.name = nextName(ctx, *fs, type);
                       ctx.doc.setFeatures(b.id, fs);
                       json out = featureJson(f);
                       out["sigmaBefore"] = r4(old.shape.rms);
                       return out;
                   }
               throw CommandError("failed", "internal: shape vanished");
           }});

    R.add({"features.list", "features", {"List shapes", "Listar formas"},
           {"The shapes of a body with their sizes, position and σ.", "Las formas de un cuerpo con sus medidas, posición y σ."},
           object({{"body", body(kBody)}}, {"body"}), false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               json list = json::array();
               if (b.features)
                   for (const auto& f : b.features->list) list.push_back(featureJson(f));
               return json{{"body", b.ref()}, {"noise", b.features ? r4(b.features->noise) : 0.0}, {"features", list}};
           }});

    R.add({"features.measure", "features", {"Measure between shapes", "Medir entre formas"},
           {"Angle and distance between two shapes of a body: between parallel planes, from a plane to an axis or centre, between two axes, between centres; where an axis meets a plane.",
            "Ángulo y distancia entre dos formas de un cuerpo: entre planos paralelos, de un plano a un eje o centro, entre dos ejes, entre centros; dónde corta un eje a un plano."},
           object({{"body", body(kBody)}, {"a", string(kFeature, "feature")}, {"b", string(kFeature, "feature")}}, {"body", "a", "b"}), false, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Feature& fa = findFeature(ctx, b, in["a"]);
               const Feature& fb = findFeature(ctx, b, in["b"]);
               json out = measurePair(fa.shape, fb.shape);
               out["a"] = fa.ref();
               out["b"] = fb.ref();
               out["units"] = "mm";
               return out;
           }});

    R.add({"features.rename", "features", {"Rename shape", "Renombrar forma"}, {"Give a shape a name of your own.", "Pone a una forma un nombre propio."},
           object({{"body", body(kBody)}, {"feature", string(kFeature, "feature")}, {"name", string({"New name", "Nombre nuevo"})}}, {"body", "feature", "name"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               const Feature& f = findFeature(ctx, b, in["feature"]);
               const std::string name = in["name"];
               if (name.empty()) throw CommandError("invalid_input", ctx.say("A shape needs a name.", "Una forma necesita un nombre."));
               auto fs = std::make_shared<Features>(*b.features);
               for (auto& x : fs->list) {
                   if (x.id != f.id && x.name == name) throw CommandError("invalid_input", ctx.say("Another shape is already called " + name + ".", "Otra forma ya se llama " + name + "."));
                   if (x.id == f.id) x.name = name;
               }
               ctx.doc.setFeatures(b.id, fs);
               return json{{"ref", f.ref()}, {"name", name}};
           }});

    R.add({"features.delete", "features", {"Delete shape", "Borrar forma"}, {"Remove one shape, or all of them (feature \"all\").", "Quita una forma, o todas (feature \"all\")."},
           object({{"body", body(kBody)}, {"feature", string(kFeature, "feature")}}, {"body", "feature"}), true, false,
           [](CommandContext& ctx, const json& in) {
               const Body b = ctx.doc.body(in["body"]);
               if (in["feature"] == "all") {
                   ctx.doc.setFeatures(b.id, nullptr);
                   return json{{"body", b.ref()}, {"deleted", "all"}};
               }
               const Feature& f = findFeature(ctx, b, in["feature"]);
               auto fs = std::make_shared<Features>(*b.features);
               const std::uint32_t id = f.id;
               const std::int32_t region = f.region;
               std::erase_if(fs->list, [&](const Feature& x) { return x.id == id; });
               if (region >= 0)  // its faces become unassigned
                   for (auto& r : fs->faceRegion)
                       if (r == region) r = -1;
               ctx.doc.setFeatures(b.id, fs);
               return json{{"body", b.ref()}, {"deleted", "f" + std::to_string(id)}};
           }});
}

}  // namespace orv

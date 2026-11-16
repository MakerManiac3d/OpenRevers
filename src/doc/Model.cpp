// SPDX-License-Identifier: GPL-3.0-or-later
#include "doc/Model.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>

#include <cmath>
#include <map>
#include <stdexcept>

namespace orv {

using nlohmann::json;

const Sketch* Model::sketch(const std::string& r) const {
    for (const auto& s : sketches)
        if (s.ref() == r || s.name == r) return &s;
    return nullptr;
}

const Operation* Model::operation(const std::string& r) const {
    for (const auto& o : operations)
        if (o.ref() == r || o.name == r) return &o;
    return nullptr;
}

const FreeSurface* Model::surface(const std::string& r) const {
    for (const auto& s : surfaces)
        if (s.ref() == r || s.name == r) return &s;
    return nullptr;
}

double Model::value(const Value& v) const {
    if (v.parameter.empty()) return v.number;
    const auto it = parameters.find(v.parameter);
    if (it == parameters.end()) throw SolidError("no parameter called \"" + v.parameter + "\"");
    return v.negate ? -it->second : it->second;
}

void addOperation(Model& m, Operation op) {
    if (m.rollback != 0) {
        const auto it = std::find_if(m.operations.begin(), m.operations.end(), [&](const Operation& o) { return o.id == m.rollback; });
        if (it != m.operations.end()) {
            const auto id = op.id;
            m.operations.insert(it + 1, std::move(op));
            m.rollback = id;
            return;
        }
    }
    m.rollback = 0;
    m.operations.push_back(std::move(op));
}

BuildResult rebuild(const Model& m) {
    BuildResult r;
    std::map<std::uint64_t, Solid> pieceOf;  // what each operation made, for patterns
    auto sketchOf = [&](std::uint64_t id) -> const Sketch& {
        for (const auto& s : m.sketches)
            if (s.id == id) return s;
        throw SolidError("its sketch was deleted");
    };
    auto surfaceOf = [&](std::uint64_t id) -> const FreeSurface& {
        for (const auto& s : m.surfaces)
            if (s.id == id) return s;
        throw SolidError("its surface was deleted");
    };
    bool pastRollback = false;
    for (const auto& op : m.operations) {
        if (pastRollback) break;
        pastRollback = m.rollback != 0 && op.id == m.rollback;
        if (op.suppressed) continue;
        try {
            Solid piece;
            OpMode mode = op.mode;
            switch (op.kind) {
                case OpKind::Extrude:
                case OpKind::Revolve: {
                    const Sketch& sk = sketchOf(op.sketch);
                    if (op.kind == OpKind::Revolve) {
                        piece = Solid::revolve(sk.profile.solidPart(), op.axisPoint, op.axisDir, m.value(op.angle));
                        break;
                    }
                    SketchPlane pl = sk.profile.plane;
                    pl.normalize();
                    double from = m.value(op.start), to = m.value(op.end);
                    if (op.extent == Extent::Symmetric) {
                        from = -0.5 * to;
                        to = 0.5 * to;
                    } else if (op.extent == Extent::ThroughAll || op.extent == Extent::UpToFace) {
                        if (r.solid.empty()) throw SolidError("there is no part yet to go through or up to");
                        double limit = 0.0;
                        if (op.extent == Extent::UpToFace) {
                            const int f = r.solid.faceNear(op.toPoint, 0.1);
                            if (f < 0) throw SolidError("no face of the part at the point it goes up to");
                            const ShapeInfo info = r.solid.faceInfo(f);
                            if (info.type != "plane" || std::abs(info.axis.normalized().dot(pl.normal)) < 0.9999)
                                throw SolidError("it can only go up to a flat face parallel to the sketch");
                            limit = (info.origin - pl.origin).dot(pl.normal);
                        } else {
                            const auto box = r.solid.bounds();
                            double lo = 1e300, hi = -1e300;
                            for (int c = 0; c < 8; ++c) {
                                const double t = (box.corner(static_cast<Eigen::AlignedBox3d::CornerType>(c)) - pl.origin).dot(pl.normal);
                                lo = std::min(lo, t);
                                hi = std::max(hi, t);
                            }
                            limit = op.reverse ? lo - 1.0 : hi + 1.0;
                        }
                        if (op.reverse) { to = from; from = limit; }
                        else to = limit;
                        if (!(to > from)) throw SolidError("the part does not reach that way from the sketch");
                    }
                    piece = Solid::extrude(sk.profile.solidPart(), from, to);
                    break;
                }
                case OpKind::Hole: {
                    const Sketch& sk = sketchOf(op.sketch);
                    SketchPlane pl = sk.profile.plane;
                    pl.normalize();
                    if (r.solid.empty()) throw SolidError("there is nothing to drill yet");
                    const Vec3d axis = op.reverse ? pl.normal : Vec3d(-pl.normal);
                    const double depth = op.throughAll ? r.solid.bounds().diagonal().norm() + 1.0 : m.value(op.depth);
                    for (const auto& loop : sk.profile.loops) {
                        if (loop.construction) continue;
                        if (loop.segments.size() != 1 || loop.segments[0].kind != Segment::Kind::Circle)
                            throw SolidError("a hole's sketch holds circles only (one per hole)");
                        const Segment& c = loop.segments[0];
                        const double want = m.value(op.diameter);
                        const double dia = want > 0 ? want : 2 * c.radius;
                        const Solid tool = Solid::drill(pl.toWorld(c.centre), axis, dia, depth,
                                                        op.holeKind == HoleKind::Counterbore ? m.value(op.cbDiameter) : 0.0,
                                                        op.holeKind == HoleKind::Counterbore ? m.value(op.cbDepth) : 0.0,
                                                        op.holeKind == HoleKind::Countersink ? m.value(op.csDiameter) : 0.0, m.value(op.csAngle));
                        piece = piece.empty() ? tool : piece.fused(tool);
                    }
                    if (piece.empty()) throw SolidError("the sketch has no circle to drill");
                    mode = OpMode::Cut;
                    break;
                }
                case OpKind::Mirror: {
                    Solid src;
                    if (op.source) {
                        const auto it = pieceOf.find(op.source);
                        if (it == pieceOf.end()) throw SolidError("the operation it mirrors is missing, suppressed or after it");
                        src = it->second;
                    } else {
                        src = r.solid;
                    }
                    if (src.empty()) throw SolidError("there is nothing to mirror yet");
                    piece = src.mirrored(op.axisOrigin, op.axisDirection);
                    if (mode == OpMode::New) mode = OpMode::Join;
                    break;
                }
                case OpKind::Loft: {
                    std::vector<Profile> sections;
                    for (auto id : op.sections) sections.push_back(sketchOf(id).profile.solidPart());
                    piece = Solid::loft(sections, op.ruled);
                    break;
                }
                case OpKind::Thicken: piece = Solid::thicken(surfaceOf(op.surface).surface, m.value(op.thickness)); break;
                case OpKind::Pattern: {
                    auto it = pieceOf.find(op.source);
                    if (it == pieceOf.end()) throw SolidError("the operation it repeats is missing, suppressed or after it");
                    const int n = static_cast<int>(std::lround(m.value(op.count)));
                    if (n < 2 || n > 1000) throw SolidError("a pattern needs 2 to 1000 copies");
                    const double span = m.value(op.angle);
                    // A whole turn spreads the copies evenly; a smaller span puts the last at its end.
                    const double step = (std::abs(std::abs(span) - 360.0) < 1e-9 ? span / n : span / (n - 1)) * 3.14159265358979323846 / 180;
                    if (op.axisDirection.norm() < 1e-12) throw SolidError("the pattern's axis needs a direction");
                    const Vec3d axis = op.axisDirection.normalized();
                    for (int k = 1; k < n; ++k) {
                        Eigen::Isometry3d t = Eigen::Isometry3d::Identity();
                        t.translate(op.axisOrigin);
                        t.rotate(Eigen::AngleAxisd(step * k, axis));
                        t.translate(-op.axisOrigin);
                        const Solid copy = it->second.transformed(t);
                        piece = piece.empty() ? copy : piece.fused(copy);
                    }
                    break;
                }
                case OpKind::Import: piece = Solid::fromBrep(op.brep); break;
                case OpKind::Fillet:
                    if (r.solid.empty()) throw SolidError("there is nothing to round yet");
                    r.solid = r.solid.filleted(op.edgePoints, m.value(op.radius), 0.05, op.edgeHints);
                    continue;
                case OpKind::Chamfer:
                    if (r.solid.empty()) throw SolidError("there is nothing to bevel yet");
                    r.solid = r.solid.chamfered(op.edgePoints, m.value(op.radius), 0.05, op.edgeHints);
                    continue;
                case OpKind::SurfaceCut:
                    r.solid = r.solid.cutBySurface(surfaceOf(op.surface).surface, op.keepBelow);
                    continue;
            }
            pieceOf[op.id] = piece;
            if (r.solid.empty() || mode == OpMode::New) {
                if (mode == OpMode::Cut || mode == OpMode::Intersect) throw SolidError("there is nothing to cut yet");
                r.solid = r.solid.empty() ? piece : r.solid.fused(piece);  // a second "new" body joins the part
            } else if (mode == OpMode::Join) {
                r.solid = r.solid.fused(piece);
            } else if (mode == OpMode::Cut) {
                r.solid = r.solid.cut(piece);
            } else {
                r.solid = r.solid.common(piece);
            }
        } catch (const std::exception& e) {
            r.failedOperation = op.ref();
            r.error = op.name + ": " + e.what();
            break;
        }
    }
    return r;
}

json constraintToJson(const SketchConstraint& c) {
    json refs = json::array();
    for (const auto& r : c.refs) {
        const char* part = r.part == SketchRef::Part::Start ? "start" : r.part == SketchRef::Part::End ? "end" : r.part == SketchRef::Part::Centre ? "centre" : "edge";
        refs.push_back({{"loop", r.loop}, {"segment", r.segment}, {"part", part}});
    }
    json j{{"kind", constraintKindName(c.kind)}, {"refs", refs}};
    if (constraintHasValue(c.kind)) j["value"] = c.parameter.empty() ? json(c.value) : json(c.parameter);
    if (!c.driving) j["driving"] = false;
    if (!c.name.empty()) j["name"] = c.name;
    return j;
}

SketchConstraint constraintFromJson(const json& j) {
    SketchConstraint c;
    const std::string kind = j.at("kind").get<std::string>();
    const auto k = constraintKindFromName(kind);
    if (!k) throw std::invalid_argument("unknown constraint \"" + kind + "\"");
    c.kind = *k;
    for (const auto& jr : j.value("refs", json::array())) {
        SketchRef r;
        if (jr.is_string()) {  // the sketch's own origin and axes
            const std::string name = jr.get<std::string>();
            r.loop = -1;
            if (name == "origin") { r.segment = 0; r.part = SketchRef::Part::Start; }
            else if (name == "x_axis") { r.segment = 0; r.part = SketchRef::Part::Edge; }
            else if (name == "y_axis") { r.segment = 1; r.part = SketchRef::Part::Edge; }
            else throw std::invalid_argument("a reference given as text is origin, x_axis or y_axis");
            c.refs.push_back(r);
            continue;
        }
        r.loop = jr.value("loop", 0);
        r.segment = jr.at("segment").get<int>();
        const std::string part = jr.value("part", "edge");
        if (part == "start") r.part = SketchRef::Part::Start;
        else if (part == "end") r.part = SketchRef::Part::End;
        else if (part == "centre" || part == "center") r.part = SketchRef::Part::Centre;
        else if (part == "edge") r.part = SketchRef::Part::Edge;
        else throw std::invalid_argument("unknown part \"" + part + "\" (edge, start, end or centre)");
        c.refs.push_back(r);
    }
    if (j.contains("value")) {
        const json& v = j["value"];
        if (v.is_number()) c.value = v.get<double>();
        else if (v.is_string()) {
            const std::string t = v.get<std::string>();
            try {
                std::size_t used = 0;
                c.value = std::stod(t, &used);
                if (used != t.size()) c.parameter = t;
            } catch (const std::exception&) {
                c.parameter = t;
            }
        }
    }
    c.driving = j.value("driving", true);
    c.name = j.value("name", "");
    return c;
}

std::string solveSketchOf(const Model& m, Sketch& s, SketchSolveResult* result) {
    std::vector<SketchConstraint> cs = s.constraints;
    for (auto& c : cs)
        if (!c.parameter.empty()) {
            const auto it = m.parameters.find(c.parameter);
            if (it == m.parameters.end()) return "no parameter named " + c.parameter;
            c.value = it->second;
        }
    Profile p = s.profile;
    const SketchSolveResult r = solveSketch(p, cs);
    if (result) *result = r;
    if (!r.solved) {
        std::string why = r.message.empty() ? "the constraints cannot all hold" : r.message;
        if (!r.conflicting.empty()) {
            why += " (";
            for (std::size_t i = 0; i < r.conflicting.size(); ++i) {
                const auto& c = s.constraints[static_cast<std::size_t>(r.conflicting[i])];
                why += (i ? ", " : "") + (c.name.empty() ? std::string(constraintKindName(c.kind)) + " #" + std::to_string(r.conflicting[i] + 1) : c.name);
            }
            why += ")";
        }
        return why;
    }
    s.profile = std::move(p);
    s.dof = r.dof;
    return {};
}

std::string solveSketchesUsing(Model& m, const std::string& parameter) {
    for (auto& s : m.sketches) {
        bool uses = parameter.empty() && !s.constraints.empty();
        for (const auto& c : s.constraints) uses = uses || (!parameter.empty() && c.parameter == parameter);
        if (!uses) continue;
        if (const std::string why = solveSketchOf(m, s); !why.empty()) return s.name + ": " + why;
    }
    return {};
}

const char* opKindName(OpKind k) {
    switch (k) {
        case OpKind::Extrude: return "extrude";
        case OpKind::Revolve: return "revolve";
        case OpKind::Fillet: return "fillet";
        case OpKind::Import: return "import";
        case OpKind::Loft: return "loft";
        case OpKind::Pattern: return "pattern";
        case OpKind::SurfaceCut: return "surface_cut";
        case OpKind::Thicken: return "thicken";
        case OpKind::Chamfer: return "chamfer";
        case OpKind::Hole: return "hole";
        case OpKind::Mirror: return "mirror";
    }
    return "?";
}

const char* extentName(Extent e) {
    switch (e) {
        case Extent::Distance: return "distance";
        case Extent::Symmetric: return "symmetric";
        case Extent::ThroughAll: return "through_all";
        case Extent::UpToFace: return "up_to_face";
    }
    return "?";
}

std::optional<Extent> extentFromName(const std::string& s) {
    for (auto e : {Extent::Distance, Extent::Symmetric, Extent::ThroughAll, Extent::UpToFace})
        if (s == extentName(e)) return e;
    return std::nullopt;
}

const char* holeKindName(HoleKind k) {
    switch (k) {
        case HoleKind::Simple: return "simple";
        case HoleKind::Counterbore: return "counterbore";
        case HoleKind::Countersink: return "countersink";
    }
    return "?";
}

std::optional<HoleKind> holeKindFromName(const std::string& s) {
    for (auto k : {HoleKind::Simple, HoleKind::Counterbore, HoleKind::Countersink})
        if (s == holeKindName(k)) return k;
    return std::nullopt;
}

const char* opModeName(OpMode m) {
    switch (m) {
        case OpMode::New: return "new";
        case OpMode::Join: return "join";
        case OpMode::Cut: return "cut";
        case OpMode::Intersect: return "intersect";
    }
    return "?";
}

std::optional<OpMode> opModeFromName(const std::string& s) {
    for (auto m : {OpMode::New, OpMode::Join, OpMode::Cut, OpMode::Intersect})
        if (s == opModeName(m)) return m;
    return std::nullopt;
}

namespace {
json v2(const Vec2d& v) { return json::array({v.x(), v.y()}); }
json v3(const Vec3d& v) { return json::array({v.x(), v.y(), v.z()}); }
Vec2d v2(const json& j, const char* what) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) throw std::invalid_argument(std::string(what) + " must be [x, y]");
    return {j[0].get<double>(), j[1].get<double>()};
}
Vec3d v3(const json& j, const char* what) {
    if (!j.is_array() || j.size() != 3) throw std::invalid_argument(std::string(what) + " must be [x, y, z]");
    return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
}
}  // namespace

json valueToJson(const Value& v) { return v.parameter.empty() ? json(v.number) : json((v.negate ? "-" : "") + v.parameter); }

Value valueFromJson(const json& j) {
    Value v;
    if (j.is_number()) {
        v.number = j.get<double>();
        return v;
    }
    if (!j.is_string()) throw std::invalid_argument("a value must be a number or the name of a parameter");
    // A number written as text ("-4.5", "-4,5") is a number, not a name: some
    // clients send every value as a string.
    std::string t = j.get<std::string>();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    if (t.empty()) throw std::invalid_argument("a value cannot be empty");
    std::string dot = t;
    std::replace(dot.begin(), dot.end(), ',', '.');
    char* end = nullptr;
    const double d = std::strtod(dot.c_str(), &end);
    if (end && *end == '\0' && std::isfinite(d)) {
        v.number = d;
        return v;
    }
    if (t.front() == '-' || t.front() == '+') {  // "-pared": minus the parameter
        v.negate = t.front() == '-';
        t.erase(t.begin());
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
        if (t.empty()) throw std::invalid_argument("a value must be a number or the name of a parameter");
    }
    v.parameter = t;
    return v;
}

json surfaceToJson(const FreeSurface& f) {
    const HeightSurface& s = f.surface;
    return {{"id", f.id}, {"name", f.name}, {"origin", v3(s.origin)}, {"xDir", v3(s.xDir)}, {"yDir", v3(s.yDir)}, {"normal", v3(s.normal)},
            {"x0", s.x0}, {"y0", s.y0}, {"width", s.width}, {"height", s.height}, {"nu", s.nu}, {"nv", s.nv}, {"heights", s.heights},
            {"rms", f.rms}, {"maxDeviation", f.maxDeviation}};
}

FreeSurface surfaceFromJson(const json& j) {
    FreeSurface f;
    f.id = j.at("id").get<std::uint64_t>();
    f.name = j.value("name", "");
    HeightSurface& s = f.surface;
    s.origin = v3(j.at("origin"), "origin");
    s.xDir = v3(j.at("xDir"), "xDir");
    s.yDir = v3(j.at("yDir"), "yDir");
    s.normal = v3(j.at("normal"), "normal");
    s.x0 = j.at("x0").get<double>();
    s.y0 = j.at("y0").get<double>();
    s.width = j.at("width").get<double>();
    s.height = j.at("height").get<double>();
    s.nu = j.at("nu").get<int>();
    s.nv = j.at("nv").get<int>();
    s.heights = j.at("heights").get<std::vector<double>>();
    if (s.nu < 2 || s.nv < 2 || s.heights.size() != static_cast<std::size_t>(s.nu * s.nv)) throw std::invalid_argument("a surface's heights do not match its poles");
    f.rms = j.value("rms", 0.0);
    f.maxDeviation = j.value("maxDeviation", 0.0);
    return f;
}

json profileToJson(const Profile& p) {
    json loops = json::array();
    for (const auto& l : p.loops) {
        json segs = json::array();
        for (const auto& s : l.segments) {
            switch (s.kind) {
                case Segment::Kind::Line: segs.push_back({{"type", "line"}, {"from", v2(s.a)}, {"to", v2(s.b)}}); break;
                case Segment::Kind::Arc: segs.push_back({{"type", "arc"}, {"from", v2(s.a)}, {"to", v2(s.b)}, {"centre", v2(s.centre)}, {"ccw", s.ccw}}); break;
                case Segment::Kind::Circle: segs.push_back({{"type", "circle"}, {"centre", v2(s.centre)}, {"radius", s.radius}}); break;
                case Segment::Kind::Spline: {
                    json poles = json::array();
                    for (const auto& q : s.poles) poles.push_back(v2(q));
                    segs.push_back({{"type", "spline"}, {"poles", poles}});
                    break;
                }
            }
        }
        if (l.construction)
            for (auto& sj : segs) sj["construction"] = true;
        loops.push_back(segs);
    }
    return {{"plane", {{"origin", v3(p.plane.origin)}, {"normal", v3(p.plane.normal)}, {"xDir", v3(p.plane.xDir)}}}, {"loops", loops}};
}

Profile profileFromJson(const json& j) {
    Profile p;
    if (j.contains("plane")) {
        const json& pl = j["plane"];
        if (pl.contains("origin")) p.plane.origin = v3(pl["origin"], "plane.origin");
        if (pl.contains("normal")) p.plane.normal = v3(pl["normal"], "plane.normal");
        if (pl.contains("xDir")) p.plane.xDir = v3(pl["xDir"], "plane.xDir");
        if (p.plane.normal.norm() < 1e-12) throw std::invalid_argument("plane.normal cannot be zero");
        p.plane.normalize();
    }
    if (!j.contains("loops") || !j["loops"].is_array()) throw std::invalid_argument("loops must be a list of loops, each a list of segments");
    for (const auto& jl : j["loops"]) {
        if (!jl.is_array()) throw std::invalid_argument("each loop must be a list of segments");
        Loop l;
        for (const auto& s : jl) {
            const std::string t = s.value("type", "");
            if (t == "line") l.segments.push_back(Segment::line(v2(s.at("from"), "from"), v2(s.at("to"), "to")));
            else if (t == "arc") l.segments.push_back(Segment::arc(v2(s.at("from"), "from"), v2(s.at("to"), "to"), v2(s.at("centre"), "centre"), s.value("ccw", true)));
            else if (t == "circle") l.segments.push_back(Segment::circle(v2(s.at("centre"), "centre"), s.at("radius").get<double>()));
            else if (t == "spline") {
                std::vector<Vec2d> poles;
                for (const auto& q : s.at("poles")) poles.push_back(v2(q, "poles"));
                l.segments.push_back(Segment::spline(std::move(poles)));
            } else throw std::invalid_argument("a segment's type must be line, arc, circle or spline");
            if (s.value("construction", false)) l.construction = true;
        }
        p.loops.push_back(std::move(l));
    }
    return p;
}

json modelToJson(const Model& m) {
    json sk = json::array(), ops = json::array();
    for (const auto& s : m.sketches) {
        json js = profileToJson(s.profile);
        js["id"] = s.id;
        js["name"] = s.name;
        if (!s.constraints.empty()) {
            json cs = json::array();
            for (const auto& c : s.constraints) cs.push_back(constraintToJson(c));
            js["constraints"] = cs;
            js["dof"] = s.dof;
        }
        sk.push_back(js);
    }
    for (const auto& o : m.operations) {
        json jo{{"id", o.id}, {"name", o.name}, {"kind", opKindName(o.kind)}, {"mode", opModeName(o.mode)}, {"suppressed", o.suppressed}};
        switch (o.kind) {
            case OpKind::Extrude:
                jo["sketch"] = o.sketch;
                jo["start"] = valueToJson(o.start);
                jo["end"] = valueToJson(o.end);
                if (o.extent != Extent::Distance) jo["extent"] = extentName(o.extent);
                if (o.reverse) jo["reverse"] = true;
                if (o.extent == Extent::UpToFace) jo["toPoint"] = v3(o.toPoint);
                break;
            case OpKind::Hole:
                jo["sketch"] = o.sketch;
                jo["holeKind"] = holeKindName(o.holeKind);
                jo["diameter"] = valueToJson(o.diameter);
                jo["depth"] = valueToJson(o.depth);
                jo["throughAll"] = o.throughAll;
                jo["reverse"] = o.reverse;
                jo["cbDiameter"] = valueToJson(o.cbDiameter);
                jo["cbDepth"] = valueToJson(o.cbDepth);
                jo["csDiameter"] = valueToJson(o.csDiameter);
                jo["csAngle"] = valueToJson(o.csAngle);
                break;
            case OpKind::Mirror:
                jo["source"] = o.source;
                jo["axisOrigin"] = v3(o.axisOrigin);
                jo["axisDirection"] = v3(o.axisDirection);
                break;
            case OpKind::Revolve:
                jo["sketch"] = o.sketch;
                jo["axisPoint"] = v2(o.axisPoint);
                jo["axisDir"] = v2(o.axisDir);
                jo["angle"] = valueToJson(o.angle);
                break;
            case OpKind::Chamfer:
            case OpKind::Fillet: {
                json pts = json::array();
                for (const auto& p : o.edgePoints) pts.push_back(v3(p));
                jo["edgePoints"] = pts;
                json hints = json::array();
                for (const auto& h : o.edgeHints) hints.push_back({{"type", h.type}, {"dir", v3(h.dir)}});
                if (!o.edgeHints.empty()) jo["edgeHints"] = hints;
                jo["radius"] = valueToJson(o.radius);
                break;
            }
            case OpKind::Import: jo["brep"] = o.brep; break;
            case OpKind::Loft: jo["sections"] = o.sections; jo["ruled"] = o.ruled; break;
            case OpKind::Pattern:
                jo["source"] = o.source;
                jo["axisOrigin"] = v3(o.axisOrigin);
                jo["axisDirection"] = v3(o.axisDirection);
                jo["count"] = valueToJson(o.count);
                jo["angle"] = valueToJson(o.angle);
                break;
            case OpKind::SurfaceCut: jo["surface"] = o.surface; jo["keepBelow"] = o.keepBelow; break;
            case OpKind::Thicken: jo["surface"] = o.surface; jo["thickness"] = valueToJson(o.thickness); break;
        }
        ops.push_back(jo);
    }
    json sf = json::array();
    for (const auto& f : m.surfaces) sf.push_back(surfaceToJson(f));
    json out{{"parameters", m.parameters}, {"sketches", sk}, {"operations", ops}, {"nextId", m.nextId}};
    if (m.rollback != 0) out["rollback"] = m.rollback;
    if (!m.surfaces.empty()) out["surfaces"] = sf;
    return out;
}

Model modelFromJson(const json& j) {
    Model m;
    m.parameters = j.value("parameters", std::map<std::string, double>{});
    for (const auto& js : j.value("sketches", json::array())) {
        Sketch s;
        s.id = js.at("id").get<std::uint64_t>();
        s.name = js.value("name", "");
        s.profile = profileFromJson(js);
        for (const auto& jc : js.value("constraints", json::array())) s.constraints.push_back(constraintFromJson(jc));
        s.dof = js.value("dof", -1);
        m.sketches.push_back(std::move(s));
    }
    for (const auto& jo : j.value("operations", json::array())) {
        Operation o;
        o.id = jo.at("id").get<std::uint64_t>();
        o.name = jo.value("name", "");
        const std::string k = jo.value("kind", "extrude");
        o.kind = OpKind::Extrude;
        for (auto kind : {OpKind::Revolve, OpKind::Fillet, OpKind::Import, OpKind::Loft, OpKind::Pattern, OpKind::SurfaceCut, OpKind::Thicken, OpKind::Chamfer, OpKind::Hole, OpKind::Mirror})
            if (k == opKindName(kind)) o.kind = kind;
        o.mode = opModeFromName(jo.value("mode", "new")).value_or(OpMode::New);
        o.suppressed = jo.value("suppressed", false);
        o.sketch = jo.value("sketch", std::uint64_t{0});
        if (jo.contains("start")) o.start = valueFromJson(jo["start"]);
        if (jo.contains("end")) o.end = valueFromJson(jo["end"]);
        if (jo.contains("axisPoint")) o.axisPoint = v2(jo["axisPoint"], "axisPoint");
        if (jo.contains("axisDir")) o.axisDir = v2(jo["axisDir"], "axisDir");
        if (jo.contains("angle")) o.angle = valueFromJson(jo["angle"]);
        for (const auto& p : jo.value("edgePoints", json::array())) o.edgePoints.push_back(v3(p, "edgePoints"));
        for (const auto& h : jo.value("edgeHints", json::array())) o.edgeHints.push_back({h.value("type", std::string()), h.contains("dir") ? v3(h["dir"], "edgeHints") : Vec3d::Zero()});
        if (jo.contains("radius")) o.radius = valueFromJson(jo["radius"]);
        o.brep = jo.value("brep", "");
        o.sections = jo.value("sections", std::vector<std::uint64_t>{});
        o.ruled = jo.value("ruled", false);
        o.source = jo.value("source", std::uint64_t{0});
        if (jo.contains("axisOrigin")) o.axisOrigin = v3(jo["axisOrigin"], "axisOrigin");
        if (jo.contains("axisDirection")) o.axisDirection = v3(jo["axisDirection"], "axisDirection");
        if (jo.contains("count")) o.count = valueFromJson(jo["count"]);
        o.surface = jo.value("surface", std::uint64_t{0});
        o.keepBelow = jo.value("keepBelow", true);
        if (jo.contains("thickness")) o.thickness = valueFromJson(jo["thickness"]);
        o.extent = extentFromName(jo.value("extent", "distance")).value_or(Extent::Distance);
        o.reverse = jo.value("reverse", false);
        if (jo.contains("toPoint")) o.toPoint = v3(jo["toPoint"], "toPoint");
        o.holeKind = holeKindFromName(jo.value("holeKind", "simple")).value_or(HoleKind::Simple);
        if (jo.contains("diameter")) o.diameter = valueFromJson(jo["diameter"]);
        if (jo.contains("depth")) o.depth = valueFromJson(jo["depth"]);
        o.throughAll = jo.value("throughAll", false);
        if (jo.contains("cbDiameter")) o.cbDiameter = valueFromJson(jo["cbDiameter"]);
        if (jo.contains("cbDepth")) o.cbDepth = valueFromJson(jo["cbDepth"]);
        if (jo.contains("csDiameter")) o.csDiameter = valueFromJson(jo["csDiameter"]);
        if (jo.contains("csAngle")) o.csAngle = valueFromJson(jo["csAngle"]);
        m.operations.push_back(std::move(o));
    }
    for (const auto& jf : j.value("surfaces", json::array())) m.surfaces.push_back(surfaceFromJson(jf));
    m.rollback = j.value("rollback", std::uint64_t{0});
    m.nextId = j.value("nextId", std::uint64_t{1});
    for (const auto& s : m.sketches) m.nextId = std::max(m.nextId, s.id + 1);
    for (const auto& o : m.operations) m.nextId = std::max(m.nextId, o.id + 1);
    for (const auto& f : m.surfaces) m.nextId = std::max(m.nextId, f.id + 1);
    return m;
}

}  // namespace orv

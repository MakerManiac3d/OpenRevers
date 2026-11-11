// SPDX-License-Identifier: GPL-3.0-or-later
#include "doc/Model.h"

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

double Model::value(const Value& v) const {
    if (v.parameter.empty()) return v.number;
    const auto it = parameters.find(v.parameter);
    if (it == parameters.end()) throw SolidError("no parameter called \"" + v.parameter + "\"");
    return it->second;
}

BuildResult rebuild(const Model& m) {
    BuildResult r;
    for (const auto& op : m.operations) {
        if (op.suppressed) continue;
        try {
            Solid piece;
            switch (op.kind) {
                case OpKind::Extrude:
                case OpKind::Revolve: {
                    const Sketch* sk = nullptr;
                    for (const auto& s : m.sketches)
                        if (s.id == op.sketch) sk = &s;
                    if (!sk) throw SolidError("its sketch was deleted");
                    piece = op.kind == OpKind::Extrude ? Solid::extrude(sk->profile, m.value(op.start), m.value(op.end))
                                                       : Solid::revolve(sk->profile, op.axisPoint, op.axisDir, m.value(op.angle));
                    break;
                }
                case OpKind::Import: piece = Solid::fromBrep(op.brep); break;
                case OpKind::Fillet:
                    if (r.solid.empty()) throw SolidError("there is nothing to round yet");
                    r.solid = r.solid.filleted(op.edgePoints, m.value(op.radius));
                    continue;
            }
            if (r.solid.empty() || op.mode == OpMode::New) {
                if (op.mode == OpMode::Cut || op.mode == OpMode::Intersect) throw SolidError("there is nothing to cut yet");
                r.solid = r.solid.empty() ? piece : r.solid.fused(piece);  // a second "new" body joins the part
            } else if (op.mode == OpMode::Join) {
                r.solid = r.solid.fused(piece);
            } else if (op.mode == OpMode::Cut) {
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

const char* opKindName(OpKind k) {
    switch (k) {
        case OpKind::Extrude: return "extrude";
        case OpKind::Revolve: return "revolve";
        case OpKind::Fillet: return "fillet";
        case OpKind::Import: return "import";
    }
    return "?";
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

json valueToJson(const Value& v) { return v.parameter.empty() ? json(v.number) : json(v.parameter); }

Value valueFromJson(const json& j) {
    Value v;
    if (j.is_number()) v.number = j.get<double>();
    else if (j.is_string()) v.parameter = j.get<std::string>();
    else throw std::invalid_argument("a value must be a number or the name of a parameter");
    return v;
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
            }
        }
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
            else throw std::invalid_argument("a segment's type must be line, arc or circle");
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
        sk.push_back(js);
    }
    for (const auto& o : m.operations) {
        json jo{{"id", o.id}, {"name", o.name}, {"kind", opKindName(o.kind)}, {"mode", opModeName(o.mode)}, {"suppressed", o.suppressed}};
        switch (o.kind) {
            case OpKind::Extrude: jo["sketch"] = o.sketch; jo["start"] = valueToJson(o.start); jo["end"] = valueToJson(o.end); break;
            case OpKind::Revolve:
                jo["sketch"] = o.sketch;
                jo["axisPoint"] = v2(o.axisPoint);
                jo["axisDir"] = v2(o.axisDir);
                jo["angle"] = valueToJson(o.angle);
                break;
            case OpKind::Fillet: {
                json pts = json::array();
                for (const auto& p : o.edgePoints) pts.push_back(v3(p));
                jo["edgePoints"] = pts;
                jo["radius"] = valueToJson(o.radius);
                break;
            }
            case OpKind::Import: jo["brep"] = o.brep; break;
        }
        ops.push_back(jo);
    }
    return {{"parameters", m.parameters}, {"sketches", sk}, {"operations", ops}, {"nextId", m.nextId}};
}

Model modelFromJson(const json& j) {
    Model m;
    m.parameters = j.value("parameters", std::map<std::string, double>{});
    for (const auto& js : j.value("sketches", json::array())) {
        Sketch s;
        s.id = js.at("id").get<std::uint64_t>();
        s.name = js.value("name", "");
        s.profile = profileFromJson(js);
        m.sketches.push_back(std::move(s));
    }
    for (const auto& jo : j.value("operations", json::array())) {
        Operation o;
        o.id = jo.at("id").get<std::uint64_t>();
        o.name = jo.value("name", "");
        const std::string k = jo.value("kind", "extrude");
        o.kind = k == "revolve" ? OpKind::Revolve : k == "fillet" ? OpKind::Fillet : k == "import" ? OpKind::Import : OpKind::Extrude;
        o.mode = opModeFromName(jo.value("mode", "new")).value_or(OpMode::New);
        o.suppressed = jo.value("suppressed", false);
        o.sketch = jo.value("sketch", std::uint64_t{0});
        if (jo.contains("start")) o.start = valueFromJson(jo["start"]);
        if (jo.contains("end")) o.end = valueFromJson(jo["end"]);
        if (jo.contains("axisPoint")) o.axisPoint = v2(jo["axisPoint"], "axisPoint");
        if (jo.contains("axisDir")) o.axisDir = v2(jo["axisDir"], "axisDir");
        if (jo.contains("angle")) o.angle = valueFromJson(jo["angle"]);
        for (const auto& p : jo.value("edgePoints", json::array())) o.edgePoints.push_back(v3(p, "edgePoints"));
        if (jo.contains("radius")) o.radius = valueFromJson(jo["radius"]);
        o.brep = jo.value("brep", "");
        m.operations.push_back(std::move(o));
    }
    m.nextId = j.value("nextId", std::uint64_t{1});
    for (const auto& s : m.sketches) m.nextId = std::max(m.nextId, s.id + 1);
    for (const auto& o : m.operations) m.nextId = std::max(m.nextId, o.id + 1);
    return m;
}

}  // namespace orv

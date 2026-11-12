// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a parametric part: named parameters, sketches and a list of
// operations applied in order (the history). Rebuilding replays the list into
// a Solid. Plain data: it is saved in the project as JSON and rebuilt on load.
#pragma once

#include "core/solid/BSpline.h"
#include "core/solid/Profile.h"
#include "core/solid/Solid.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orv {

/// A number, or the name of a parameter that gives it.
struct Value {
    double number{0.0};
    std::string parameter;  // empty: use number
    bool negate{false};     // "-name": minus the parameter
};

struct Sketch {
    std::uint64_t id{0};
    std::string name;
    Profile profile;
    std::string ref() const { return "s" + std::to_string(id); }
};

/// A free-form surface of the part, fitted to a zone of the scan: it cuts
/// the solid or is thickened into one.
struct FreeSurface {
    std::uint64_t id{0};
    std::string name;
    HeightSurface surface;
    double rms{0}, maxDeviation{0};  // of the scan's points it was fitted to
    std::string ref() const { return "sf" + std::to_string(id); }
};

enum class OpKind { Extrude, Revolve, Fillet, Import, Loft, Pattern, SurfaceCut, Thicken };
/// How an operation's solid combines with what was built before it.
enum class OpMode { New, Join, Cut, Intersect };

struct Operation {
    std::uint64_t id{0};
    std::string name;
    OpKind kind{OpKind::Extrude};
    OpMode mode{OpMode::New};
    bool suppressed{false};
    // extrude and revolve
    std::uint64_t sketch{0};
    // extrude: from `start` to `end` along the sketch's normal
    Value start, end;
    // revolve: an axis in the sketch's plane, and the angle
    Vec2d axisPoint{Vec2d::Zero()}, axisDir{Vec2d::UnitY()};
    Value angle{360.0, {}};
    // fillet: edges picked by points on them, and the radius
    std::vector<Vec3d> edgePoints;
    Value radius;
    // import: a solid read from a file (Open CASCADE text)
    std::string brep;
    // loft: its sections (sketches, in order), flat or smooth between them
    std::vector<std::uint64_t> sections;
    bool ruled{false};
    // pattern: copies of another operation's piece round an axis (angle: the
    // whole span; 360 spreads them evenly)
    std::uint64_t source{0};
    Vec3d axisOrigin{Vec3d::Zero()}, axisDirection{Vec3d::UnitZ()};
    Value count{2.0, {}};
    // surface cut and thicken: a free surface of the part
    std::uint64_t surface{0};
    bool keepBelow{true};
    Value thickness{1.0, {}};

    std::string ref() const { return "o" + std::to_string(id); }
};

struct Model {
    std::map<std::string, double> parameters;
    std::vector<Sketch> sketches;
    std::vector<FreeSurface> surfaces;
    std::vector<Operation> operations;
    std::uint64_t nextId{1};

    const Sketch* sketch(const std::string& refOrName) const;
    const Operation* operation(const std::string& refOrName) const;
    const FreeSurface* surface(const std::string& refOrName) const;
    double value(const Value& v) const;  // throws SolidError for an unknown parameter
};

/// What a rebuild produced: the solid after the last operation that worked,
/// and, when one failed, which and why (later ones were not applied).
struct BuildResult {
    Solid solid;
    std::string failedOperation;  // its ref, empty when all worked
    std::string error;
};
BuildResult rebuild(const Model& model);

const char* opKindName(OpKind k);
const char* opModeName(OpMode m);
std::optional<OpMode> opModeFromName(const std::string& s);

nlohmann::json valueToJson(const Value& v);
Value valueFromJson(const nlohmann::json& j);
nlohmann::json profileToJson(const Profile& p);
Profile profileFromJson(const nlohmann::json& j);  // throws std::invalid_argument with the reason
nlohmann::json surfaceToJson(const FreeSurface& f);
FreeSurface surfaceFromJson(const nlohmann::json& j);  // throws std::invalid_argument
nlohmann::json modelToJson(const Model& m);
Model modelFromJson(const nlohmann::json& j);

}  // namespace orv

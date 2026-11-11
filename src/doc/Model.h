// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a parametric part: named parameters, sketches and a list of
// operations applied in order (the history). Rebuilding replays the list into
// a Solid. Plain data: it is saved in the project as JSON and rebuilt on load.
#pragma once

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
};

struct Sketch {
    std::uint64_t id{0};
    std::string name;
    Profile profile;
    std::string ref() const { return "s" + std::to_string(id); }
};

enum class OpKind { Extrude, Revolve, Fillet, Import };
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

    std::string ref() const { return "o" + std::to_string(id); }
};

struct Model {
    std::map<std::string, double> parameters;
    std::vector<Sketch> sketches;
    std::vector<Operation> operations;
    std::uint64_t nextId{1};

    const Sketch* sketch(const std::string& refOrName) const;
    const Operation* operation(const std::string& refOrName) const;
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
nlohmann::json modelToJson(const Model& m);
Model modelFromJson(const nlohmann::json& j);

}  // namespace orv

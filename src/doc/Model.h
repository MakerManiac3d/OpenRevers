// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a parametric part: named parameters, sketches and a list of
// operations applied in order (the history). Rebuilding replays the list into
// a Solid. Plain data: it is saved in the project as JSON and rebuilt on load.
#pragma once

#include "core/solid/BSpline.h"
#include "core/solid/Profile.h"
#include "core/solid/Solid.h"
#include "core/solid/SketchSolve.h"

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
    /// Geometric constraints and dimensions (a dimension may take its value
    /// from a parameter of the part). The profile is kept solved.
    std::vector<SketchConstraint> constraints;
    int dof{-1};  // degrees of freedom left after the last solve (-1: no constraints)
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

enum class OpKind { Extrude, Revolve, Fillet, Import, Loft, Pattern, SurfaceCut, Thicken, Chamfer, Hole, Mirror };
/// How far an extrusion goes: `start` to `end`; both ways from the sketch
/// (`end` is the whole length); on past the whole part; or up to a face
/// parallel to the sketch, picked by a point on it.
enum class Extent { Distance, Symmetric, ThroughAll, UpToFace };
/// The shape of a hole's mouth.
enum class HoleKind { Simple, Counterbore, Countersink };
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
    Extent extent{Extent::Distance};
    bool reverse{false};     // extent through all / hole: the other way along the normal
    Vec3d toPoint{Vec3d::Zero()};  // extent up to face: a point on that face
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
    // hole: one per circle of the sketch, going into the material (against the
    // sketch's normal); diameter 0 takes each circle's own
    HoleKind holeKind{HoleKind::Simple};
    Value diameter{0.0, {}}, depth{10.0, {}};
    bool throughAll{false};
    Value cbDiameter{0.0, {}}, cbDepth{0.0, {}}, csDiameter{0.0, {}}, csAngle{90.0, {}};
    // mirror: another operation's piece (source 0: the whole part so far)
    // reflected in the plane through axisOrigin with normal axisDirection

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

/// Solves a sketch's constraints, with the dimensions that name a parameter
/// taking its value from the model. Returns an empty string when solved,
/// else why not (the sketch is then left as it was).
std::string solveSketchOf(const Model& m, Sketch& s, SketchSolveResult* result = nullptr);
/// Solves every sketch whose dimensions use `parameter` (all, if empty).
std::string solveSketchesUsing(Model& m, const std::string& parameter);
nlohmann::json constraintToJson(const SketchConstraint& c);
SketchConstraint constraintFromJson(const nlohmann::json& j);

const char* opKindName(OpKind k);
const char* opModeName(OpMode m);
const char* extentName(Extent e);
std::optional<Extent> extentFromName(const std::string& s);
const char* holeKindName(HoleKind k);
std::optional<HoleKind> holeKindFromName(const std::string& s);
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

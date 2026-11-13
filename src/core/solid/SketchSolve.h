// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — geometric constraints on a sketch's loops (horizontal,
// parallel, tangent, equal, distances, radii, angles...) solved with PlaneGCS,
// FreeCAD's sketch solver. The loops stay joined: in a loop each segment
// starts where the previous one ends, and the solver keeps it so.
#pragma once

#include "core/solid/Profile.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orv {

/// A piece of the sketch: a segment (its edge) or one of its points.
struct SketchRef {
    enum class Part { Edge, Start, End, Centre };
    int loop{0};
    int segment{0};
    Part part{Part::Edge};
};

enum class ConstraintKind {
    Coincident,     // two points
    Horizontal,     // a line, or two points
    Vertical,       // a line, or two points
    Parallel,       // two lines
    Perpendicular,  // two lines
    Tangent,        // line–circle/arc, circle/arc–circle/arc
    Equal,          // two lines (length) or two circles/arcs (radius)
    Distance,       // a line's length, two points, or a point and a line
    DistanceX,      // across: a line, or two points
    DistanceY,      // up: a line, or two points
    Radius,         // a circle or an arc
    Diameter,       // a circle or an arc
    Angle,          // two lines, degrees from the first to the second
    Fix,            // a point stays where it is
    OnObject,       // a point on a line, circle or arc
    Symmetric,      // two points about a line or a point
    Midpoint,       // a point at the middle of a line
};

struct SketchConstraint {
    ConstraintKind kind{ConstraintKind::Coincident};
    std::vector<SketchRef> refs;
    double value{0.0};       // distances in mm, angles in degrees
    std::string parameter;   // the part's parameter that gives value (kept for the model)
    bool driving{true};      // false: a reference dimension, only measured
    std::string name;        // shown to the person ("Distancia 3")
};

struct SketchSolveResult {
    bool solved{false};
    /// Degrees of freedom left: 0 is a fully defined sketch.
    int dof{-1};
    /// Indices (into the constraint list) that contradict each other or
    /// that say again what others already say.
    std::vector<int> conflicting, redundant;
    std::string message;
};

struct SketchSolveOptions {
    /// Points pulled towards a place (dragging in the editor): followed as
    /// far as the constraints allow.
    std::vector<std::pair<SketchRef, Vec2d>> drag;
};

/// Moves the profile's geometry so that the constraints hold. Splines are not
/// moved (their poles are fixed). On failure the profile is left as it was.
SketchSolveResult solveSketch(Profile& profile, const std::vector<SketchConstraint>& constraints, const SketchSolveOptions& options = {});

/// What the constraint measures now on the profile (a dimension's current
/// value), or nothing for constraints with no value.
std::optional<double> measureConstraint(const Profile& profile, const SketchConstraint& c);

const char* constraintKindName(ConstraintKind k);
std::optional<ConstraintKind> constraintKindFromName(const std::string& name);
bool constraintHasValue(ConstraintKind k);

}  // namespace orv

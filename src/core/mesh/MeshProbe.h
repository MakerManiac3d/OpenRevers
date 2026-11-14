// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — measuring a scan without building anything from it: rays
// through the mesh (chords: wall thicknesses, hole diameters, slot widths),
// the radial profile round an axis (the lathe-turned zones of a part), and
// the unwrapped map (radius against angle and position along the axis) that
// shows what is not round: slots, flats, bosses, flanges.
#pragma once

#include "core/mesh/TriMesh.h"

#include <string>
#include <vector>

namespace orv {

struct RayHit {
    double t{0};            // distance along the (unit) direction
    Vec3d point{Vec3d::Zero()};
    bool entering{false};   // the surface faces the ray: it enters material (on a closed, outward mesh)
};

/// Where a ray crosses the mesh's triangles, nearest first, within [tMin, tMax].
std::vector<RayHit> castRay(const TriMesh& mesh, const Vec3d& origin, const Vec3d& direction, double tMin, double tMax);

/// A stretch of a ray inside material (an entering hit followed by a leaving one).
struct Chord {
    double t0{0}, t1{0};
    Vec3d from{Vec3d::Zero()}, to{Vec3d::Zero()};
    double length() const { return t1 - t0; }
};
std::vector<Chord> chordsOf(const std::vector<RayHit>& hits);

struct Axis {
    Vec3d point{Vec3d::Zero()};
    Vec3d direction{Vec3d::UnitZ()};
    Vec3d reference{Vec3d::UnitX()};  // where angle 0 is (perpendicular to the direction)
    /// Normalises the direction and makes `reference` perpendicular to it
    /// (any perpendicular if none was given).
    void normalize(const Vec3d* wanted = nullptr);
    double along(const Vec3d& p) const { return (p - point).dot(direction); }
    double radius(const Vec3d& p) const;
    double angleDeg(const Vec3d& p) const;  // 0..360, counterclockwise seen from the tip of direction
};

/// Which surface of a hollow part: the outermost (outer: 95th percentile of
/// the radius) or the innermost (inner: 5th percentile) at each place.
enum class Skin { Outer, Inner };

struct ProfileBin {
    double z0{0}, z1{0};
    std::size_t count{0};
    double rMin{0}, rInner{0}, rMedian{0}, rOuter{0}, rMax{0};
};

/// Points sampled from the mesh: its vertices and the centres of its
/// triangles (the vertices alone for a point cloud).
// Vertices y puntos sobre cada triangulo; si spacing > 0 los triangulos largos se
// subdividen para que los puntos no queden mas separados que spacing.
std::vector<Vec3d> samplePoints(const TriMesh& mesh, double spacing = 0.0);

/// Radius against position along the axis, in bins of `step`, from zMin to
/// zMax (both given in the axis' own coordinate; NaN: the mesh's extent).
std::vector<ProfileBin> radialProfile(const std::vector<Vec3d>& points, const Axis& axis, double step, double zMin, double zMax, double maxRadius);

/// A stretch of the profile that is one cylinder (radius constant) or cone
/// (radius changing evenly).
struct ProfileZone {
    double z0{0}, z1{0};
    double rStart{0}, rEnd{0};
    double halfAngleDeg{0};  // 0: a cylinder
    double maxResidual{0};
    std::size_t bins{0};
    std::string kind;        // "cylinder", "cone" or "step" (too short to tell)
};
/// Splits the profile of one skin into zones, each within `tolerance` of a straight line.
std::vector<ProfileZone> profileZones(const std::vector<ProfileBin>& bins, Skin skin, double tolerance);

/// Radius of one skin on a grid of angle × position along the axis.
struct Unwrapped {
    int nAngle{0}, nAlong{0};
    double z0{0}, dz{0};
    std::vector<float> radius;      // nAlong rows of nAngle; NaN where no points
    std::vector<float> rowMedian;   // per row (NaN: empty row)
    std::size_t empty{0};
    double at(int row, int col) const { return radius[static_cast<std::size_t>(row) * static_cast<std::size_t>(nAngle) + static_cast<std::size_t>(col)]; }
};
Unwrapped unwrap(const std::vector<Vec3d>& points, const Axis& axis, Skin skin, int nAngle, double dz, double zMin, double zMax, double maxRadius);

/// A patch of the unwrapped map whose radius differs from its row's by more
/// than the threshold: a slot (deviation < 0 on the outer skin), a boss, a flat.
struct Deviation {
    double z0{0}, z1{0};
    double angle0{0}, angle1{0};  // degrees; angle1 may pass 360 (the patch wraps round)
    double meanDeviation{0}, worstDeviation{0};
    double rowRadius{0};          // the radius the rest of those rows has
    std::size_t cells{0};
};
std::vector<Deviation> deviations(const Unwrapped& u, double threshold, std::size_t minCells);

}  // namespace orv

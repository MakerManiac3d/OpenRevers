// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — geometric primitives fitted to scanned points: plane, sphere,
// cylinder, cone and torus. Each fit starts from a closed-form estimate made
// with the points' normals, is refined by Levenberg–Marquardt on the
// geometric (orthogonal) distances, and drops outliers beyond 3σ.
#pragma once

#include "core/mesh/TriMesh.h"

#include <optional>
#include <string>
#include <vector>

namespace orv {

enum class PrimitiveType { Plane, Sphere, Cylinder, Cone, Torus };

const char* primitiveName(PrimitiveType t);                     // "plane", "sphere", …
std::optional<PrimitiveType> primitiveFromName(const std::string& n);

/// A fitted shape. Which fields mean something depends on the type:
///   plane:    origin (a point on it), axis (unit normal)
///   sphere:   origin (centre), radius
///   cylinder: origin (a point on the axis), axis, radius
///   cone:     origin (apex), axis (from the apex into the cone), angle (half angle, rad)
///   torus:    origin (centre), axis, radius (major), radius2 (minor)
struct Primitive {
    PrimitiveType type{PrimitiveType::Plane};
    Vec3d origin{Vec3d::Zero()};
    Vec3d axis{Vec3d::UnitZ()};
    double radius{0.0};
    double radius2{0.0};
    double angle{0.0};

    // Quality, over the points the fit kept.
    double rms{0.0};               // σ: root mean square distance, mm
    double maxAbs{0.0};            // largest distance, mm
    std::size_t points{0};         // points kept
    std::size_t outliers{0};       // points left out (beyond 3σ)

    /// Signed distance of p to the surface (positive outside / along the normal).
    double distance(const Vec3d& p) const;
    /// Unit normal of the surface at its point nearest p.
    Vec3d normal(const Vec3d& p) const;
};

struct FitOptions {
    bool rejectOutliers{true};
    int maxIterations{60};
};

/// Fit one type. `normals` may be empty (then plane and sphere still work;
/// cylinder, cone and torus need them for their first estimate).
/// Returns nothing when the points do not determine the shape.
std::optional<Primitive> fitPrimitive(PrimitiveType type, const std::vector<Vec3d>& points, const std::vector<Vec3d>& normals,
                                      const FitOptions& options = {});

/// Fit every type and keep the simplest whose σ is within `slack` of the
/// best (plane, then sphere and cylinder, then cone, then torus), with
/// sanity checks (a cone of almost no angle is a cylinder, a cylinder of a
/// huge radius a plane…). `noise` is the scanner's noise in mm, used as a
/// floor for comparing σ.
std::optional<Primitive> fitBest(const std::vector<Vec3d>& points, const std::vector<Vec3d>& normals, double noise = 0.0,
                                 double slack = 1.25);

}  // namespace orv

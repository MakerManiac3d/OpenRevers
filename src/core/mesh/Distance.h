// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — distances from points to a triangle mesh (a solid's
// triangles): how far a scan lies from the model built from it.
#pragma once

#include "core/mesh/TriMesh.h"

#include <memory>
#include <vector>

namespace orv {

/// Nearest-point queries on a triangle mesh. Long triangles are split first
/// so that a search among the nearest triangle centres finds the true
/// nearest triangle.
class MeshDistance {
public:
    /// `maxEdge`: triangles are split until no edge is longer (0: automatic,
    /// 1% of the mesh's size).
    explicit MeshDistance(const TriMesh& mesh, double maxEdge = 0.0);
    ~MeshDistance();
    /// Signed distance: positive on the side the triangle faces (outside a
    /// closed, outward mesh).
    double signedDistance(const Vec3d& p) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

struct DeviationStats {
    std::size_t points{0};
    double mean{0}, rms{0}, median{0}, p90{0}, p95{0}, maxAbs{0};  // of the absolute distance, but mean is signed
    double minSigned{0}, maxSigned{0};
    double withinFraction{0};  // share of points within ±tolerance
};

/// Distances from (at most `maxPoints` of) `points` to `mesh`.
DeviationStats deviation(const std::vector<Vec3f>& points, const TriMesh& mesh, double tolerance, std::size_t maxPoints = 200000,
                         std::vector<double>* distances = nullptr);

/// The closed and open polylines where a plane cuts a mesh, in the plane's
/// own coordinates (x along xDir, y along normal × xDir).
struct SectionPolyline {
    std::vector<Eigen::Vector2d> points;
    bool closed{false};
};
std::vector<SectionPolyline> sectionMesh(const TriMesh& mesh, const Vec3d& origin, const Vec3d& normal, const Vec3d& xDir);
/// Douglas–Peucker: drops points closer than `tolerance` to the chord.
std::vector<Eigen::Vector2d> simplifyPolyline(const std::vector<Eigen::Vector2d>& pts, double tolerance, bool closed);

}  // namespace orv

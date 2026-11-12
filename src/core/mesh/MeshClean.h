// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — cleaning a scan: the table or turntable it stood on, stray
// points, and whatever lies inside or outside a region. Works on meshes
// (faces touching a removed vertex go with it) and on point clouds.
#pragma once

#include "core/mesh/TriMesh.h"

#include <functional>
#include <optional>
#include <vector>

namespace orv {

struct PlaneFound {
    Vec3d point{Vec3d::Zero()}, normal{Vec3d::UnitZ()};  // normal towards the object
    std::size_t inliers{0};
    double share{0.0};  // of the vertices on it
};

/// The plane with most vertices within `distance` (RANSAC, refined by a
/// least-squares fit): the table under a scan. Its normal points to the side
/// where most of the rest lies. Nothing when no plane holds 2 % of the points.
std::optional<PlaneFound> findLargestPlane(const TriMesh& mesh, double distance, unsigned seed = 7);

/// Remove the vertices for which `drop` is true (and the faces using them).
/// Returns how many vertices went.
std::size_t removeVertices(TriMesh& mesh, const std::function<bool(const Vec3f&)>& drop);

/// Remove what lies within `distance` of the plane and, when `beyond`, all
/// that lies on its far side (against its normal). Returns vertices removed.
std::size_t removePlane(TriMesh& mesh, const PlaneFound& plane, double distance, bool beyond);

/// Statistical outlier removal: a vertex whose mean distance to its
/// `neighbours` nearest is above the mean of all by `ratio` standard
/// deviations goes. Returns vertices removed.
std::size_t removeOutliers(TriMesh& mesh, int neighbours, double ratio);

/// Remove these faces of a mesh (or these points of a cloud), and the
/// vertices left unused. Returns faces (or points) removed.
std::size_t removeFaces(TriMesh& mesh, const std::vector<std::uint32_t>& faces);

}  // namespace orv

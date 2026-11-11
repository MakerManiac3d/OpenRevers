// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — putting a part in place: by its principal axes, by datums
// (3-2-1 from planes, axes and centres, as on a measuring machine) and by
// best fit against a reference (robust point-to-plane ICP).
#pragma once

#include "core/fit/Primitive.h"
#include "core/mesh/TriMesh.h"

#include <Eigen/Geometry>

#include <optional>
#include <string>

namespace orv {

using Transform = Eigen::Isometry3d;

/// Move a mesh (points and point normals) by a rigid transform.
void transformMesh(TriMesh& mesh, const Transform& t);
Primitive transformPrimitive(const Primitive& p, const Transform& t);

/// Transform that puts the mesh's area centroid at the origin and its
/// principal axes on X (longest), Y, Z (shortest), right-handed. Signs follow
/// the third moments, so the same part always lands the same way.
Transform principalAlignment(const TriMesh& mesh);

/// 3-2-1 alignment from datums, as on a measuring machine. The primary gives
/// Z: into the part from a datum face (against its outward normal), or along
/// an axis. The secondary gives X: into the part from a face, or along an axis
/// across Z; for an axis along Z (a hole), X points from it to the tertiary.
/// The origin takes what each datum fixes, in order: primary, then secondary,
/// then tertiary. Returns nothing, with `why`, when the datums do not fix a frame.
std::optional<Transform> datumAlignment(const Primitive& primary, const std::optional<Primitive>& secondary,
                                        const std::optional<Primitive>& tertiary, std::string* why = nullptr);

struct BestFitOptions {
    int maxIterations{60};
    std::size_t samples{20000};   // points of the moving mesh used
    double maxDistance{0.0};      // correspondences farther are ignored (0: automatic)
    bool coarseStart{false};      // start from matching principal axes (for parts far apart)
};

struct BestFitResult {
    Transform transform{Transform::Identity()};  // moving -> reference
    int iterations{0};
    double rms{0.0};              // of the points in use, mm
    double median{0.0};           // median absolute distance, mm
    double p95{0.0};              // 95th percentile of absolute distance, mm
    double overlap{0.0};          // fraction of the sampled points matched
};

/// Best-fit alignment of `moving` onto `reference` (both meshes). Throws
/// std::runtime_error when there is too little overlap.
BestFitResult bestFit(const TriMesh& moving, const TriMesh& reference, const BestFitOptions& options = {});

}  // namespace orv

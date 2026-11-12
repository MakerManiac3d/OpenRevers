// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — putting one scan onto another when nothing places them yet:
// from pairs of points the user picks (least squares, Kabsch 1976), or from
// their shape alone (FPFH descriptors, Rusu et al. 2009, matched under
// RANSAC, Fischler & Bolles 1981), each finished by the best-fit ICP.
#pragma once

#include "core/fit/Align.h"

#include <vector>

namespace orv {

/// The rigid transform taking `from` onto `to` (same count, at least three
/// points not on a line) in the least-squares sense. `residuals`, if given,
/// gets each pair's remaining distance. Throws std::invalid_argument.
Transform rigidFromPairs(const std::vector<Vec3d>& from, const std::vector<Vec3d>& to, std::vector<double>* residuals = nullptr);

/// Points of a mesh or cloud with normals, one per cell of side `voxel`.
struct SampledCloud {
    std::vector<Vec3d> points, normals;
};
SampledCloud sampleForRegistration(const TriMesh& mesh, double voxel);

/// Fast Point Feature Histograms (33 numbers per point) within `radius`.
std::vector<std::array<float, 33>> fpfh(const SampledCloud& cloud, double radius);

struct GlobalRegOptions {
    double voxel{0.0};          // sampling cell, mm (0: 1/60 of the reference's size)
    int maxIterations{200000};  // RANSAC draws
    double seconds{8.0};        // and at most this long
    bool refine{true};          // finish with the best-fit ICP
    unsigned seed{12345};
};

struct GlobalRegResult {
    Transform transform{Transform::Identity()};  // moving -> reference
    double voxel{0.0};
    std::size_t correspondences{0};  // descriptor matches tried
    std::size_t inliers{0};          // of them, agreeing with the transform
    double coarseOverlap{0.0};       // share of the moving samples near the reference after RANSAC
    BestFitResult refined;           // the ICP's figures (when refine)
};

/// Align `moving` onto `reference` from their shape alone (any start). They
/// must overlap (a third or more of `moving` should be seen in `reference`).
/// Throws std::runtime_error when no consistent match is found.
GlobalRegResult globalRegistration(const TriMesh& moving, const TriMesh& reference, const GlobalRegOptions& options = {});

/// The meshes and clouds joined into one: faces renumbered; normals kept
/// when every input has them. Clouds and meshes cannot be mixed.
TriMesh mergeMeshes(const std::vector<const TriMesh*>& meshes);

}  // namespace orv

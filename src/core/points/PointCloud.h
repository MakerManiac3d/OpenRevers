// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — point clouds: thinning, normals and surface reconstruction.
// A point cloud is a TriMesh with vertices, optional normals and no faces.
#pragma once

#include "core/mesh/TriMesh.h"

#include <cstddef>

namespace orv {

/// Keep one point per cubic cell of side `spacing` mm (the one nearest the
/// cell's centre of mass, with its normal). Returns the points removed.
std::size_t thinPoints(TriMesh& cloud, float spacing);

enum class NormalOrientation {
    Propagate,  // spread one orientation over the neighbour graph (Hoppe et al. 1992)
    Outward,    // away from the cloud's centre: closed, roughly convex objects
    Keep,       // keep each point's side from the normals it already has
};

struct NormalsResult {
    std::size_t points{0};
    std::size_t flipped{0};       // normals turned to agree with the orientation
    std::size_t isolated{0};      // points with too few neighbours (normal left as is or zero)
    double meanSpacing{0.0};      // mean distance to the nearest neighbour, mm
};

/// Estimate a normal per point from its `neighbours` nearest points (plane
/// fit by principal components), then orient them.
NormalsResult estimateNormals(TriMesh& cloud, int neighbours, NormalOrientation orientation);

/// Mean distance from each point to its nearest neighbour (sampled on up to
/// 20 000 points), mm.
double meanPointSpacing(const TriMesh& cloud);

struct PoissonOptions {
    int depth{8};                 // octree depth: resolution ~ size / 2^depth
    float pointWeight{4.f};       // how strongly the surface passes through the points
    float samplesPerNode{1.5f};   // more for noisy scans
    float trimQuantile{0.f};      // remove the surface where the point density is in this lowest fraction (0..0.5)
    bool keepLargest{true};       // after trimming, keep only the largest piece
};

struct PoissonResult {
    std::size_t facesBeforeTrim{0};
    std::size_t facesTrimmed{0};
};

/// Screened Poisson surface reconstruction (Kazhdan & Hoppe 2013, the
/// authors' PoissonRecon). The cloud must have normals. Throws
/// std::runtime_error on failure.
PoissonResult reconstructPoisson(const TriMesh& cloud, const PoissonOptions& options, TriMesh& out);

}  // namespace orv

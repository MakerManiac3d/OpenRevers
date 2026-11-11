// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — automatic segmentation of a scanned mesh into regions that
// each follow one primitive (plane, sphere, cylinder, cone or torus).
//
// Region growing guided by the fitted shape: a seed patch on a smooth spot
// gets the simplest primitive that fits it; the region then takes in every
// neighbouring face that lies on that primitive (within a tolerance set by
// the scanner's noise) and faces the same way, refitting as it grows. What is
// left (fillets, blends, freeform, noise) stays unassigned.
#pragma once

#include "core/fit/Primitive.h"
#include "core/mesh/TriMesh.h"

#include <cstdint>
#include <vector>

namespace orv {

struct SegmentOptions {
    double noise{0.0};          // scanner noise, mm (0: estimate it from the mesh)
    double tolerance{3.0};      // a face belongs to a region within tolerance × noise
    double angleDeg{12.0};      // and with its normal within this angle of the shape's
    std::size_t minFaces{0};    // smaller regions are dropped (0: 0.05 % of the faces, at least 20)
    bool merge{true};           // join regions that are the same shape (a plane cut by a slot…)
};

struct Region {
    Primitive shape;
    std::size_t faces{0};
    double area{0.0};           // mm²
};

struct Segmentation {
    static constexpr std::int32_t kNone = -1;
    std::vector<std::int32_t> faceRegion;  // one per face: region index or kNone
    std::vector<Region> regions;
    double noise{0.0};                     // the noise used, mm
    std::size_t unassignedFaces{0};
};

/// Estimate the scanner noise: the typical σ of small patches fitted with
/// their best primitive, on the flattest quarter of the surface.
double estimateNoise(const TriMesh& mesh);

Segmentation segment(const TriMesh& mesh, const SegmentOptions& options = {});

/// Points and normals of the faces of one region (face centres), for refits.
void regionSamples(const TriMesh& mesh, const Segmentation& s, std::int32_t region, std::vector<Vec3d>& points, std::vector<Vec3d>& normals);

}  // namespace orv

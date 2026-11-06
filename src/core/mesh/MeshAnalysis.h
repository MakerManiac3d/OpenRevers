// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — what a mesh is like before it is used for modelling: holes,
// non-manifold edges, flipped faces, loose pieces, degenerate triangles.
#pragma once

#include "core/mesh/TriMesh.h"

#include <cstdint>
#include <vector>

namespace orv {

/// An open rim (the edge of a hole or of the scanned surface), as vertices in order.
struct BoundaryLoop {
    std::vector<std::uint32_t> vertices;
    double length{0.0};  // mm
};

struct MeshReport {
    std::size_t vertices{0}, faces{0};
    std::size_t edges{0};
    std::size_t boundaryEdges{0};
    std::size_t nonManifoldEdges{0};     // edges shared by more than two faces
    std::size_t degenerateFaces{0};      // zero area (repeated or collinear corners)
    std::size_t duplicateFaces{0};       // the same three vertices twice
    std::size_t orientationConflicts{0}; // neighbouring faces wound against each other
    std::size_t components{0};           // pieces connected through edges
    std::size_t largestComponentFaces{0};
    std::vector<BoundaryLoop> boundaryLoops;
    double area{0.0};          // mm²
    double volume{0.0};        // mm³, only meaningful when closed
    bool closed{false};        // no boundary edges, no non-manifold edges
    double meanEdgeLength{0.0};  // mm
    Box3 bounds;
};

/// Analyse the mesh. Linear in its size; nothing is changed.
MeshReport analyze(const TriMesh& mesh);

/// The connected pieces, as a component id per face; returns the number of pieces.
std::size_t faceComponents(const TriMesh& mesh, std::vector<std::uint32_t>& componentOfFace);

}  // namespace orv

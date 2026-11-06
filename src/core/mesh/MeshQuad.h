// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — quad remeshing and T-Spline export: from a scanned triangle
// mesh to a clean quad mesh that CAD programs turn into a smooth surface.
#pragma once

#include "core/mesh/TriMesh.h"

#include <filesystem>
#include <string>

namespace orv {

struct QuadRemeshOptions {
    std::size_t targetFaces{2000};   // quads wanted (the result is close, not exact)
    bool preserveSharp{true};        // follow the part's sharp edges
    bool preserveBoundary{true};     // follow open rims
    bool adaptive{false};            // smaller quads where the surface curves
    bool minimumCostFlow{false};     // slower, sometimes fewer singularities
    int seed{0};                     // same seed, same result
};

struct QuadRemeshResult {
    std::size_t quads{0};
    std::size_t droppedDegenerate{0};  // quads with a repeated corner, left out
};

/// Replace the mesh by a quad mesh (QuadriFlow, Huang et al., SGP 2018).
/// The mesh must have no non-manifold edges; throws std::runtime_error otherwise
/// or when the solver gives no quads.
QuadRemeshResult remeshQuads(TriMesh& mesh, const QuadRemeshOptions& options);

/// Why a quad mesh cannot become a T-Spline body, or empty if it can:
/// it must be all quads, closed, manifold and with no vertex of valence 2.
std::string tsmProblem(const TriMesh& mesh);

struct TsmResult {
    std::size_t faces{0}, edges{0}, vertices{0};
    bool flipped{false};  // the quads were turned outward
};

/// Write a closed quad mesh as an Autodesk Fusion T-Spline file (.tsm,
/// format TS0200; coordinates in centimetres). Throws std::runtime_error
/// with tsmProblem() when the mesh cannot be written.
TsmResult writeTsm(const TriMesh& mesh, const std::filesystem::path& path);

}  // namespace orv

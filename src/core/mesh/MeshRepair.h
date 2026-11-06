// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — cleaning and processing a scanned mesh. Every function changes
// the mesh it is given and says, in its result, what it did. They work on the
// triangles: on a quad mesh, all but flipNormals leave the quads out of date,
// and the caller drops them (TriMesh::dropQuads).
#pragma once

#include "core/mesh/TriMesh.h"

#include <cstddef>

namespace orv {

/// Remove faces with repeated corners or zero area; returns how many.
std::size_t removeDegenerateFaces(TriMesh& mesh, float areaEpsilon = 0.f);

/// Remove faces that repeat another face's three vertices; returns how many.
std::size_t removeDuplicateFaces(TriMesh& mesh);

/// Merge vertices closer than `tolerance` mm; returns how many were merged.
std::size_t weldVertices(TriMesh& mesh, float tolerance);

/// Remove connected pieces with fewer than `minFaces` faces, or every piece
/// but the largest when `keepLargestOnly`. Returns the number of faces removed.
std::size_t removeSmallComponents(TriMesh& mesh, std::size_t minFaces, bool keepLargestOnly = false);

/// Make neighbouring faces agree on their winding, piece by piece, and turn
/// each closed piece outward. Returns the number of faces flipped.
std::size_t orientConsistently(TriMesh& mesh);

/// Reverse every face, and every quad (normals point the other way).
void flipNormals(TriMesh& mesh);

struct FillHolesResult {
    std::size_t filled{0};
    std::size_t skipped{0};   // larger than maxEdges, or the filler refused them
    std::size_t facesAdded{0};
};
/// Close holes whose rim has at most `maxEdges` edges (0 = all), with a
/// fair patch (Liepa 2003, as implemented in PMP).
FillHolesResult fillHoles(TriMesh& mesh, std::size_t maxEdges);

/// Laplacian smoothing. `implicit` uses the stable implicit scheme with
/// `timestep`; otherwise `iterations` explicit steps.
void smooth(TriMesh& mesh, bool implicitScheme, unsigned iterations, float timestep);

struct DecimateOptions {
    std::size_t targetVertices{0};    // stop at this many vertices
    float maxError{0.f};              // mm, Hausdorff bound (0 = none)
    float normalDeviationDeg{0.f};    // limit on normal change (0 = none)
    float aspectRatio{0.f};           // limit on triangle aspect ratio (0 = none)
    // Largest number of edges at a vertex after a collapse (0 = none). Without
    // a limit, flat regions (every collapse free) gather into fans of
    // thousands of triangles, and the time grows with the square of that.
    unsigned maxValence{12};
};
/// Quadric edge-collapse decimation (Garland & Heckbert 1997, PMP).
void decimate(TriMesh& mesh, const DecimateOptions& options);

/// Isotropic remeshing to a uniform edge length (Botsch & Kobbelt 2004, PMP).
/// Edges where the faces meet at more than `featureAngleDeg` (0 = none) are
/// kept as sharp edges: a remesh must not round a part's corners.
void remeshUniform(TriMesh& mesh, float edgeLength, unsigned iterations, float featureAngleDeg = 30.f);

/// Curvature-adaptive remeshing between `minEdge` and `maxEdge`, keeping
/// within `approxError` of the surface (Dunyach et al. 2013, PMP).
void remeshAdaptive(TriMesh& mesh, float minEdge, float maxEdge, float approxError, unsigned iterations, float featureAngleDeg = 30.f);

}  // namespace orv

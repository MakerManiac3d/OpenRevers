// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — moving meshes into and out of PMP's half-edge structure.
#pragma once

#include "core/mesh/TriMesh.h"

#include <pmp/surface_mesh.h>

namespace orv {

/// Builds a PMP mesh. Faces PMP cannot add (they would make the mesh
/// non-manifold) are left out and counted in `skipped`.
pmp::SurfaceMesh toPmp(const TriMesh& mesh, std::size_t* skipped = nullptr);

TriMesh fromPmp(pmp::SurfaceMesh& mesh);

}  // namespace orv

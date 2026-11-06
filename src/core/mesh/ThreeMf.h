// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — 3MF (3D Manufacturing Format, 3MF Consortium core spec 1.3):
// a ZIP package holding the model as XML. Read: every build item, with its
// objects, components and transforms, converted to millimetres. Write: one
// object in millimetres.
#pragma once

#include "core/mesh/TriMesh.h"

#include <filesystem>
#include <string>

namespace orv {

/// Read a 3MF package from its bytes. Throws IoError.
TriMesh read3mf(const std::string& bytes);

/// Write the mesh as a 3MF package (unit: millimetre). Throws IoError.
void write3mf(const TriMesh& mesh, const std::filesystem::path& path);

}  // namespace orv

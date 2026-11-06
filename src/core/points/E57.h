// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — reading ASTM E57 point clouds (ASTM E2807, the format of most
// terrestrial and many handheld scanners). Own reader, no other library:
//   - the file's 1024-byte pages, each ending in a CRC-32C, are checked;
//   - every scan in /data3D is read: Cartesian or spherical coordinates as
//     float, double, integer or scaled integer; its pose (rotation and
//     translation) is applied; invalid points are left out; normals from the
//     "nor" extension are kept;
//   - metres are converted to millimetres.
// Images, intensity and colour are not read.
#pragma once

#include "core/mesh/TriMesh.h"

#include <string>

namespace orv {

struct E57Summary {
    std::size_t scans{0};
    std::size_t records{0};    // points in the file
    std::size_t invalid{0};    // left out: invalid or direction-only points
};

/// Read every scan of an E57 file (its bytes) into one point cloud, in
/// millimetres. Throws IoError.
TriMesh readE57(const std::string& bytes, E57Summary* summary = nullptr);

}  // namespace orv

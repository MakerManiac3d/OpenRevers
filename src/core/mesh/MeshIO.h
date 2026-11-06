// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — reading and writing meshes: STL (binary and ASCII), OBJ, PLY
// (ASCII and binary little/big endian) and 3MF. Coordinates are kept as written
// in the file (millimetres assumed), except 3MF, which states its unit and is
// converted to millimetres.
#pragma once

#include "core/mesh/TriMesh.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace orv {

/// Error while reading or writing a file; the message says which file and why.
class IoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class MeshFormat { Stl, StlAscii, Obj, Ply, PlyAscii, ThreeMf, Xyz };

/// The format a file name's extension names; throws IoError for one it does not.
MeshFormat formatFromPath(const std::filesystem::path& path);

/// Read a mesh or a point cloud (a PLY, OBJ or XYZ file without faces; its
/// normals are kept when the file has them). STL vertices are welded (exactly coincident corners become one
/// vertex). Polygons in OBJ and PLY are fan-triangulated (kept as quads too
/// when every polygon is a quad). 3MF: every build item, in millimetres.
TriMesh readMesh(const std::filesystem::path& path);

/// Write a mesh or a point cloud. `.stl` writes binary STL; `.ply` binary
/// little endian; `.xyz` points (and normals) as text. A point cloud cannot be
/// written as STL or 3MF, nor a mesh as XYZ (IoError).
void writeMesh(const TriMesh& mesh, const std::filesystem::path& path);
void writeMesh(const TriMesh& mesh, const std::filesystem::path& path, MeshFormat format);

}  // namespace orv

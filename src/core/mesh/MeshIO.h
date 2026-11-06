// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — reading and writing meshes: STL (binary and ASCII), OBJ, PLY
// (ASCII and binary little/big endian). Coordinates are kept as written in the
// file; the caller says what unit they are in.
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

enum class MeshFormat { Stl, StlAscii, Obj, Ply, PlyAscii };

/// The format a file name's extension names; throws IoError for one it does not.
MeshFormat formatFromPath(const std::filesystem::path& path);

/// Read a mesh. STL vertices are welded (exactly coincident corners become one
/// vertex). Polygons in OBJ and PLY are fan-triangulated.
TriMesh readMesh(const std::filesystem::path& path);

/// Write a mesh. `.stl` writes binary STL; `.ply` binary little endian.
void writeMesh(const TriMesh& mesh, const std::filesystem::path& path);
void writeMesh(const TriMesh& mesh, const std::filesystem::path& path, MeshFormat format);

}  // namespace orv

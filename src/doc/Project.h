// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the project file (.orvp). Documented in docs/desarrollo/formato-proyecto.md:
//   8 bytes  magic "ORVPROJ\0"
//   4 bytes  format version (uint32, little endian) = 1
//   8 bytes  length of the JSON manifest (uint64, little endian)
//   N bytes  JSON manifest (UTF-8): bodies, their names, colours and where their data lies
//   ...      binary blocks: per mesh, vertices (float32 x,y,z) then faces (uint32 a,b,c)
// Nothing in a project is ever executed.
#pragma once

#include "doc/Document.h"

#include <filesystem>

namespace orv {

void saveProject(const Document& doc, const std::filesystem::path& path);
void loadProject(Document& doc, const std::filesystem::path& path);

}  // namespace orv

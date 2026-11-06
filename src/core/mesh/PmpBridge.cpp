// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/PmpBridge.h"

#include <pmp/exceptions.h>

namespace orv {

pmp::SurfaceMesh toPmp(const TriMesh& mesh, std::size_t* skipped) {
    pmp::SurfaceMesh pm;
    pm.reserve(mesh.vertices.size(), mesh.faces.size() * 3 / 2, mesh.faces.size());
    for (const auto& p : mesh.vertices) pm.add_vertex(pmp::Point(p.x(), p.y(), p.z()));
    std::size_t bad = 0;
    for (const Tri& t : mesh.faces) {
        try {
            pm.add_triangle(pmp::Vertex(t[0]), pmp::Vertex(t[1]), pmp::Vertex(t[2]));
        } catch (const pmp::TopologyException&) {
            ++bad;
        }
    }
    if (skipped) *skipped = bad;
    return pm;
}

TriMesh fromPmp(pmp::SurfaceMesh& pm) {
    pm.garbage_collection();
    TriMesh m;
    m.vertices.reserve(pm.n_vertices());
    for (auto v : pm.vertices()) {
        const auto& p = pm.position(v);
        m.vertices.emplace_back(static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]));
    }
    m.faces.reserve(pm.n_faces());
    for (auto f : pm.faces()) {
        Tri t{};
        int k = 0;
        for (auto v : pm.vertices(f)) {
            if (k < 3) t[k] = static_cast<std::uint32_t>(v.idx());
            ++k;
        }
        if (k == 3) m.faces.push_back(t);
    }
    m.removeUnusedVertices();
    return m;
}

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/MeshAnalysis.h"

#include "core/mesh/EdgeMap.h"

#include <Eigen/Geometry>

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace orv {

namespace {

std::size_t components(const TriMesh& mesh, const EdgeMap& em, std::vector<std::uint32_t>& comp) {
    const std::size_t n = mesh.faces.size();
    comp.assign(n, UINT32_MAX);
    std::uint32_t next = 0;
    std::vector<std::uint32_t> stack;
    for (std::size_t s = 0; s < n; ++s) {
        if (comp[s] != UINT32_MAX) continue;
        comp[s] = next;
        stack.push_back(static_cast<std::uint32_t>(s));
        while (!stack.empty()) {
            const std::uint32_t f = stack.back();
            stack.pop_back();
            for (std::size_t k = 0; k < 3; ++k) {
                const std::uint32_t e = em.edgeOf(f, k);
                if (e == EdgeMap::kNone) continue;
                for (std::uint32_t g : em.faces(em.edge(e))) {
                    if (comp[g] == UINT32_MAX) {
                        comp[g] = next;
                        stack.push_back(g);
                    }
                }
            }
        }
        ++next;
    }
    return next;
}

}  // namespace

std::size_t faceComponents(const TriMesh& mesh, std::vector<std::uint32_t>& comp) {
    return components(mesh, EdgeMap(mesh), comp);
}

MeshReport analyze(const TriMesh& mesh) {
    MeshReport r;
    r.vertices = mesh.vertexCount();
    r.faces = mesh.faceCount();
    r.bounds = mesh.bounds();
    r.area = mesh.area();
    r.volume = mesh.signedVolume();

    // Degenerate and duplicate faces.
    const float eps = 1e-12f * std::max(1.f, r.bounds.diagonal() * r.bounds.diagonal());
    std::vector<std::array<std::uint32_t, 3>> sorted;
    sorted.reserve(mesh.faces.size());
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
        const Tri& t = mesh.faces[f];
        const Vec3f c = (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]);
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2] || c.squaredNorm() <= eps * eps) ++r.degenerateFaces;
        std::array<std::uint32_t, 3> k = t;
        std::sort(k.begin(), k.end());
        sorted.push_back(k);
    }
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i] == sorted[i - 1]) ++r.duplicateFaces;

    // Edges: boundary, non-manifold, orientation.
    const EdgeMap em(mesh);
    double lengthSum = 0.0;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> boundaryNext;  // directed boundary half-edges
    for (const auto& e : em.edges()) {
        const auto ef = em.faces(e);
        ++r.edges;
        lengthSum += (mesh.vertices[e.a] - mesh.vertices[e.b]).cast<double>().norm();
        if (ef.size() == 1) {
            ++r.boundaryEdges;
            // Walk the rim in the direction that keeps the face on the left.
            const Tri& t = mesh.faces[ef[0]];
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t u = t[k], v = t[(k + 1) % 3];
                if ((u == e.a && v == e.b) || (u == e.b && v == e.a)) boundaryNext[v].push_back(u);
            }
        } else if (ef.size() > 2) {
            ++r.nonManifoldEdges;
        } else {
            // Two faces: consistent when they traverse the edge in opposite directions.
            int dir[2];
            for (int i = 0; i < 2; ++i) {
                const Tri& t = mesh.faces[ef[static_cast<std::size_t>(i)]];
                dir[i] = 0;
                for (int k = 0; k < 3; ++k)
                    if (t[k] == e.a && t[(k + 1) % 3] == e.b) dir[i] = 1;
                    else if (t[k] == e.b && t[(k + 1) % 3] == e.a) dir[i] = -1;
            }
            if (dir[0] == dir[1]) ++r.orientationConflicts;
        }
    }
    r.meanEdgeLength = r.edges ? lengthSum / static_cast<double>(r.edges) : 0.0;
    r.closed = r.boundaryEdges == 0 && r.nonManifoldEdges == 0;

    // Boundary loops.
    std::unordered_set<std::uint64_t> used;
    for (auto& [start, outs] : boundaryNext) {
        for (std::uint32_t first : outs) {
            const std::uint64_t k0 = (static_cast<std::uint64_t>(start) << 32) | first;
            if (used.count(k0)) continue;
            BoundaryLoop loop;
            std::uint32_t u = start, v = first;
            for (std::size_t guard = 0; guard <= r.boundaryEdges; ++guard) {
                used.insert((static_cast<std::uint64_t>(u) << 32) | v);
                loop.vertices.push_back(u);
                loop.length += (mesh.vertices[u] - mesh.vertices[v]).cast<double>().norm();
                if (v == start) break;
                auto it = boundaryNext.find(v);
                if (it == boundaryNext.end()) break;
                std::uint32_t w = UINT32_MAX;
                for (std::uint32_t c : it->second)
                    if (!used.count((static_cast<std::uint64_t>(v) << 32) | c)) { w = c; break; }
                if (w == UINT32_MAX) break;
                u = v;
                v = w;
            }
            r.boundaryLoops.push_back(std::move(loop));
        }
    }
    std::sort(r.boundaryLoops.begin(), r.boundaryLoops.end(), [](const auto& a, const auto& b) { return a.length > b.length; });

    // Components.
    std::vector<std::uint32_t> comp;
    r.components = components(mesh, em, comp);
    std::vector<std::size_t> sizes(r.components, 0);
    for (auto c : comp) ++sizes[c];
    r.largestComponentFaces = sizes.empty() ? 0 : *std::max_element(sizes.begin(), sizes.end());
    return r;
}

}  // namespace orv

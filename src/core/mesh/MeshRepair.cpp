// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/MeshRepair.h"

#include "core/mesh/EdgeMap.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/PmpBridge.h"

#include <pmp/algorithms/decimation.h>
#include <pmp/algorithms/features.h>
#include <pmp/algorithms/hole_filling.h>
#include <pmp/algorithms/remeshing.h>
#include <pmp/algorithms/smoothing.h>
#include <pmp/exceptions.h>

#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace orv {

std::size_t removeDegenerateFaces(TriMesh& mesh, float areaEpsilon) {
    const std::size_t before = mesh.faces.size();
    const float eps2 = 4.f * areaEpsilon * areaEpsilon;
    std::erase_if(mesh.faces, [&](const Tri& t) {
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) return true;
        const Vec3f c = (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]);
        return c.squaredNorm() <= eps2;
    });
    mesh.removeUnusedVertices();
    return before - mesh.faces.size();
}

std::size_t removeDuplicateFaces(TriMesh& mesh) {
    std::vector<std::pair<std::array<std::uint32_t, 3>, std::uint32_t>> keyed;
    keyed.reserve(mesh.faces.size());
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
        std::array<std::uint32_t, 3> k = mesh.faces[f];
        std::sort(k.begin(), k.end());
        keyed.emplace_back(k, static_cast<std::uint32_t>(f));
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<char> drop(mesh.faces.size(), 0);
    std::size_t removed = 0;
    for (std::size_t i = 1; i < keyed.size(); ++i)
        if (keyed[i].first == keyed[i - 1].first) {
            drop[keyed[i].second] = 1;
            ++removed;
        }
    if (!removed) return 0;
    std::vector<Tri> kept;
    kept.reserve(mesh.faces.size() - removed);
    for (std::size_t f = 0; f < mesh.faces.size(); ++f)
        if (!drop[f]) kept.push_back(mesh.faces[f]);
    mesh.faces = std::move(kept);
    return removed;
}

std::size_t weldVertices(TriMesh& mesh, float tolerance) {
    if (tolerance <= 0.f || mesh.vertices.empty()) return 0;
    // Grid hashing with cells twice the tolerance: a vertex can only be within
    // tolerance of vertices in its own cell or in the 7 neighbours on the sides
    // it is closest to. It merges into the earliest such kept vertex.
    const double inv = 0.5 / static_cast<double>(tolerance);
    struct CellHash {
        std::size_t operator()(const std::array<long long, 3>& c) const noexcept {
            return static_cast<std::size_t>(c[0] * 73856093ll ^ c[1] * 19349663ll ^ c[2] * 83492791ll);
        }
    };
    // Each cell holds a chain of kept vertices: head[cell] -> next[] -> ...
    std::unordered_map<std::array<long long, 3>, std::uint32_t, CellHash> head;
    head.reserve(mesh.vertices.size());
    std::vector<std::uint32_t> next;
    next.reserve(mesh.vertices.size());
    std::vector<std::uint32_t> remap(mesh.vertices.size());
    std::vector<Vec3f> kept;
    kept.reserve(mesh.vertices.size());
    const float tol2 = tolerance * tolerance;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f& p = mesh.vertices[i];
        std::array<long long, 3> c{};
        std::array<long long, 3> side{};
        for (int k = 0; k < 3; ++k) {
            const double g = static_cast<double>(p[k]) * inv;
            const double fl = std::floor(g);
            c[static_cast<std::size_t>(k)] = static_cast<long long>(fl);
            side[static_cast<std::size_t>(k)] = g - fl < 0.5 ? -1 : 1;
        }
        std::uint32_t found = UINT32_MAX;
        for (int m = 0; m < 8; ++m) {
            const std::array<long long, 3> n{c[0] + (m & 1 ? side[0] : 0), c[1] + (m & 2 ? side[1] : 0), c[2] + (m & 4 ? side[2] : 0)};
            auto it = head.find(n);
            if (it == head.end()) continue;
            for (std::uint32_t j = it->second; j != UINT32_MAX; j = next[j])
                if (j < found && (kept[j] - p).squaredNorm() <= tol2) found = j;
        }
        if (found == UINT32_MAX) {
            found = static_cast<std::uint32_t>(kept.size());
            kept.push_back(p);
            auto [it, isNew] = head.try_emplace(c, found);
            next.push_back(isNew ? UINT32_MAX : it->second);
            it->second = found;
        }
        remap[i] = found;
    }
    const std::size_t merged = mesh.vertices.size() - kept.size();
    for (Tri& t : mesh.faces)
        for (auto& v : t) v = remap[v];
    mesh.vertices = std::move(kept);
    removeDegenerateFaces(mesh);
    return merged;
}

std::size_t removeSmallComponents(TriMesh& mesh, std::size_t minFaces, bool keepLargestOnly) {
    std::vector<std::uint32_t> comp;
    const std::size_t n = faceComponents(mesh, comp);
    if (n <= 1) return 0;
    std::vector<std::size_t> size(n, 0);
    for (auto c : comp) ++size[c];
    const std::size_t largest = static_cast<std::size_t>(std::max_element(size.begin(), size.end()) - size.begin());
    std::vector<Tri> kept;
    kept.reserve(mesh.faces.size());
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
        const auto c = comp[f];
        const bool keep = keepLargestOnly ? c == largest : size[c] >= minFaces;
        if (keep) kept.push_back(mesh.faces[f]);
    }
    const std::size_t removed = mesh.faces.size() - kept.size();
    mesh.faces = std::move(kept);
    mesh.removeUnusedVertices();
    return removed;
}

std::size_t orientConsistently(TriMesh& mesh) {
    const EdgeMap em(mesh);
    const std::size_t n = mesh.faces.size();
    std::vector<char> visited(n, 0);
    std::size_t flipped = 0;
    std::vector<std::uint32_t> stack, piece;
    auto runs = [](const Tri& t, std::uint32_t a, std::uint32_t b) {
        for (int k = 0; k < 3; ++k)
            if (t[k] == a && t[(k + 1) % 3] == b) return true;
        return false;
    };
    for (std::size_t s = 0; s < n; ++s) {
        if (visited[s]) continue;
        visited[s] = 1;
        stack.assign(1, static_cast<std::uint32_t>(s));
        piece.clear();
        while (!stack.empty()) {
            const std::uint32_t f = stack.back();
            stack.pop_back();
            piece.push_back(f);
            for (std::size_t k = 0; k < 3; ++k) {
                const std::uint32_t id = em.edgeOf(f, k);
                if (id == EdgeMap::kNone) continue;
                const auto& e = em.edge(id);
                const auto fs = em.faces(e);
                if (fs.size() != 2) continue;  // boundary or non-manifold: do not propagate across
                const std::uint32_t g = fs[0] == f ? fs[1] : fs[0];
                if (visited[g]) continue;
                visited[g] = 1;
                // f may have been flipped already: compare the directions as they are now.
                if (runs(mesh.faces[g], e.a, e.b) == runs(mesh.faces[f], e.a, e.b)) {  // g is wound against f
                    std::swap(mesh.faces[g][1], mesh.faces[g][2]);
                    ++flipped;
                }
                stack.push_back(g);
            }
        }
        // A closed piece encloses a volume: make it positive (normals outward).
        double vol = 0.0;
        for (auto f : piece) {
            const Tri& t = mesh.faces[f];
            vol += mesh.vertices[t[0]].cast<double>().dot(mesh.vertices[t[1]].cast<double>().cross(mesh.vertices[t[2]].cast<double>()));
        }
        if (vol < 0.0) {
            for (auto f : piece) std::swap(mesh.faces[f][1], mesh.faces[f][2]);
            flipped += piece.size();
        }
    }
    return flipped;
}

void flipNormals(TriMesh& mesh) {
    for (Tri& t : mesh.faces) std::swap(t[1], t[2]);
    for (Quad& q : mesh.quads) std::swap(q[1], q[3]);
}

FillHolesResult fillHoles(TriMesh& mesh, std::size_t maxEdges) {
    FillHolesResult r;
    pmp::SurfaceMesh pm = toPmp(mesh);
    const std::size_t facesBefore = pm.n_faces();
    // Collect one half-edge per hole first: filling changes the mesh.
    std::vector<pmp::Halfedge> holes;
    std::vector<char> seen(pm.halfedges_size(), 0);
    for (auto h : pm.halfedges()) {
        if (!pm.is_boundary(h) || seen[h.idx()]) continue;
        std::size_t len = 0;
        pmp::Halfedge c = h;
        do {
            seen[c.idx()] = 1;
            c = pm.next_halfedge(c);
            ++len;
        } while (c != h && len < 10'000'000);
        if (maxEdges && len > maxEdges) ++r.skipped;
        else holes.push_back(h);
    }
    for (auto h : holes) {
        try {
            if (!pm.is_boundary(h)) continue;
            pmp::fill_hole(pm, h);
            ++r.filled;
        } catch (const std::exception&) {
            ++r.skipped;
        }
    }
    r.facesAdded = pm.n_faces() - facesBefore;
    mesh = fromPmp(pm);
    return r;
}

void smooth(TriMesh& mesh, bool implicitScheme, unsigned iterations, float timestep) {
    pmp::SurfaceMesh pm = toPmp(mesh);
    if (implicitScheme) pmp::implicit_smoothing(pm, timestep, std::max(1u, iterations), false, true);
    else pmp::explicit_smoothing(pm, std::max(1u, iterations), false);
    mesh = fromPmp(pm);
}

void decimate(TriMesh& mesh, const DecimateOptions& o) {
    pmp::SurfaceMesh pm = toPmp(mesh);
    const auto target = static_cast<unsigned>(std::max<std::size_t>(4, o.targetVertices));
    pmp::decimate(pm, target, o.aspectRatio, 0.f, o.maxValence, o.normalDeviationDeg, o.maxError, 0.01f, 1.f);
    mesh = fromPmp(pm);
}

void remeshUniform(TriMesh& mesh, float edgeLength, unsigned iterations, float featureAngleDeg) {
    pmp::SurfaceMesh pm = toPmp(mesh);
    if (featureAngleDeg > 0.f) pmp::detect_features(pm, featureAngleDeg);
    pmp::uniform_remeshing(pm, edgeLength, std::max(1u, iterations), true);
    mesh = fromPmp(pm);
}

void remeshAdaptive(TriMesh& mesh, float minEdge, float maxEdge, float approxError, unsigned iterations, float featureAngleDeg) {
    pmp::SurfaceMesh pm = toPmp(mesh);
    if (featureAngleDeg > 0.f) pmp::detect_features(pm, featureAngleDeg);
    pmp::adaptive_remeshing(pm, minEdge, maxEdge, approxError, std::max(1u, iterations), true);
    mesh = fromPmp(pm);
}

}  // namespace orv

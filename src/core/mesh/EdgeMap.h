// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the undirected edges of a triangle mesh and the faces on each.
//
// Built by sorting the 3·F half-edges by their vertex pair: flat arrays, no
// per-edge allocation, so it stays small and fast on scans of tens of
// millions of triangles (about 1 GB of peak memory for 10 M triangles).
#pragma once

#include "core/mesh/TriMesh.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace orv {

class EdgeMap {
public:
    static constexpr std::uint32_t kNone = UINT32_MAX;

    struct Edge {
        std::uint32_t a, b;          // a < b
        std::uint32_t first, count;  // its faces: faceList[first .. first + count)
    };

    explicit EdgeMap(const TriMesh& mesh) {
        struct Half {
            std::uint64_t key;
            std::uint32_t corner;  // face * 3 + k
        };
        std::vector<Half> halves;
        halves.reserve(mesh.faces.size() * 3);
        for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
            const Tri& t = mesh.faces[f];
            for (std::size_t k = 0; k < 3; ++k) {
                const std::uint32_t a = t[k], b = t[(k + 1) % 3];
                if (a != b) halves.push_back({key(a, b), static_cast<std::uint32_t>(f * 3 + k)});
            }
        }
        std::sort(halves.begin(), halves.end(), [](const Half& x, const Half& y) { return x.key != y.key ? x.key < y.key : x.corner < y.corner; });
        cornerEdge_.assign(mesh.faces.size() * 3, kNone);
        faceList_.reserve(halves.size());
        edges_.reserve(halves.size() / 2 + 1);
        for (std::size_t i = 0; i < halves.size();) {
            std::size_t j = i;
            Edge e{static_cast<std::uint32_t>(halves[i].key >> 32), static_cast<std::uint32_t>(halves[i].key & 0xffffffffu),
                   static_cast<std::uint32_t>(faceList_.size()), 0};
            const auto id = static_cast<std::uint32_t>(edges_.size());
            for (; j < halves.size() && halves[j].key == halves[i].key; ++j) {
                faceList_.push_back(halves[j].corner / 3);
                cornerEdge_[halves[j].corner] = id;
            }
            e.count = static_cast<std::uint32_t>(j - i);
            edges_.push_back(e);
            i = j;
        }
    }

    static std::uint64_t key(std::uint32_t a, std::uint32_t b) {
        if (a > b) std::swap(a, b);
        return (static_cast<std::uint64_t>(a) << 32) | b;
    }

    /// Every edge, sorted by its vertex pair.
    const std::vector<Edge>& edges() const { return edges_; }

    /// The faces using an edge.
    std::span<const std::uint32_t> faces(const Edge& e) const { return {faceList_.data() + e.first, e.count}; }

    /// The edge on corner k of face f, as the mesh was when the map was built
    /// (kNone for a degenerate corner). Its vertex pair does not depend on
    /// the corner order, so it stays right after a face is flipped.
    std::uint32_t edgeOf(std::size_t f, std::size_t k) const { return cornerEdge_[f * 3 + k]; }
    const Edge& edge(std::uint32_t id) const { return edges_[id]; }

    /// The faces using the edge between a and b (binary search).
    std::span<const std::uint32_t> facesOf(std::uint32_t a, std::uint32_t b) const {
        const std::uint64_t k = key(a, b);
        auto it = std::lower_bound(edges_.begin(), edges_.end(), k, [](const Edge& e, std::uint64_t v) { return key(e.a, e.b) < v; });
        if (it == edges_.end() || key(it->a, it->b) != k) return {};
        return faces(*it);
    }

private:
    std::vector<Edge> edges_;
    std::vector<std::uint32_t> faceList_;
    std::vector<std::uint32_t> cornerEdge_;
};

}  // namespace orv

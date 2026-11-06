// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the undirected edges of a triangle mesh and the faces on each.
#pragma once

#include "core/mesh/TriMesh.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace orv {

class EdgeMap {
public:
    struct Edge {
        std::uint32_t a, b;               // a < b
        std::vector<std::uint32_t> faces; // faces using the edge
    };

    explicit EdgeMap(const TriMesh& mesh) {
        edges_.reserve(mesh.faces.size() * 3 / 2 + 16);
        for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
            const Tri& t = mesh.faces[f];
            for (int k = 0; k < 3; ++k) {
                std::uint32_t a = t[k], b = t[(k + 1) % 3];
                if (a == b) continue;
                if (a > b) std::swap(a, b);
                auto& e = edges_[key(a, b)];
                e.a = a;
                e.b = b;
                e.faces.push_back(static_cast<std::uint32_t>(f));
            }
        }
    }

    static std::uint64_t key(std::uint32_t a, std::uint32_t b) {
        if (a > b) std::swap(a, b);
        return (static_cast<std::uint64_t>(a) << 32) | b;
    }

    std::span<const std::uint32_t> facesOf(std::uint32_t a, std::uint32_t b) const {
        auto it = edges_.find(key(a, b));
        if (it == edges_.end()) return {};
        return it->second.faces;
    }

    const std::unordered_map<std::uint64_t, Edge>& edges() const { return edges_; }

private:
    std::unordered_map<std::uint64_t, Edge> edges_;
};

}  // namespace orv

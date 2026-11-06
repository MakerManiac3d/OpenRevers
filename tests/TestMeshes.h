// SPDX-License-Identifier: GPL-3.0-or-later
// Meshes made in code for the tests: their exact answers are known.
#pragma once

#include "core/mesh/TriMesh.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <filesystem>
#include <map>
#include <string>

namespace orv::test {

/// Axis-aligned box from (0,0,0) to (sx,sy,sz), 12 outward triangles.
inline TriMesh box(float sx = 10, float sy = 20, float sz = 30) {
    TriMesh m;
    for (int i = 0; i < 8; ++i) m.vertices.emplace_back(i & 1 ? sx : 0.f, i & 2 ? sy : 0.f, i & 4 ? sz : 0.f);
    const int q[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (auto& f : q) {
        m.faces.push_back({static_cast<std::uint32_t>(f[0]), static_cast<std::uint32_t>(f[1]), static_cast<std::uint32_t>(f[2])});
        m.faces.push_back({static_cast<std::uint32_t>(f[0]), static_cast<std::uint32_t>(f[2]), static_cast<std::uint32_t>(f[3])});
    }
    return m;
}

/// Sphere of radius r: a subdivided icosahedron, outward, closed.
inline TriMesh sphere(float r = 10, int subdivisions = 3) {
    TriMesh m;
    const float t = (1.f + std::sqrt(5.f)) / 2.f;
    const float v[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) m.vertices.push_back(Vec3f(p[0], p[1], p[2]).normalized());
    const std::uint32_t f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                    {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (auto& x : f) m.faces.push_back({x[0], x[1], x[2]});
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> mid;
        auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
            auto key = std::minmax(a, b);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            m.vertices.push_back(((m.vertices[a] + m.vertices[b]) * 0.5f).normalized());
            const auto id = static_cast<std::uint32_t>(m.vertices.size() - 1);
            mid[key] = id;
            return id;
        };
        std::vector<Tri> next;
        for (const auto& tr : m.faces) {
            const auto a = midpoint(tr[0], tr[1]), b = midpoint(tr[1], tr[2]), c = midpoint(tr[2], tr[0]);
            next.push_back({tr[0], a, c});
            next.push_back({tr[1], b, a});
            next.push_back({tr[2], c, b});
            next.push_back({a, b, c});
        }
        m.faces = std::move(next);
    }
    for (auto& p : m.vertices) p *= r;
    return m;
}

/// A flat square grid of n×n cells in the XY plane (an open sheet).
inline TriMesh grid(int n = 10, float size = 10.f) {
    TriMesh m;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) m.vertices.emplace_back(size * static_cast<float>(i) / static_cast<float>(n), size * static_cast<float>(j) / static_cast<float>(n), 0.f);
    auto id = [n](int i, int j) { return static_cast<std::uint32_t>(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            m.faces.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1)});
            m.faces.push_back({id(i, j), id(i + 1, j + 1), id(i, j + 1)});
        }
    return m;
}

/// A temporary folder for one test.
inline std::filesystem::path tempDir(const std::string& name) {
    auto d = std::filesystem::temp_directory_path() / ("openrevers-test-" + name);
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}

}  // namespace orv::test

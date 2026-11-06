// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the one place that includes PoissonRecon (compiled on its own,
// as C++17, in the orv_poisson library). Plain arrays in and out.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orv::poisson {

struct Input {
    const float* points{nullptr};   // 3 per point
    const float* normals{nullptr};  // 3 per point
    std::size_t count{0};
    int depth{8};
    float pointWeight{4.f};
    float samplesPerNode{1.5f};
};

struct Output {
    std::vector<float> vertices;        // 3 per vertex
    std::vector<float> density;         // 1 per vertex: point density near it
    std::vector<std::uint32_t> faces;   // 3 per triangle
};

/// Returns an empty string on success, otherwise what went wrong.
std::string solve(const Input& in, Output& out);

}  // namespace orv::poisson

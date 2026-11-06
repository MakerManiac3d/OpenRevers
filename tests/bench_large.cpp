// SPDX-License-Identifier: GPL-3.0-or-later
// Large-mesh benchmark (not part of ctest): builds a closed, wavy slab of
// about N million triangles, writes it as binary STL, then times reading,
// analysis, repair steps, decimation and the project save and load.
//   orv_bench [millions=10] [folder=temp]
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshRepair.h"
#include "doc/Project.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace orv;

namespace {

double peakMb() {
    std::ifstream s("/proc/self/status");
    std::string line;
    while (std::getline(s, line))
        if (line.rfind("VmHWM:", 0) == 0) return std::stod(line.substr(6)) / 1024.0;
    return 0;  // not Linux: not measured
}

template <class F>
void step(const char* name, F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("| %-28s | %8.2f s | %8.0f MB |\n", name, s, peakMb());
    std::fflush(stdout);
}

/// A closed slab: a wavy top grid of n×n, a flat bottom grid and the four sides.
TriMesh slab(int n) {
    TriMesh m;
    const float size = 200.f;
    auto id = [n](int layer, int i, int j) { return static_cast<std::uint32_t>(layer * (n + 1) * (n + 1) + i * (n + 1) + j); };
    for (int layer = 0; layer < 2; ++layer)
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j) {
                const float x = size * static_cast<float>(i) / static_cast<float>(n), y = size * static_cast<float>(j) / static_cast<float>(n);
                const float z = layer ? 20.f + 2.f * std::sin(x * 0.2f) * std::cos(y * 0.15f) : 0.f;
                m.vertices.emplace_back(x, y, z);
            }
    m.faces.reserve(static_cast<std::size_t>(4) * n * n + 8 * n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const auto a = id(1, i, j), b = id(1, i + 1, j), c = id(1, i + 1, j + 1), d = id(1, i, j + 1);
            m.faces.push_back({a, b, c});
            m.faces.push_back({a, c, d});
            const auto e = id(0, i, j), f = id(0, i + 1, j), g = id(0, i + 1, j + 1), h = id(0, i, j + 1);
            m.faces.push_back({e, g, f});
            m.faces.push_back({e, h, g});
        }
    for (int k = 0; k < n; ++k) {  // sides
        auto wall = [&](std::uint32_t t0, std::uint32_t t1, std::uint32_t b0, std::uint32_t b1) {
            m.faces.push_back({b0, b1, t1});
            m.faces.push_back({b0, t1, t0});
        };
        wall(id(1, k, 0), id(1, k + 1, 0), id(0, k, 0), id(0, k + 1, 0));
        wall(id(1, k + 1, n), id(1, k, n), id(0, k + 1, n), id(0, k, n));
        wall(id(1, n, k), id(1, n, k + 1), id(0, n, k), id(0, n, k + 1));
        wall(id(1, 0, k + 1), id(1, 0, k), id(0, 0, k + 1), id(0, 0, k));
    }
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    const double millions = argc > 1 ? std::stod(argv[1]) : 10.0;
    const std::filesystem::path dir = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::temp_directory_path();
    const int n = static_cast<int>(std::sqrt(millions * 1e6 / 4.0));
    const auto stl = dir / "orv_bench.stl";
    const auto proj = dir / "orv_bench.orvp";
    {
        TriMesh m = slab(n);
        std::printf("Mesh: %zu triangles, %zu vertices\n\n| Step | Time | Peak memory |\n| --- | --- | --- |\n", m.faces.size(), m.vertices.size());
        step("Write binary STL", [&] { writeMesh(m, stl); });
    }
    TriMesh m;
    step("Read binary STL (weld)", [&] { m = readMesh(stl); });
    MeshReport r;
    step("Analyse", [&] { r = analyze(m); });
    if (!r.closed || r.components != 1) std::printf("unexpected: closed %d, pieces %zu\n", r.closed, r.components);
    step("Remove degenerate faces", [&] { removeDegenerateFaces(m); });
    step("Remove duplicate faces", [&] { removeDuplicateFaces(m); });
    step("Weld vertices (0.001 mm)", [&] { weldVertices(m, 0.001f); });
    step("Orient consistently", [&] { orientConsistently(m); });
    step("Save project", [&] {
        Document d;
        d.addBody("bench", std::make_shared<TriMesh>(m));
        saveProject(d, proj);
    });
    step("Open project", [&] {
        Document d;
        loadProject(d, proj);
    });
    step("Decimate to 1 M triangles", [&] {
        DecimateOptions o;
        o.targetVertices = 500000;
        decimate(m, o);
    });
    std::printf("\nAfter decimation: %zu triangles\n", m.faces.size());
    std::filesystem::remove(stl);
    std::filesystem::remove(proj);
    return 0;
}

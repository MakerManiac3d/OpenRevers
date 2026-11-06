// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestMeshes.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshRepair.h"

#include <gtest/gtest.h>

#include <fstream>
#include <numbers>

using namespace orv;
using namespace orv::test;

TEST(TriMesh, BoxAreaVolumeBounds) {
    const TriMesh m = box(10, 20, 30);
    EXPECT_NEAR(m.area(), 2 * (10 * 20 + 20 * 30 + 10 * 30), 1e-6);
    EXPECT_NEAR(m.signedVolume(), 6000.0, 1e-6);
    const Box3 b = m.bounds();
    EXPECT_FLOAT_EQ(b.size().z(), 30.f);
}

TEST(MeshIO, RoundTripEveryFormat) {
    const auto dir = tempDir("io");
    const TriMesh m = sphere(5, 2);
    for (const char* name : {"a.stl", "a.obj", "a.ply"}) {
        writeMesh(m, dir / name);
        const TriMesh r = readMesh(dir / name);
        EXPECT_EQ(r.faceCount(), m.faceCount()) << name;
        EXPECT_EQ(r.vertexCount(), m.vertexCount()) << name;  // STL is welded back
        EXPECT_NEAR(r.signedVolume(), m.signedVolume(), 1e-3) << name;
    }
    writeMesh(m, dir / "ascii.stl", MeshFormat::StlAscii);
    EXPECT_EQ(readMesh(dir / "ascii.stl").vertexCount(), m.vertexCount());
    writeMesh(m, dir / "ascii.ply", MeshFormat::PlyAscii);
    EXPECT_EQ(readMesh(dir / "ascii.ply").faceCount(), m.faceCount());
}

TEST(MeshIO, ObjPolygonsAndNegativeIndices) {
    const auto dir = tempDir("obj");
    std::ofstream(dir / "q.obj") << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1/1/1 2/2/2 3/3/3 4/4/4\nf -4 -2 -1\n";
    const TriMesh m = readMesh(dir / "q.obj");
    EXPECT_EQ(m.faceCount(), 3u);
}

TEST(MeshIO, ErrorsSayWhatIsWrong) {
    const auto dir = tempDir("err");
    EXPECT_THROW(readMesh(dir / "missing.stl"), IoError);
    EXPECT_THROW(readMesh(dir / "x.step"), IoError);
    std::ofstream(dir / "empty.obj") << "# nothing\n";
    try {
        readMesh(dir / "empty.obj");
        FAIL();
    } catch (const IoError& e) {
        EXPECT_NE(std::string(e.what()).find("no triangles"), std::string::npos);
    }
}

TEST(Analysis, ClosedSphere) {
    const MeshReport r = analyze(sphere(10, 3));
    EXPECT_TRUE(r.closed);
    EXPECT_EQ(r.boundaryLoops.size(), 0u);
    EXPECT_EQ(r.components, 1u);
    EXPECT_EQ(r.orientationConflicts, 0u);
    EXPECT_NEAR(r.volume, 4.0 / 3.0 * std::numbers::pi * 1000.0, 80.0);  // a polyhedron, a little under the sphere
}

TEST(Analysis, OpenSheetHasOneRim) {
    const MeshReport r = analyze(grid(10, 10));
    EXPECT_FALSE(r.closed);
    ASSERT_EQ(r.boundaryLoops.size(), 1u);
    EXPECT_EQ(r.boundaryLoops[0].vertices.size(), 40u);
    EXPECT_NEAR(r.boundaryLoops[0].length, 40.0, 1e-4);
}

TEST(Analysis, FindsProblems) {
    TriMesh m = box();
    m.faces.push_back(m.faces[0]);              // duplicate
    m.faces.push_back({0, 0, 1});               // degenerate
    std::swap(m.faces[3][1], m.faces[3][2]);    // one face flipped
    TriMesh loose = box(1, 1, 1);
    const auto off = static_cast<std::uint32_t>(m.vertices.size());
    for (auto& v : loose.vertices) m.vertices.push_back(v + Vec3f(50, 0, 0));
    for (auto t : loose.faces) m.faces.push_back({t[0] + off, t[1] + off, t[2] + off});
    const MeshReport r = analyze(m);
    EXPECT_EQ(r.duplicateFaces, 1u);
    EXPECT_GE(r.degenerateFaces, 1u);
    EXPECT_GE(r.orientationConflicts, 1u);
    EXPECT_EQ(r.components, 2u);
}

TEST(Repair, CleansAndOrients) {
    TriMesh m = box();
    m.faces.push_back(m.faces[0]);
    std::swap(m.faces[3][1], m.faces[3][2]);
    flipNormals(m);  // now inward, one face out
    EXPECT_EQ(removeDuplicateFaces(m), 1u);
    EXPECT_GT(orientConsistently(m), 0u);
    const MeshReport r = analyze(m);
    EXPECT_EQ(r.orientationConflicts, 0u);
    EXPECT_GT(r.volume, 0.0);
}

TEST(Repair, WeldJoinsSplitSeams) {
    TriMesh m = grid(4, 4);
    // Split the sheet along x = 2 by duplicating those vertices for the right half.
    const std::size_t n0 = m.vertices.size();
    for (auto& t : m.faces) {
        const Vec3f c = (m.vertices[t[0]] + m.vertices[t[1]] + m.vertices[t[2]]) / 3.f;
        if (c.x() < 2.f) continue;
        for (auto& v : t)
            if (std::abs(m.vertices[v].x() - 2.f) < 1e-6f && v < n0) {
                m.vertices.push_back(m.vertices[v] + Vec3f(1e-4f, 0, 0));
                v = static_cast<std::uint32_t>(m.vertices.size() - 1);
            }
    }
    EXPECT_GT(analyze(m).boundaryLoops.size(), 1u);
    EXPECT_GT(weldVertices(m, 0.001f), 0u);
    EXPECT_EQ(analyze(m).boundaryLoops.size(), 1u);
}

TEST(Repair, RemovesSmallPieces) {
    TriMesh m = sphere(10, 2);
    const auto off = static_cast<std::uint32_t>(m.vertices.size());
    const TriMesh crumb = box(0.1f, 0.1f, 0.1f);
    for (auto& v : crumb.vertices) m.vertices.push_back(v + Vec3f(30, 0, 0));
    for (auto t : crumb.faces) m.faces.push_back({t[0] + off, t[1] + off, t[2] + off});
    EXPECT_EQ(removeSmallComponents(m, 50), 12u);
    EXPECT_EQ(analyze(m).components, 1u);
}

TEST(Repair, FillsSmallHolesOnly) {
    TriMesh m = sphere(10, 3);
    // Open two holes: remove the faces around one vertex, and around another far away.
    auto openAround = [&](std::uint32_t v) { std::erase_if(m.faces, [v](const Tri& t) { return t[0] == v || t[1] == v || t[2] == v; }); };
    openAround(0);
    openAround(3);
    m.removeUnusedVertices();
    ASSERT_EQ(analyze(m).boundaryLoops.size(), 2u);
    const auto r = fillHoles(m, 100);
    EXPECT_EQ(r.filled, 2u);
    EXPECT_TRUE(analyze(m).closed);
}

TEST(Processing, DecimateKeepsShape) {
    TriMesh m = sphere(10, 4);  // 5120 faces
    const double v0 = m.signedVolume();
    DecimateOptions o;
    o.targetVertices = 300;
    decimate(m, o);
    EXPECT_LT(m.faceCount(), 1000u);
    EXPECT_NEAR(m.signedVolume(), v0, v0 * 0.05);
    EXPECT_TRUE(analyze(m).closed);
}

TEST(Processing, UniformRemeshHitsTheEdgeLength) {
    TriMesh m = sphere(10, 3);
    remeshUniform(m, 1.0f, 10);
    const MeshReport r = analyze(m);
    EXPECT_NEAR(r.meanEdgeLength, 1.0, 0.25);
    EXPECT_TRUE(r.closed);
    for (const auto& p : m.vertices) EXPECT_NEAR(p.norm(), 10.f, 0.2f);
}

TEST(Processing, SmoothingReducesNoise) {
    TriMesh m = sphere(10, 4);
    std::uint32_t seed = 1;
    for (auto& p : m.vertices) {
        seed = seed * 1664525u + 1013904223u;
        p *= 1.f + 0.02f * (static_cast<float>(seed % 1000) / 1000.f - 0.5f);
    }
    auto spread = [](const TriMesh& x) {
        double s = 0;
        for (const auto& p : x.vertices) s += std::abs(p.norm() - 10.0);
        return s / static_cast<double>(x.vertices.size());
    };
    const double before = spread(m);
    smooth(m, true, 1, 0.0005f);
    EXPECT_LT(spread(m), before);
}

TEST(Processing, RemeshKeepsSharpEdges) {
    TriMesh m = box(20, 20, 20);
    remeshUniform(m, 1.0f, 5, 30.f);
    // Every vertex still lies on the box: corners and edges not rounded off.
    for (const auto& p : m.vertices) {
        const float d = std::min({p.x(), 20 - p.x(), p.y(), 20 - p.y(), p.z(), 20 - p.z()});
        EXPECT_LT(std::abs(d), 0.05f);
    }
    const Box3 b = m.bounds();
    EXPECT_NEAR(b.size().x(), 20.f, 0.05f);
    EXPECT_GT(m.faceCount(), 1000u);
}

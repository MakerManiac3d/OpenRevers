// SPDX-License-Identifier: GPL-3.0-or-later
// Quad remeshing (QuadriFlow) and the T-Spline (.tsm) writer.
#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <sstream>

#include "TestMeshes.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshQuad.h"
#include "core/mesh/MeshRepair.h"

using namespace orv;

namespace {

std::filesystem::path tmpFile(const std::string& name) { return std::filesystem::temp_directory_path() / ("orv_test_" + name); }

TriMesh quadSphere() {
    TriMesh m = test::sphere(10, 4);
    QuadRemeshOptions o;
    o.targetFaces = 800;
    remeshQuads(m, o);
    return m;
}

}  // namespace

TEST(Quads, RemeshSphereGivesClosedQuadMesh) {
    const TriMesh m = quadSphere();
    ASSERT_TRUE(m.hasQuads());
    EXPECT_EQ(m.faces.size(), m.quads.size() * 2);
    // Close to the count asked for.
    EXPECT_GT(m.quads.size(), 600u);
    EXPECT_LT(m.quads.size(), 1100u);
    const MeshReport r = analyze(m);
    EXPECT_TRUE(r.closed);
    EXPECT_EQ(r.components, 1u);
    EXPECT_GT(m.signedVolume(), 0.0);  // still outward
    // On the sphere: every vertex within a few percent of the radius.
    for (const auto& v : m.vertices) EXPECT_NEAR(v.norm(), 10.f, 0.3f);
    EXPECT_TRUE(tsmProblem(m).empty()) << tsmProblem(m);
}

TEST(Quads, RemeshKeepsBoxCorners) {
    TriMesh m = test::box(20, 20, 20);
    remeshUniform(m, 1.5f, 5);
    QuadRemeshOptions o;
    o.targetFaces = 600;
    remeshQuads(m, o);
    const Box3 b = m.bounds();
    EXPECT_NEAR(b.size().x(), 20.f, 0.3f);
    EXPECT_NEAR(b.size().y(), 20.f, 0.3f);
    EXPECT_NEAR(b.size().z(), 20.f, 0.3f);
    EXPECT_NEAR(m.signedVolume(), 8000.0, 8000.0 * 0.03);
}

TEST(Quads, RefusesNonManifold) {
    TriMesh m = test::box();
    m.faces.push_back(m.faces[0]);  // one edge now has three faces
    m.faces.back()[2] = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.emplace_back(50.f, 50.f, 50.f);
    EXPECT_THROW(remeshQuads(m, {}), std::runtime_error);
}

TEST(Quads, TriangleEditDropsNothingButFlipKeepsQuads) {
    TriMesh m = quadSphere();
    const auto n = m.quads.size();
    flipNormals(m);
    EXPECT_EQ(m.quads.size(), n);
    EXPECT_LT(m.signedVolume(), 0.0);
    // The triangulation still matches the quads after the flip.
    TriMesh again = m;
    again.setQuads(again.quads);
    EXPECT_NEAR(again.signedVolume(), m.signedVolume(), 1e-3);
}

TEST(Quads, ObjAndPlyKeepQuads) {
    const TriMesh m = quadSphere();
    for (const char* name : {"quads.obj", "quads.ply"}) {
        const auto p = tmpFile(name);
        writeMesh(m, p, formatFromPath(p));
        const TriMesh back = readMesh(p);
        EXPECT_EQ(back.quads.size(), m.quads.size()) << name;
        EXPECT_EQ(back.faces.size(), m.faces.size()) << name;
        EXPECT_NEAR(back.signedVolume(), m.signedVolume(), 1e-2) << name;
        std::filesystem::remove(p);
    }
}

TEST(Quads, MixedPolygonsReadAsTriangles) {
    const auto p = tmpFile("mixed.obj");
    {
        std::ofstream o(p);
        o << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nf 1 2 3 4\nf 2 5 3\n";
    }
    const TriMesh m = readMesh(p);
    EXPECT_FALSE(m.hasQuads());
    EXPECT_EQ(m.faces.size(), 3u);
    std::filesystem::remove(p);
}

TEST(Tsm, WritesTheTSplineFormat) {
    TriMesh m = quadSphere();
    const auto p = tmpFile("sphere.tsm");
    const TsmResult r = writeTsm(m, p);
    EXPECT_FALSE(r.flipped);
    EXPECT_EQ(r.faces, m.quads.size());
    EXPECT_EQ(r.vertices, m.vertices.size());
    // Euler characteristic of a sphere: V - E + F = 2.
    EXPECT_EQ(static_cast<long>(r.vertices) - static_cast<long>(r.edges) + static_cast<long>(r.faces), 2);

    std::map<std::string, std::size_t> count;
    std::vector<std::array<long, 6>> links;
    {  // closed before the file is removed (Windows will not remove an open file)
        std::ifstream in(p);
        std::string line;
        std::getline(in, line);
        EXPECT_EQ(line, "#TS0200");
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string tag;
            ls >> tag;
            ++count[tag];
            if (tag == "l") {
                std::array<long, 6> l{};
                for (auto& x : l) ls >> x;
                links.push_back(l);
            }
            if (tag == "0g") {  // centimetres: the sphere of 10 mm has radius 1 cm
                double x, y, z;
                ls >> x >> y >> z;
                EXPECT_NEAR(std::sqrt(x * x + y * y + z * z), 1.0, 0.03);
            }
        }
    }
    EXPECT_EQ(count["f"], r.faces);
    EXPECT_EQ(count["e"], r.edges);
    EXPECT_EQ(count["v"], r.vertices);
    EXPECT_EQ(count["l"], r.faces * 4);
    EXPECT_EQ(count["0g"], r.vertices);
    EXPECT_EQ(count["ver"], 1u);
    // Every link's twin points back, and runs between the same two vertices the other way.
    for (std::size_t i = 0; i < links.size(); ++i) {
        const auto& l = links[i];
        const auto& t = links[static_cast<std::size_t>(l[2])];
        EXPECT_EQ(static_cast<std::size_t>(t[2]), i);
        EXPECT_EQ(t[3], links[static_cast<std::size_t>(l[1])][3]);
        EXPECT_EQ(t[5], l[5]);
        EXPECT_EQ(static_cast<std::size_t>(links[static_cast<std::size_t>(l[1])][0]), i);
    }
    std::filesystem::remove(p);
}

TEST(Tsm, TurnsAnInsideOutMeshOutward) {
    TriMesh m = quadSphere();
    flipNormals(m);
    const auto p = tmpFile("flipped.tsm");
    EXPECT_TRUE(writeTsm(m, p).flipped);
    std::filesystem::remove(p);
}

TEST(Tsm, RefusesOpenOrTriangleMeshes) {
    EXPECT_FALSE(tsmProblem(test::box()).empty());  // no quads
    TriMesh m = quadSphere();
    m.quads.pop_back();
    m.setQuads(m.quads);
    EXPECT_NE(tsmProblem(m).find("open"), std::string::npos);
    EXPECT_THROW(writeTsm(m, tmpFile("open.tsm")), std::runtime_error);
}

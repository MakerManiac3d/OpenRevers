// SPDX-License-Identifier: GPL-3.0-or-later
// Point clouds: files, thinning, normals and Poisson reconstruction.
#include "TestMeshes.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include "core/points/PointCloud.h"
#include "doc/Project.h"
#include <gtest/gtest.h>
#include <fstream>
#include <random>

using namespace orv;

namespace {

std::filesystem::path tmpFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("orv_test_" + name);
}

/// Points on a sphere of radius r, with a little noise; no normals.
TriMesh sphereCloud(float r, int count, float noise = 0.f, bool upperHalfOnly = false) {
    TriMesh c;
    std::mt19937 g(7);
    std::normal_distribution<float> n;
    std::normal_distribution<float> e(0.f, noise > 0.f ? noise : 1e-9f);
    while (static_cast<int>(c.vertices.size()) < count) {
        Vec3f p(n(g), n(g), n(g));
        p.normalize();
        if (upperHalfOnly && p.z() < 0.f) continue;
        c.vertices.push_back(p * (r + (noise > 0.f ? e(g) : 0.f)));
    }
    return c;
}

}  // namespace

TEST(Points, XyzAndPlyKeepPointsAndNormals) {
    TriMesh c = sphereCloud(10, 500);
    estimateNormals(c, 12, NormalOrientation::Outward);
    for (const char* name : {"cloud.xyz", "cloud.ply", "cloud.obj"}) {
        const auto p = tmpFile(name);
        writeMesh(c, p);
        const TriMesh back = readMesh(p);
        EXPECT_TRUE(back.isPointCloud()) << name;
        ASSERT_EQ(back.vertices.size(), c.vertices.size()) << name;
        ASSERT_TRUE(back.hasNormals()) << name;
        EXPECT_NEAR((back.normals[17] - c.normals[17]).norm(), 0.f, 1e-5f) << name;
        std::filesystem::remove(p);
    }
    EXPECT_THROW(writeMesh(c, tmpFile("cloud.stl")), IoError);
    EXPECT_THROW(writeMesh(test::box(), tmpFile("box.xyz")), IoError);
}

TEST(Points, XyzVariants) {
    const auto p = tmpFile("variants.pts");
    {
        std::ofstream o(p);
        o << "3\n# a comment\n1,2,3,100,10,20,30\n4;5;6;100;10;20;30\n+7 8 9 1 2 3 4\n";
    }
    const TriMesh m = readMesh(p);
    ASSERT_EQ(m.vertices.size(), 3u);
    EXPECT_FALSE(m.hasNormals());  // 7 columns: intensity and colour, not normals
    EXPECT_EQ(m.vertices[2], Vec3f(7, 8, 9));
    std::filesystem::remove(p);
    const auto q = tmpFile("bad.xyz");
    std::ofstream(q) << "1 2\n";
    EXPECT_THROW(readMesh(q), IoError);
    std::filesystem::remove(q);
}

TEST(Points, ProjectKeepsCloudsWithNormals) {
    TriMesh c = sphereCloud(5, 300);
    estimateNormals(c, 10, NormalOrientation::Outward);
    Document d;
    d.addBody("cloud", std::make_shared<TriMesh>(c));
    const auto p = tmpFile("cloud.orvp");
    saveProject(d, p);
    Document e;
    loadProject(e, p);
    const auto& m = *e.bodies()[0].mesh;
    EXPECT_TRUE(m.isPointCloud());
    ASSERT_TRUE(m.hasNormals());
    EXPECT_EQ(m.normals[5], c.normals[5]);
    std::filesystem::remove(p);
}

TEST(Points, ThinKeepsOnePerCell) {
    TriMesh c = sphereCloud(10, 20000);
    const std::size_t removed = thinPoints(c, 2.f);
    EXPECT_GT(removed, 15000u);
    EXPECT_GT(c.vertices.size(), 300u);
    EXPECT_GT(meanPointSpacing(c), 0.8);
}

TEST(Points, NormalsPointOutwardOnASphere) {
    for (auto o : {NormalOrientation::Propagate, NormalOrientation::Outward}) {
        TriMesh c = sphereCloud(10, 4000, 0.02f);
        const NormalsResult r = estimateNormals(c, 16, o);
        EXPECT_EQ(r.points, 4000u);
        std::size_t good = 0;
        for (std::size_t i = 0; i < c.vertices.size(); ++i)
            if (c.normals[i].dot(c.vertices[i].normalized()) > 0.9f) ++good;
        EXPECT_GT(good, 3960u);  // 99 %
    }
}

TEST(Points, KeepOrientationFollowsTheOldNormals) {
    TriMesh c = sphereCloud(10, 2000);
    c.normals.clear();
    for (const auto& p : c.vertices) c.normals.push_back(-p.normalized());  // inward on purpose
    estimateNormals(c, 12, NormalOrientation::Keep);
    std::size_t inward = 0;
    for (std::size_t i = 0; i < c.vertices.size(); ++i)
        if (c.normals[i].dot(c.vertices[i]) < 0.f) ++inward;
    EXPECT_EQ(inward, c.vertices.size());
}

TEST(Points, PoissonRebuildsAClosedSphere) {
    TriMesh c = sphereCloud(10, 20000, 0.01f);
    estimateNormals(c, 16, NormalOrientation::Propagate);
    TriMesh m;
    PoissonOptions o;
    o.depth = 7;
    reconstructPoisson(c, o, m);
    const MeshReport r = analyze(m);
    EXPECT_TRUE(r.closed);
    EXPECT_EQ(r.components, 1u);
    const double v = 4.0 / 3.0 * 3.14159265 * 1000.0;
    EXPECT_NEAR(m.signedVolume(), v, v * 0.04);  // outward, about the right size
    for (std::size_t i = 0; i < m.vertices.size(); i += 37) EXPECT_NEAR(m.vertices[i].norm(), 10.f, 0.3f);
}

TEST(Points, PoissonTrimOpensAHalfScan) {
    TriMesh c = sphereCloud(10, 15000, 0.01f, true);
    estimateNormals(c, 16, NormalOrientation::Outward);
    TriMesh closed, trimmed;
    PoissonOptions o;
    o.depth = 7;
    reconstructPoisson(c, o, closed);
    o.trimQuantile = 0.15f;
    const PoissonResult r = reconstructPoisson(c, o, trimmed);
    EXPECT_GT(r.facesTrimmed, 0u);
    const MeshReport t = analyze(trimmed);
    EXPECT_FALSE(t.closed);
    EXPECT_EQ(t.components, 1u);
    // The invented lower half is gone: almost nothing well below the equator.
    std::size_t low = 0;
    for (const auto& p : trimmed.vertices)
        if (p.z() < -3.f) ++low;
    EXPECT_LT(low, trimmed.vertices.size() / 50);
    TriMesh noNormals = c;
    noNormals.normals.clear();
    EXPECT_THROW(reconstructPoisson(noNormals, o, trimmed), std::runtime_error);
}

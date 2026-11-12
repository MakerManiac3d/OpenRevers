// SPDX-License-Identifier: GPL-3.0-or-later
// Cleaning scans (table, stray points, crop, faces) and aligning several
// scans (pairs of points, shape alone), through the commands.
#include "TestMeshes.h"
#include "commands/Command.h"
#include "core/fit/Register.h"
#include "core/mesh/MeshClean.h"
#include "core/mesh/MeshIO.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace orv;

namespace {

struct Cmd {
    Document doc;
    CommandRegistry reg;
    Cmd() { registerCoreCommands(reg); }
    json run(const std::string& name, const json& in) {
        CommandContext ctx{doc, PathPolicy::unrestricted(), "en", "test", nullptr};
        return reg.run(name, in, ctx);
    }
};

/// A fine grid of triangles: the table, z = 0, 200 × 200 mm.
TriMesh table() {
    TriMesh t;
    const int n = 81;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) t.vertices.emplace_back(-100.f + 2.5f * static_cast<float>(i), -100.f + 2.5f * static_cast<float>(j), 0.f);
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const auto a = static_cast<std::uint32_t>(i + n * j), b = a + 1, c = a + static_cast<std::uint32_t>(n), d = c + 1;
            t.faces.push_back({a, b, d});
            t.faces.push_back({a, d, c});
        }
    return t;
}

}  // namespace

TEST(Clean, TableAndStrayPointsGo) {
    TriMesh m = table();
    const TriMesh b = test::box(20, 30, 10);  // sits on the table from z = 0? move it up a little
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (const auto& v : b.vertices) m.vertices.push_back(v + Vec3f(0, 0, 5.f));
    for (const Tri& t : b.faces) m.faces.push_back({t[0] + base, t[1] + base, t[2] + base});
    const auto pl = findLargestPlane(m, 0.5);
    ASSERT_TRUE(pl.has_value());
    EXPECT_NEAR(std::abs(pl->normal.z()), 1.0, 1e-6);
    EXPECT_GT(pl->normal.z(), 0);  // towards the part
    removePlane(m, *pl, 0.5, true);
    EXPECT_EQ(m.faces.size(), b.faces.size());

    // A cloud on a sphere with a few points flung away.
    TriMesh cloud;
    std::mt19937 rng(3);
    std::normal_distribution<float> g(0.f, 1.f);
    for (int i = 0; i < 4000; ++i) {
        Vec3f v(g(rng), g(rng), g(rng));
        cloud.vertices.push_back(20.f * v.normalized());
    }
    for (int i = 0; i < 10; ++i) cloud.vertices.emplace_back(60.f + 7.f * static_cast<float>(i), 0.f, 0.f);
    const std::size_t gone = removeOutliers(cloud, 16, 2.5);
    EXPECT_GE(gone, 10u);
    EXPECT_LT(gone, 200u);
    for (const auto& v : cloud.vertices) EXPECT_LT(v.norm(), 25.f);
}

TEST(Clean, CommandsCropDeleteAndRemovePlane) {
    const auto dir = test::tempDir("clean");
    TriMesh m = table();
    writeMesh(m, dir / "table.stl");
    Cmd c;
    c.run("mesh.import", {{"path", (dir / "table.stl").string()}});
    const json crop = c.run("mesh.crop", {{"body", "b1"}, {"shape", "box"}, {"min", {-50, -50, -1}}, {"max", {50, 50, 1}}});
    EXPECT_GT(crop["verticesRemoved"].get<int>(), 0);
    const Box3 bb = c.doc.body("b1").mesh->bounds();
    EXPECT_LE(bb.max.x(), 50.f);
    EXPECT_GE(bb.min.y(), -50.f);
    const json del = c.run("mesh.delete_faces", {{"body", "b1"}, {"faces", {0, 1, 2, 3}}});
    EXPECT_EQ(del["removed"], 4);
    c.doc.undo();
    c.doc.undo();
    // The table alone: removing it leaves nothing, and the body is not emptied.
    EXPECT_THROW(c.run("mesh.remove_plane", {{"body", "b1"}}), CommandError);
}

TEST(Register, PairsOfPointsGiveTheExactMove) {
    std::vector<Vec3d> a = {{0, 0, 0}, {10, 0, 0}, {0, 20, 0}, {3, 4, 15}};
    Transform t = Transform::Identity();
    t.rotate(Eigen::AngleAxisd(0.7, Vec3d(1, 2, 3).normalized()));
    t.translation() = Vec3d(5, -40, 12);
    std::vector<Vec3d> b;
    for (const auto& p : a) b.push_back(t * p);
    std::vector<double> res;
    const Transform got = rigidFromPairs(a, b, &res);
    EXPECT_LT((got.matrix() - t.matrix()).norm(), 1e-9);
    for (double d : res) EXPECT_LT(d, 1e-9);
    EXPECT_THROW(rigidFromPairs({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}), std::invalid_argument);
}

TEST(Register, ScansFoundByShapeAloneAndByPairs) {
    const auto dir = test::tempDir("register");
    Cmd c;
    c.run("part.new", {{"name", "Bracket"}});
    json l = json::array({json::array({{{"type", "line"}, {"from", {0, 0}}, {"to", {60, 0}}}, {{"type", "line"}, {"from", {60, 0}}, {"to", {60, 12}}},
                                       {{"type", "line"}, {"from", {60, 12}}, {"to", {14, 12}}}, {{"type", "line"}, {"from", {14, 12}}, {"to", {14, 40}}},
                                       {{"type", "line"}, {"from", {14, 40}}, {"to", {0, 40}}}, {{"type", "line"}, {"from", {0, 40}}, {"to", {0, 0}}}}),
                          json::array({{{"type", "circle"}, {"centre", {42, 6}}, {"radius", 3.0}}})});
    c.run("part.sketch", {{"body", "b1"}, {"loops", l}});
    c.run("part.extrude", {{"body", "b1"}, {"sketch", "s1"}, {"end", 25.0}, {"mode", "new"}});
    const auto p = dir / "bracket.stl";
    c.run("mesh.export", {{"body", "b1"}, {"path", p.string()}, {"overwrite", true}});
    c.run("mesh.import", {{"path", p.string()}, {"name", "Reference"}});  // b2
    c.run("mesh.remesh", {{"body", "b2"}, {"edgeLength", 1.0}});
    c.run("mesh.import", {{"path", p.string()}, {"name", "Moving"}});  // b3
    c.run("mesh.remesh", {{"body", "b3"}, {"edgeLength", 1.2}});
    // The second scan sees only part of the bracket (a scan from one side)...
    c.run("mesh.crop", {{"body", "b3"}, {"shape", "plane"}, {"origin", {0, 0, 6}}, {"normal", {0, 0, 1}}});
    const TriMesh truth = *c.doc.body("b3").mesh;
    // ...and lies elsewhere, turned.
    c.run("align.move", {{"body", "b3"}, {"axis", {0.3, 1, 0.2}}, {"angle", 130.0}, {"about", {10, 10, 10}}, {"translate", {150, -80, 40}}});
    const json g = c.run("align.global", {{"body", "b3"}, {"reference", "b2"}});
    EXPECT_GT(g["overlapPercent"].get<double>(), 90.0) << g.dump();
    const TriMesh& got = *c.doc.body("b3").mesh;
    double worst = 0;
    for (std::size_t i = 0; i < got.vertices.size(); ++i) worst = std::max(worst, static_cast<double>((got.vertices[i] - truth.vertices[i]).norm()));
    EXPECT_LT(worst, 0.2) << g.dump();

    // By pairs: three corners picked roughly (0.3 mm off), then refined.
    c.run("align.move", {{"body", "b3"}, {"axis", {0, 0, 1}}, {"angle", 90.0}, {"translate", {0, 100, 0}}});
    Transform t = Transform::Identity();
    t.translation() = Vec3d(0, 100, 0);
    t.rotate(Eigen::AngleAxisd(3.14159265358979323846 / 2, Vec3d::UnitZ()));
    std::vector<Vec3d> ref = {{60, 0, 25}, {0, 40, 25}, {60, 12, 25}, {0, 0, 25}};
    json pts = json::array(), rpts = json::array();
    for (const auto& r : ref) {
        const Vec3d m = t * r + Vec3d(0.2, -0.2, 0.1);
        pts.push_back({m.x(), m.y(), m.z()});
        rpts.push_back({r.x(), r.y(), r.z()});
    }
    const json pp = c.run("align.point_pairs", {{"body", "b3"}, {"reference", "b2"}, {"points", pts}, {"referencePoints", rpts}});
    worst = 0;
    for (std::size_t i = 0; i < got.vertices.size(); ++i) worst = std::max(worst, static_cast<double>((c.doc.body("b3").mesh->vertices[i] - truth.vertices[i]).norm()));
    EXPECT_LT(worst, 0.2) << pp.dump();

    // Joined into one, the originals hidden.
    const json j = c.run("mesh.merge", {{"bodies", {"b2", "b3"}}});
    EXPECT_EQ(j["faces"].get<std::size_t>(), c.doc.body("b2").mesh->faceCount() + c.doc.body("b3").mesh->faceCount());
    EXPECT_FALSE(c.doc.body("b2").visible);
}

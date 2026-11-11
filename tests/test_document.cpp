// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestMeshes.h"
#include "commands/Command.h"
#include "core/fit/Align.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshRepair.h"
#include "doc/Project.h"
#include "mcp/LocalBackend.h"
#include "mcp/McpServer.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace orv;
using namespace orv::test;

namespace {
struct Fixture {
    CommandRegistry registry;
    Document doc;
    Fixture() { registerCoreCommands(registry); }
    json run(const std::string& name, json params = json::object(), PathPolicy p = PathPolicy::unrestricted()) {
        CommandContext ctx{doc, p, "en", "test", nullptr};
        return registry.run(name, params, ctx);
    }
};
}  // namespace

TEST(Document, UndoRedoAndModified) {
    Document d;
    EXPECT_FALSE(d.modified());
    d.beginChange("add");
    const Body b = d.addBody("part", std::make_shared<TriMesh>(box()));
    d.endChange();
    EXPECT_TRUE(d.modified());
    d.setSaved();
    EXPECT_FALSE(d.modified());
    d.beginChange("rename");
    d.renameBody(b.id, "bracket");
    d.endChange();
    EXPECT_TRUE(d.modified());
    EXPECT_EQ(d.undo(), "rename");
    EXPECT_FALSE(d.modified());
    EXPECT_EQ(d.bodies()[0].name, "part");
    EXPECT_EQ(d.redo(), "rename");
    EXPECT_EQ(d.bodies()[0].name, "bracket");
    d.beginChange("bad");
    d.removeBody(b.id);
    d.abortChange();
    EXPECT_EQ(d.bodies().size(), 1u);
}

TEST(Document, UniqueNamesAndReferences) {
    Document d;
    d.addBody("part", std::make_shared<TriMesh>(box()));
    const Body b2 = d.addBody("part", std::make_shared<TriMesh>(box()));
    EXPECT_EQ(b2.name, "part 2");
    EXPECT_EQ(d.body("b2").name, "part 2");
    EXPECT_EQ(d.body("part 2").id, b2.id);
    EXPECT_THROW(d.body("b9"), std::invalid_argument);
}

TEST(Project, SaveAndLoad) {
    const auto dir = tempDir("project");
    Document d;
    d.addBody("ball", std::make_shared<TriMesh>(sphere(5, 2)));
    const Body b = d.addBody("brick", std::make_shared<TriMesh>(box()));
    d.setVisible(b.id, false);
    saveProject(d, dir / "p.orvp");
    Document e;
    loadProject(e, dir / "p.orvp");
    ASSERT_EQ(e.bodies().size(), 2u);
    EXPECT_EQ(e.bodies()[1].name, "brick");
    EXPECT_FALSE(e.bodies()[1].visible);
    EXPECT_EQ(e.bodies()[0].mesh->faceCount(), sphere(5, 2).faceCount());
    EXPECT_FALSE(e.modified());
    std::ofstream(dir / "bad.orvp") << "not a project";
    EXPECT_THROW(loadProject(e, dir / "bad.orvp"), std::exception);
}

TEST(Commands, ImportRepairExportUndo) {
    const auto dir = tempDir("commands");
    TriMesh m = sphere(10, 3);
    std::erase_if(m.faces, [](const Tri& t) { return t[0] == 0 || t[1] == 0 || t[2] == 0; });
    writeMesh(m, dir / "scan.stl");
    Fixture f;
    const json imp = f.run("mesh.import", {{"path", (dir / "scan.stl").string()}});
    EXPECT_EQ(imp["ref"], "b1");
    EXPECT_EQ(imp["name"], "scan");
    const json a = f.run("mesh_analyze", {{"body", "b1"}});
    EXPECT_EQ(a["holes"], 1);
    f.run("mesh.fill_holes", {{"body", "scan"}});
    EXPECT_EQ(f.run("mesh.analyze", {{"body", "b1"}})["closed"], true);
    f.run("history.undo");
    EXPECT_EQ(f.run("mesh.analyze", {{"body", "b1"}})["holes"], 1);
    f.run("history.redo");
    f.run("mesh.export", {{"body", "b1"}, {"path", (dir / "out.stl").string()}});
    EXPECT_THROW(f.run("mesh.export", {{"body", "b1"}, {"path", (dir / "out.stl").string()}}), CommandError);  // no overwrite
    f.run("mesh.export", {{"body", "b1"}, {"path", (dir / "out.stl").string()}, {"overwrite", true}});
    f.run("project.save", {{"path", (dir / "p").string()}});
    EXPECT_TRUE(std::filesystem::exists(dir / "p.orvp"));
}

TEST(Commands, QuadRemeshToTSplineAndProject) {
    const auto dir = tempDir("quads");
    writeMesh(sphere(10, 4), dir / "ball.stl");
    Fixture f;
    f.run("mesh.import", {{"path", (dir / "ball.stl").string()}});
    const json q = f.run("mesh.remesh_quads", {{"body", "b1"}, {"targetFaces", 500}});
    EXPECT_GT(q["quads"].get<int>(), 300);
    EXPECT_EQ(q["tsmReady"], true);
    EXPECT_EQ(f.run("body.list")["bodies"][0]["quads"], q["quads"]);
    const json t = f.run("mesh.export_tsm", {{"body", "b1"}, {"path", (dir / "ball.tsm").string()}});
    EXPECT_EQ(t["faces"], q["quads"]);
    EXPECT_THROW(f.run("mesh.export_tsm", {{"body", "b1"}, {"path", (dir / "ball.tsm").string()}}), CommandError);  // no overwrite

    // The project keeps the quads (format 2).
    f.run("project.save", {{"path", (dir / "q.orvp").string()}});
    Document e;
    loadProject(e, dir / "q.orvp");
    EXPECT_EQ(e.bodies()[0].mesh->quads.size(), q["quads"].get<std::size_t>());

    // A triangle operation drops the quads, and the T-Spline export says why it cannot go on.
    f.run("mesh.smooth", {{"body", "b1"}});
    EXPECT_EQ(f.run("body.list")["bodies"][0]["quads"], 0);
    EXPECT_THROW(f.run("mesh.export_tsm", {{"body", "b1"}, {"path", (dir / "again.tsm").string()}}), CommandError);
    f.run("history.undo");
    EXPECT_EQ(f.run("body.list")["bodies"][0]["quads"], q["quads"]);
}

TEST(Commands, PointCloudToMesh) {
    const auto dir = tempDir("points");
    {
        std::ofstream o(dir / "ball.xyz");
        for (const auto& v : sphere(10, 4).vertices) o << v.x() << ' ' << v.y() << ' ' << v.z() << '\n';
    }
    Fixture f;
    const json imp = f.run("mesh.import", {{"path", (dir / "ball.xyz").string()}});
    EXPECT_EQ(imp["pointCloud"], true);
    EXPECT_EQ(imp["normals"], false);
    EXPECT_THROW(f.run("mesh.analyze", {{"body", "b1"}}), CommandError);         // a mesh command on a cloud
    EXPECT_THROW(f.run("points.to_mesh", {{"body", "b1"}}), CommandError);       // no normals yet
    const json n = f.run("points.estimate_normals", {{"body", "b1"}});
    EXPECT_EQ(n["points"], 2562);
    const json r = f.run("points.to_mesh", {{"body", "b1"}, {"depth", 6}});
    EXPECT_EQ(r["ref"], "b2");
    EXPECT_EQ(r["closed"], true);
    EXPECT_GT(r["faces"].get<int>(), 1000);
    EXPECT_NO_THROW(f.run("mesh.analyze", {{"body", "b2"}}));
    EXPECT_THROW(f.run("points.thin", {{"body", "b2"}, {"spacing", 1.0}}), CommandError);  // a cloud command on a mesh
    f.run("history.undo");  // takes the reconstruction back
    EXPECT_EQ(f.run("body.list")["bodies"].size(), 1u);
}

TEST(Commands, ShapesAndAlignment) {
    const auto dir = tempDir("shapes");
    TriMesh box = test::box(40, 30, 20);
    remeshUniform(box, 1.5f, 4);
    // Somewhere off the axes, as a scan comes in.
    Transform pose = Transform::Identity();
    pose.rotate(Eigen::AngleAxisd(0.4, Vec3d(1, 2, 3).normalized()));
    pose.pretranslate(Vec3d(30, -10, 5));
    transformMesh(box, pose);
    writeMesh(box, dir / "box.stl");
    Fixture f;
    f.run("mesh.import", {{"path", (dir / "box.stl").string()}});
    const json seg = f.run("features.segment", {{"body", "b1"}, {"noise", 0.01}});
    ASSERT_EQ(seg["features"].size(), 6u);
    for (const auto& x : seg["features"]) EXPECT_EQ(x["type"], "plane");
    EXPECT_EQ(seg["features"][0]["name"], "Plane 1");

    // The two largest faces are opposite: 20 mm apart, parallel.
    const json m = f.run("features.measure", {{"body", "b1"}, {"a", "f1"}, {"b", "f2"}});
    EXPECT_NEAR(m["angleDeg"].get<double>(), 0.0, 0.05);
    EXPECT_NEAR(m["distance"].get<double>(), 20.0, 0.02);

    // 3-2-1 on three mutually perpendicular faces: the box lands in the positive octant.
    std::vector<std::string> perpendicular{"f1"};
    for (const auto& x : seg["features"]) {
        const std::string r = x["ref"];
        if (r == "f1" || perpendicular.size() == 3) continue;
        bool ok = true;
        for (const auto& p : perpendicular)
            ok = ok && f.run("features.measure", {{"body", "b1"}, {"a", p}, {"b", r}})["angleDeg"].get<double>() > 89;
        if (ok) perpendicular.push_back(r);
    }
    ASSERT_EQ(perpendicular.size(), 3u);
    const json al = f.run("align.datums", {{"body", "b1"}, {"primary", perpendicular[0]}, {"secondary", perpendicular[1]}, {"tertiary", perpendicular[2]}});
    // A corner at the origin and the box along the axes (Y's sign follows the
    // right-hand rule from Z and X, so the tertiary face may be at either end).
    EXPECT_NEAR(al["bounds"]["min"][0].get<double>(), 0.0, 0.05);
    EXPECT_NEAR(al["bounds"]["min"][2].get<double>(), 0.0, 0.05);
    const double ymin = al["bounds"]["min"][1].get<double>(), ymax = al["bounds"]["max"][1].get<double>();
    EXPECT_LT(std::min(std::abs(ymin), std::abs(ymax)), 0.05);
    // The shapes moved with the body.
    const json listed = f.run("features.list", {{"body", "b1"}});
    EXPECT_NEAR(std::abs(listed["features"][0]["normal"][2].get<double>()), 1.0, 1e-4);

    // A shape fitted on a spot, and a project that keeps them all.
    const double ymid = (ymin + ymax) / 2, ztop = al["bounds"]["max"][2].get<double>();
    const json spot = f.run("features.fit", {{"body", "b1"}, {"at", {20, ymid, ztop}}, {"radius", 6.0}});
    EXPECT_EQ(spot["type"], "plane");
    f.run("project.save", {{"path", (dir / "s.orvp").string()}});
    Document e;
    loadProject(e, dir / "s.orvp");
    ASSERT_TRUE(e.bodies()[0].features);
    EXPECT_EQ(e.bodies()[0].features->list.size(), 7u);
    EXPECT_EQ(e.bodies()[0].features->faceRegion.size(), e.bodies()[0].mesh->faces.size());

    // Best fit of a second copy, moved, back onto the first.
    f.run("mesh.import", {{"path", (dir / "box.stl").string()}});
    const json bf = f.run("align.best_fit", {{"body", "b2"}, {"reference", "b1"}, {"coarse", true}});
    EXPECT_LT(bf["rms"].get<double>(), 0.01);
    EXPECT_GT(bf["overlapPercent"].get<double>(), 95.0);

    // Undo puts the shapes back where they were; a mesh edit drops them.
    f.run("history.undo");  // best fit
    f.run("history.undo");  // import b2
    f.run("mesh.smooth", {{"body", "b1"}});
    EXPECT_EQ(f.run("features.list", {{"body", "b1"}})["features"].size(), 0u);
    f.run("history.undo");
    EXPECT_EQ(f.run("features.list", {{"body", "b1"}})["features"].size(), 7u);
}

TEST(Commands, InputIsChecked) {
    Fixture f;
    try {
        f.run("mesh.analyze", {{"bodyy", "b1"}});
        FAIL();
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), "invalid_input");
        EXPECT_NE(std::string(e.what()).find("unknown input"), std::string::npos);
    }
    EXPECT_THROW(f.run("mesh.analyze"), CommandError);                                  // required
    EXPECT_THROW(f.run("mesh.smooth", {{"body", "b1"}, {"method", "magic"}}), CommandError);  // enum
    EXPECT_THROW(f.run("mesh.nothing"), CommandError);
}

TEST(Commands, AgentsStayInTheirFolders) {
    const auto dir = tempDir("policy");
    writeMesh(box(), dir / "in.stl");
    AgentSettings s;
    s.allowedFolders = {dir};
    const PathPolicy p = PathPolicy::forAgent(s);
    Fixture f;
    EXPECT_NO_THROW(f.run("mesh.import", {{"path", (dir / "in.stl").string()}}, p));
    try {
        f.run("mesh.export", {{"body", "b1"}, {"path", (dir.parent_path() / "outside.stl").string()}}, p);
        FAIL();
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), "invalid_input");
        EXPECT_NE(std::string(e.what()).find("outside the folders"), std::string::npos);
    }
    EXPECT_THROW(f.run("mesh.import", {{"path", (dir / ".." / "x.stl").string()}}, p), CommandError);
}

TEST(Mcp, HandshakeListAndCall) {
    const auto dir = tempDir("mcp");
    writeMesh(box(), dir / "b.stl");
    CommandRegistry reg;
    registerCoreCommands(reg);
    Document doc;
    LocalBackend backend(reg, doc, PathPolicy::unrestricted(), "es", "headless", false);
    McpServer s(backend, "es");
    auto init = s.handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-06-18"}}}});
    ASSERT_TRUE(init);
    EXPECT_EQ((*init)["result"]["protocolVersion"], "2025-06-18");
    EXPECT_EQ((*init)["result"]["serverInfo"]["name"], "openrevers");
    EXPECT_FALSE(s.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    auto list = s.handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    bool found = false;
    for (const auto& t : (*list)["result"]["tools"]) {
        if (t["name"] == "mesh_import") {
            found = true;
            EXPECT_EQ(t["title"], "Importar malla");
            EXPECT_FALSE(t["inputSchema"]["properties"]["path"].contains("x-kind"));
        }
    }
    EXPECT_TRUE(found);
    auto call = s.handle({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                          {"params", {{"name", "mesh_import"}, {"arguments", {{"path", (dir / "b.stl").string()}}}}}});
    EXPECT_EQ((*call)["result"]["isError"], false);
    EXPECT_EQ((*call)["result"]["structuredContent"]["faces"], 12);
    auto bad = s.handle({{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"}, {"params", {{"name", "mesh_analyze"}, {"arguments", {{"body", "b7"}}}}}});
    EXPECT_EQ((*bad)["result"]["isError"], true);
    auto unknown = s.handle({{"jsonrpc", "2.0"}, {"id", 5}, {"method", "nope"}});
    EXPECT_EQ((*unknown)["error"]["code"], -32601);
    EXPECT_NE(s.handleLine("{not json")->find("-32700"), std::string::npos);
}

TEST(Commands, HolesFittedOnASpotAndTheirPitch) {
    // Two Ø2.75 bores 4 mm apart through a 10 mm plate, the second one fitted
    // 3° off (as a fitted axis always is a little); a ring of the plate's
    // top face round the first. Fitting in a ball around a spot on the wall
    // must keep the wall only, and the pitch must be the centre distance.
    const auto dir = tempDir("holes");
    TriMesh m;
    auto tube = [&](Vec3d c, Vec3d axis) {
        const Vec3d u = axis.unitOrthogonal(), v = axis.cross(u);
        const int nu = 64, nv = 40;
        const auto b = static_cast<std::uint32_t>(m.vertices.size());
        for (int j = 0; j <= nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const double t = 2 * 3.14159265358979 * i / nu, z = 10.0 * j / nv - 5.0;
                m.vertices.push_back((c + 1.375 * (std::cos(t) * u + std::sin(t) * v) + z * axis).cast<float>());
            }
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                auto id = [&](int ii, int jj) { return b + static_cast<std::uint32_t>(jj * nu + ii % nu); };
                m.faces.push_back({id(i, j), id(i + 1, j + 1), id(i + 1, j)});
                m.faces.push_back({id(i, j), id(i, j + 1), id(i + 1, j + 1)});
            }
    };
    tube(Vec3d(0, 0, 5), Vec3d::UnitZ());
    tube(Vec3d(4, 0, 5), Vec3d(0, std::sin(3 * 3.14159265358979 / 180), std::cos(3 * 3.14159265358979 / 180)));
    {  // the top face round the first bore
        const int nu = 64, nr = 8;
        const auto b = static_cast<std::uint32_t>(m.vertices.size());
        for (int j = 0; j <= nr; ++j)
            for (int i = 0; i < nu; ++i) {
                const double t = 2 * 3.14159265358979 * i / nu, r = 1.375 + 1.6 * j / nr;
                m.vertices.push_back(Vec3f(static_cast<float>(r * std::cos(t)), static_cast<float>(r * std::sin(t)), 10.f));
            }
        for (int j = 0; j < nr; ++j)
            for (int i = 0; i < nu; ++i) {
                auto id = [&](int ii, int jj) { return b + static_cast<std::uint32_t>(jj * nu + ii % nu); };
                m.faces.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1)});
                m.faces.push_back({id(i, j), id(i + 1, j + 1), id(i, j + 1)});
            }
    }
    writeMesh(m, dir / "holes.stl");
    Fixture f;
    f.run("mesh.import", {{"path", (dir / "holes.stl").string()}});
    const json a = f.run("features.fit", {{"body", "b1"}, {"at", {1.375, 0.0, 8.8}}, {"radius", 2.0}, {"type", "cylinder"}});
    EXPECT_NEAR(a["diameter"].get<double>(), 2.75, 0.01);
    EXPECT_GT(a["area"].get<double>(), 5.0);
    const json b = f.run("features.fit", {{"body", "b1"}, {"at", {5.375, 0.0, 5.0}}, {"radius", 2.0}, {"type", "cylinder"}});
    EXPECT_NEAR(b["diameter"].get<double>(), 2.75, 0.01);
    const json d = f.run("features.measure", {{"body", "b1"}, {"a", a["ref"]}, {"b", b["ref"]}});
    EXPECT_NEAR(d["angleDeg"].get<double>(), 3.0, 0.05);
    EXPECT_NEAR(d["distance"].get<double>(), 4.0, 0.15);  // not the 0.0x where the two lines pass closest
}

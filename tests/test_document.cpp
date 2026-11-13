// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestMeshes.h"
#include "commands/Command.h"
#include "core/fit/Align.h"
#include "core/mesh/MeshIO.h"
#include "core/mesh/MeshRepair.h"
#include "core/solid/BSpline.h"
#include "core/solid/SketchFit.h"
#include "core/mesh/Distance.h"
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

TEST(Mcp, CommandListAndRunReachEveryCommand) {
    // A client that cached an old tool list still reaches new commands.
    CommandRegistry reg;
    registerCoreCommands(reg);
    Document doc;
    LocalBackend backend(reg, doc, PathPolicy::unrestricted(), "es", "headless", false);
    McpServer s(backend, "es");
    auto call = [&](const std::string& name, json args) {
        return (*s.handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", name}, {"arguments", args}}}}))["result"];
    };
    auto list = call("command_list", json::object());
    ASSERT_FALSE(list["isError"].get<bool>());
    bool scan = false;
    for (const auto& c : list["structuredContent"]["commands"]) scan = scan || c["name"] == "scan.status";
    EXPECT_TRUE(scan);
    auto one = call("command_list", {{"command", "mesh_crop"}});
    EXPECT_TRUE(one["structuredContent"]["schema"]["properties"].contains("dryRun"));
    auto run = call("command_run", {{"command", "scan_status"}});
    EXPECT_FALSE(run["isError"].get<bool>());
    auto bad = call("command_run", {{"command", "nope.nada"}});
    EXPECT_TRUE(bad["isError"].get<bool>());
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

TEST(Commands, ParametricPartFromSketchToStep) {
    const auto dir = tempDir("part");
    Fixture f;
    const json p = f.run("part.new", {{"name", "Plate"}});
    ASSERT_EQ(p["body"], "b1");
    f.run("part.parameter", {{"body", "b1"}, {"name", "thickness"}, {"value", 2.5}});
    json loops = json::array();
    loops.push_back(json::array({{{"type", "line"}, {"from", {0, 0}}, {"to", {30, 0}}}, {{"type", "line"}, {"from", {30, 0}}, {"to", {30, 12}}},
                                 {{"type", "line"}, {"from", {30, 12}}, {"to", {0, 12}}}, {{"type", "line"}, {"from", {0, 12}}, {"to", {0, 0}}}}));
    for (double x : {9.0, 13.0, 17.0, 21.0}) loops.push_back(json::array({{{"type", "circle"}, {"centre", {x, 6.0}}, {"radius", 1.375}}}));
    const json s = f.run("part.sketch", {{"body", "b1"}, {"loops", loops}});
    EXPECT_EQ(s["ref"], "s1");
    const json e = f.run("part.extrude", {{"body", "b1"}, {"sketch", "s1"}, {"end", "thickness"}, {"mode", "new"}});
    const double area = 30 * 12 - 4 * 3.14159265358979 * 1.375 * 1.375;
    EXPECT_NEAR(e["solid"]["volume"].get<double>(), 2.5 * area, 1e-3);
    EXPECT_EQ(e["solid"]["faces"], 10);
    // Change the parameter: the part follows.
    const json t = f.run("part.parameter", {{"body", "b1"}, {"name", "thickness"}, {"value", 3.0}});
    EXPECT_NEAR(t["solid"]["volume"].get<double>(), 3.0 * area, 1e-3);
    // An edit that breaks the part is refused and changes nothing.
    EXPECT_THROW(f.run("part.fillet", {{"body", "b1"}, {"points", {{15.0, 0.0, 3.0}}}, {"radius", 50.0}}), CommandError);
    EXPECT_EQ(f.run("part.list", {{"body", "b1"}})["operations"].size(), 1u);
    // A 0.5 mm round on the top front edge.
    const json r = f.run("part.fillet", {{"body", "b1"}, {"points", {{15.0, 0.0, 3.0}}}, {"radius", 0.5}});
    EXPECT_LT(r["solid"]["volume"].get<double>(), 3.0 * area);
    EXPECT_THROW(f.run("mesh.smooth", {{"body", "b1"}}), CommandError);  // its triangles only show the solid
    EXPECT_THROW(f.run("part.delete", {{"body", "b1"}, {"item", "s1"}}), CommandError);  // used by the extrusion

    // A scan of it: a remeshed box in the same place, holes not drilled.
    TriMesh scan = test::box(30, 12, 3);
    remeshUniform(scan, 0.5f, 4);
    writeMesh(scan, dir / "scan.stl");
    f.run("mesh.import", {{"path", (dir / "scan.stl").string()}});
    const json c = f.run("part.compare", {{"body", "b1"}, {"scan", "b2"}, {"tolerance", 0.2}});
    EXPECT_GT(c["withinPercent"].get<double>(), 90.0);  // all but the rounded edge and the plain top over the holes
    EXPECT_LT(c["median"].get<double>(), 0.01);

    // The section of the scan half way up: one closed rectangle.
    const json sec = f.run("mesh.section", {{"body", "b2"}, {"origin", {0.0, 0.0, 1.5}}, {"normal", {0.0, 0.0, 1.0}}, {"xDir", {1.0, 0.0, 0.0}}});
    ASSERT_EQ(sec["polylines"].size(), 1u);
    EXPECT_TRUE(sec["polylines"][0]["closed"].get<bool>());
    EXPECT_NEAR(std::abs(sec["polylines"][0]["area"].get<double>()), 360.0, 0.5);
    EXPECT_LE(sec["polylines"][0]["points"].size(), 8u);  // simplified to its corners

    // The scan's section as a sketch: one rectangle of four lines, 30 × 12.
    f.run("part.new", {{"name", "From scan"}});
    const json fs = f.run("part.sketch_from_section", {{"body", "b3"}, {"scan", "b2"}, {"origin", {0.0, 0.0, 1.5}}, {"normal", {0.0, 0.0, 1.0}}, {"xDir", {1.0, 0.0, 0.0}}});
    ASSERT_EQ(fs["loops"].size(), 1u);
    EXPECT_EQ(fs["loops"][0]["segments"].size(), 4u);
    EXPECT_NEAR(fs["loops"][0]["area"].get<double>(), 360.0, 0.2);
    const json fe = f.run("part.extrude", {{"body", "b3"}, {"sketch", "s1"}, {"start", -1.5}, {"end", 1.5}, {"mode", "new"}});
    EXPECT_NEAR(fe["solid"]["volume"].get<double>(), 30 * 12 * 3, 2.0);
    EXPECT_GT(f.run("part.compare", {{"body", "b3"}, {"scan", "b2"}, {"tolerance", 0.05}})["withinPercent"].get<double>(), 99.0);
    f.run("body.delete", {{"body", "b3"}});

    // STEP out and in; the project keeps the part.
    f.run("part.export_step", {{"body", "b1"}, {"path", (dir / "plate.step").string()}});
    const json imp = f.run("part.import_step", {{"path", (dir / "plate.step").string()}});
    EXPECT_NEAR(imp["solid"]["volume"].get<double>(), r["solid"]["volume"].get<double>(), 1e-2);
    f.run("project.save", {{"path", (dir / "p.orvp").string()}});
    Fixture g;
    g.run("project.open", {{"path", (dir / "p.orvp").string()}});
    const json back = g.run("part.list", {{"body", "Plate"}});
    EXPECT_EQ(back["operations"].size(), 2u);
    EXPECT_NEAR(back["solid"]["volume"].get<double>(), r["solid"]["volume"].get<double>(), 1e-6);
    EXPECT_EQ(back["parameters"]["thickness"], 3.0);
    // Undo takes the fillet back.
    f.run("history.undo");  // the STEP import
    for (int k = 0; k < 4; ++k) f.run("history.undo");  // delete, extrude, sketch and new part from the scan
    f.run("history.undo");  // the scan import
    f.run("history.undo");  // the fillet
    EXPECT_NEAR(f.run("part.list", {{"body", "b1"}})["solid"]["volume"].get<double>(), 3.0 * area, 1e-3);
}

TEST(Commands, EditingAPartByHand) {
    // What the person does with the part's history in the window, as commands.
    Fixture f;
    f.run("part.new", {{"name", "Plate"}});
    json loops = json::array();
    loops.push_back(json::array({{{"type", "line"}, {"from", {0, 0}}, {"to", {30, 0}}}, {{"type", "line"}, {"from", {30, 0}}, {"to", {30, 12}}},
                                 {{"type", "line"}, {"from", {30, 12}}, {"to", {0, 12}}}, {{"type", "line"}, {"from", {0, 12}}, {"to", {0, 0}}}}));
    f.run("part.sketch", {{"body", "b1"}, {"loops", loops}});
    f.run("part.sketch", {{"body", "b1"}, {"loops", json::array({json::array({{{"type", "circle"}, {"centre", {10.0, 6.0}}, {"radius", 2.0}}})})}});
    f.run("part.extrude", {{"body", "b1"}, {"sketch", "s1"}, {"end", 3.0}, {"mode", "new"}});
    const json cut = f.run("part.extrude", {{"body", "b1"}, {"sketch", "s2"}, {"end", 3.0}, {"mode", "cut"}});
    const double withHole = cut["solid"]["volume"].get<double>();
    EXPECT_NEAR(withHole, 3 * (360 - 3.14159265358979 * 4), 1e-3);

    // Renaming a sketch alone, and moving its plane alone, keep its loops.
    f.run("part.edit_sketch", {{"body", "b1"}, {"sketch", "s2"}, {"name", "Pivot"}});
    EXPECT_EQ(f.run("part.list", {{"body", "b1"}})["sketches"][1]["name"], "Pivot");
    const json moved = f.run("part.edit_sketch", {{"body", "b1"}, {"sketch", "Pivot"}, {"plane", {{"origin", {5.0, 0.0, 0.0}}, {"normal", {0.0, 0.0, 1.0}}, {"xDir", {1.0, 0.0, 0.0}}}}});
    EXPECT_NEAR(moved["solid"]["volume"].get<double>(), withHole, 1e-3);  // the hole moved, still inside
    EXPECT_EQ(moved["sketches"][1]["segments"], 1);

    // Moving the cut first is refused (nothing to cut from) and changes nothing.
    EXPECT_THROW(f.run("part.move", {{"body", "b1"}, {"operation", "o4"}, {"position", 1}}), CommandError);
    EXPECT_EQ(f.run("part.list", {{"body", "b1"}})["operations"][0]["ref"], "o3");
    // A fillet moved before the cut: the part is rebuilt in the new order.
    f.run("part.fillet", {{"body", "b1"}, {"points", {{15.0, 0.0, 3.0}}}, {"radius", 0.5}});
    const json m = f.run("part.move", {{"body", "b1"}, {"operation", "o5"}, {"position", 2}});
    EXPECT_EQ(m["operations"][1]["ref"], "o5");
    EXPECT_EQ(m["operations"][2]["ref"], "o4");

    // Opacity is a step of the history; it comes back on undo.
    f.run("body.set_opacity", {{"body", "b1"}, {"opacity", 0.4}});
    EXPECT_NEAR(f.doc.body("b1").opacity, 0.4, 1e-6);
    f.run("history.undo");
    EXPECT_NEAR(f.doc.body("b1").opacity, 1.0, 1e-6);

    // The part's view: a face for each triangle, every edge as a polyline.
    const Body b = f.doc.body("b1");
    ASSERT_TRUE(b.view);
    EXPECT_EQ(b.view->faceOf.size(), b.mesh->faces.size());
    EXPECT_EQ(static_cast<int>(b.view->edges.size()), b.solid.edgeCount());
    int planes = 0, cylinders = 0;
    for (int i = 0; i < b.solid.faceCount(); ++i) {
        const ShapeInfo s = b.solid.faceInfo(i);
        planes += s.type == "plane";
        if (s.type == "cylinder") {
            ++cylinders;
            if (s.radius > 1) EXPECT_NEAR(std::abs(s.axis.z()), 1.0, 1e-9);  // the hole, Ø4 along z
            else EXPECT_NEAR(std::abs(s.axis.x()), 1.0, 1e-9);              // the fillet, R0.5 along x
        }
    }
    EXPECT_EQ(cylinders, 2);
    EXPECT_GE(planes, 6);
    // The edge the fillet rounded is gone; a straight top edge reports its length.
    bool found = false;
    for (int i = 0; i < b.solid.edgeCount(); ++i) {
        const ShapeInfo e = b.solid.edgeInfo(i);
        if (e.type == "line" && std::abs(e.size - 12.0) < 1e-6) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(Commands, CompareShowsWhereAndColoursTheScan) {
    const auto dir = tempDir("compare");
    Fixture f;
    // A scan of a 30 × 12 × 3 block, and a part 0.5 mm too short in x.
    TriMesh scan = test::box(30, 12, 3);
    remeshUniform(scan, 0.5f, 4);
    writeMesh(scan, dir / "scan.stl");
    f.run("mesh.import", {{"path", (dir / "scan.stl").string()}});
    f.run("part.new");
    json loops = json::array();
    loops.push_back(json::array({{{"type", "line"}, {"from", {0, 0}}, {"to", {29.5, 0}}}, {{"type", "line"}, {"from", {29.5, 0}}, {"to", {29.5, 12}}},
                                 {{"type", "line"}, {"from", {29.5, 12}}, {"to", {0, 12}}}, {{"type", "line"}, {"from", {0, 12}}, {"to", {0, 0}}}}));
    f.run("part.sketch", {{"body", "b2"}, {"loops", loops}});
    f.run("part.extrude", {{"body", "b2"}, {"sketch", "s1"}, {"end", 3.0}, {"mode", "new"}});
    const bool couldUndo = f.doc.canUndo();
    const auto undoSteps = f.doc.undoList().size();
    const json c = f.run("part.compare", {{"body", "b2"}, {"scan", "b1"}, {"tolerance", 0.1}});
    // One zone: the end face at x = 30, outside the part by 0.5 mm.
    ASSERT_GE(c["zones"].size(), 1u);
    const json& z = c["zones"][0];
    EXPECT_NEAR(z["centre"][0].get<double>(), 30.0, 0.3);
    EXPECT_NEAR(z["meanDistance"].get<double>(), 0.5, 0.05);
    // The scan is coloured, and that is not a step of the history.
    const Body s = f.doc.body("b1");
    ASSERT_TRUE(s.deviation);
    EXPECT_EQ(s.deviation->distance.size(), s.mesh->vertices.size());
    EXPECT_EQ(f.doc.canUndo(), couldUndo);
    EXPECT_EQ(f.doc.undoList().size(), undoSteps);
    f.run("body.clear_deviation", {{"body", "b1"}});
    EXPECT_FALSE(f.doc.body("b1").deviation);
}

TEST(Commands, LoftPatternAndFreeSurfaces) {
    const auto dir = tempDir("surfaces");
    Fixture f;
    const double pi = 3.14159265358979323846;
    // A loft from a Ø20 circle at z = 0 to a Ø10 circle at z = 10: a cone frustum.
    f.run("part.new", {{"name", "Turbine"}});
    auto circle = [](double r) { return json::array({json::array({{{"type", "circle"}, {"centre", {0.0, 0.0}}, {"radius", r}}})}); };
    f.run("part.sketch", {{"body", "b1"}, {"loops", circle(10)}});
    f.run("part.sketch", {{"body", "b1"}, {"loops", circle(5)}, {"plane", {{"origin", {0.0, 0.0, 10.0}}, {"normal", {0.0, 0.0, 1.0}}}}});
    const json loft = f.run("part.loft", {{"body", "b1"}, {"sections", {"s1", "s2"}}, {"mode", "new"}});
    const double frustum = pi * 10 / 3 * (100 + 50 + 25);
    EXPECT_NEAR(loft["solid"]["volume"].get<double>(), frustum, 0.005 * frustum);
    // A Ø2 hole through it at x = 7, repeated six times round z.
    f.run("part.sketch", {{"body", "b1"}, {"loops", json::array({json::array({{{"type", "circle"}, {"centre", {7.0, 0.0}}, {"radius", 1.0}}})})}});
    f.run("part.extrude", {{"body", "b1"}, {"sketch", "s4"}, {"start", -1.0}, {"end", 3.0}, {"mode", "cut"}});
    const double lofted = loft["solid"]["volume"].get<double>();
    const double oneHole = lofted - f.run("part.list", {{"body", "b1"}})["solid"]["volume"].get<double>();
    EXPECT_NEAR(oneHole, pi * 3, 0.01 * pi * 3);  // Ø2 through 3 mm
    f.run("part.parameter", {{"body", "b1"}, {"name", "holes"}, {"value", 6}});
    const json pat = f.run("part.pattern", {{"body", "b1"}, {"operation", "o5"}, {"axisOrigin", {0.0, 0.0, 0.0}}, {"axisDirection", {0.0, 0.0, 1.0}}, {"count", "holes"}});
    EXPECT_EQ(pat["operations"][2]["kind"], "pattern");
    EXPECT_EQ(pat["operations"][2]["mode"], "cut");  // as the operation it repeats
    EXPECT_NEAR(lofted - pat["solid"]["volume"].get<double>(), 6 * oneHole, 0.01 * 6 * oneHole);
    // The parameter drives the count.
    const json four = f.run("part.parameter", {{"body", "b1"}, {"name", "holes"}, {"value", 4}});
    EXPECT_NEAR(lofted - four["solid"]["volume"].get<double>(), 4 * oneHole, 0.01 * 4 * oneHole);

    // A scan with a curved top: a 40 × 30 block whose top is z = 5 + (x² + y²)/400, sampled densely.
    TriMesh scan;
    const int nx = 81, ny = 61;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const double x = -20 + 0.5 * i, y = -15 + 0.5 * j;
            scan.vertices.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(5 + (x * x + y * y) / 400));
        }
    for (int j = 0; j + 1 < ny; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const auto a = static_cast<std::uint32_t>(i + nx * j), b = a + 1, c = a + static_cast<std::uint32_t>(nx), d = c + 1;
            scan.faces.push_back({a, b, d});
            scan.faces.push_back({a, d, c});
        }
    writeMesh(scan, dir / "top.stl");
    f.run("mesh.import", {{"path", (dir / "top.stl").string()}});
    f.run("part.new", {{"name", "Lid"}});
    json rect = json::array();
    rect.push_back(json::array({{{"type", "line"}, {"from", {-20, -15}}, {"to", {20, -15}}}, {{"type", "line"}, {"from", {20, -15}}, {"to", {20, 15}}},
                                {{"type", "line"}, {"from", {20, 15}}, {"to", {-20, 15}}}, {{"type", "line"}, {"from", {-20, 15}}, {"to", {-20, -15}}}}));
    f.run("part.sketch", {{"body", "b3"}, {"loops", rect}});
    f.run("part.extrude", {{"body", "b3"}, {"sketch", "s1"}, {"end", 12.0}, {"mode", "new"}});
    const json sf = f.run("part.surface_from_scan", {{"body", "b3"}, {"scan", "b2"}, {"margin", 3.0}});
    EXPECT_EQ(sf["ref"], "sf3");
    EXPECT_LT(sf["rms"].get<double>(), 0.005);
    EXPECT_NEAR(std::abs(sf["normal"][2].get<double>()), 1.0, 1e-6);
    const json cut = f.run("part.cut_by_surface", {{"body", "b3"}, {"surface", "sf3"}, {"keep", "below"}});
    // Volume under z = 5 + (x² + y²)/400 over the rectangle: 40·30·5 + (∫x² ∫dy + ∫dx ∫y²)/400.
    const double under = 40 * 30 * 5 + (30 * (2 * 8000.0 / 3) + 40 * (2 * 3375.0 / 3)) / 400;
    EXPECT_NEAR(cut["solid"]["volume"].get<double>(), under, 0.002 * under);
    // The scan now lies on the part's top: the comparison says so.
    const json cmp = f.run("part.compare", {{"body", "b3"}, {"scan", "b2"}, {"tolerance", 0.02}});
    EXPECT_GT(cmp["withinPercent"].get<double>(), 99.0);
    // Keep the other side instead.
    const json above = f.run("part.edit", {{"body", "b3"}, {"operation", "o4"}, {"keep", "above"}});
    EXPECT_NEAR(above["solid"]["volume"].get<double>(), 40 * 30 * 12 - under, 0.002 * under);

    // A surface thickened into a 1 mm shell.
    f.run("part.new", {{"name", "Shell"}});
    f.run("part.surface_from_scan", {{"body", "b4"}, {"scan", "b2"}, {"margin", 0.0}});
    const json th = f.run("part.thicken", {{"body", "b4"}, {"surface", "sf1"}, {"thickness", 1.0}, {"mode", "new"}});
    EXPECT_NEAR(th["solid"]["volume"].get<double>(), 40 * 30 * 1.0, 0.02 * 1200);
    EXPECT_THROW(f.run("part.delete", {{"body", "b4"}, {"item", "sf1"}}), CommandError);  // the thickening uses it

    // All of it is saved and read back.
    f.run("project.save", {{"path", (dir / "s.orvp").string()}});
    Fixture g;
    g.run("project.open", {{"path", (dir / "s.orvp").string()}});
    EXPECT_NEAR(g.run("part.list", {{"body", "Turbine"}})["solid"]["volume"].get<double>(), four["solid"]["volume"].get<double>(), 1e-3);
    const json lid = g.run("part.list", {{"body", "Lid"}});
    EXPECT_EQ(lid["surfaces"].size(), 1u);
    // Rebuilt from the saved model, the lid is the same.
    const json again = g.run("part.edit", {{"body", "Lid"}, {"operation", "o4"}, {"keep", "below"}});
    EXPECT_NEAR(again["solid"]["volume"].get<double>(), cut["solid"]["volume"].get<double>(), 1e-3);
}

TEST(Commands, SketchFromSectionWithSplines) {
    // A scan of an extruded airfoil-like lens: its section comes back as splines.
    const auto dir = tempDir("splinesection");
    Fixture f;
    f.run("part.new", {{"name", "Lens"}});
    // An ellipse 30 × 8 drawn as two splines, extruded as the "scan".
    std::vector<Vec2d> top, bottom;
    for (int i = 0; i <= 60; ++i) {
        const double t = 3.14159265358979323846 * i / 60;
        top.emplace_back(15 * std::cos(t), 4 * std::sin(t));
        bottom.emplace_back(-15 * std::cos(t), -4 * std::sin(t));
    }
    const CurveFit a = fitCurve(std::vector<Vec2d>(top.begin() + 1, top.end() - 1), top.front(), top.back(), 0.001);
    const CurveFit b = fitCurve(std::vector<Vec2d>(bottom.begin() + 1, bottom.end() - 1), bottom.front(), bottom.back(), 0.001);
    json pa = json::array(), pb = json::array();
    for (const auto& q : a.poles) pa.push_back({q.x(), q.y()});
    for (const auto& q : b.poles) pb.push_back({q.x(), q.y()});
    f.run("part.sketch", {{"body", "b1"}, {"loops", json::array({json::array({{{"type", "spline"}, {"poles", pa}}, {{"type", "spline"}, {"poles", pb}}})})}});
    const json e = f.run("part.extrude", {{"body", "b1"}, {"sketch", "s1"}, {"end", 5.0}, {"mode", "new"}});
    EXPECT_NEAR(e["solid"]["volume"].get<double>(), 5 * 3.14159265358979323846 * 15 * 4, 0.003 * 5 * 3.14159265358979323846 * 60);
    f.run("mesh.export", {{"body", "b1"}, {"path", (dir / "lens.stl").string()}, {"overwrite", true}});
    f.run("mesh.import", {{"path", (dir / "lens.stl").string()}});
    f.run("part.new");
    const json s = f.run("part.sketch_from_section", {{"body", "b3"}, {"scan", "b2"}, {"origin", {0.0, 0.0, 2.5}}, {"normal", {0.0, 0.0, 1.0}}, {"xDir", {1.0, 0.0, 0.0}},
                                                      {"splines", true}, {"tolerance", 0.02}});
    ASSERT_EQ(s["loops"].size(), 1u);
    int splines = 0;
    for (const auto& sg : s["loops"][0]["segments"]) splines += sg["type"] == "spline";
    EXPECT_GE(splines, 1);
    EXPECT_LT(s["loops"][0]["maxDeviation"].get<double>(), 0.05);
}

// ---- 0.7.1: what the air-box test of 0.6.0 found ----------------------------

TEST(Commands, NumbersWrittenAsTextAndMinusAParameter) {
    Fixture f;
    f.run("part.new", {{"name", "Plate"}});
    f.run("part.sketch", {{"body", "b1"}, {"loops", json::array({json::array({{{"type", "circle"}, {"centre", {0.0, 0.0}}, {"radius", 10.0}}})})}});
    // "-4.5" is a number, not a parameter's name (some clients send text).
    const json e = f.run("part.extrude", {{"body", "b1"}, {"sketch", "s1"}, {"start", "-4.5"}, {"end", "1,5"}, {"mode", "new"}});
    EXPECT_NEAR(e["solid"]["volume"].get<double>(), 3.14159265358979323846 * 100 * 6, 1.0);
    // "-depth": minus the parameter.
    f.run("part.parameter", {{"body", "b1"}, {"name", "depth"}, {"value", 2.0}});
    const json g = f.run("part.edit", {{"body", "b1"}, {"operation", "o2"}, {"start", "-depth"}});
    EXPECT_EQ(g["operations"][0]["start"], "-depth");
    EXPECT_NEAR(g["solid"]["volume"].get<double>(), 3.14159265358979323846 * 100 * 3.5, 1.0);
}

TEST(Commands, HollowLoftAndPickingLoopsOfASection) {
    const auto dir = tempDir("hollowloft");
    Fixture f;
    const double pi = 3.14159265358979323846;
    // A tube Ø20/Ø16, 10 high, as the "scan".
    f.run("part.new", {{"name", "Tube"}});
    f.run("part.sketch", {{"body", "b1"}, {"loops", json::array({json::array({{{"type", "circle"}, {"centre", {0.0, 0.0}}, {"radius", 10.0}}}),
                                                                 json::array({{{"type", "circle"}, {"centre", {0.0, 0.0}}, {"radius", 8.0}}})})}});
    f.run("part.extrude", {{"body", "b1"}, {"sketch", "s1"}, {"end", 10.0}, {"mode", "new"}});
    f.run("mesh.export", {{"body", "b1"}, {"path", (dir / "tube.stl").string()}, {"overwrite", true}});
    f.run("mesh.import", {{"path", (dir / "tube.stl").string()}});
    f.run("part.new", {{"name", "Wall"}});
    auto section = [&](double z, const std::string& keep) {
        return f.run("part.sketch_from_section", {{"body", "b3"}, {"scan", "b2"}, {"origin", {0.0, 0.0, z}}, {"normal", {0.0, 0.0, 1.0}},
                                                  {"xDir", {1.0, 0.0, 0.0}}, {"tolerance", 0.05}, {"keep", keep}});
    };
    const json outer = section(2, "outer");
    ASSERT_EQ(outer["loops"].size(), 1u);
    EXPECT_EQ(outer["loops"][0]["index"], 0);
    EXPECT_NEAR(outer["loops"][0]["segments"][0]["diameter"].get<double>(), 20.0, 0.1);
    const json inner = section(2, "inner");
    ASSERT_EQ(inner["loops"].size(), 1u);
    EXPECT_NEAR(inner["loops"][0]["segments"][0]["diameter"].get<double>(), 16.0, 0.1);
    const json only = f.run("part.sketch_from_section", {{"body", "b3"}, {"scan", "b2"}, {"origin", {0.0, 0.0, 3.0}}, {"normal", {0.0, 0.0, 1.0}}, {"keepLoops", {1}}});
    ASSERT_EQ(only["loops"].size(), 1u);
    EXPECT_EQ(only["loops"][0]["index"], 1);
    // Both loops at two heights: a hollow loft, the wall alone.
    section(1, "all");
    section(9, "all");
    const json l = f.run("part.loft", {{"body", "b3"}, {"sections", {"s4", "s5"}}, {"mode", "new"}});
    const double wall = pi * (100 - 64) * 8;
    EXPECT_NEAR(l["solid"]["volume"].get<double>(), wall, 0.01 * wall);
    // The geometry of a sketch comes back in full, to edit.
    const json lst = f.run("part.list", {{"body", "b3"}, {"sketch", "s4"}});
    ASSERT_TRUE(lst["sketches"][3].contains("loopsGeometry"));
    EXPECT_EQ(lst["sketches"][3]["loopsGeometry"].size(), 2u);
    EXPECT_FALSE(lst["sketches"][0].contains("loopsGeometry"));
}

TEST(Commands, CutBySurfaceRefusesASurfaceThatDoesNotCoverThePart) {
    const auto dir = tempDir("shortsurface");
    Fixture f;
    TriMesh scan;
    const int nx = 81, ny = 61;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const double x = -20 + 0.5 * i, y = -15 + 0.5 * j;
            scan.vertices.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(5 + (x * x + y * y) / 400));
        }
    for (int j = 0; j + 1 < ny; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const auto a = static_cast<std::uint32_t>(i + nx * j), b = a + 1, c = a + static_cast<std::uint32_t>(nx), d = c + 1;
            scan.faces.push_back({a, b, d});
            scan.faces.push_back({a, d, c});
        }
    writeMesh(scan, dir / "top.stl");
    f.run("mesh.import", {{"path", (dir / "top.stl").string()}});
    f.run("part.new", {{"name", "Lid"}});
    json rect = json::array();
    rect.push_back(json::array({{{"type", "line"}, {"from", {-20, -15}}, {"to", {20, -15}}}, {{"type", "line"}, {"from", {20, -15}}, {"to", {20, 15}}},
                                {{"type", "line"}, {"from", {20, 15}}, {"to", {-20, 15}}}, {{"type", "line"}, {"from", {-20, 15}}, {"to", {-20, -15}}}}));
    f.run("part.sketch", {{"body", "b2"}, {"loops", rect}});
    f.run("part.extrude", {{"body", "b2"}, {"sketch", "s1"}, {"end", 12.0}, {"mode", "new"}});
    // A surface fitted to a small patch only: it cannot cut the whole top.
    f.run("part.surface_from_scan", {{"body", "b2"}, {"scan", "b1"}, {"centre", {0.0, 0.0, 5.0}}, {"radius", 6.0}, {"margin", 1.0}});
    const double before = f.run("part.list", {{"body", "b2"}})["solid"]["volume"].get<double>();
    try {
        f.run("part.cut_by_surface", {{"body", "b2"}, {"surface", "sf3"}, {"keep", "below"}});
        FAIL() << "a short surface must be refused";
    } catch (const CommandError& e) {
        EXPECT_NE(std::string(e.what()).find("cover"), std::string::npos) << e.what();
    }
    EXPECT_NEAR(f.run("part.list", {{"body", "b2"}})["solid"]["volume"].get<double>(), before, 1e-6);
}

TEST(Distance, SignAtAnEdgeOrACornerIsRight) {
    // A 10 mm cube: points just outside its edges and corners are outside.
    TriMesh cube;
    cube.vertices = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}, {0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}};
    cube.faces = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    const MeshDistance md(cube);
    for (const Vec3d& p : {Vec3d(12, 12, 5), Vec3d(11, 5, 11), Vec3d(5, -3, 13), Vec3d(13, 13, 13), Vec3d(-15, -15, -15), Vec3d(25, 5, 25)})
        EXPECT_GT(md.signedDistance(p), 0) << p.transpose();
    for (const Vec3d& p : {Vec3d(9.5, 9.5, 5), Vec3d(5, 5, 9.9), Vec3d(9.9, 9.9, 9.9)})
        EXPECT_LT(md.signedDistance(p), 0) << p.transpose();
}

TEST(SketchFit, LongOrganicSplinesMeetTheTolerance) {
    // A wavy outline that needs far more than 40 poles in one run.
    std::vector<Vec2d> pts;
    const int n = 3000;
    for (int i = 0; i < n; ++i) {
        const double t = 2 * 3.14159265358979323846 * i / n;
        const double r = 40 + 1.5 * std::sin(17 * t) + 0.8 * std::sin(29 * t + 0.3);
        pts.emplace_back(r * std::cos(t), 0.6 * r * std::sin(t));
    }
    SketchFitOptions o;
    o.tolerance = 0.1;
    o.splines = true;
    const LoopFit f = fitLoop(pts, o);
    EXPECT_LE(f.maxDeviation, 0.12);
    EXPECT_TRUE(f.loop.segments.size() >= 1);
}

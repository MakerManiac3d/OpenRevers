// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestMeshes.h"
#include "commands/Command.h"
#include "core/mesh/MeshIO.h"
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

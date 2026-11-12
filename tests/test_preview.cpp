// SPDX-License-Identifier: GPL-3.0-or-later
// dryRun / preview: a command worked out on a copy of the document leaves the
// real one exactly as it was (same meshes, same history) and says what it
// would change, add and remove.
#include "TestMeshes.h"
#include "commands/Command.h"
#include "doc/Document.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace orv;

namespace {
struct Fixture {
    Document doc;
    CommandRegistry reg;
    Fixture() {
        registerCoreCommands(reg);
        doc.addBody("Caja", std::make_shared<TriMesh>(test::box()));
    }
    json run(const std::string& name, json in) {
        CommandContext ctx{doc, PathPolicy::unrestricted(), "en", "test", nullptr};
        return reg.run(name, in, ctx);
    }
};
bool has(const json& a, const std::string& s) { return std::find(a.begin(), a.end(), s) != a.end(); }
}  // namespace

TEST(Preview, ChangeLeavesDocumentAndHistoryAlone) {
    Fixture f;
    const auto mesh = f.doc.body("b1").mesh;
    const auto history = f.doc.undoList().size();
    const json out = f.run("mesh.remesh", {{"body", "b1"}, {"edgeLength", 2.0}, {"dryRun", true}});
    EXPECT_TRUE(out.value("dryRun", false));
    EXPECT_TRUE(has(out["preview"]["changed"], "b1"));
    EXPECT_EQ(f.doc.body("b1").mesh, mesh);
    EXPECT_EQ(f.doc.undoList().size(), history);
    EXPECT_FALSE(f.doc.canRedo());
}

TEST(Preview, AddAndRemoveAreReported) {
    Fixture f;
    const json dup = f.run("body.duplicate", {{"body", "b1"}, {"dryRun", true}});
    ASSERT_EQ(dup["preview"]["added"].size(), 1u);
    EXPECT_EQ(f.doc.bodies().size(), 1u);
    const json del = f.run("body.delete", {{"body", "b1"}, {"dryRun", true}});
    EXPECT_TRUE(has(del["preview"]["removed"], "b1"));
    EXPECT_TRUE(f.doc.findBody("b1").has_value());
}

TEST(Preview, DryRunFalseRunsForReal) {
    Fixture f;
    const auto history = f.doc.undoList().size();
    const json out = f.run("body.duplicate", {{"body", "b1"}, {"dryRun", false}});
    EXPECT_FALSE(out.contains("dryRun"));
    EXPECT_EQ(f.doc.bodies().size(), 2u);
    EXPECT_EQ(f.doc.undoList().size(), history + 1);
}

TEST(Preview, ReadOnlyCommandIgnoresDryRun) {
    Fixture f;
    const json out = f.run("body.list", {{"dryRun", true}});
    EXPECT_FALSE(out.contains("preview"));
}

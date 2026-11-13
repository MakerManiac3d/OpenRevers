// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 6: scanning through the scan.* commands, with the simulator; and the
// guarantee that the Orbbec library is only asked for what we declared.
#include "commands/Command.h"
#include "scan/ObScanApi.h"
#include "scan/ScanBackend.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

using namespace orv;

namespace {

struct ScanFixture : ::testing::Test {
    Document doc;
    CommandRegistry reg;
    ScanFixture() { registerCoreCommands(reg); }
    json run(const std::string& name, const json& in = json::object()) {
        CommandContext ctx{doc, PathPolicy::unrestricted(), "en", "test", nullptr};
        return reg.run(name, in, ctx);
    }
};

}  // namespace

TEST(ScanApi, NeverResolvesNetworkFirmwareOrAiFunctions) {
    for (const char* name : obscan::kResolvedFunctions) {
        const std::string n = name;
        EXPECT_EQ(n.rfind("obscan_", 0), 0u) << n;
        for (const char* bad : obscan::kForbiddenFragments) EXPECT_EQ(n.find(bad), std::string::npos) << n << " contains " << bad;
    }
    EXPECT_STREQ(obscan::statusName(0), "SUCCESS");
    EXPECT_STREQ(obscan::statusName(-5), "UNKNOWN");
    // Errors are coded 0x326201xx, not 1, 2, 3... (constants in the metadata).
    EXPECT_STREQ(obscan::statusName(0x32620109), "LISTENER_NOT_FOUND");
    EXPECT_STREQ(obscan::statusName(0x32620123), "DEVICE_BUSY");
    EXPECT_STREQ(obscan::statusName(1), "UNKNOWN");
}

TEST(ScanApi, StructureLayoutsMatchTheMetadata) {
    // 64-bit layouts the library expects (natural alignment).
    EXPECT_EQ(sizeof(obscan::Point), 16u);
    EXPECT_EQ(sizeof(obscan::PointColorClouds), 32u);
    EXPECT_EQ(sizeof(obscan::TriangleMesh), 40u);
    EXPECT_EQ(sizeof(obscan::ModeConfig), 56u);
    EXPECT_EQ(offsetof(obscan::ModeConfig, voxel_size), 32u);
    EXPECT_EQ(offsetof(obscan::ModeConfig, detail_voxel_size), 48u);
    EXPECT_EQ(sizeof(obscan::OptimizationConfig), 20u);
}

TEST_F(ScanFixture, SimulatedScanGoesIntoTheProject) {
    run("scan.use_simulator", {{"on", true}});
    auto st = run("scan.status");
    EXPECT_EQ(st["backend"], "simulator");
    EXPECT_EQ(st["state"], "disconnected");

    // Order is enforced: no scanning before connecting.
    EXPECT_THROW(run("scan.start"), CommandError);

    EXPECT_EQ(run("scan.devices")["devices"].size(), 1u);
    run("scan.connect", {{"mode", "object_small"}});
    run("scan.preview");
    run("scan.start");
    auto& mgr = ScanManager::instance();
    std::size_t before = 0, after = 0;
    {
        auto lock = mgr.lock();
        before = mgr.backend().cloud(0)->vertexCount();
        after = mgr.backend().cloud(1000)->vertexCount();
    }
    EXPECT_GT(before, 0u);
    EXPECT_LE(after, 1000u);  // thinned for the view
    run("scan.pause");
    run("scan.start");
    run("scan.stop");
    EXPECT_EQ(run("scan.status")["state"], "stopped");

    const auto pts = run("scan.export_to_body", {{"as", "points"}, {"name", "Placa"}});
    const Body b = doc.body(pts["ref"].get<std::string>());
    EXPECT_TRUE(b.mesh->isPointCloud());
    EXPECT_TRUE(b.mesh->hasNormals());
    const Box3 box = b.mesh->bounds();
    EXPECT_NEAR(box.size().x(), 60.0, 0.5);  // millimetres
    EXPECT_NEAR(box.size().z(), 8.0, 0.5);

    const auto mesh = run("scan.export_to_body", {{"as", "mesh"}});
    EXPECT_EQ(mesh["faces"], 12);
    EXPECT_EQ(doc.bodies().size(), 2u);
    doc.undo();
    EXPECT_EQ(doc.bodies().size(), 1u);

    run("scan.disconnect");
    EXPECT_EQ(run("scan.status")["state"], "disconnected");
}

TEST_F(ScanFixture, MissingLibraryIsReportedNotFatal) {
    run("scan.use_simulator", {{"on", false}, {"library_dir", "/nonexistent/CrealityScan"}});
    const auto st = run("scan.status");
    EXPECT_EQ(st["backend"], "obscan");
    EXPECT_FALSE(st["libraryFound"].get<bool>());
    EXPECT_FALSE(st["error"].is_null());
    try {
        run("scan.devices");
        FAIL() << "expected unavailable";
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), "unavailable");
    }
    run("scan.use_simulator", {{"on", true}});
}

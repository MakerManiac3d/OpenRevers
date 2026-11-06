// SPDX-License-Identifier: GPL-3.0-or-later
// The E57 reader, on files written by the reference library (tests/data/e57).
#include "core/mesh/MeshIO.h"
#include "core/points/E57.h"
#include <gtest/gtest.h>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace orv;

namespace {

const std::filesystem::path kData = ORV_TEST_DATA_DIR "/e57";

Vec3d spiral(int i, int n) {
    const double t = (i + 0.5) / n, z = 1 - 2 * t, r = std::sqrt(1 - z * z), ph = i * 2.39996323;
    return {r * std::cos(ph), r * std::sin(ph), z};
}

std::string bytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

}  // namespace

TEST(E57, FloatsWithNormals) {
    E57Summary s;
    const TriMesh m = readE57(bytes(kData / "float_normals.e57"), &s);
    EXPECT_EQ(s.scans, 1u);
    ASSERT_EQ(m.vertices.size(), 8000u);
    ASSERT_TRUE(m.hasNormals());
    for (int i = 0; i < 8000; i += 7) {
        const Vec3d e = spiral(i, 8000);
        EXPECT_NEAR((m.vertices[static_cast<std::size_t>(i)].cast<double>() - e * 50000.0).norm(), 0.0, 0.05) << i;  // mm; float precision
        EXPECT_NEAR((m.normals[static_cast<std::size_t>(i)].cast<double>() - e).norm(), 0.0, 1e-5) << i;
    }
}

TEST(E57, ScaledIntegersPoseAndInvalidPoints) {
    E57Summary s;
    const TriMesh m = readE57(bytes(kData / "scaled_pose.e57"), &s);
    EXPECT_EQ(s.records, 6000u);
    EXPECT_EQ(s.invalid, 600u);
    ASSERT_EQ(m.vertices.size(), 5400u);
    EXPECT_FALSE(m.hasNormals());
    std::size_t k = 0;
    for (int i = 0; i < 6000; ++i) {
        if (i % 10 == 0) continue;
        const Vec3d p = spiral(i, 6000) * 40.0;
        const Vec3d world(100.0 - p.y(), p.x(), p.z());  // 90° about z, then 100 m along x
        EXPECT_NEAR((m.vertices[k++].cast<double>() - world * 1000.0).norm(), 0.0, 0.2) << i;  // 0.1 mm steps
    }
}

TEST(E57, SphericalTwoScans) {
    E57Summary s;
    const TriMesh m = readE57(bytes(kData / "spherical_two_scans.e57"), &s);
    EXPECT_EQ(s.scans, 2u);
    ASSERT_EQ(m.vertices.size(), 6000u);
    for (int scan = 0; scan < 2; ++scan)
        for (int i = 0; i < 3000; i += 11) {
            const Vec3d world = spiral(i, 3000) * 30.0 + Vec3d(0, 200.0 * scan, 0);
            EXPECT_NEAR((m.vertices[static_cast<std::size_t>(scan * 3000 + i)].cast<double>() - world * 1000.0).norm(), 0.0, 1e-2);
        }
}

TEST(E57, ThroughReadMeshAsAPointCloud) {
    const TriMesh m = readMesh(kData / "float_normals.e57");
    EXPECT_TRUE(m.isPointCloud());
    EXPECT_EQ(m.vertices.size(), 8000u);
    EXPECT_THROW(writeMesh(m, std::filesystem::temp_directory_path() / "orv_test_out.e57"), IoError);
}

TEST(E57, DamagedFilesAreRefused) {
    std::string d = bytes(kData / "scaled_pose.e57");
    EXPECT_THROW(readE57("not an e57 file at all, far too short........................"), IoError);
    std::string flipped = d;
    flipped[2000] = static_cast<char>(flipped[2000] ^ 0x40);  // one bit in the data
    try {
        readE57(flipped);
        FAIL();
    } catch (const IoError& e) {
        EXPECT_NE(std::string(e.what()).find("checksum"), std::string::npos);
    }
    EXPECT_THROW(readE57(d.substr(0, d.size() - 1024)), IoError);  // truncated
}

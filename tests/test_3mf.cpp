// SPDX-License-Identifier: GPL-3.0-or-later
// 3MF packages: round trip, units, build transforms and components.
#include "TestMeshes.h"
#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshIO.h"
#include <gtest/gtest.h>
#include <miniz.h>
#include <fstream>

using namespace orv;

namespace {

std::filesystem::path tmpFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("orv_test_" + name);
}

const char* kRels =
    R"(<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
    R"(<Relationship Target="/3D/model.model" Id="r" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>)";

/// A package with this model XML at /3D/model.model (not the usual name: the
/// reader must follow _rels/.rels).
std::filesystem::path package(const std::string& name, const std::string& model) {
    mz_zip_archive z{};
    EXPECT_TRUE(mz_zip_writer_init_heap(&z, 0, 0));
    EXPECT_TRUE(mz_zip_writer_add_mem(&z, "_rels/.rels", kRels, std::strlen(kRels), MZ_DEFAULT_COMPRESSION));
    EXPECT_TRUE(mz_zip_writer_add_mem(&z, "3D/model.model", model.data(), model.size(), MZ_DEFAULT_COMPRESSION));
    void* buf = nullptr;
    std::size_t size = 0;
    EXPECT_TRUE(mz_zip_writer_finalize_heap_archive(&z, &buf, &size));
    const auto p = tmpFile(name);
    std::ofstream(p, std::ios::binary).write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
    mz_zip_writer_end(&z);
    return p;
}

/// A unit cube (0..1) as 3MF object `id`, with the XML for its triangles.
std::string cubeObject(int id) {
    const TriMesh c = test::box(1, 1, 1);
    std::string s = "<object id=\"" + std::to_string(id) + "\" type=\"model\"><mesh><vertices>";
    for (const auto& v : c.vertices)
        s += "<vertex x=\"" + std::to_string(v.x()) + "\" y=\"" + std::to_string(v.y()) + "\" z=\"" + std::to_string(v.z()) + "\"/>";
    s += "</vertices><triangles>";
    for (const auto& t : c.faces)
        s += "<triangle v1=\"" + std::to_string(t[0]) + "\" v2=\"" + std::to_string(t[1]) + "\" v3=\"" + std::to_string(t[2]) + "\"/>";
    return s + "</triangles></mesh></object>";
}

}  // namespace

TEST(ThreeMf, RoundTrip) {
    const TriMesh m = test::sphere(10, 3);
    const auto p = tmpFile("ball.3mf");
    writeMesh(m, p);
    const TriMesh back = readMesh(p);
    EXPECT_EQ(back.faces.size(), m.faces.size());
    EXPECT_EQ(back.vertices.size(), m.vertices.size());
    EXPECT_NEAR(back.signedVolume(), m.signedVolume(), 1e-3);
    std::filesystem::remove(p);
}

TEST(ThreeMf, UnitsAndBuildTransforms) {
    // Inches; the item is scaled by 2 and moved by (10, 0, 0) inches.
    const auto p = package("inch.3mf",
                           R"(<?xml version="1.0"?><model unit="inch" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02"><resources>)" +
                               cubeObject(3) + R"(</resources><build><item objectid="3" transform="2 0 0 0 2 0 0 0 2 10 0 0"/></build></model>)");
    const TriMesh m = readMesh(p);
    const Box3 b = m.bounds();
    EXPECT_NEAR(b.min.x(), 254.f, 1e-3);
    EXPECT_NEAR(b.size().x(), 50.8f, 1e-3);
    EXPECT_NEAR(m.signedVolume(), 50.8 * 50.8 * 50.8, 1.0);
    EXPECT_TRUE(analyze(m).closed);
    std::filesystem::remove(p);
}

TEST(ThreeMf, ComponentsAndSeveralItems) {
    // Object 2 holds object 1 twice (one moved by 5 in z); the build places
    // object 2 once and object 1 once more, rotated 90° about z.
    const auto p = package("comp.3mf",
                           R"(<?xml version="1.0"?><model unit="millimeter" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02"><resources>)" +
                               cubeObject(1) +
                               R"(<object id="2" type="model"><components><component objectid="1"/><component objectid="1" transform="1 0 0 0 1 0 0 0 1 0 0 5"/></components></object>)"
                               R"(</resources><build><item objectid="2"/><item objectid="1" transform="0 1 0 -1 0 0 0 0 1 20 0 0"/></build></model>)");
    const TriMesh m = readMesh(p);
    EXPECT_EQ(m.faces.size(), 36u);
    EXPECT_EQ(analyze(m).components, 3u);
    EXPECT_NEAR(m.signedVolume(), 3.0, 1e-4);
    EXPECT_NEAR(m.bounds().max.z(), 6.f, 1e-5);
    EXPECT_NEAR(m.bounds().min.x(), 0.f, 1e-5);
    EXPECT_NEAR(m.bounds().max.x(), 20.f, 1e-5);  // rotated: x' = x*0 + y*(-1) + 20
    std::filesystem::remove(p);
}

TEST(ThreeMf, DamagedPackagesAreRefused) {
    const auto p = tmpFile("bad.3mf");
    std::ofstream(p) << "not a zip";
    EXPECT_THROW(readMesh(p), IoError);
    const auto q = package("cycle.3mf",
                           R"(<model><resources><object id="1"><components><component objectid="1"/></components></object></resources><build><item objectid="1"/></build></model>)");
    EXPECT_THROW(readMesh(q), IoError);
    const auto r = package("missing.3mf", R"(<model><resources></resources><build><item objectid="7"/></build></model>)");
    EXPECT_THROW(readMesh(r), IoError);
    for (const auto& f : {p, q, r}) std::filesystem::remove(f);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Segmentation into primitives on scan-like meshes.
#include "TestMeshes.h"
#include "core/fit/Segmentation.h"
#include "core/mesh/MeshRepair.h"
#include "core/points/PointCloud.h"
#include <gtest/gtest.h>
#include <cmath>
#include <functional>
#include <map>
#include <random>

using namespace orv;

namespace {

constexpr double kPi = 3.14159265358979323846;

void addNoise(TriMesh& m, float sigma, unsigned seed) {
    std::mt19937 g(seed);
    std::normal_distribution<float> e(0.f, sigma);
    const auto n = m.vertexNormals();
    for (std::size_t i = 0; i < m.vertices.size(); ++i) m.vertices[i] += n[i] * e(g);
}

/// A closed grid surface (u around, v along) with fan caps, then remeshed so
/// that the triangles look like a scan's.
TriMesh revolve(const std::function<Vec3f(float u, float v)>& f, int nu, int nv, bool caps) {
    TriMesh m;
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i < nu; ++i) m.vertices.push_back(f(static_cast<float>(i) / nu, static_cast<float>(j) / nv));
    auto id = [nu](int i, int j) { return static_cast<std::uint32_t>(j * nu + (i % nu)); };
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            m.faces.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1)});
            m.faces.push_back({id(i, j), id(i + 1, j + 1), id(i, j + 1)});
        }
    if (caps) {
        Vec3f c0 = Vec3f::Zero(), c1 = Vec3f::Zero();
        for (int i = 0; i < nu; ++i) { c0 += m.vertices[id(i, 0)]; c1 += m.vertices[id(i, nv)]; }
        const auto b = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back(c0 / static_cast<float>(nu));
        m.vertices.push_back(c1 / static_cast<float>(nu));
        for (int i = 0; i < nu; ++i) {
            m.faces.push_back({b, id(i + 1, 0), id(i, 0)});
            m.faces.push_back({b + 1, id(i, nv), id(i + 1, nv)});
        }
    }
    orientConsistently(m);
    if (m.signedVolume() < 0) flipNormals(m);
    return m;
}

std::map<PrimitiveType, int> countTypes(const Segmentation& s, double minAreaFrac) {
    double total = 0;
    for (const auto& r : s.regions) total += r.area;
    std::map<PrimitiveType, int> c;
    for (const auto& r : s.regions)
        if (r.area >= minAreaFrac * total) ++c[r.shape.type];
    return c;
}

}  // namespace

TEST(Segment, BoxIsSixPlanes) {
    TriMesh m = test::box(40, 30, 20);
    remeshUniform(m, 1.2f, 6);
    addNoise(m, 0.02f, 1);
    const Segmentation s = segment(m);
    EXPECT_NEAR(s.noise, 0.02, 0.012);
    const auto c = countTypes(s, 0.02);
    EXPECT_EQ(c.size(), 1u);
    EXPECT_EQ(c.at(PrimitiveType::Plane), 6);
    EXPECT_LT(static_cast<double>(s.unassignedFaces) / static_cast<double>(m.faces.size()), 0.15);
    for (const auto& r : s.regions) EXPECT_LT(r.shape.rms, 0.04);
}

TEST(Segment, CylinderWithCaps) {
    TriMesh m = revolve([](float u, float v) { return Vec3f(10 * std::cos(6.2831853f * u), 10 * std::sin(6.2831853f * u), 30 * v); }, 96, 30, true);
    remeshUniform(m, 1.0f, 6);
    addNoise(m, 0.02f, 2);
    const Segmentation s = segment(m);
    const auto c = countTypes(s, 0.05);
    EXPECT_EQ(c.at(PrimitiveType::Plane), 2);
    ASSERT_EQ(c.at(PrimitiveType::Cylinder), 1);
    for (const auto& r : s.regions)
        if (r.shape.type == PrimitiveType::Cylinder) {
            EXPECT_NEAR(r.shape.radius, 10, 0.05);
            EXPECT_GT(std::abs(r.shape.axis.z()), 0.9999);
        }
}

TEST(Segment, ConeFrustumAndTorusAndSphere) {
    {
        TriMesh m = revolve([](float u, float v) { const float r = 12 - 6 * v; return Vec3f(r * std::cos(6.2831853f * u), r * std::sin(6.2831853f * u), 20 * v); }, 96, 30, true);
        remeshUniform(m, 0.9f, 6);
        addNoise(m, 0.02f, 3);
        const Segmentation s = segment(m);
        const auto c = countTypes(s, 0.05);
        ASSERT_EQ(c.count(PrimitiveType::Cone), 1u);
        EXPECT_EQ(c.at(PrimitiveType::Cone), 1);
        EXPECT_EQ(c.at(PrimitiveType::Plane), 2);
    }
    {
        TriMesh m = revolve([](float u, float v) {
            const float a = 6.2831853f * u, b = 6.2831853f * v;
            return Vec3f((30 + 6 * std::cos(b)) * std::cos(a), (30 + 6 * std::cos(b)) * std::sin(a), 6 * std::sin(b));
        }, 120, 40, false);
        // The grid repeats its first row at v = 1: weld it into a closed ring.
        weldVertices(m, 1e-3f);
        remeshUniform(m, 1.2f, 6);
        addNoise(m, 0.02f, 4);
        const Segmentation s = segment(m);
        const auto c = countTypes(s, 0.05);
        ASSERT_EQ(c.count(PrimitiveType::Torus), 1u);
        ASSERT_EQ(c.at(PrimitiveType::Torus), 1);
        EXPECT_NEAR(s.regions[0].shape.radius, 30, 0.1);
        EXPECT_NEAR(s.regions[0].shape.radius2, 6, 0.05);
    }
    {
        TriMesh m = test::sphere(15, 5);
        addNoise(m, 0.02f, 5);
        const Segmentation s = segment(m);
        ASSERT_GE(s.regions.size(), 1u);
        EXPECT_EQ(s.regions[0].shape.type, PrimitiveType::Sphere);
        EXPECT_NEAR(s.regions[0].shape.radius, 15, 0.05);
    }
}

TEST(Segment, ScannedBlockWithAHole) {
    // Points on a 60×40×20 block with a Ø16 through hole, rebuilt by Poisson
    // as a scanner's software would: rounded edges, triangles of no pattern.
    TriMesh cloud;
    std::mt19937 g(9);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    std::normal_distribution<float> e(0.f, 0.02f);
    auto add = [&](Vec3f p, Vec3f n) { cloud.vertices.push_back(p + n * e(g)); cloud.normals.push_back(n); };
    auto inHole = [](float x, float y) { return std::hypot(x - 30.f, y - 20.f) < 8.f; };
    for (int i = 0; i < 60000; ++i) {
        const float x = 60 * u(g), y = 40 * u(g);
        if (!inHole(x, y)) { add({x, y, 20}, {0, 0, 1}); add({x, y, 0}, {0, 0, -1}); }
    }
    for (int i = 0; i < 20000; ++i) {
        const float a = 60 * u(g), b = 40 * u(g), z = 20 * u(g);
        add({a, 0, z}, {0, -1, 0}); add({a, 40, z}, {0, 1, 0});
        add({0, b, z}, {-1, 0, 0}); add({60, b, z}, {1, 0, 0});
    }
    for (int i = 0; i < 20000; ++i) {
        const float t = 6.2831853f * u(g), z = 20 * u(g);
        add({30 + 8 * std::cos(t), 20 + 8 * std::sin(t), z}, {-std::cos(t), -std::sin(t), 0});
    }
    TriMesh m;
    PoissonOptions po;
    po.depth = 8;
    reconstructPoisson(cloud, po, m);
    const Segmentation s = segment(m);
    const auto c = countTypes(s, 0.01);
    EXPECT_EQ(c.at(PrimitiveType::Plane), 6);  // top and bottom each one plane round the hole
    ASSERT_EQ(c.at(PrimitiveType::Cylinder), 1);
    for (const auto& r : s.regions)
        if (r.shape.type == PrimitiveType::Cylinder && r.area > 100) {
            EXPECT_NEAR(2 * r.shape.radius, 16.0, 0.1);  // the hole's diameter
            EXPECT_GT(std::abs(r.shape.axis.z()), 0.999);
            const Vec3d d = r.shape.origin - Vec3d(30, 20, 0);
            EXPECT_LT((d - d.dot(r.shape.axis) * r.shape.axis).norm(), 0.05);  // its position
        }
}

TEST(Segment, SmoothedScanOfAThinPlateWithSmallHoles) {
    // What a scanner's software delivers on a small part: a 30×12×2.5 plate
    // with four Ø2.75 holes 4 mm apart, its points rippled by a smooth wave of
    // a few hundredths (not by white noise: the software has smoothed that
    // away) and rebuilt finely. The holes are shallower than they are wide.
    TriMesh cloud;
    std::mt19937 g(21);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    std::normal_distribution<float> e(0.f, 0.002f);
    auto ripple = [](Vec3f p) {
        return 0.012f * std::sin(7.1f * p.x() + 2.3f * p.y() + 1.1f * p.z()) + 0.009f * std::sin(3.3f * p.y() - 5.2f * p.x() + 0.7f) +
               0.006f * std::sin(11.f * p.x() + 9.f * p.z() + 2.f);
    };
    auto add = [&](Vec3f p, Vec3f n) { cloud.vertices.push_back(p + n * (ripple(p) + e(g))); cloud.normals.push_back(n); };
    const float hx[4] = {9, 13, 17, 21}, hy = 6, r = 1.375f, T = 2.5f;
    auto inHole = [&](float x, float y) {
        for (float c : hx)
            if (std::hypot(x - c, y - hy) < r) return true;
        return false;
    };
    for (int i = 0; i < 120000; ++i) {
        const float x = 30 * u(g), y = 12 * u(g);
        if (!inHole(x, y)) { add({x, y, T}, {0, 0, 1}); add({x, y, 0}, {0, 0, -1}); }
    }
    for (int i = 0; i < 25000; ++i) {
        const float a = 30 * u(g), b = 12 * u(g), z = T * u(g);
        add({a, 0, z}, {0, -1, 0}); add({a, 12, z}, {0, 1, 0});
        add({0, b, z}, {-1, 0, 0}); add({30, b, z}, {1, 0, 0});
    }
    for (float c : hx)
        for (int i = 0; i < 6000; ++i) {
            const float t = 6.2831853f * u(g), z = T * u(g);
            add({c + r * std::cos(t), hy + r * std::sin(t), z}, {-std::cos(t), -std::sin(t), 0});
        }
    TriMesh m;
    PoissonOptions po;
    po.depth = 9;
    reconstructPoisson(cloud, po, m);
    const Segmentation s = segment(m);
    EXPECT_GT(s.noise, 0.006);  // the ripple, not the smoothed-away noise
    const auto c = countTypes(s, 0.01);
    EXPECT_LE(c.count(PrimitiveType::Plane) ? c.at(PrimitiveType::Plane) : 0, 8);
    int holes = 0;
    for (const auto& reg : s.regions) {
        if (reg.shape.type != PrimitiveType::Cylinder || reg.shape.radius > 2) continue;
        ++holes;
        EXPECT_NEAR(2 * reg.shape.radius, 2.75, 0.06);
        EXPECT_GT(std::abs(reg.shape.axis.z()), 0.995);
    }
    EXPECT_EQ(holes, 4);
    EXPECT_LT(s.regions.size(), 30u);
}

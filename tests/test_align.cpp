// SPDX-License-Identifier: GPL-3.0-or-later
// Alignment: principal axes, 3-2-1 datums, best fit.
#include "TestMeshes.h"
#include "core/fit/Align.h"
#include "core/mesh/MeshRepair.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>

using namespace orv;

namespace {

constexpr double kPi = 3.14159265358979323846;

Transform somePose(double deg, Vec3d axis, Vec3d move) {
    Transform t = Transform::Identity();
    t.rotate(Eigen::AngleAxisd(deg * kPi / 180, axis.normalized()));
    t.pretranslate(move);
    return t;
}

/// A box with a ball stuck on one corner: no symmetry, so a pose is unique.
TriMesh lumpyBox() {
    TriMesh m = test::box(40, 30, 20);
    TriMesh s = test::sphere(6, 3);
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (auto v : s.vertices) m.vertices.push_back(v + Vec3f(36, 26, 18));
    for (auto t : s.faces) m.faces.push_back({t[0] + base, t[1] + base, t[2] + base});
    remeshUniform(m, 1.5f, 4);
    return m;
}

double angleDeg(const Eigen::Matrix3d& R) { return Eigen::AngleAxisd(R).angle() * 180 / kPi; }

}  // namespace

TEST(Align, PrincipalAxes) {
    TriMesh m = test::box(40, 30, 20);
    remeshUniform(m, 1.5f, 3);
    transformMesh(m, somePose(37, {1, 2, 3}, {50, -20, 7}));
    const Transform t = principalAlignment(m);
    transformMesh(m, t);
    const Box3 b = m.bounds();
    EXPECT_NEAR(b.size().x(), 40, 0.05);
    EXPECT_NEAR(b.size().y(), 30, 0.05);
    EXPECT_NEAR(b.size().z(), 20, 0.05);
    EXPECT_LT(b.center().norm(), 0.05);
    EXPECT_NEAR(t.linear().determinant(), 1.0, 1e-9);
}

TEST(Align, ThreeTwoOneFromPlanes) {
    const Transform pose = somePose(25, {3, -1, 2}, {10, 20, 30});
    auto plane = [&](Vec3d n, Vec3d p) {
        Primitive x;
        x.type = PrimitiveType::Plane;
        x.axis = pose.linear() * n;
        x.origin = pose * p;
        return x;
    };
    // The box's bottom (z = 0), left side (x = 0) and front (y = 0), as found on the scan.
    const auto t = datumAlignment(plane({0, 0, -1}, {5, 5, 0}), plane({-1, 0, 0}, {0, 7, 3}), plane({0, -1, 0}, {9, 0, 4}));
    ASSERT_TRUE(t);
    // The corner (0,0,0) of the box goes to the origin…
    EXPECT_LT((*t * (pose * Vec3d(0, 0, 0))).norm(), 1e-6);
    // …and the box sits in the positive octant, its edges on the axes.
    const Vec3d far = *t * (pose * Vec3d(40, 0, 0));
    EXPECT_NEAR(far.x(), 40, 1e-6);
    const Vec3d top = *t * (pose * Vec3d(40, 30, 20));
    EXPECT_LT((top - Vec3d(40, 30, 20)).norm(), 1e-6);
    EXPECT_NEAR(far.y(), 0, 1e-6);
    EXPECT_NEAR(far.z(), 0, 1e-6);
}

TEST(Align, ThreeTwoOneFromAFaceAndTwoHoles) {
    const Transform pose = somePose(70, {0, 0, 1}, {100, 0, 0});
    Primitive top, h1, h2;
    top.type = PrimitiveType::Plane;
    top.axis = pose.linear() * Vec3d::UnitZ();
    top.origin = pose * Vec3d(0, 0, 20);
    h1.type = h2.type = PrimitiveType::Cylinder;
    h1.axis = h2.axis = pose.linear() * Vec3d::UnitZ();
    h1.origin = pose * Vec3d(10, 10, 5);
    h2.origin = pose * Vec3d(50, 10, 5);
    h1.radius = h2.radius = 4;
    const auto t = datumAlignment(top, h1, h2);
    ASSERT_TRUE(t);
    // Origin on the first hole's axis, in the top face; X towards the second hole.
    EXPECT_LT((*t * (pose * Vec3d(10, 10, 20))).norm(), 1e-6);
    const Vec3d second = *t * (pose * Vec3d(50, 10, 20));
    EXPECT_NEAR(second.x(), 40, 1e-6);
    EXPECT_NEAR(second.y(), 0, 1e-6);
    std::string why;
    EXPECT_FALSE(datumAlignment(top, std::nullopt, std::nullopt, &why));
    EXPECT_FALSE(why.empty());
}

TEST(Align, BestFitRecoversAPose) {
    const TriMesh ref = lumpyBox();
    TriMesh moving = ref;
    std::mt19937 g(3);
    std::normal_distribution<float> e(0.f, 0.02f);
    const auto n = moving.vertexNormals();
    for (std::size_t i = 0; i < moving.vertices.size(); ++i) moving.vertices[i] += n[i] * e(g);  // another scan of the same part
    const Transform pose = somePose(6, {1, 1, 0}, {2.5, -1.5, 1});
    transformMesh(moving, pose);
    const BestFitResult r = bestFit(moving, ref);
    const Transform err = r.transform * pose;  // should be the identity
    EXPECT_LT(angleDeg(err.linear()), 0.01);
    EXPECT_LT(err.translation().norm(), 0.01);
    EXPECT_NEAR(r.rms, 0.02, 0.008);
    EXPECT_GT(r.overlap, 0.95);
}

TEST(Align, BestFitFromFarAwayWithCoarseStart) {
    const TriMesh ref = lumpyBox();
    TriMesh moving = ref;
    const Transform pose = somePose(120, {1, -2, 0.5}, {300, 80, -40});
    transformMesh(moving, pose);
    BestFitOptions o;
    o.coarseStart = true;
    const BestFitResult r = bestFit(moving, ref, o);
    const Transform err = r.transform * pose;
    EXPECT_LT(angleDeg(err.linear()), 0.05);
    EXPECT_LT(err.translation().norm(), 0.05);
}

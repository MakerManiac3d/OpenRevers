// SPDX-License-Identifier: GPL-3.0-or-later
// Primitive fitting on synthetic scans: partial patches, scanner noise,
// outliers and an arbitrary position, as a real part would give.
#include "core/fit/Primitive.h"
#include <gtest/gtest.h>
#include <Eigen/Geometry>
#include <cmath>
#include <random>

using namespace orv;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Patch {
    std::vector<Vec3d> p, n;
};

/// Place a patch made in a canonical frame (axis z) somewhere else, add
/// noise along the normal and a few outliers.
Patch finish(Patch c, double noise, double outlierFrac, unsigned seed, Eigen::Isometry3d& T) {
    std::mt19937 g(seed);
    std::normal_distribution<double> e(0, noise);
    std::uniform_real_distribution<double> u(-1, 1);
    T = Eigen::Isometry3d::Identity();
    T.rotate(Eigen::AngleAxisd(0.7, Vec3d(1, 2, 3).normalized()));
    T.translation() = Vec3d(120, -40, 75);
    Patch out;
    for (std::size_t i = 0; i < c.p.size(); ++i) {
        Vec3d p = c.p[i] + c.n[i] * e(g);
        if (u(g) * 0.5 + 0.5 < outlierFrac) p += c.n[i] * (2.0 + 3.0 * std::abs(u(g)));  // dust, reflections
        out.p.push_back(T * p);
        out.n.push_back(T.linear() * c.n[i]);
    }
    return out;
}

Patch planePatch(int n) {
    Patch c;
    std::mt19937 g(1);
    std::uniform_real_distribution<double> u(-20, 20);
    for (int i = 0; i < n; ++i) { c.p.emplace_back(u(g), u(g) * 0.5, 0); c.n.emplace_back(0, 0, 1); }
    return c;
}
Patch spherePatch(double r, int n) {  // a cap of 60° half angle
    Patch c;
    std::mt19937 g(2);
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < n; ++i) {
        const double z = std::cos(60 * kPi / 180) + (1 - std::cos(60 * kPi / 180)) * u(g), ph = 2 * kPi * u(g), s = std::sqrt(1 - z * z);
        const Vec3d d(s * std::cos(ph), s * std::sin(ph), z);
        c.p.push_back(d * r); c.n.push_back(d);
    }
    return c;
}
Patch cylinderPatch(double r, double arcDeg, int n) {
    Patch c;
    std::mt19937 g(3);
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < n; ++i) {
        const double a = arcDeg * kPi / 180 * u(g), h = 30 * u(g);
        const Vec3d d(std::cos(a), std::sin(a), 0);
        c.p.push_back(d * r + Vec3d(0, 0, h)); c.n.push_back(d);
    }
    return c;
}
Patch conePatch(double halfAngleDeg, int n) {  // apex at 0, axis +z, from h=10 to 40
    Patch c;
    std::mt19937 g(4);
    std::uniform_real_distribution<double> u(0, 1);
    const double t = halfAngleDeg * kPi / 180;
    for (int i = 0; i < n; ++i) {
        const double h = 10 + 30 * u(g), a = 1.5 * kPi * u(g);
        const Vec3d radial(std::cos(a), std::sin(a), 0);
        c.p.push_back(radial * h * std::tan(t) + Vec3d(0, 0, h));
        c.n.push_back((radial * std::cos(t) - Vec3d::UnitZ() * std::sin(t)).normalized());
    }
    return c;
}
Patch torusPatch(double R, double r, int n) {  // a quarter of the ring, outer half of the tube
    Patch c;
    std::mt19937 g(5);
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < n; ++i) {
        const double a = 0.5 * kPi * u(g), b = kPi * (u(g) - 0.5);
        const Vec3d radial(std::cos(a), std::sin(a), 0);
        const Vec3d nn = radial * std::cos(b) + Vec3d::UnitZ() * std::sin(b);
        c.p.push_back(radial * R + nn * r); c.n.push_back(nn);
    }
    return c;
}

double axisAngleDeg(const Vec3d& a, const Vec3d& b) { return std::acos(std::min(1.0, std::abs(a.normalized().dot(b.normalized())))) * 180 / kPi; }

}  // namespace

TEST(Fit, Plane) {
    Eigen::Isometry3d T;
    const Patch s = finish(planePatch(3000), 0.02, 0.03, 10, T);
    const auto f = fitPrimitive(PrimitiveType::Plane, s.p, s.n);
    ASSERT_TRUE(f);
    EXPECT_LT(axisAngleDeg(f->axis, T.linear() * Vec3d::UnitZ()), 0.05);
    EXPECT_GT(f->axis.dot(T.linear() * Vec3d::UnitZ()), 0);  // faces the way the normals do
    EXPECT_NEAR(f->rms, 0.02, 0.004);
    EXPECT_GT(f->outliers, 50u);  // the dust is left out
}

TEST(Fit, Sphere) {
    Eigen::Isometry3d T;
    const Patch s = finish(spherePatch(25, 3000), 0.02, 0.03, 11, T);
    const auto f = fitPrimitive(PrimitiveType::Sphere, s.p, s.n);
    ASSERT_TRUE(f);
    EXPECT_NEAR(f->radius, 25, 0.02);
    EXPECT_LT((f->origin - T.translation()).norm(), 0.03);
    EXPECT_NEAR(f->rms, 0.02, 0.004);
}

TEST(Fit, CylinderFromAThirdOfItsSide) {
    Eigen::Isometry3d T;
    const Patch s = finish(cylinderPatch(6.0, 120, 4000), 0.02, 0.03, 12, T);  // a Ø12 hole seen from one side
    const auto f = fitPrimitive(PrimitiveType::Cylinder, s.p, s.n);
    ASSERT_TRUE(f);
    EXPECT_NEAR(f->radius, 6.0, 0.02);
    EXPECT_LT(axisAngleDeg(f->axis, T.linear() * Vec3d::UnitZ()), 0.2);
    // The axis passes through the true one.
    const Vec3d d = f->origin - T.translation();
    EXPECT_LT((d - d.dot(f->axis) * f->axis).norm(), 0.03);
    EXPECT_NEAR(f->rms, 0.02, 0.004);
}

TEST(Fit, Cone) {
    Eigen::Isometry3d T;
    const Patch s = finish(conePatch(30, 4000), 0.02, 0.0, 13, T);
    const auto f = fitPrimitive(PrimitiveType::Cone, s.p, s.n);
    ASSERT_TRUE(f);
    EXPECT_NEAR(f->angle * 180 / kPi, 30, 0.1);
    EXPECT_LT((f->origin - T.translation()).norm(), 0.1);
    EXPECT_GT(f->axis.dot(T.linear() * Vec3d::UnitZ()), 0.999);
    EXPECT_NEAR(f->rms, 0.02, 0.004);
}

TEST(Fit, Torus) {
    Eigen::Isometry3d T;
    const Patch s = finish(torusPatch(30, 5, 5000), 0.02, 0.0, 14, T);  // a bent pipe
    const auto f = fitPrimitive(PrimitiveType::Torus, s.p, s.n);
    ASSERT_TRUE(f);
    EXPECT_NEAR(f->radius, 30, 0.05);
    EXPECT_NEAR(f->radius2, 5, 0.02);
    EXPECT_LT(axisAngleDeg(f->axis, T.linear() * Vec3d::UnitZ()), 0.2);
    EXPECT_NEAR(f->rms, 0.02, 0.004);
}

TEST(Fit, BestTypeIsFound) {
    Eigen::Isometry3d T;
    struct Case { Patch s; PrimitiveType t; };
    const Case cases[] = {
        {finish(planePatch(2000), 0.02, 0.02, 20, T), PrimitiveType::Plane},
        {finish(spherePatch(25, 2000), 0.02, 0.02, 21, T), PrimitiveType::Sphere},
        {finish(cylinderPatch(6, 150, 2000), 0.02, 0.02, 22, T), PrimitiveType::Cylinder},
        {finish(conePatch(20, 2000), 0.02, 0.02, 23, T), PrimitiveType::Cone},
        {finish(torusPatch(30, 5, 3000), 0.02, 0.02, 24, T), PrimitiveType::Torus},
    };
    for (const auto& c : cases) {
        const auto f = fitBest(c.s.p, c.s.n, 0.02);
        ASSERT_TRUE(f) << primitiveName(c.t);
        EXPECT_EQ(f->type, c.t) << "expected " << primitiveName(c.t) << ", got " << primitiveName(f->type);
    }
}

TEST(Fit, DistanceAndNormalAgree) {
    Primitive c;
    c.type = PrimitiveType::Cylinder;
    c.origin = Vec3d(1, 2, 3);
    c.axis = Vec3d::UnitZ();
    c.radius = 5;
    EXPECT_NEAR(c.distance(Vec3d(1 + 7, 2, 50)), 2.0, 1e-12);
    EXPECT_LT((c.normal(Vec3d(1, 2 - 9, 0)) - Vec3d(0, -1, 0)).norm(), 1e-12);
    EXPECT_FALSE(fitPrimitive(PrimitiveType::Cylinder, {Vec3d(0, 0, 0), Vec3d(1, 0, 0), Vec3d(0, 1, 0)}, {}));  // needs normals
}

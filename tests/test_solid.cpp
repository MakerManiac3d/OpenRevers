// SPDX-License-Identifier: GPL-3.0-or-later
// Exact solids: extrusion of sketches with holes, revolution, booleans,
// fillets, STEP and the project's text form.
#include "TestMeshes.h"
#include "core/solid/Solid.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace orv;

namespace {
constexpr double kPi = 3.14159265358979323846;

Loop rectangle(double x0, double y0, double x1, double y1) {
    Loop l;
    l.segments = {Segment::line({x0, y0}, {x1, y0}), Segment::line({x1, y0}, {x1, y1}), Segment::line({x1, y1}, {x0, y1}),
                  Segment::line({x0, y1}, {x0, y0})};
    return l;
}
Loop circle(double x, double y, double r) {
    Loop l;
    l.segments = {Segment::circle({x, y}, r)};
    return l;
}
}  // namespace

TEST(Solid, PlateWithHolesFromASketch) {
    // 30 × 12 × 2.5 with four Ø2.75 holes 4 mm apart, like the lever's end.
    Profile p;
    p.loops.push_back(rectangle(0, 0, 30, 12));
    for (double x : {9.0, 13.0, 17.0, 21.0}) p.loops.push_back(circle(x, 6, 1.375));
    const Solid s = Solid::extrude(p, 0, 2.5);
    ASSERT_FALSE(s.empty());
    EXPECT_EQ(s.checkProblem(), "");
    EXPECT_EQ(s.solidCount(), 1);
    EXPECT_EQ(s.faceCount(), 6 + 4);
    EXPECT_NEAR(s.volume(), 2.5 * (30 * 12 - 4 * kPi * 1.375 * 1.375), 1e-6);
    const auto b = s.bounds();
    EXPECT_NEAR(b.min().z(), 0, 1e-6);
    EXPECT_NEAR(b.max().x(), 30, 1e-6);
    // An island inside a hole is material again.
    Profile q;
    q.loops = {rectangle(0, 0, 20, 20), rectangle(5, 5, 15, 15), circle(10, 10, 2)};
    EXPECT_NEAR(Solid::extrude(q, -1, 1).volume(), 2 * (400 - 100 + kPi * 4), 1e-6);
}

TEST(Solid, ArcsCountersignAndAPlaneAnywhere) {
    // A slot: two lines and two half circles, drawn clockwise, on a tilted plane.
    Profile p;
    p.plane.origin = Vec3d(10, 20, 30);
    p.plane.normal = Vec3d(0, 1, 1).normalized();
    p.plane.xDir = Vec3d(1, 0, 0);
    Loop l;
    l.segments = {Segment::line({0, 2}, {10, 2}), Segment::arc({10, 2}, {10, -2}, {10, 0}, false), Segment::line({10, -2}, {0, -2}),
                  Segment::arc({0, -2}, {0, 2}, {0, 0}, false)};
    p.loops = {l};
    const Solid s = Solid::extrude(p, 0, 3);
    EXPECT_EQ(s.checkProblem(), "");
    EXPECT_NEAR(s.volume(), 3 * (40 + kPi * 4), 1e-6);
    // An open chain is refused with the reason.
    Profile bad = p;
    bad.loops[0].segments[1].b = Vec2d(10, -1.9);
    EXPECT_THROW(Solid::extrude(bad, 0, 1), SolidError);
}

TEST(Solid, RevolveBooleansAndFillet) {
    // A Ø20 × 10 disc by revolution of a rectangle; a Ø6 bore cut through it.
    Profile p;
    p.plane.normal = Vec3d(0, -1, 0);  // the XZ plane, x along X, y along Z
    p.loops = {rectangle(0, 0, 10, 10)};
    const Solid disc = Solid::revolve(p, Vec2d(0, 0), Vec2d(0, 1), 360);
    EXPECT_NEAR(disc.volume(), kPi * 100 * 10, 1e-3);
    Profile hole;
    hole.loops = {circle(0, 0, 3)};
    const Solid bored = disc.cut(Solid::extrude(hole, -1, 11));
    EXPECT_NEAR(bored.volume(), kPi * (100 - 9) * 10, 1e-3);
    // A 1 mm fillet round the top outer edge (picked by a point on it).
    const Solid r = bored.filleted({Vec3d(10, 0, 10)}, 1.0);
    EXPECT_EQ(r.checkProblem(), "");
    const double lost = (1 - kPi / 4) * 1.0 * 2 * kPi * (10 - (1 - 2 / (3 * (4 - kPi))) * 1.0);  // Pappus, near enough
    EXPECT_NEAR(bored.volume() - r.volume(), lost, 0.2);
    EXPECT_THROW(bored.filleted({Vec3d(50, 50, 50)}, 1.0), SolidError);
}

TEST(Solid, StepBrepAndTriangles) {
    Profile p;
    p.loops = {rectangle(0, 0, 40, 30), circle(20, 15, 8)};
    const Solid s = Solid::extrude(p, 0, 20);
    const auto dir = test::tempDir("solid");
    s.writeStep(dir / "block.step", "block");
    const Solid back = Solid::readStep(dir / "block.step");
    EXPECT_NEAR(back.volume(), s.volume(), 1e-3);
    EXPECT_EQ(back.faceCount(), s.faceCount());
    const Solid again = Solid::fromBrep(s.toBrep());
    EXPECT_NEAR(again.volume(), s.volume(), 1e-9);
    // Triangles close to the surfaces, closed, outward.
    const auto t = s.tessellate(0.01);
    EXPECT_EQ(t.faceOf.size(), t.mesh.faces.size());
    EXPECT_NEAR(t.mesh.signedVolume(), s.volume(), 0.01 * s.volume());
    EXPECT_GT(t.mesh.signedVolume(), 0);
    EXPECT_FALSE(occtVersion().empty());
}

#include "core/solid/SketchFit.h"
#include <random>

TEST(SketchFit, OutlineOfLinesArcsAndAHole) {
    // A lever-like outline: sharp corners, a round end of R6, a stepped notch;
    // sampled every 0.05 mm with 0.01 mm of noise, as a section of a scan.
    Loop truth;
    truth.segments = {Segment::line({0, 0}, {40, 0}), Segment::arc({40, 0}, {40, 12}, {40, 6}, true), Segment::line({40, 12}, {30, 12}),
                      Segment::line({30, 12}, {30, 9}), Segment::line({30, 9}, {25, 9}), Segment::line({25, 9}, {25, 12}),
                      Segment::line({25, 12}, {0, 12}), Segment::line({0, 12}, {0, 0})};
    std::mt19937 g(3);
    std::normal_distribution<double> e(0, 0.01);
    auto pts = truth.polygon(0.05);
    for (auto& p : pts) p += Vec2d(e(g), e(g));
    SketchFitOptions o;
    o.tolerance = 0.05;
    const LoopFit f = fitLoop(pts, o);
    ASSERT_FALSE(f.loop.segments.empty());
    EXPECT_LT(f.maxDeviation, 0.06);
    int lines = 0, arcs = 0;
    for (const auto& s : f.loop.segments) {
        if (s.kind == Segment::Kind::Line) ++lines;
        if (s.kind == Segment::Kind::Arc) {
            ++arcs;
            EXPECT_NEAR(s.radius, 6.0, 0.03);
            EXPECT_NEAR((s.centre - Vec2d(40, 6)).norm(), 0.0, 0.03);
        }
    }
    EXPECT_EQ(arcs, 1);
    EXPECT_EQ(lines, 7);
    // Sharp corners where lines meet: the notch's corner at (30, 9).
    double nearest = 1e9;
    for (const auto& s : f.loop.segments) nearest = std::min(nearest, (s.a - Vec2d(30, 9)).norm());
    EXPECT_LT(nearest, 0.03);
    // It closes and extrudes.
    Profile p;
    p.loops = {f.loop};
    EXPECT_EQ(p.problem(), "");
    EXPECT_NEAR(Solid::extrude(p, 0, 1).volume(), std::abs(truth.area()), 0.5);

    // A hole: a whole circle.
    std::vector<Vec2d> hole;
    for (int k = 0; k < 200; ++k) hole.push_back(Vec2d(10, 6) + 1.375 * Vec2d(std::cos(k * 0.0314159), std::sin(k * 0.0314159)) + Vec2d(e(g), e(g)));
    const LoopFit h = fitLoop(hole, o);
    ASSERT_EQ(h.loop.segments.size(), 1u);
    EXPECT_EQ(h.loop.segments[0].kind, Segment::Kind::Circle);
    EXPECT_NEAR(h.loop.segments[0].radius, 1.375, 0.005);
}

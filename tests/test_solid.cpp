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

// A section of the scanned lever at 1.5 mm (cut by OpenRevers 0.4.0, every
// 0.03 mm simplified): 0.4.0 fitted an arc the long way round here, and arcs
// whose ends were off their circles, which the extrusion then refused.
namespace {
const std::vector<Vec2d> kLeverOutline = {
    {6.1238, -3.2512}, {5.8014, -3.4899}, {5.4411, -3.6547}, {4.9994, -3.7814}, {4.4028, -3.8754}, {1.9735, -3.8132},
    {0.8178, -3.6881}, {-1.1452, -3.3168}, {-4.3801, -2.5904}, {-7.3138, -2.009}, {-12.4529, -0.8821}, {-14.6398, -0.4509},
    {-16.1919, -0.0728}, {-20.3932, 0.7553}, {-21.9014, 1.1019}, {-24.8945, 1.688}, {-26.6745, 2.0924}, {-37.4486, 4.321},
    {-38.1762, 4.4228}, {-38.6341, 4.4019}, {-39.0258, 4.2753}, {-39.3071, 4.1104}, {-39.5065, 3.859}, {-39.7343, 3.4001},
    {-39.8317, 3.0849}, {-40.0023, 1.1944}, {-40.1597, 0.2034}, {-40.2451, 0.0536}, {-40.3975, -0.0542}, {-40.5852, -0.0967},
    {-40.9222, -0.0764}, {-41.4182, 0.0504}, {-42.044, 0.2867}, {-42.8873, 0.6818}, {-43.4322, 0.9934}, {-45.7308, 2.4851},
    {-47.4843, 3.7855}, {-47.8863, 4.215}, {-47.9621, 4.4355}, {-47.8987, 5.4316}, {-47.7844, 6.269}, {-47.6749, 6.5815},
    {-47.365, 6.9279}, {-46.6087, 7.1518}, {-45.5663, 7.2194}, {-43.5545, 7.2455}, {-28.6115, 6.8309}, {-27.6587, 6.7652},
    {-23.956, 6.6729}, {-14.8852, 6.3427}, {-10.7045, 6.2429}, {0.2441, 5.8153}, {1.1069, 5.7422}, {1.781, 5.7476},
    {2.8202, 5.8855}, {3.4885, 6.1013}, {4.2623, 6.5246}, {4.974, 7.1217}, {6.4387, 7.6457}, {9.9697, 9.4993},
    {11.9719, 10.3597}, {15.0539, 11.3823}, {17.3012, 11.965}, {18.3579, 12.1694}, {19.6799, 12.3485}, {21.1336, 12.485},
    {23.1464, 12.5695}, {29.2674, 12.5673}, {31.5299, 12.4573}, {32.6842, 12.3129}, {33.7987, 12.0295}, {34.4236, 11.7618},
    {34.7828, 11.437}, {35.0414, 10.9139}, {35.1223, 10.5936}, {35.145, 10.0207}, {35.0251, 8.9332}, {34.8093, 8.1527},
    {34.5368, 7.5274}, {34.4318, 7.4196}, {33.537, 6.9468}, {33.0592, 6.7588}, {32.5967, 6.6102}, {31.8887, 6.4876},
    {28.0068, 6.4453}, {26.3212, 6.4929}, {23.0844, 6.4823}, {21.9119, 6.3578}, {21.5486, 5.9861}, {21.4417, 5.7925},
    {21.3855, 5.5501}, {21.3016, 4.2775}, {21.2177, 4.05}, {20.9982, 3.855}, {20.7698, 3.768}, {20.4162, 3.7323},
    {18.0956, 3.7366}, {17.4941, 3.6622}, {17.301, 3.569}, {16.9492, 3.1636}, {16.83, 2.8599}, {16.8221, 1.7744},
    {16.7259, 1.1811}, {16.5961, 0.9828}, {16.3475, 0.8442}, {16.0677, 0.8066}, {13.1196, 0.7983}, {12.2534, 0.8429},
    {11.9319, 1.0244}, {11.7232, 1.4176}, {11.5765, 3.5126}, {11.5031, 3.6981}, {11.3995, 3.7905}, {10.6911, 3.88},
    {8.6454, 3.8681}, {7.9353, 3.7866}, {7.7491, 3.7261}, {7.3781, 3.4162}, {7.1201, 2.768}, {7.131, 1.6371},
    {6.9979, 0.1699}, {6.9773, -0.9873}, {6.8551, -1.9247}, {6.5844, -2.6851}, {6.4098, -2.9484}};
const std::vector<Vec2d> kLeverPivot = {
    {-0.2377, -1.8774}, {0.0732, -1.9466}, {0.3465, -1.94}, {0.8548, -1.8456}, {1.0829, -1.7485}, {1.3891, -1.4879},
    {1.6441, -1.1804}, {1.8227, -0.8736}, {1.9588, -0.4961}, {1.9545, 0.3489}, {1.7217, 1.0291}, {1.4557, 1.3795},
    {1.238, 1.5755}, {0.8458, 1.8111}, {0.4326, 1.9423}, {-0.4328, 1.9446}, {-0.6936, 1.8689}, {-0.8868, 1.7542},
    {-1.2821, 1.3439}, {-1.5079, 1.0029}, {-1.7471, 0.7462}, {-1.9312, 0.1516}, {-1.9458, -0.3122}, {-1.5895, -1.0253},
    {-0.9002, -1.6173}};
}  // namespace

TEST(SketchFit, ScannedLeverSection) {
    SketchFitOptions o;
    o.tolerance = 0.05;
    const LoopFit f = fitLoop(kLeverOutline, o);
    ASSERT_FALSE(f.loop.segments.empty());
    EXPECT_LT(f.maxDeviation, 0.2);
    for (const auto& s : f.loop.segments) {
        if (s.kind != Segment::Kind::Arc) continue;
        EXPECT_NEAR((s.a - s.centre).norm(), (s.b - s.centre).norm(), 1e-9);
        // No arc sweeps more than a half turn on this outline.
        EXPECT_LT(s.length(), kPi * s.radius + 1e-6);
    }
    // The pivot, out of round by 0.1 mm in the scan, is a circle.
    const LoopFit h = fitLoop(kLeverPivot, o);
    ASSERT_EQ(h.loop.segments.size(), 1u);
    EXPECT_EQ(h.loop.segments[0].kind, Segment::Kind::Circle);
    EXPECT_NEAR(2 * h.loop.segments[0].radius, 3.90, 0.04);  // f7 of the segmentation: Ø3.90
    Profile p;
    p.loops = {f.loop, h.loop};
    ASSERT_EQ(p.problem(), "");
    const Solid s = Solid::extrude(p, 0, 4.6);
    EXPECT_NEAR(s.volume(), 4.6 * (std::abs(f.loop.area()) - std::abs(h.loop.area())), 1.0);
}

TEST(Solid, ArcAfterArcWithRoundedCentres) {
    // Two arcs in a row whose centres were rounded to 6 decimals: their ends
    // are a micron off their circles. 0.4.0 refused to extrude this.
    Loop l;
    l.segments = {Segment::line({0, 0}, {10, 0}),
                  Segment::arc({10, 0}, {12.123457, 2.345679}, {9.999999, 3.123457}, true),
                  Segment::arc({12.123457, 2.345679}, {10, 6}, {13.234568, 4.654321}, false),
                  Segment::line({10, 6}, {0, 6}), Segment::line({0, 6}, {0, 0})};
    for (auto& s : l.segments)
        if (s.kind == Segment::Kind::Arc) {  // put the ends on the circle to 1e-6, as part.edit_sketch accepts
            const Vec2d m = 0.5 * (s.a + s.b), d = (s.b - s.a).normalized(), x(-d.y(), d.x());
            s.centre = m + x * (s.centre - m).dot(x);
            s.centre = {std::round(s.centre.x() * 1e6) / 1e6, std::round(s.centre.y() * 1e6) / 1e6};
            s.radius = (s.a - s.centre).norm();
        }
    Profile p;
    p.loops = {l};
    ASSERT_EQ(p.problem(), "");
    EXPECT_NO_THROW(Solid::extrude(p, 0, 2));
}

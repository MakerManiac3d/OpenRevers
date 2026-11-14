// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch constraints solved with PlaneGCS.
#include "core/solid/SketchSolve.h"

#include <gtest/gtest.h>

using namespace orv;

namespace {
using P = SketchRef::Part;
SketchRef edge(int s) { return {0, s, P::Edge}; }
SketchRef start(int s) { return {0, s, P::Start}; }

/// A rough quadrilateral, drawn by hand: nothing square, nothing exact.
Profile roughQuad() {
    Profile p;
    Loop l;
    const Vec2d a(0.3, -0.2), b(31.0, 1.1), c(29.4, 12.6), d(-0.8, 11.7);
    l.segments = {Segment::line(a, b), Segment::line(b, c), Segment::line(c, d), Segment::line(d, a)};
    p.loops.push_back(l);
    return p;
}

SketchConstraint con(ConstraintKind k, std::vector<SketchRef> refs, double v = 0) {
    SketchConstraint c;
    c.kind = k;
    c.refs = std::move(refs);
    c.value = v;
    return c;
}
}  // namespace

TEST(SketchSolve, RoughQuadBecomesAnExactRectangle) {
    Profile p = roughQuad();
    std::vector<SketchConstraint> cs = {
        con(ConstraintKind::Fix, {start(0)}),
        con(ConstraintKind::Horizontal, {edge(0)}),
        con(ConstraintKind::Horizontal, {edge(2)}),
        con(ConstraintKind::Vertical, {edge(1)}),
        con(ConstraintKind::Vertical, {edge(3)}),
        con(ConstraintKind::Distance, {edge(0)}, 30.0),
        con(ConstraintKind::Distance, {edge(1)}, 12.0),
    };
    const auto r = solveSketch(p, cs);
    ASSERT_TRUE(r.solved) << r.message;
    EXPECT_EQ(r.dof, 0);
    const auto& s = p.loops[0].segments;
    EXPECT_NEAR((s[0].b - s[0].a).norm(), 30.0, 1e-6);
    EXPECT_NEAR(s[0].a.y(), s[0].b.y(), 1e-9);
    EXPECT_NEAR(s[1].a.x(), s[1].b.x(), 1e-9);
    EXPECT_NEAR((s[1].b - s[1].a).norm(), 12.0, 1e-6);
    EXPECT_NEAR(s[0].a.x(), 0.3, 1e-9);  // the fixed corner stayed
    EXPECT_NEAR(std::abs(p.loops[0].area()), 360.0, 1e-4);
    // The loop is still closed: each segment starts where the last one ends.
    for (std::size_t k = 0; k < 4; ++k) EXPECT_LT((s[k].b - s[(k + 1) % 4].a).norm(), 1e-9);
    EXPECT_TRUE(p.problem().empty());
}

TEST(SketchSolve, CountsFreedomAndFindsConflicts) {
    Profile p = roughQuad();
    // Only horizontal/vertical: still free to move and stretch.
    std::vector<SketchConstraint> cs = {con(ConstraintKind::Horizontal, {edge(0)}), con(ConstraintKind::Vertical, {edge(1)})};
    auto r = solveSketch(p, cs);
    ASSERT_TRUE(r.solved);
    EXPECT_EQ(r.dof, 6);  // 8 coordinates minus 2
    // A line both horizontal and vertical with a length: impossible.
    Profile q = roughQuad();
    std::vector<SketchConstraint> bad = {con(ConstraintKind::Horizontal, {edge(0)}), con(ConstraintKind::Vertical, {edge(0)}),
                                         con(ConstraintKind::Distance, {edge(0)}, 10.0)};
    r = solveSketch(q, bad);
    EXPECT_FALSE(r.solved);
    EXPECT_FALSE(r.conflicting.empty());
    EXPECT_EQ(q.loops[0].segments[0].a, roughQuad().loops[0].segments[0].a);  // left as it was
}

TEST(SketchSolve, ArcTangentToLinesAndRadius) {
    // A slot end: line, arc, line, line, the arc tangent to both long lines.
    Profile p;
    Loop l;
    l.segments = {Segment::line({0, 0}, {20, 0.4}), Segment::arc({20, 0.4}, {19.6, 10.2}, {21, 5}, true), Segment::line({19.6, 10.2}, {0, 10}),
                  Segment::line({0, 10}, {0, 0})};
    p.loops.push_back(l);
    std::vector<SketchConstraint> cs = {
        con(ConstraintKind::Horizontal, {edge(0)}), con(ConstraintKind::Horizontal, {edge(2)}), con(ConstraintKind::Tangent, {edge(0), edge(1)}),
        con(ConstraintKind::Tangent, {edge(2), edge(1)}), con(ConstraintKind::Radius, {edge(1)}, 5.0),
    };
    const auto r = solveSketch(p, cs);
    ASSERT_TRUE(r.solved) << r.message;
    const auto& s = p.loops[0].segments;
    EXPECT_NEAR(s[1].radius, 5.0, 1e-6);
    EXPECT_NEAR(std::abs(s[0].a.y() - s[2].a.y()), 10.0, 1e-5);  // the slot is 2 × radius wide
    EXPECT_TRUE(p.problem(1e-6).empty()) << p.problem(1e-6);
}

TEST(SketchSolve, DraggingFollowsWhatTheConstraintsAllow) {
    Profile p = roughQuad();
    std::vector<SketchConstraint> cs = {con(ConstraintKind::Horizontal, {edge(0)}), con(ConstraintKind::Vertical, {edge(1)})};
    SketchSolveOptions o;
    o.drag = {{start(2), Vec2d(40, 20)}};  // corner c
    const auto r = solveSketch(p, cs, o);
    ASSERT_TRUE(r.solved);
    EXPECT_NEAR(p.loops[0].segments[2].a.x(), 40, 1e-6);
    EXPECT_NEAR(p.loops[0].segments[2].a.y(), 20, 1e-6);
    // The vertical line follows the dragged corner.
    EXPECT_NEAR(p.loops[0].segments[1].a.x(), 40, 1e-6);
}

TEST(SketchSolve, MeasuresDimensions) {
    Profile p = roughQuad();
    EXPECT_NEAR(*measureConstraint(p, con(ConstraintKind::Distance, {edge(0)})), (Vec2d(31.0, 1.1) - Vec2d(0.3, -0.2)).norm(), 1e-9);
    EXPECT_FALSE(measureConstraint(p, con(ConstraintKind::Parallel, {edge(0), edge(2)})).has_value());
    EXPECT_EQ(constraintKindFromName("perpendicular"), ConstraintKind::Perpendicular);
}

TEST(SketchSolve, DimensionsFromTheOriginAndAxes) {
    Profile p = roughQuad();
    const SketchRef origin{-1, 0, P::Start}, xAxis{-1, 0, P::Edge}, yAxis{-1, 1, P::Edge};
    std::vector<SketchConstraint> cs = {
        con(ConstraintKind::Coincident, {start(0), origin}),
        con(ConstraintKind::Horizontal, {edge(0)}),
        con(ConstraintKind::Horizontal, {edge(2)}),
        con(ConstraintKind::Vertical, {edge(1)}),
        con(ConstraintKind::Vertical, {edge(3)}),
        con(ConstraintKind::Distance, {edge(0)}, 30.0),
        con(ConstraintKind::Distance, {{0, 2, P::Start}, xAxis}, 12.0),  // the top edge is 12 above the x axis
    };
    const auto r = solveSketch(p, cs);
    ASSERT_TRUE(r.solved) << r.message;
    EXPECT_EQ(r.dof, 0);
    EXPECT_NEAR(p.loops[0].segments[0].a.x(), 0.0, 1e-9);
    EXPECT_NEAR(p.loops[0].segments[0].a.y(), 0.0, 1e-9);
    EXPECT_NEAR(p.loops[0].segments[2].a.y(), 12.0, 1e-6);
    (void)yAxis;
    // A point on the y axis, and the measured distance to it.
    cs = {con(ConstraintKind::Coincident, {start(0), origin}), con(ConstraintKind::Horizontal, {edge(0)}), con(ConstraintKind::Distance, {edge(0)}, 30.0),
          con(ConstraintKind::OnObject, {{0, 1, P::End}, yAxis})};
    Profile q = roughQuad();
    ASSERT_TRUE(solveSketch(q, cs).solved);
    EXPECT_NEAR(q.loops[0].segments[1].b.x(), 0.0, 1e-6);
}

TEST(SketchSolve, ConstructionLineAndAnglesInEitherDirection) {
    // A triangle with an angle given as 135 or as 45 gives the one the sketch is nearer to.
    for (double want : {45.0, 135.0}) {
        Profile p;
        Loop l;
        const Vec2d a(0, 0), b(20, 0), c(20, 20);
        l.segments = {Segment::line(a, b), Segment::line(b, c), Segment::line(c, a)};
        p.loops.push_back(l);
        // A construction line: an open chain, from the origin out to the right.
        Loop k;
        k.construction = true;
        k.segments = {Segment::line({0, 0}, {10, 3})};
        p.loops.push_back(k);
        std::vector<SketchConstraint> cs = {
            con(ConstraintKind::Fix, {start(0)}),
            con(ConstraintKind::Horizontal, {edge(0)}),
            con(ConstraintKind::Angle, {{0, 0, P::Edge}, {0, 2, P::Edge}}, want),
            con(ConstraintKind::Coincident, {{1, 0, P::Start}, start(0)}),
            con(ConstraintKind::Parallel, {{1, 0, P::Edge}, edge(2)}),
        };
        const auto r = solveSketch(p, cs);
        ASSERT_TRUE(r.solved) << want << ": " << r.message;
        const Vec2d u = p.loops[0].segments[0].b - p.loops[0].segments[0].a, v = p.loops[0].segments[2].b - p.loops[0].segments[2].a;
        double got = std::fmod(std::atan2(u.x() * v.y() - u.y() * v.x(), u.dot(v)) * 180 / 3.14159265358979, 180.0);
        if (got < 0) got += 180;
        EXPECT_NEAR(got, want, 1e-6);
        // The construction line stayed open and follows its constraints.
        const auto& line = p.loops[1].segments[0];
        EXPECT_NEAR(line.a.norm(), 0.0, 1e-6);
        const Vec2d d = line.b - line.a, e = p.loops[0].segments[2].b - p.loops[0].segments[2].a;
        EXPECT_NEAR(d.x() * e.y() - d.y() * e.x(), 0.0, 1e-6);
        EXPECT_NEAR(measureConstraint(p, cs[2]).value_or(-1), want, 1e-6);
    }
}

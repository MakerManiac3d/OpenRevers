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

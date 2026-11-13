// SPDX-License-Identifier: GPL-3.0-or-later
// The sketch editor's constraints: added, solved, dragged and kept through
// the edits that change a loop.
#include "app/SketchEditor.h"

#include <QApplication>

#include <cstdlib>
#include <gtest/gtest.h>

using namespace orv;

namespace {

Profile rectangle() {
    Profile p;
    Loop l;
    l.segments = {Segment::line({0, 0}, {29, 1}), Segment::line({29, 1}, {31, 11}), Segment::line({31, 11}, {-1, 13}), Segment::line({-1, 13}, {0, 0})};
    p.loops.push_back(l);
    return p;
}

SketchRef edge(int s) { return {0, s, SketchRef::Part::Edge}; }

SketchConstraint make(ConstraintKind k, std::vector<SketchRef> refs, double v = 0, std::string param = {}) {
    SketchConstraint c;
    c.kind = k;
    c.refs = std::move(refs);
    c.value = v;
    c.parameter = std::move(param);
    return c;
}

}  // namespace

TEST(SketchEditor, ConstraintsAreSolvedAndUndone) {
    SketchCanvas cv;
    cv.resize(600, 400);
    cv.setProfile(rectangle());
    cv.setConstraints({}, {{"ancho", 30.0}});
    EXPECT_EQ(cv.dof(), -1);
    for (int s : {0, 2}) EXPECT_TRUE(cv.addConstraint(make(ConstraintKind::Horizontal, {edge(s)})).isEmpty());
    for (int s : {1, 3}) EXPECT_TRUE(cv.addConstraint(make(ConstraintKind::Vertical, {edge(s)})).isEmpty());
    EXPECT_TRUE(cv.addConstraint(make(ConstraintKind::Fix, {{0, 0, SketchRef::Part::Start}})).isEmpty());
    EXPECT_TRUE(cv.addConstraint(make(ConstraintKind::Distance, {edge(0)}, 0, "ancho")).isEmpty());
    EXPECT_EQ(cv.dof(), 1);
    EXPECT_NEAR((cv.profile().loops[0].segments[0].b - cv.profile().loops[0].segments[0].a).norm(), 30.0, 1e-6);
    // A contradiction is refused and changes nothing.
    EXPECT_FALSE(cv.addConstraint(make(ConstraintKind::Distance, {edge(2)}, 20)).isEmpty());
    EXPECT_EQ(cv.constraints().size(), 6u);
    EXPECT_TRUE(cv.addConstraint(make(ConstraintKind::Distance, {edge(1)}, 12)).isEmpty());
    EXPECT_EQ(cv.dof(), 0);
    // A dimension's value changes the geometry.
    EXPECT_TRUE(cv.setConstraintValue(6, 15, {}).isEmpty());
    EXPECT_NEAR((cv.profile().loops[0].segments[1].b - cv.profile().loops[0].segments[1].a).norm(), 15.0, 1e-6);
    cv.undo();
    EXPECT_NEAR((cv.profile().loops[0].segments[1].b - cv.profile().loops[0].segments[1].a).norm(), 12.0, 1e-6);
    cv.removeConstraint(6);
    EXPECT_EQ(cv.dof(), 1);
}

TEST(SketchEditor, SplittingKeepsTheOtherConstraints) {
    SketchCanvas cv;
    cv.resize(600, 400);
    cv.setProfile(rectangle());
    cv.setConstraints({make(ConstraintKind::Horizontal, {edge(0)}), make(ConstraintKind::Vertical, {edge(1)}), make(ConstraintKind::Horizontal, {edge(2)}),
                       make(ConstraintKind::Distance, {edge(2)}, 32)},
                      {});
    ASSERT_TRUE(cv.solved());
    cv.setPick({SketchPick::What::Segment, 0, 1});
    cv.split();
    // The vertical on the split segment goes; the later ones move with their segment.
    ASSERT_EQ(cv.constraints().size(), 3u);
    EXPECT_EQ(cv.constraints()[1].refs[0].segment, 3);
    EXPECT_EQ(cv.constraints()[2].refs[0].segment, 3);
    EXPECT_TRUE(cv.solved());
    const Segment& top = cv.profile().loops[0].segments[3];
    EXPECT_NEAR(top.a.y(), top.b.y(), 1e-6);
    EXPECT_NEAR((top.b - top.a).norm(), 32.0, 1e-6);
}

TEST(SketchEditor, ADragFollowsTheConstraints) {
    SketchCanvas cv;
    cv.resize(600, 400);
    cv.setProfile(rectangle());
    cv.setConstraints({make(ConstraintKind::Horizontal, {edge(0)}), make(ConstraintKind::Vertical, {edge(1)}), make(ConstraintKind::Horizontal, {edge(2)}),
                       make(ConstraintKind::Vertical, {edge(3)})},
                      {});
    cv.fit();
    cv.show();
    // Drag the corner (30, 11)-ish up and to the right: the rectangle stays one.
    const Vec2d corner = cv.profile().loops[0].segments[2].a;
    // Through the API: what a drag does is a solve with the corner pinned.
    SketchSolveOptions opt;
    opt.drag.push_back({SketchRef{0, 2, SketchRef::Part::Start}, corner + Vec2d(5, 4)});
    Profile p = cv.profile();
    ASSERT_TRUE(solveSketch(p, cv.constraints(), opt).solved);
    const auto& sg = p.loops[0].segments;
    EXPECT_NEAR(sg[2].a.x(), corner.x() + 5, 1e-6);
    EXPECT_NEAR(sg[1].a.x(), sg[1].b.x(), 1e-6);
    EXPECT_NEAR(sg[0].a.y(), sg[0].b.y(), 1e-6);
}

TEST(SketchEditor, OpensWithItsConstraints) {
    std::vector<SketchConstraint> cs{make(ConstraintKind::Horizontal, {edge(0)}), make(ConstraintKind::Vertical, {edge(1)}), make(ConstraintKind::Horizontal, {edge(2)}),
                                     make(ConstraintKind::Vertical, {edge(3)}), make(ConstraintKind::Distance, {edge(0)}, 0, "ancho"),
                                     make(ConstraintKind::Distance, {edge(1)}, 12)};
    SketchEditor ed(rectangle(), "s1", {}, "es", nullptr, cs, {{"ancho", 30.0}});
    EXPECT_EQ(ed.constraints().size(), 6u);
    const Segment& bottom = ed.profile().loops[0].segments[0];
    EXPECT_NEAR((bottom.b - bottom.a).norm(), 30.0, 1e-6);
    // A picture of it, when asked for (to look at by hand).
    if (const char* path = std::getenv("ORV_EDITOR_PICTURE")) {
        ed.show();
        QApplication::processEvents();
        ed.grab().save(path);
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

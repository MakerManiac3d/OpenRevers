// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a sketch's geometry: closed loops of lines and arcs (and full
// circles) on a plane. Plain data, no Open CASCADE: the document, the
// commands and the tests use it as is; Solid turns it into faces.
#pragma once

#include "core/mesh/TriMesh.h"

#include <Eigen/Core>

#include <string>
#include <vector>

namespace orv {

using Vec2d = Eigen::Vector2d;

/// A plane in space with its own 2D axes: x along xDir, y = normal × xDir.
struct SketchPlane {
    Vec3d origin{Vec3d::Zero()};
    Vec3d normal{Vec3d::UnitZ()};
    Vec3d xDir{Vec3d::UnitX()};

    Vec3d yDir() const { return normal.cross(xDir); }
    Vec3d toWorld(const Vec2d& p) const { return origin + p.x() * xDir + p.y() * yDir(); }
    Vec2d toPlane(const Vec3d& p) const {
        const Vec3d d = p - origin;
        return {d.dot(xDir), d.dot(yDir())};
    }
    /// Makes the axes orthonormal (xDir projected onto the plane).
    void normalize();
};

/// One piece of a loop. A line goes from a to b. An arc goes from a to b
/// round `centre`, counterclockwise (seen from the normal) when ccw. A circle
/// is a whole loop by itself: centre and radius; a and b are not used.
struct Segment {
    enum class Kind { Line, Arc, Circle };
    Kind kind{Kind::Line};
    Vec2d a{Vec2d::Zero()}, b{Vec2d::Zero()}, centre{Vec2d::Zero()};
    double radius{0.0};
    bool ccw{true};

    static Segment line(Vec2d from, Vec2d to);
    static Segment arc(Vec2d from, Vec2d to, Vec2d centre, bool ccw);
    static Segment circle(Vec2d centre, double radius);
    double length() const;
    /// Points along the segment, every `step` at most (both ends included).
    std::vector<Vec2d> sample(double step) const;
};

/// A closed chain: each segment starts where the previous one ended.
struct Loop {
    std::vector<Segment> segments;
    /// Signed area (positive counterclockwise).
    double area() const;
    std::vector<Vec2d> polygon(double step) const;
    bool contains(const Vec2d& p, double step) const;
};

struct Profile {
    SketchPlane plane;
    std::vector<Loop> loops;
    /// Empty when the loops are usable, else what is wrong (open chain, a
    /// segment of no length, an arc whose ends are not on its circle).
    std::string problem(double tolerance = 1e-6) const;
};

const char* segmentKindName(Segment::Kind k);

}  // namespace orv

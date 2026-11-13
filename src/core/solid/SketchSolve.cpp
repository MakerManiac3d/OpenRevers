// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/solid/SketchSolve.h"

#include <GCS.h>

#include <cmath>
#include <deque>
#include <numbers>
#include <stdexcept>

namespace orv {

namespace {

constexpr double kPi = std::numbers::pi;

/// The solver's view of one segment. Its points hold pointers into the
/// parameter store; a loop's segments share the point where they meet.
struct Geo {
    Segment::Kind kind{Segment::Kind::Line};
    GCS::Point start, end;  // our a and b
    GCS::Line line;
    GCS::Circle circle;
    GCS::Arc arc;
    bool reversed{false};  // a clockwise arc: the solver's arc runs from b to a
};

class Builder {
public:
    explicit Builder(Profile& p) : p_(p) {}

    double* param(double v, bool unknown) {
        store_.push_back(v);
        double* x = &store_.back();
        if (unknown) unknowns_.push_back(x);
        return x;
    }
    double* constant(double v) { return param(v, false); }

    void build() {
        geos_.resize(p_.loops.size());
        for (std::size_t li = 0; li < p_.loops.size(); ++li) {
            auto& loop = p_.loops[li];
            auto& geos = geos_[li];
            geos.resize(loop.segments.size());
            const std::size_t n = loop.segments.size();
            // The joints: point k is where segment k starts (and k-1 ends).
            std::vector<GCS::Point> joints(n);
            for (std::size_t k = 0; k < n; ++k) {
                const Segment& s = loop.segments[k];
                if (s.kind == Segment::Kind::Circle) continue;
                const bool movable = s.kind != Segment::Kind::Spline && loop.segments[(k + n - 1) % n].kind != Segment::Kind::Spline;
                joints[k] = GCS::Point(param(s.a.x(), movable), param(s.a.y(), movable));
            }
            for (std::size_t k = 0; k < n; ++k) {
                const Segment& s = loop.segments[k];
                Geo& g = geos[k];
                g.kind = s.kind;
                if (s.kind == Segment::Kind::Circle) {
                    g.circle.center = GCS::Point(param(s.centre.x(), true), param(s.centre.y(), true));
                    g.circle.rad = param(s.radius, true);
                    g.start = g.end = g.circle.center;
                    continue;
                }
                g.start = joints[k];
                g.end = joints[(k + 1) % n];
                if (s.kind == Segment::Kind::Line) {
                    g.line.p1 = g.start;
                    g.line.p2 = g.end;
                } else if (s.kind == Segment::Kind::Arc) {
                    g.reversed = !s.ccw;
                    GCS::Arc& a = g.arc;
                    a.center = GCS::Point(param(s.centre.x(), true), param(s.centre.y(), true));
                    a.rad = param(s.radius, true);
                    a.start = g.reversed ? g.end : g.start;
                    a.end = g.reversed ? g.start : g.end;
                    const Vec2d from = g.reversed ? s.b : s.a, to = g.reversed ? s.a : s.b;
                    double sa = std::atan2(from.y() - s.centre.y(), from.x() - s.centre.x());
                    double ea = std::atan2(to.y() - s.centre.y(), to.x() - s.centre.x());
                    while (ea <= sa) ea += 2 * kPi;
                    a.startAngle = param(sa, true);
                    a.endAngle = param(ea, true);
                    sys_.addConstraintArcRules(a, 0);
                }
            }
        }
    }

    const Geo& geo(const SketchRef& r) const {
        if (r.loop < 0 || r.loop >= static_cast<int>(geos_.size()) || r.segment < 0 || r.segment >= static_cast<int>(geos_[static_cast<std::size_t>(r.loop)].size()))
            throw std::invalid_argument("no such segment (loop " + std::to_string(r.loop) + ", segment " + std::to_string(r.segment) + ")");
        return geos_[static_cast<std::size_t>(r.loop)][static_cast<std::size_t>(r.segment)];
    }
    GCS::Point point(const SketchRef& r) const {
        const Geo& g = geo(r);
        switch (r.part) {
            case SketchRef::Part::Start: return g.start;
            case SketchRef::Part::End: return g.end;
            case SketchRef::Part::Centre:
                if (g.kind == Segment::Kind::Arc) return g.arc.center;
                if (g.kind == Segment::Kind::Circle) return g.circle.center;
                throw std::invalid_argument("only arcs and circles have a centre");
            case SketchRef::Part::Edge: break;
        }
        throw std::invalid_argument("a point is needed (start, end or centre), not an edge");
    }
    GCS::Line& line(const SketchRef& r) {
        auto& g = const_cast<Geo&>(geo(r));
        if (g.kind != Segment::Kind::Line || r.part != SketchRef::Part::Edge) throw std::invalid_argument("a line is needed");
        return g.line;
    }
    bool isLine(const SketchRef& r) const { return r.part == SketchRef::Part::Edge && geo(r).kind == Segment::Kind::Line; }
    bool isCircle(const SketchRef& r) const { return r.part == SketchRef::Part::Edge && geo(r).kind == Segment::Kind::Circle; }
    bool isArc(const SketchRef& r) const { return r.part == SketchRef::Part::Edge && geo(r).kind == Segment::Kind::Arc; }
    bool isPoint(const SketchRef& r) const { return r.part != SketchRef::Part::Edge; }
    Geo& g(const SketchRef& r) { return const_cast<Geo&>(geo(r)); }

    void add(const SketchConstraint& c, int tag) {
        const auto& R = c.refs;
        const bool drv = c.driving;
        auto need = [&](std::size_t n) {
            if (R.size() < n) throw std::invalid_argument(std::string(constraintKindName(c.kind)) + " needs " + std::to_string(n) + " references");
        };
        auto fail = [&]() { throw std::invalid_argument(std::string(constraintKindName(c.kind)) + " does not apply to these pieces"); };
        double* value = constant(c.value);
        switch (c.kind) {
            case ConstraintKind::Coincident: {
                need(2);
                GCS::Point a = point(R[0]), b = point(R[1]);
                sys_.addConstraintP2PCoincident(a, b, tag, drv);
                break;
            }
            case ConstraintKind::Horizontal:
            case ConstraintKind::Vertical: {
                need(1);
                const bool h = c.kind == ConstraintKind::Horizontal;
                if (R.size() == 1 && isLine(R[0])) {
                    if (h) sys_.addConstraintHorizontal(line(R[0]), tag, drv);
                    else sys_.addConstraintVertical(line(R[0]), tag, drv);
                } else {
                    need(2);
                    GCS::Point a = point(R[0]), b = point(R[1]);
                    if (h) sys_.addConstraintHorizontal(a, b, tag, drv);
                    else sys_.addConstraintVertical(a, b, tag, drv);
                }
                break;
            }
            case ConstraintKind::Parallel:
                need(2);
                sys_.addConstraintParallel(line(R[0]), line(R[1]), tag, drv);
                break;
            case ConstraintKind::Perpendicular:
                need(2);
                sys_.addConstraintPerpendicular(line(R[0]), line(R[1]), tag, drv);
                break;
            case ConstraintKind::Tangent: {
                need(2);
                SketchRef a = R[0], b = R[1];
                if (!isLine(a) && isLine(b)) std::swap(a, b);
                if (isLine(a) && isCircle(b)) sys_.addConstraintTangent(line(a), g(b).circle, tag, drv);
                else if (isLine(a) && isArc(b)) sys_.addConstraintTangent(line(a), g(b).arc, tag, drv);
                else if (isCircle(a) && isCircle(b)) sys_.addConstraintTangent(g(a).circle, g(b).circle, tag, drv);
                else if (isArc(a) && isArc(b)) sys_.addConstraintTangent(g(a).arc, g(b).arc, tag, drv);
                else if (isCircle(a) && isArc(b)) sys_.addConstraintTangent(g(a).circle, g(b).arc, tag, drv);
                else if (isArc(a) && isCircle(b)) sys_.addConstraintTangent(g(b).circle, g(a).arc, tag, drv);
                else fail();
                break;
            }
            case ConstraintKind::Equal: {
                need(2);
                const SketchRef a = R[0], b = R[1];
                if (isLine(a) && isLine(b)) sys_.addConstraintEqualLength(line(a), line(b), tag, drv);
                else if (isCircle(a) && isCircle(b)) sys_.addConstraintEqualRadius(g(a).circle, g(b).circle, tag, drv);
                else if (isArc(a) && isArc(b)) sys_.addConstraintEqualRadius(g(a).arc, g(b).arc, tag, drv);
                else if (isCircle(a) && isArc(b)) sys_.addConstraintEqualRadius(g(a).circle, g(b).arc, tag, drv);
                else if (isArc(a) && isCircle(b)) sys_.addConstraintEqualRadius(g(b).circle, g(a).arc, tag, drv);
                else fail();
                break;
            }
            case ConstraintKind::Distance: {
                need(1);
                if (R.size() == 1 && isLine(R[0])) {
                    GCS::Line& l = line(R[0]);
                    sys_.addConstraintP2PDistance(l.p1, l.p2, value, tag, drv);
                } else {
                    need(2);
                    if (isPoint(R[0]) && isPoint(R[1])) {
                        GCS::Point a = point(R[0]), b = point(R[1]);
                        sys_.addConstraintP2PDistance(a, b, value, tag, drv);
                    } else if (isPoint(R[0]) && isLine(R[1])) {
                        GCS::Point a = point(R[0]);
                        sys_.addConstraintP2LDistance(a, line(R[1]), value, tag, drv);
                    } else if (isLine(R[0]) && isPoint(R[1])) {
                        GCS::Point a = point(R[1]);
                        sys_.addConstraintP2LDistance(a, line(R[0]), value, tag, drv);
                    } else {
                        fail();
                    }
                }
                break;
            }
            case ConstraintKind::DistanceX:
            case ConstraintKind::DistanceY: {
                need(1);
                GCS::Point a, b;
                if (R.size() == 1 && isLine(R[0])) {
                    a = line(R[0]).p1;
                    b = line(R[0]).p2;
                } else {
                    need(2);
                    a = point(R[0]);
                    b = point(R[1]);
                }
                if (c.kind == ConstraintKind::DistanceX) sys_.addConstraintDifference(a.x, b.x, value, tag, drv);
                else sys_.addConstraintDifference(a.y, b.y, value, tag, drv);
                break;
            }
            case ConstraintKind::Radius:
            case ConstraintKind::Diameter: {
                need(1);
                const bool d = c.kind == ConstraintKind::Diameter;
                if (isCircle(R[0])) {
                    if (d) sys_.addConstraintCircleDiameter(g(R[0]).circle, value, tag, drv);
                    else sys_.addConstraintCircleRadius(g(R[0]).circle, value, tag, drv);
                } else if (isArc(R[0])) {
                    if (d) sys_.addConstraintArcDiameter(g(R[0]).arc, value, tag, drv);
                    else sys_.addConstraintArcRadius(g(R[0]).arc, value, tag, drv);
                } else {
                    fail();
                }
                break;
            }
            case ConstraintKind::Angle: {
                need(2);
                *value = c.value * kPi / 180.0;
                sys_.addConstraintL2LAngle(line(R[0]), line(R[1]), value, tag, drv);
                break;
            }
            case ConstraintKind::Fix: {
                need(1);
                GCS::Point a = point(R[0]);
                sys_.addConstraintCoordinateX(a, constant(*a.x), tag, drv);
                sys_.addConstraintCoordinateY(a, constant(*a.y), tag, drv);
                break;
            }
            case ConstraintKind::OnObject: {
                need(2);
                GCS::Point a = point(R[0]);
                if (isLine(R[1])) sys_.addConstraintPointOnLine(a, line(R[1]), tag, drv);
                else if (isCircle(R[1])) sys_.addConstraintPointOnCircle(a, g(R[1]).circle, tag, drv);
                else if (isArc(R[1])) sys_.addConstraintPointOnArc(a, g(R[1]).arc, tag, drv);
                else fail();
                break;
            }
            case ConstraintKind::Symmetric: {
                need(3);
                GCS::Point a = point(R[0]), b = point(R[1]);
                if (isLine(R[2])) sys_.addConstraintP2PSymmetric(a, b, line(R[2]), tag, drv);
                else {
                    GCS::Point m = point(R[2]);
                    sys_.addConstraintP2PSymmetric(a, b, m, tag, drv);
                }
                break;
            }
            case ConstraintKind::Midpoint: {
                need(2);
                GCS::Point m = point(R[0]);
                GCS::Line& l = line(R[1]);
                sys_.addConstraintP2PSymmetric(l.p1, l.p2, m, tag, drv);
                break;
            }
        }
    }

    void drag(const SketchRef& r, const Vec2d& to, int tag) {
        GCS::Point a = point(r);
        sys_.addConstraintCoordinateX(a, constant(to.x()), tag, true);
        sys_.addConstraintCoordinateY(a, constant(to.y()), tag, true);
    }

    /// Writes the solved values back into the profile.
    void writeBack() {
        for (std::size_t li = 0; li < p_.loops.size(); ++li)
            for (std::size_t k = 0; k < p_.loops[li].segments.size(); ++k) {
                Segment& s = p_.loops[li].segments[k];
                const Geo& g = geos_[li][k];
                if (s.kind == Segment::Kind::Circle) {
                    s.centre = {*g.circle.center.x, *g.circle.center.y};
                    s.radius = std::abs(*g.circle.rad);
                    continue;
                }
                if (s.kind == Segment::Kind::Spline) continue;
                s.a = {*g.start.x, *g.start.y};
                s.b = {*g.end.x, *g.end.y};
                if (s.kind == Segment::Kind::Arc) {
                    s.centre = {*g.arc.center.x, *g.arc.center.y};
                    s.radius = std::abs(*g.arc.rad);
                }
            }
    }

    GCS::System& sys() { return sys_; }
    GCS::VEC_pD& unknowns() { return unknowns_; }

private:
    Profile& p_;
    std::deque<double> store_;  // stable addresses
    GCS::VEC_pD unknowns_;
    std::vector<std::vector<Geo>> geos_;
    GCS::System sys_;
};

std::vector<int> fromTags(const GCS::VEC_I& tags, std::size_t n) {
    std::vector<int> out;
    for (int t : tags)
        if (t >= 1 && static_cast<std::size_t>(t) <= n) out.push_back(t - 1);
    return out;
}

}  // namespace

SketchSolveResult solveSketch(Profile& profile, const std::vector<SketchConstraint>& constraints, const SketchSolveOptions& options) {
    SketchSolveResult r;
    Profile work = profile;
    Builder b(work);
    try {
        b.build();
        // Tags 1..n are the person's constraints; the drag uses n+1.
        for (std::size_t i = 0; i < constraints.size(); ++i) b.add(constraints[i], static_cast<int>(i) + 1);
    } catch (const std::invalid_argument& e) {
        r.message = e.what();
        return r;
    }
    b.sys().declareUnknowns(b.unknowns());
    b.sys().initSolution(GCS::DogLeg);
    r.dof = b.sys().dofsNumber();
    r.conflicting = fromTags([&] { GCS::VEC_I v; b.sys().getConflicting(v); return v; }(), constraints.size());
    r.redundant = fromTags([&] { GCS::VEC_I v; b.sys().getRedundant(v); return v; }(), constraints.size());
    if (!r.conflicting.empty()) {
        r.message = "conflicting constraints";
        return r;
    }

    auto attempt = [&](bool withDrag) -> bool {
        Profile again = profile;
        Builder bb(again);
        bb.build();
        for (std::size_t i = 0; i < constraints.size(); ++i) bb.add(constraints[i], static_cast<int>(i) + 1);
        if (withDrag)
            for (const auto& [ref, to] : options.drag) bb.drag(ref, to, static_cast<int>(constraints.size()) + 1);
        bb.sys().declareUnknowns(bb.unknowns());
        bb.sys().initSolution(GCS::DogLeg);
        int status = bb.sys().solve(true, GCS::DogLeg);
        // Only an exact solution: "converged" means the error was minimised, not zeroed.
        if (status != GCS::Success) status = bb.sys().solve(true, GCS::LevenbergMarquardt);
        if (status != GCS::Success) return false;
        bb.sys().applySolution();
        bb.writeBack();
        profile = std::move(again);
        return true;
    };
    try {
        r.solved = (!options.drag.empty() && attempt(true)) || attempt(false);
    } catch (const std::invalid_argument& e) {
        r.message = e.what();
        return r;
    }
    if (!r.solved) r.message = "the solver found no solution";
    return r;
}

std::optional<double> measureConstraint(const Profile& p, const SketchConstraint& c) {
    if (!constraintHasValue(c.kind) || c.refs.empty()) return std::nullopt;
    auto seg = [&](const SketchRef& r) -> const Segment& { return p.loops.at(static_cast<std::size_t>(r.loop)).segments.at(static_cast<std::size_t>(r.segment)); };
    auto pt = [&](const SketchRef& r) -> Vec2d {
        const Segment& s = seg(r);
        switch (r.part) {
            case SketchRef::Part::Start: return s.kind == Segment::Kind::Circle ? s.centre : s.a;
            case SketchRef::Part::End: return s.kind == Segment::Kind::Circle ? s.centre : s.b;
            default: return s.centre;
        }
    };
    const auto& R = c.refs;
    const bool oneLine = R.size() == 1 && R[0].part == SketchRef::Part::Edge && seg(R[0]).kind == Segment::Kind::Line;
    try {
        switch (c.kind) {
            case ConstraintKind::Distance:
                if (oneLine) return (seg(R[0]).b - seg(R[0]).a).norm();
                if (R.size() >= 2 && R[1].part == SketchRef::Part::Edge && seg(R[1]).kind == Segment::Kind::Line) {
                    const Segment& l = seg(R[1]);
                    const Vec2d d = (l.b - l.a).normalized();
                    const Vec2d v = pt(R[0]) - l.a;
                    return std::abs(d.x() * v.y() - d.y() * v.x());
                }
                if (R.size() >= 2) return (pt(R[1]) - pt(R[0])).norm();
                break;
            case ConstraintKind::DistanceX:
                if (oneLine) return seg(R[0]).b.x() - seg(R[0]).a.x();
                if (R.size() >= 2) return pt(R[1]).x() - pt(R[0]).x();
                break;
            case ConstraintKind::DistanceY:
                if (oneLine) return seg(R[0]).b.y() - seg(R[0]).a.y();
                if (R.size() >= 2) return pt(R[1]).y() - pt(R[0]).y();
                break;
            case ConstraintKind::Radius: return seg(R[0]).radius;
            case ConstraintKind::Diameter: return 2 * seg(R[0]).radius;
            case ConstraintKind::Angle:
                if (R.size() >= 2) {
                    const Vec2d u = seg(R[0]).b - seg(R[0]).a, v = seg(R[1]).b - seg(R[1]).a;
                    double a = std::atan2(u.x() * v.y() - u.y() * v.x(), u.dot(v)) * 180.0 / kPi;
                    return a;
                }
                break;
            default: break;
        }
    } catch (const std::out_of_range&) {
    }
    return std::nullopt;
}

namespace {
const std::pair<ConstraintKind, const char*> kNames[] = {
    {ConstraintKind::Coincident, "coincident"}, {ConstraintKind::Horizontal, "horizontal"}, {ConstraintKind::Vertical, "vertical"},
    {ConstraintKind::Parallel, "parallel"},     {ConstraintKind::Perpendicular, "perpendicular"}, {ConstraintKind::Tangent, "tangent"},
    {ConstraintKind::Equal, "equal"},           {ConstraintKind::Distance, "distance"},     {ConstraintKind::DistanceX, "distance_x"},
    {ConstraintKind::DistanceY, "distance_y"},  {ConstraintKind::Radius, "radius"},         {ConstraintKind::Diameter, "diameter"},
    {ConstraintKind::Angle, "angle"},           {ConstraintKind::Fix, "fix"},               {ConstraintKind::OnObject, "on_object"},
    {ConstraintKind::Symmetric, "symmetric"},   {ConstraintKind::Midpoint, "midpoint"},
};
}  // namespace

const char* constraintKindName(ConstraintKind k) {
    for (const auto& [kind, name] : kNames)
        if (kind == k) return name;
    return "?";
}

std::optional<ConstraintKind> constraintKindFromName(const std::string& name) {
    for (const auto& [kind, n] : kNames)
        if (name == n) return kind;
    return std::nullopt;
}

bool constraintHasValue(ConstraintKind k) {
    switch (k) {
        case ConstraintKind::Distance:
        case ConstraintKind::DistanceX:
        case ConstraintKind::DistanceY:
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter:
        case ConstraintKind::Angle: return true;
        default: return false;
    }
}

}  // namespace orv

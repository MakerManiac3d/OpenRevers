// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/solid/Profile.h"

#include <cmath>

namespace orv {
namespace {
constexpr double kPi = 3.14159265358979323846;

/// Angle swept by an arc, in (0, 2π].
double sweep(const Segment& s) {
    const double a0 = std::atan2(s.a.y() - s.centre.y(), s.a.x() - s.centre.x());
    const double a1 = std::atan2(s.b.y() - s.centre.y(), s.b.x() - s.centre.x());
    double d = s.ccw ? a1 - a0 : a0 - a1;
    while (d <= 1e-12) d += 2 * kPi;
    while (d > 2 * kPi) d -= 2 * kPi;
    return d;
}
}  // namespace

void SketchPlane::normalize() {
    normal.normalize();
    xDir = xDir - xDir.dot(normal) * normal;
    if (xDir.norm() < 1e-9) xDir = normal.unitOrthogonal();
    xDir.normalize();
}

Segment Segment::line(Vec2d from, Vec2d to) {
    Segment s;
    s.kind = Kind::Line;
    s.a = from;
    s.b = to;
    return s;
}

Segment Segment::arc(Vec2d from, Vec2d to, Vec2d centre, bool ccw) {
    Segment s;
    s.kind = Kind::Arc;
    s.a = from;
    s.b = to;
    s.centre = centre;
    s.radius = 0.5 * ((from - centre).norm() + (to - centre).norm());
    s.ccw = ccw;
    return s;
}

Segment Segment::circle(Vec2d centre, double radius) {
    Segment s;
    s.kind = Kind::Circle;
    s.centre = centre;
    s.radius = radius;
    s.a = s.b = centre + Vec2d(radius, 0);
    return s;
}

double Segment::length() const {
    switch (kind) {
        case Kind::Line: return (b - a).norm();
        case Kind::Arc: return radius * sweep(*this);
        case Kind::Circle: return 2 * kPi * radius;
    }
    return 0;
}

std::vector<Vec2d> Segment::sample(double step) const {
    std::vector<Vec2d> out;
    const int n = std::max(1, static_cast<int>(std::ceil(length() / std::max(step, 1e-9))));
    if (kind == Kind::Line) {
        for (int i = 0; i <= n; ++i) out.push_back(a + (b - a) * (static_cast<double>(i) / n));
        return out;
    }
    const int m = std::max(n, kind == Kind::Circle ? 24 : 4);
    const double a0 = kind == Kind::Circle ? 0.0 : std::atan2(a.y() - centre.y(), a.x() - centre.x());
    const double sw = kind == Kind::Circle ? 2 * kPi : sweep(*this);
    for (int i = 0; i <= m; ++i) {
        const double t = a0 + (ccw || kind == Kind::Circle ? 1 : -1) * sw * i / m;
        out.push_back(centre + radius * Vec2d(std::cos(t), std::sin(t)));
    }
    return out;
}

std::vector<Vec2d> Loop::polygon(double step) const {
    std::vector<Vec2d> out;
    for (const auto& s : segments) {
        auto pts = s.sample(step);
        if (!out.empty()) pts.erase(pts.begin());
        out.insert(out.end(), pts.begin(), pts.end());
    }
    if (out.size() > 1 && (out.front() - out.back()).norm() < 1e-9) out.pop_back();
    return out;
}

double Loop::area() const {
    const auto p = polygon(0.05);
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Vec2d& u = p[i];
        const Vec2d& v = p[(i + 1) % p.size()];
        a += u.x() * v.y() - v.x() * u.y();
    }
    return 0.5 * a;
}

bool Loop::contains(const Vec2d& q, double step) const {
    const auto p = polygon(step);
    bool in = false;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        if ((p[i].y() > q.y()) != (p[j].y() > q.y()) &&
            q.x() < (p[j].x() - p[i].x()) * (q.y() - p[i].y()) / (p[j].y() - p[i].y()) + p[i].x())
            in = !in;
    }
    return in;
}

std::string Profile::problem(double tol) const {
    if (loops.empty()) return "the sketch has no loops";
    for (std::size_t l = 0; l < loops.size(); ++l) {
        const auto& sg = loops[l].segments;
        const std::string where = "loop " + std::to_string(l + 1);
        if (sg.empty()) return where + " is empty";
        if (sg.size() == 1 && sg[0].kind == Segment::Kind::Circle) {
            if (sg[0].radius <= tol) return where + ": a circle needs a radius";
            continue;
        }
        for (std::size_t i = 0; i < sg.size(); ++i) {
            const Segment& s = sg[i];
            const std::string seg = where + ", segment " + std::to_string(i + 1);
            if (s.kind == Segment::Kind::Circle) return seg + ": a circle must be a loop by itself";
            if ((s.b - s.a).norm() <= tol && s.kind == Segment::Kind::Line) return seg + " has no length";
            if (s.kind == Segment::Kind::Arc) {
                const double ra = (s.a - s.centre).norm(), rb = (s.b - s.centre).norm();
                if (std::abs(ra - rb) > std::max(1e-4, 1e-4 * ra)) return seg + ": its ends are not at the same distance from the centre";
            }
            const Segment& next = sg[(i + 1) % sg.size()];
            if ((s.b - next.a).norm() > std::max(tol, 1e-6)) return seg + " does not end where the next one starts (the loop must be closed)";
        }
    }
    return {};
}

const char* segmentKindName(Segment::Kind k) {
    switch (k) {
        case Segment::Kind::Line: return "line";
        case Segment::Kind::Arc: return "arc";
        case Segment::Kind::Circle: return "circle";
    }
    return "?";
}

}  // namespace orv

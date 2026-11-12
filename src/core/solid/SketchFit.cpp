// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/solid/SketchFit.h"

#include "core/solid/BSpline.h"

#include <Eigen/Dense>

#include <algorithm>
#include <functional>
#include <cmath>
#include <optional>

namespace orv {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Piece {
    bool arc{false};
    bool spline{false};
    std::size_t from{0}, to{0};  // indices into the (unrolled) points, inclusive
    Vec2d p0{Vec2d::Zero()}, dir{Vec2d::UnitX()};  // line: a point and its direction
    Vec2d centre{Vec2d::Zero()};
    double radius{0};
};

/// Least-squares line through points[i..j]: its worst distance.
std::optional<Piece> fitLine(const std::vector<Vec2d>& p, std::size_t i, std::size_t j, double tol) {
    const std::size_t n = j - i + 1;
    if (n < 2) return std::nullopt;
    Vec2d c = Vec2d::Zero();
    for (std::size_t k = i; k <= j; ++k) c += p[k];
    c /= static_cast<double>(n);
    Eigen::Matrix2d C = Eigen::Matrix2d::Zero();
    for (std::size_t k = i; k <= j; ++k) C += (p[k] - c) * (p[k] - c).transpose();
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(C);
    const Vec2d d = es.eigenvectors().col(1);
    const Vec2d nrm(-d.y(), d.x());
    for (std::size_t k = i; k <= j; ++k)
        if (std::abs((p[k] - c).dot(nrm)) > tol) return std::nullopt;
    Piece pc;
    pc.from = i;
    pc.to = j;
    pc.p0 = c;
    pc.dir = d;
    return pc;
}

/// Algebraic circle (Kåsa) through points[i..j], kept when every point is
/// within tol of it.
std::optional<Piece> fitArc(const std::vector<Vec2d>& p, std::size_t i, std::size_t j, double tol, double maxRadius) {
    const std::size_t n = j - i + 1;
    if (n < 4) return std::nullopt;
    Eigen::MatrixXd A(static_cast<Eigen::Index>(n), 3);
    Eigen::VectorXd b(static_cast<Eigen::Index>(n));
    const Vec2d o = p[i];
    for (std::size_t k = 0; k < n; ++k) {
        const Vec2d q = p[i + k] - o;
        A.row(static_cast<Eigen::Index>(k)) << q.x(), q.y(), 1.0;
        b[static_cast<Eigen::Index>(k)] = -(q.x() * q.x() + q.y() * q.y());
    }
    const Eigen::Vector3d s = A.colPivHouseholderQr().solve(b);
    const Vec2d c(-s[0] / 2, -s[1] / 2);
    const double r2 = c.squaredNorm() - s[2];
    if (!(r2 > 0)) return std::nullopt;
    const double r = std::sqrt(r2);
    if (r > maxRadius) return std::nullopt;
    for (std::size_t k = i; k <= j; ++k)
        if (std::abs((p[k] - o - c).norm() - r) > tol) return std::nullopt;
    Piece pc;
    pc.arc = true;
    pc.from = i;
    pc.to = j;
    pc.centre = c + o;
    pc.radius = r;
    return pc;
}

double distToSegment(const Vec2d& q, const Segment& s) {
    if (s.kind == Segment::Kind::Line) {
        const Vec2d ab = s.b - s.a;
        const double t = std::clamp(ab.squaredNorm() > 0 ? (q - s.a).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
        return (q - (s.a + t * ab)).norm();
    }
    if (s.kind == Segment::Kind::Circle) return std::abs((q - s.centre).norm() - s.radius);
    if (s.kind == Segment::Kind::Spline) {
        const auto pts = s.sample(0.02);
        double best = 1e300;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec2d ab = pts[i + 1] - pts[i];
            const double t = std::clamp(ab.squaredNorm() > 0 ? (q - pts[i]).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
            best = std::min(best, (q - (pts[i] + t * ab)).norm());
        }
        return best;
    }
    // Arc: the circle where q's angle falls inside the arc, else the nearer end.
    auto ang = [&](const Vec2d& v) { return std::atan2(v.y() - s.centre.y(), v.x() - s.centre.x()); };
    auto ccwFrom = [](double a, double b) {
        double d = b - a;
        while (d < 0) d += 2 * kPi;
        while (d >= 2 * kPi) d -= 2 * kPi;
        return d;
    };
    const double a0 = ang(s.a), a1 = ang(s.b), aq = ang(q);
    const bool inside = s.ccw ? ccwFrom(a0, aq) <= ccwFrom(a0, a1) : ccwFrom(a1, aq) <= ccwFrom(a1, a0);
    if (inside) return std::abs((q - s.centre).norm() - s.radius);
    return std::min((q - s.a).norm(), (q - s.b).norm());
}

std::optional<Vec2d> intersect(const Piece& a, const Piece& b) {
    const double den = a.dir.x() * b.dir.y() - a.dir.y() * b.dir.x();
    if (std::abs(den) < 1e-6) return std::nullopt;  // parallel
    const Vec2d d = b.p0 - a.p0;
    const double t = (d.x() * b.dir.y() - d.y() * b.dir.x()) / den;
    return a.p0 + t * a.dir;
}

}  // namespace

LoopFit fitLoop(const std::vector<Vec2d>& raw, const SketchFitOptions& o) {
    LoopFit out;
    if (raw.size() < 3) return out;
    double perim = 0;
    for (std::size_t k = 0; k < raw.size(); ++k) perim += (raw[(k + 1) % raw.size()] - raw[k]).norm();
    // Even spacing, fine enough to see the tolerance and no more than ~2000 points.
    const double step = std::max(perim / 2000.0, std::min(o.tolerance, perim / 200.0));
    std::vector<Vec2d> p;
    {
        double carry = 0;
        for (std::size_t k = 0; k < raw.size(); ++k) {
            const Vec2d a = raw[k], b = raw[(k + 1) % raw.size()];
            const double L = (b - a).norm();
            double t = carry;
            while (t < L) {
                p.push_back(a + (b - a) * (t / L));
                t += step;
            }
            carry = t - L;
        }
    }
    const std::size_t n = p.size();
    if (n < 8) return out;

    // A whole circle?
    if (auto c = fitArc(p, 0, n - 1, o.tolerance, 1e9)) {
        Loop l;
        l.segments.push_back(Segment::circle(c->centre, c->radius));
        out.loop = l;
        for (const auto& q : raw) out.maxDeviation = std::max(out.maxDeviation, std::abs((q - c->centre).norm() - c->radius));
        return out;
    }

    // Start at the sharpest corner, so no piece straddles the start.
    const auto k0 = static_cast<std::size_t>(std::max(1.0, std::round(0.5 * o.minSegment / step)));
    std::size_t start = 0;
    double sharpest = -1;
    for (std::size_t k = 0; k < n; ++k) {
        const Vec2d u = (p[k] - p[(k + n - k0) % n]).normalized(), v = (p[(k + k0) % n] - p[k]).normalized();
        const double turn = std::acos(std::clamp(u.dot(v), -1.0, 1.0));
        if (turn > sharpest) { sharpest = turn; start = k; }
    }
    std::vector<Vec2d> q(n + 1);  // unrolled from the start, closed
    for (std::size_t k = 0; k <= n; ++k) q[k] = p[(start + k) % n];

    // Greedy: from each break, the longest line or arc that stays within the tolerance.
    std::vector<Piece> pieces;
    std::size_t i = 0;
    while (i < n) {
        std::optional<Piece> best;
        for (bool arc : {false, true}) {
            std::size_t lo = i + 1, hi = n;  // the last end that fits, by doubling then bisection
            std::optional<Piece> ok;
            std::size_t len = std::min<std::size_t>(arc ? std::max<std::size_t>(4, k0) : 1, n - i);
            lo = i + len - 1;
            while (i + len <= n) {
                auto f = arc ? fitArc(q, i, i + len, o.tolerance, o.maxRadius) : fitLine(q, i, i + len, o.tolerance);
                if (!f) { hi = i + len; break; }
                ok = f;
                lo = i + len;
                if (i + len == n) { hi = n + 1; break; }
                len = std::min(2 * len, n - i);
            }
            while (hi - lo > 1) {
                const std::size_t mid = (lo + hi) / 2;
                auto f = arc ? fitArc(q, i, mid, o.tolerance, o.maxRadius) : fitLine(q, i, mid, o.tolerance);
                if (f) { ok = f; lo = mid; } else { hi = mid; }
            }
            // An arc must reach clearly further than a line to be chosen.
            if (ok && (!best || (arc ? ok->to > best->to + std::max<std::size_t>(2, k0) : ok->to > best->to))) best = ok;
        }
        if (!best) {  // too few points to fit anything: a short line
            Piece pc;
            pc.from = i;
            pc.to = std::min(i + 1, n);
            pc.p0 = q[i];
            pc.dir = (q[pc.to] - q[i]).normalized();
            best = pc;
        }
        pieces.push_back(*best);
        i = best->to;
    }

    // Short pieces (a corner's few rounded points) go to the neighbour that can take them.
    for (bool changed = true; changed && pieces.size() > 2;) {
        changed = false;
        for (std::size_t k = 0; k < pieces.size(); ++k) {
            const double len = static_cast<double>(pieces[k].to - pieces[k].from) * step;
            if (len >= o.minSegment || pieces.size() <= 2) continue;
            const std::size_t prev = (k + pieces.size() - 1) % pieces.size(), next = (k + 1) % pieces.size();
            // Split it between its neighbours at its middle; they keep their own fits.
            const std::size_t mid = (pieces[k].from + pieces[k].to) / 2;
            if (prev < k || k == 0) pieces[prev].to = mid;
            pieces[next].from = mid;
            if (k == 0) pieces[prev].to = n + mid;  // wraps round
            pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(k));
            changed = true;
            break;
        }
    }

    // Free-form runs: three or more short pieces, joined without a corner,
    // become one spline (an organic outline otherwise comes out as many small
    // arcs). Sharp corners, long lines and clear arcs stay as they are.
    if (o.splines && pieces.size() >= 3) {
        const auto np = pieces.size();
        const double shortLen = std::max(6 * o.minSegment, 0.08 * perim);
        auto lengthOf = [&](const Piece& pc) { return static_cast<double>((pc.to >= pc.from ? pc.to - pc.from : pc.to + n - pc.from)) * step; };
        // The turn at the joint before piece k, from the section's points either side.
        const std::size_t kk = std::max<std::size_t>(2, static_cast<std::size_t>(std::round(0.5 * o.minSegment / step)));
        auto smoothBefore = [&](std::size_t k) {
            const std::size_t at = pieces[k].from;
            const Vec2d u = (q[(at + n) % n] - q[(at + n - kk) % n]).normalized(), v = (q[(at + kk) % n] - q[at % n]).normalized();
            return std::acos(std::clamp(u.dot(v), -1.0, 1.0)) < 25.0 * kPi / 180;
        };
        // Soft: an arc, or a short line; a run must hold two arcs at least (a
        // line and its fillet stay a line and an arc).
        std::vector<bool> soft(np);
        for (std::size_t k = 0; k < np; ++k) soft[k] = pieces[k].arc || lengthOf(pieces[k]) < shortLen;
        std::vector<Piece> merged;
        // Start the walk at a piece that does not continue a run, so no run wraps round the start.
        std::size_t begin = 0;
        for (std::size_t k = 0; k < np; ++k)
            if (!soft[k] || !smoothBefore(k)) { begin = k; break; }
        for (std::size_t c = 0; c < np;) {
            const std::size_t k = (begin + c) % np;
            std::size_t len = 1;
            if (soft[k])
                while (c + len < np && soft[(k + len) % np] && smoothBefore((k + len) % np)) ++len;
            int arcs = 0;
            for (std::size_t t = 0; t < len; ++t) arcs += pieces[(k + t) % np].arc;
            if (len >= 3 && arcs >= 2) {
                Piece sp;
                sp.spline = true;
                sp.from = pieces[k].from;
                sp.to = pieces[(k + len - 1) % np].to;
                if (sp.to <= sp.from) sp.to += n;
                if (len == np) {  // the whole loop: two splines, so each has two distinct ends
                    Piece half = sp;
                    half.to = sp.from + (sp.to - sp.from) / 2;
                    merged.push_back(half);
                    half.from = half.to;
                    half.to = sp.to;
                    merged.push_back(half);
                } else {
                    merged.push_back(sp);
                }
            } else {
                for (std::size_t t = 0; t < len; ++t) merged.push_back(pieces[(k + t) % np]);
            }
            c += len;
        }
        pieces = merged;
    }

    // The joints: where two lines meet, their intersection (a sharp corner the
    // scan rounded a little); otherwise the section's own point.
    const std::size_t m = pieces.size();
    std::vector<Vec2d> joint(m);
    for (std::size_t k = 0; k < m; ++k) {
        const Piece& a = pieces[(k + m - 1) % m];
        const Piece& b = pieces[k];
        const Vec2d at = q[b.from % n];
        joint[k] = at;
        if (!a.arc && !b.arc && !a.spline && !b.spline)
            if (auto x = intersect(a, b); x && (*x - at).norm() < std::max(10 * o.tolerance, o.minSegment)) joint[k] = *x;
    }
    // Arcs that touch lines are made tangent to them: the centre at the
    // radius from each line, the joints at the feet of the centre on them.
    std::vector<std::optional<Vec2d>> centreOf(m);
    for (std::size_t k = 0; k < m; ++k) {
        Piece& pc = pieces[k];
        if (!pc.arc) continue;
        const Piece& before = pieces[(k + m - 1) % m];
        const Piece& after = pieces[(k + 1) % m];
        auto normalOf = [](const Piece& L) { return Vec2d(-L.dir.y(), L.dir.x()); };
        auto signedDist = [&](const Piece& L, const Vec2d& c) { return (c - L.p0).dot(normalOf(L)); };
        const double slack = std::max(3 * o.tolerance, 0.02 * pc.radius);
        const bool t1 = !before.arc && !before.spline && std::abs(std::abs(signedDist(before, pc.centre)) - pc.radius) < slack;
        const bool t2 = !after.arc && !after.spline && std::abs(std::abs(signedDist(after, pc.centre)) - pc.radius) < slack;
        Vec2d c = pc.centre;
        double r = pc.radius;
        if (t1 && t2) {
            const double s1 = signedDist(before, c) >= 0 ? 1 : -1, s2 = signedDist(after, c) >= 0 ? 1 : -1;
            const Vec2d n1 = normalOf(before), n2 = normalOf(after);
            const double det = n1.x() * n2.y() - n1.y() * n2.x();
            if (std::abs(det) < 1e-6) {  // parallel sides, a slot's end: the radius is half the gap
                const double gap = std::abs(signedDist(before, after.p0));
                r = 0.5 * gap;
                c = c - n1 * (signedDist(before, c) - s1 * r);
            } else {
                r = 0.5 * (std::abs(signedDist(before, c)) + std::abs(signedDist(after, c)));
                // n1·c = n1·p1 + s1 r, n2·c = n2·p2 + s2 r
                const double e1 = n1.dot(before.p0) + s1 * r, e2 = n2.dot(after.p0) + s2 * r;
                c = Vec2d((e1 * n2.y() - e2 * n1.y()) / det, (n1.x() * e2 - n2.x() * e1) / det);
            }
        } else if (t1 || t2) {
            const Piece& L = t1 ? before : after;
            const double sd = signedDist(L, c);
            c = c - normalOf(L) * (sd - (sd >= 0 ? 1 : -1) * r);
        }
        if (!(t1 || t2)) continue;
        if ((c - pc.centre).norm() > std::max(10 * o.tolerance, 0.2 * pc.radius)) continue;  // not really tangent: keep the fit
        if (t1) joint[k] = c - normalOf(before) * signedDist(before, c);
        else joint[k] = c + r * (joint[k] - c).normalized();
        if (t2) joint[(k + 1) % m] = c - normalOf(after) * signedDist(after, c);
        else joint[(k + 1) % m] = c + r * (joint[(k + 1) % m] - c).normalized();
        centreOf[k] = c;
    }
    // Every joint is final now: each arc gets its centre on its chord's
    // bisector (both ends exactly on its circle, whatever its neighbours did
    // to the joints), and its sense from the way the section turns round it.
    Loop l;
    std::vector<std::size_t> pieceOf;  // the piece each segment came from
    for (std::size_t k = 0; k < m; ++k) {
        const Piece& pc = pieces[k];
        const Vec2d a = joint[k], b = joint[(k + 1) % m];
        if ((b - a).norm() < 1e-9) continue;
        pieceOf.push_back(k);
        if (pc.spline) {
            std::vector<Vec2d> pts;
            for (std::size_t t = pc.from + 1; t < (pc.to < pc.from ? pc.to + n : pc.to); ++t) pts.push_back(q[t % n]);
            // As many poles as the run needs (up to 200); a run that still
            // misses the tolerance is split at its middle point, each half
            // fitted again (a slight corner at the split, within tolerance).
            const std::function<void(const std::vector<Vec2d>&, Vec2d, Vec2d, int)> addSpline =
                [&](const std::vector<Vec2d>& ps, Vec2d from, Vec2d to, int depth) {
                    const int maxPoles = std::clamp(static_cast<int>(ps.size() / 2), 40, 200);
                    const CurveFit f = fitCurve(ps, from, to, o.tolerance, maxPoles);
                    if (f.maxDeviation <= o.tolerance || ps.size() < 16 || depth >= 5) {
                        l.segments.push_back(Segment::spline(f.poles));
                        pieceOf.push_back(k);
                        return;
                    }
                    const std::size_t mid = ps.size() / 2;
                    const std::vector<Vec2d> left(ps.begin(), ps.begin() + static_cast<std::ptrdiff_t>(mid));
                    const std::vector<Vec2d> right(ps.begin() + static_cast<std::ptrdiff_t>(mid) + 1, ps.end());
                    addSpline(left, from, ps[mid], depth + 1);
                    addSpline(right, ps[mid], to, depth + 1);
                };
            pieceOf.pop_back();  // added below, once per spline
            addSpline(pts, a, b, 0);
            continue;
        }
        if (!pc.arc) {
            l.segments.push_back(Segment::line(a, b));
            continue;
        }
        const Vec2d mid = 0.5 * (a + b), along = (b - a).normalized(), across(-along.y(), along.x());
        const Vec2d target = centreOf[k] ? *centreOf[k] : pc.centre;
        const Vec2d c = mid + across * (target - mid).dot(across);
        // The angle the section's points sweep round the centre, signed.
        double sweep = 0;
        const std::size_t sweepTo = pc.to < pc.from ? pc.to + n : pc.to;
        for (std::size_t t = pc.from; t < sweepTo; ++t) {
            const Vec2d u = q[t % n] - c, v = q[(t + 1) % n] - c;
            sweep += std::atan2(u.x() * v.y() - u.y() * v.x(), u.dot(v));
        }
        l.segments.push_back(Segment::arc(a, b, c, sweep >= 0));
    }
    // Each segment against its own points: a piece that strays (a bad joint,
    // an arc the long way round) is mended or replaced by a line.
    // Only the middle of a piece's points: its ends belong partly to the
    // neighbours once the joints have moved to corners and tangent points.
    auto worst = [&](const Segment& s, std::size_t k) {
        std::size_t to = pieces[k].to;
        if (to < pieces[k].from) to += n;  // the piece that wraps round the start
        const std::size_t trim = (to - pieces[k].from) / 5;
        double w = 0;
        for (std::size_t t = pieces[k].from + trim; t + trim <= to; ++t) w = std::max(w, distToSegment(q[t % n], s));
        return w;
    };
    const double strayLimit = 3 * o.tolerance;
    for (std::size_t si = 0; si < l.segments.size(); ++si) {
        Segment& s = l.segments[si];
        if (s.kind != Segment::Kind::Arc) continue;
        const std::size_t k = pieceOf[si];
        if (worst(s, k) <= strayLimit) continue;
        Segment flipped = s;
        flipped.ccw = !s.ccw;
        const Segment line = Segment::line(s.a, s.b);
        const double wf = worst(flipped, k), wl = worst(line, k), ws = worst(s, k);
        if (wf < ws && wf <= wl) s = flipped;
        else if (wl < ws) s = line;
    }
    // A loop that is nearly round is a hole: a whole circle (scanned holes
    // are seldom round to the tolerance of the outline, and the fit then
    // breaks them into arcs and short flats). Round means: within three
    // tolerances of one circle, with its area, and no flat longer than the radius.
    if (l.segments.size() >= 2)
        if (auto c = fitArc(p, 0, n - 1, strayLimit, 1e9)) {
            bool round = std::abs(std::abs(l.area()) - kPi * c->radius * c->radius) < 0.05 * kPi * c->radius * c->radius;
            for (const auto& s : l.segments) round = round && (s.kind != Segment::Kind::Line || s.length() < c->radius);
            if (round) {
                l.segments = {Segment::circle(c->centre, c->radius)};
                out.loop = l;
                for (const auto& r : raw) out.maxDeviation = std::max(out.maxDeviation, std::abs((r - c->centre).norm() - c->radius));
                return out;
            }
        }
    out.loop = l;
    // Splines as fine polylines, once (sampling one per point is slow).
    std::vector<Segment> flat;
    for (const auto& s : l.segments) {
        if (s.kind != Segment::Kind::Spline) {
            flat.push_back(s);
            continue;
        }
        const auto pts = s.sample(std::max(0.005, 0.25 * o.tolerance));
        for (std::size_t k = 0; k + 1 < pts.size(); ++k) flat.push_back(Segment::line(pts[k], pts[k + 1]));
    }
    for (const auto& r : raw) {
        double d = 1e300;
        for (const auto& s : flat) d = std::min(d, distToSegment(r, s));
        out.maxDeviation = std::max(out.maxDeviation, d);
    }
    return out;
}

}  // namespace orv

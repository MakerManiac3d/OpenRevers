// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/Distance.h"

#include <nanoflann.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>

namespace orv {
namespace {

struct Cloud {
    std::vector<Vec3f> pts;
    std::size_t kdtree_get_point_count() const { return pts.size(); }
    float kdtree_get_pt(std::size_t i, std::size_t d) const { return pts[i][static_cast<Eigen::Index>(d)]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};
using Tree = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, Cloud>, Cloud, 3, std::uint32_t>;

/// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5).
Vec3d closestOnTriangle(const Vec3d& p, const Vec3d& a, const Vec3d& b, const Vec3d& c) {
    const Vec3d ab = b - a, ac = c - a, ap = p - a;
    const double d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const Vec3d bp = p - b;
    const double d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const Vec3d cp = p - c;
    const double d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double den = 1.0 / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

}  // namespace

struct MeshDistance::Impl {
    std::vector<std::array<Vec3d, 3>> tri;
    std::vector<Vec3d> normal;
    Cloud cloud;
    std::unique_ptr<Tree> tree;
};

MeshDistance::MeshDistance(const TriMesh& m, double maxEdge) : d_(std::make_unique<Impl>()) {
    if (maxEdge <= 0) {
        const auto b = m.bounds();
        maxEdge = std::max(1e-3, 0.01 * static_cast<double>((b.max - b.min).norm()));
    }
    std::vector<std::array<Vec3d, 3>> todo;
    for (const Tri& t : m.faces) todo.push_back({m.vertices[t[0]].cast<double>(), m.vertices[t[1]].cast<double>(), m.vertices[t[2]].cast<double>()});
    while (!todo.empty()) {
        auto t = todo.back();
        todo.pop_back();
        int longest = 0;
        double len = 0;
        for (int k = 0; k < 3; ++k) {
            const double l = (t[(k + 1) % 3] - t[k]).norm();
            if (l > len) { len = l; longest = k; }
        }
        if (len <= maxEdge || d_->tri.size() > 20'000'000) {
            const Vec3d n = (t[1] - t[0]).cross(t[2] - t[0]);
            if (n.norm() <= 0) continue;
            d_->tri.push_back(t);
            d_->normal.push_back(n.normalized());
            continue;
        }
        const Vec3d mid = 0.5 * (t[longest] + t[(longest + 1) % 3]);
        const Vec3d a = t[longest], b = t[(longest + 1) % 3], c = t[(longest + 2) % 3];
        todo.push_back({a, mid, c});
        todo.push_back({mid, b, c});
    }
    d_->cloud.pts.reserve(d_->tri.size());
    for (const auto& t : d_->tri) d_->cloud.pts.push_back(((t[0] + t[1] + t[2]) / 3.0).cast<float>());
    d_->tree = std::make_unique<Tree>(3, d_->cloud, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    d_->tree->buildIndex();
}

MeshDistance::~MeshDistance() = default;

double MeshDistance::signedDistance(const Vec3d& p) const {
    if (d_->tri.empty()) return 0;
    constexpr std::size_t K = 32;
    std::array<std::uint32_t, K> idx{};
    std::array<float, K> d2{};
    const Vec3f q = p.cast<float>();
    const std::size_t n = d_->tree->knnSearch(q.data(), K, idx.data(), d2.data());
    std::array<double, K> dist{};
    std::array<Vec3d, K> closest{};
    double best = 1e300;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& t = d_->tri[idx[i]];
        closest[i] = closestOnTriangle(p, t[0], t[1], t[2]);
        dist[i] = (p - closest[i]).norm();
        best = std::min(best, dist[i]);
    }
    // When the nearest place is an edge or a corner, several faces are as
    // near: their normals together (a pseudo-normal) tell inside from
    // outside; one of them alone can say the opposite.
    const double tie = 1e-6 * std::max(1.0, best);
    Vec3d sum = Vec3d::Zero(), at = Vec3d::Zero();
    for (std::size_t i = 0; i < n; ++i) {
        if (dist[i] > best + tie) continue;
        sum += d_->normal[idx[i]];
        at = closest[i];
    }
    const double side = (p - at).dot(sum);
    return (side >= 0 ? 1.0 : -1.0) * best;
}

DeviationStats deviation(const std::vector<Vec3f>& points, const TriMesh& mesh, double tol, std::size_t maxPoints, std::vector<double>* out) {
    DeviationStats s;
    if (points.empty() || mesh.faces.empty()) return s;
    const MeshDistance md(mesh);
    const std::size_t step = std::max<std::size_t>(1, points.size() / std::max<std::size_t>(maxPoints, 1));
    std::vector<double> d;
    for (std::size_t i = 0; i < points.size(); i += step) d.push_back(md.signedDistance(points[i].cast<double>()));
    s.points = d.size();
    double sum = 0, sq = 0;
    std::size_t within = 0;
    s.minSigned = 1e300;
    s.maxSigned = -1e300;
    std::vector<double> a(d.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        sum += d[i];
        sq += d[i] * d[i];
        a[i] = std::abs(d[i]);
        within += a[i] <= tol ? 1 : 0;
        s.minSigned = std::min(s.minSigned, d[i]);
        s.maxSigned = std::max(s.maxSigned, d[i]);
    }
    s.mean = sum / static_cast<double>(d.size());
    s.rms = std::sqrt(sq / static_cast<double>(d.size()));
    std::sort(a.begin(), a.end());
    auto q = [&](double f) { return a[static_cast<std::size_t>(f * static_cast<double>(a.size() - 1))]; };
    s.median = q(0.5);
    s.p90 = q(0.9);
    s.p95 = q(0.95);
    s.maxAbs = a.back();
    s.withinFraction = static_cast<double>(within) / static_cast<double>(d.size());
    if (out) *out = std::move(d);
    return s;
}

std::vector<SectionPolyline> sectionMesh(const TriMesh& m, const Vec3d& origin, const Vec3d& normalIn, const Vec3d& xIn) {
    const Vec3d n = normalIn.normalized();
    Vec3d x = xIn - xIn.dot(n) * n;
    if (x.norm() < 1e-9) x = n.unitOrthogonal();
    x.normalize();
    const Vec3d y = n.cross(x);
    std::vector<double> h(m.vertices.size());
    for (std::size_t i = 0; i < h.size(); ++i) {
        h[i] = (m.vertices[i].cast<double>() - origin).dot(n);
        if (h[i] == 0) h[i] = 1e-12;  // a vertex on the plane counts as above it
    }
    // One point per cut edge, shared by the two faces on it.
    std::unordered_map<std::uint64_t, std::uint32_t> onEdge;
    std::vector<Eigen::Vector2d> pts;
    auto edgePoint = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t key = a < b ? (static_cast<std::uint64_t>(a) << 32) | b : (static_cast<std::uint64_t>(b) << 32) | a;
        auto it = onEdge.find(key);
        if (it != onEdge.end()) return it->second;
        const double t = h[a] / (h[a] - h[b]);
        const Vec3d p = m.vertices[a].cast<double>() + t * (m.vertices[b].cast<double>() - m.vertices[a].cast<double>()) - origin;
        const auto id = static_cast<std::uint32_t>(pts.size());
        pts.emplace_back(p.dot(x), p.dot(y));
        onEdge.emplace(key, id);
        return id;
    };
    std::vector<std::vector<std::uint32_t>> link;
    for (const Tri& t : m.faces) {
        std::array<std::uint32_t, 2> seg{};
        int k = 0;
        for (int e = 0; e < 3; ++e) {
            const auto a = t[e], b = t[(e + 1) % 3];
            if ((h[a] > 0) != (h[b] > 0) && k < 2) seg[k++] = edgePoint(a, b);
        }
        if (k != 2 || seg[0] == seg[1]) continue;
        link.resize(pts.size());
        link[seg[0]].push_back(seg[1]);
        link[seg[1]].push_back(seg[0]);
    }
    link.resize(pts.size());
    std::vector<char> used(pts.size(), 0);
    std::vector<SectionPolyline> out;
    auto walk = [&](std::uint32_t start) {
        SectionPolyline pl;
        std::uint32_t prev = UINT32_MAX, cur = start;
        while (true) {
            used[cur] = 1;
            pl.points.push_back(pts[cur]);
            std::uint32_t next = UINT32_MAX;
            for (auto nb : link[cur])
                if (nb != prev && !used[nb]) { next = nb; break; }
            if (next == UINT32_MAX) {
                for (auto nb : link[cur])
                    if (nb == start && pl.points.size() > 2) pl.closed = true;
                break;
            }
            prev = cur;
            cur = next;
        }
        return pl;
    };
    // Open chains first, from their ends; then the closed loops.
    for (std::uint32_t i = 0; i < pts.size(); ++i)
        if (!used[i] && link[i].size() == 1) out.push_back(walk(i));
    for (std::uint32_t i = 0; i < pts.size(); ++i)
        if (!used[i] && !link[i].empty()) out.push_back(walk(i));
    std::sort(out.begin(), out.end(), [](const SectionPolyline& a, const SectionPolyline& b) { return a.points.size() > b.points.size(); });
    return out;
}

namespace {
void dp(const std::vector<Eigen::Vector2d>& p, std::size_t i, std::size_t j, double tol, std::vector<char>& keep) {
    if (j <= i + 1) return;
    const Eigen::Vector2d a = p[i], b = p[j], ab = b - a;
    const double L = ab.norm();
    double worst = -1;
    std::size_t at = i;
    for (std::size_t k = i + 1; k < j; ++k) {
        const Eigen::Vector2d ap = p[k] - a;
        const double d = L > 1e-15 ? std::abs(ab.x() * ap.y() - ab.y() * ap.x()) / L : ap.norm();
        if (d > worst) { worst = d; at = k; }
    }
    if (worst > tol) {
        keep[at] = 1;
        dp(p, i, at, tol, keep);
        dp(p, at, j, tol, keep);
    }
}
}  // namespace

std::vector<Eigen::Vector2d> simplifyPolyline(const std::vector<Eigen::Vector2d>& p, double tol, bool closed) {
    if (p.size() < 3) return p;
    std::vector<char> keep(p.size(), 0);
    if (closed) {
        // Split at the point farthest from the first one.
        std::size_t far = 0;
        for (std::size_t k = 1; k < p.size(); ++k)
            if ((p[k] - p[0]).norm() > (p[far] - p[0]).norm()) far = k;
        std::vector<Eigen::Vector2d> q = p;
        q.push_back(p[0]);
        std::vector<char> kq(q.size(), 0);
        kq[0] = kq[far] = kq.back() = 1;
        dp(q, 0, far, tol, kq);
        dp(q, far, q.size() - 1, tol, kq);
        std::vector<Eigen::Vector2d> out;
        for (std::size_t k = 0; k + 1 < q.size(); ++k)
            if (kq[k]) out.push_back(q[k]);
        return out;
    }
    keep.front() = keep.back() = 1;
    dp(p, 0, p.size() - 1, tol, keep);
    std::vector<Eigen::Vector2d> out;
    for (std::size_t k = 0; k < p.size(); ++k)
        if (keep[k]) out.push_back(p[k]);
    return out;
}

}  // namespace orv

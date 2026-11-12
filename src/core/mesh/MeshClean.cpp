// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/MeshClean.h"

#include <Eigen/Dense>
#include <nanoflann.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace orv {

namespace {
struct Pts {
    const std::vector<Vec3f>* p;
    std::size_t kdtree_get_point_count() const { return p->size(); }
    float kdtree_get_pt(std::size_t i, std::size_t d) const { return (*p)[i][static_cast<Eigen::Index>(d)]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};
using Tree = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, Pts>, Pts, 3, std::uint32_t>;
}  // namespace

std::optional<PlaneFound> findLargestPlane(const TriMesh& m, double distance, unsigned seed) {
    const auto& v = m.vertices;
    if (v.size() < 10) return std::nullopt;
    // Work on a sample: tens of thousands of points decide a plane as well as millions.
    std::vector<Vec3d> s;
    const std::size_t step = std::max<std::size_t>(1, v.size() / 50000);
    for (std::size_t i = 0; i < v.size(); i += step) s.push_back(v[i].cast<double>());
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, s.size() - 1);
    std::size_t best = 0;
    Vec3d bp = Vec3d::Zero(), bn = Vec3d::UnitZ();
    for (int it = 0; it < 600; ++it) {
        const Vec3d a = s[pick(rng)], b = s[pick(rng)], c = s[pick(rng)];
        Vec3d n = (b - a).cross(c - a);
        if (n.norm() < 1e-9) continue;
        n.normalize();
        std::size_t cnt = 0;
        for (const auto& p : s)
            if (std::abs((p - a).dot(n)) < distance) ++cnt;
        if (cnt > best) { best = cnt; bp = a; bn = n; }
    }
    if (static_cast<double>(best) < 0.02 * static_cast<double>(s.size()) || best < 3) return std::nullopt;
    // Least squares on the points of that plane, twice (the band follows the better plane).
    for (int pass = 0; pass < 2; ++pass) {
        Vec3d c = Vec3d::Zero();
        std::size_t k = 0;
        for (const auto& p : s)
            if (std::abs((p - bp).dot(bn)) < distance) { c += p; ++k; }
        if (k < 3) break;
        c /= static_cast<double>(k);
        Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
        for (const auto& p : s)
            if (std::abs((p - bp).dot(bn)) < distance) C += (p - c) * (p - c).transpose();
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
        Vec3d n = es.eigenvectors().col(0);
        if (n.dot(bn) < 0) n = -n;
        bp = c;
        bn = n.normalized();
    }
    PlaneFound f;
    f.point = bp;
    // The object is on the side with more of what is off the plane.
    std::size_t above = 0, below = 0;
    for (const auto& p : s) {
        const double d = (p - bp).dot(bn);
        if (d >= distance) ++above;
        else if (d <= -distance) ++below;
    }
    f.normal = above >= below ? bn : Vec3d(-bn);
    std::size_t on = 0;
    for (const auto& p : v)
        if (std::abs((p.cast<double>() - bp).dot(bn)) < distance) ++on;
    f.inliers = on;
    f.share = static_cast<double>(on) / static_cast<double>(v.size());
    return f;
}

std::size_t removeVertices(TriMesh& m, const std::function<bool(const Vec3f&)>& drop) {
    std::vector<char> gone(m.vertices.size(), 0);
    std::size_t n = 0;
    for (std::size_t i = 0; i < m.vertices.size(); ++i)
        if (drop(m.vertices[i])) { gone[i] = 1; ++n; }
    if (!n) return 0;
    if (m.isPointCloud()) {
        std::vector<Vec3f> v, nn;
        const bool hn = m.hasNormals();
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            if (gone[i]) continue;
            v.push_back(m.vertices[i]);
            if (hn) nn.push_back(m.normals[i]);
        }
        m.vertices = std::move(v);
        m.normals = std::move(nn);
        return n;
    }
    std::vector<Tri> keep;
    keep.reserve(m.faces.size());
    for (const Tri& t : m.faces)
        if (!gone[t[0]] && !gone[t[1]] && !gone[t[2]]) keep.push_back(t);
    m.faces = std::move(keep);
    m.dropQuads();
    const std::size_t before = m.vertices.size();
    if (m.faces.empty()) {
        m.vertices.clear();
        m.normals.clear();
        return before;
    }
    m.removeUnusedVertices();
    return before - m.vertices.size();
}

std::size_t removePlane(TriMesh& m, const PlaneFound& p, double distance, bool beyond) {
    const Vec3d n = p.normal.normalized();
    return removeVertices(m, [&](const Vec3f& v) {
        const double d = (v.cast<double>() - p.point).dot(n);
        return std::abs(d) < distance || (beyond && d < 0);
    });
}

std::size_t removeOutliers(TriMesh& m, int neighbours, double ratio) {
    const std::size_t n = m.vertices.size();
    if (n < static_cast<std::size_t>(neighbours) + 2) return 0;
    Pts a{&m.vertices};
    Tree t(3, a, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    t.buildIndex();
    const std::size_t k = static_cast<std::size_t>(neighbours) + 1;  // the point itself comes first
    std::vector<double> mean(n);
    std::vector<std::uint32_t> idx(k);
    std::vector<float> d2(k);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t got = t.knnSearch(m.vertices[i].data(), k, idx.data(), d2.data());
        double s = 0;
        for (std::size_t j = 1; j < got; ++j) s += std::sqrt(static_cast<double>(d2[j]));
        mean[i] = got > 1 ? s / static_cast<double>(got - 1) : 0;
    }
    double mu = 0, sq = 0;
    for (double x : mean) { mu += x; sq += x * x; }
    mu /= static_cast<double>(n);
    const double sd = std::sqrt(std::max(0.0, sq / static_cast<double>(n) - mu * mu));
    const double limit = mu + ratio * sd;
    std::size_t i = 0;
    return removeVertices(m, [&](const Vec3f&) { return mean[i++] > limit; });
}

std::size_t removeFaces(TriMesh& m, const std::vector<std::uint32_t>& faces) {
    if (m.isPointCloud()) {
        std::vector<char> gone(m.vertices.size(), 0);
        for (auto f : faces)
            if (f < gone.size()) gone[f] = 1;
        std::size_t i = 0;
        return removeVertices(m, [&](const Vec3f&) { return gone[i++] != 0; });
    }
    std::vector<char> gone(m.faces.size(), 0);
    std::size_t n = 0;
    for (auto f : faces)
        if (f < gone.size() && !gone[f]) { gone[f] = 1; ++n; }
    std::vector<Tri> keep;
    keep.reserve(m.faces.size() - n);
    for (std::size_t f = 0; f < m.faces.size(); ++f)
        if (!gone[f]) keep.push_back(m.faces[f]);
    m.faces = std::move(keep);
    m.dropQuads();
    if (m.faces.empty()) { m.vertices.clear(); m.normals.clear(); }
    else m.removeUnusedVertices();
    return n;
}

}  // namespace orv

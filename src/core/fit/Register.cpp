// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fit/Register.h"
#include "core/util/Cancel.h"

#include <Eigen/Dense>
#include <nanoflann.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace orv {

namespace {

struct Pts3 {
    const std::vector<Vec3d>* p;
    std::size_t kdtree_get_point_count() const { return p->size(); }
    double kdtree_get_pt(std::size_t i, std::size_t d) const { return (*p)[i][static_cast<Eigen::Index>(d)]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};
using Tree3 = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<double, Pts3>, Pts3, 3, std::uint32_t>;

using Feature = std::array<float, 33>;
struct Feats {
    const std::vector<Feature>* f;
    std::size_t kdtree_get_point_count() const { return f->size(); }
    float kdtree_get_pt(std::size_t i, std::size_t d) const { return (*f)[i][d]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};
using TreeF = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, Feats>, Feats, 33, std::uint32_t>;

std::vector<std::pair<std::uint32_t, double>> within(const Tree3& t, const Vec3d& q, double r) {
    std::vector<nanoflann::ResultItem<std::uint32_t, double>> m;
    t.radiusSearch(q.data(), r * r, m, nanoflann::SearchParameters());
    std::vector<std::pair<std::uint32_t, double>> out;
    out.reserve(m.size());
    for (const auto& x : m) out.emplace_back(x.first, std::sqrt(x.second));
    return out;
}

std::uint32_t nearest(const Tree3& t, const Vec3d& q, double* dist) {
    std::uint32_t i = 0;
    double d2 = 0;
    t.knnSearch(q.data(), 1, &i, &d2);
    if (dist) *dist = std::sqrt(d2);
    return i;
}

}  // namespace

Transform rigidFromPairs(const std::vector<Vec3d>& from, const std::vector<Vec3d>& to, std::vector<double>* residuals) {
    if (from.size() != to.size()) throw std::invalid_argument("the two lists of points must be the same length");
    if (from.size() < 3) throw std::invalid_argument("at least three pairs of points are needed");
    const double n = static_cast<double>(from.size());
    Vec3d ca = Vec3d::Zero(), cb = Vec3d::Zero();
    for (std::size_t i = 0; i < from.size(); ++i) { ca += from[i]; cb += to[i]; }
    ca /= n;
    cb /= n;
    Eigen::Matrix3d H = Eigen::Matrix3d::Zero(), S = Eigen::Matrix3d::Zero();
    for (std::size_t i = 0; i < from.size(); ++i) {
        H += (from[i] - ca) * (to[i] - cb).transpose();
        S += (from[i] - ca) * (from[i] - ca).transpose();
    }
    // Points nearly on a line do not fix the turn about it.
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(S);
    const double spread = std::sqrt(std::max(0.0, es.eigenvalues()(2) / n)), across = std::sqrt(std::max(0.0, es.eigenvalues()(1) / n));
    if (spread <= 0 || across < 1e-3 * spread) throw std::invalid_argument("the points are on a line: pick them spread out, not in a row");
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix3d D = Eigen::Matrix3d::Identity();
    if ((svd.matrixV() * svd.matrixU().transpose()).determinant() < 0) D(2, 2) = -1;
    Transform t = Transform::Identity();
    t.linear() = svd.matrixV() * D * svd.matrixU().transpose();
    t.translation() = cb - t.linear() * ca;
    if (residuals) {
        residuals->clear();
        for (std::size_t i = 0; i < from.size(); ++i) residuals->push_back((t * from[i] - to[i]).norm());
    }
    return t;
}

SampledCloud sampleForRegistration(const TriMesh& m, double voxel) {
    std::vector<Vec3f> normals;
    if (!m.isPointCloud()) normals = m.vertexNormals();
    else if (m.hasNormals()) normals = m.normals;
    struct Cell {
        Vec3d p{Vec3d::Zero()}, n{Vec3d::Zero()};
        int count{0};
    };
    std::unordered_map<std::uint64_t, Cell> cells;
    auto key = [&](const Vec3f& v) {
        auto c = [&](float x) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(x / voxel)) + (1 << 20)) & 0x1FFFFF; };
        return c(v.x()) | (c(v.y()) << 21) | (c(v.z()) << 42);
    };
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        Cell& c = cells[key(m.vertices[i])];
        c.p += m.vertices[i].cast<double>();
        if (!normals.empty()) c.n += normals[i].cast<double>();
        ++c.count;
    }
    SampledCloud out;
    out.points.reserve(cells.size());
    for (auto& [k, c] : cells) {
        out.points.push_back(c.p / c.count);
        out.normals.push_back(c.n.norm() > 1e-12 ? Vec3d(c.n.normalized()) : Vec3d::Zero());
    }
    // Points without normals (a bare cloud): from their neighbours, away from the centre.
    const bool missing = std::any_of(out.normals.begin(), out.normals.end(), [](const Vec3d& n) { return n.squaredNorm() < 0.5; });
    if (missing && out.points.size() >= 4) {
        Pts3 a{&out.points};
        Tree3 t(3, a, nanoflann::KDTreeSingleIndexAdaptorParams(16));
        t.buildIndex();
        Vec3d centre = Vec3d::Zero();
        for (const auto& p : out.points) centre += p;
        centre /= static_cast<double>(out.points.size());
        for (std::size_t i = 0; i < out.points.size(); ++i) {
            if (out.normals[i].squaredNorm() > 0.5) continue;
            const std::size_t k = std::min<std::size_t>(12, out.points.size());
            std::vector<std::uint32_t> idx(k);
            std::vector<double> d2(k);
            t.knnSearch(out.points[i].data(), k, idx.data(), d2.data());
            Vec3d c = Vec3d::Zero();
            for (auto j : idx) c += out.points[j];
            c /= static_cast<double>(k);
            Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
            for (auto j : idx) C += (out.points[j] - c) * (out.points[j] - c).transpose();
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
            Vec3d nn = es.eigenvectors().col(0);
            if (nn.dot(out.points[i] - centre) < 0) nn = -nn;
            out.normals[i] = nn;
        }
    }
    return out;
}

std::vector<Feature> fpfh(const SampledCloud& c, double radius) {
    const std::size_t n = c.points.size();
    Pts3 a{&c.points};
    Tree3 tree(3, a, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    tree.buildIndex();
    std::vector<std::vector<std::pair<std::uint32_t, double>>> nb(n);
    for (std::size_t i = 0; i < n; ++i) nb[i] = within(tree, c.points[i], radius);
    // Simplified point feature histograms: three angles of each neighbour pair, 11 bins each.
    const double pi = std::numbers::pi;
    auto bin = [](double v, double lo, double hi) { return std::clamp(static_cast<int>(std::floor(11 * (v - lo) / (hi - lo))), 0, 10); };
    std::vector<Feature> spfh(n);
    for (std::size_t i = 0; i < n; ++i) {
        Feature& h = spfh[i];
        h.fill(0.f);
        int count = 0;
        for (const auto& [j, dist] : nb[i]) {
            if (j == i || dist <= 1e-12) continue;
            Vec3d p1 = c.points[i], n1 = c.normals[i], p2 = c.points[j], n2 = c.normals[j];
            Vec3d dp = p2 - p1;
            const double a1 = n1.dot(dp) / dist, a2 = n2.dot(dp) / dist;
            double f3;
            if (std::acos(std::clamp(std::abs(a1), 0.0, 1.0)) > std::acos(std::clamp(std::abs(a2), 0.0, 1.0))) {
                std::swap(p1, p2);
                std::swap(n1, n2);
                dp = -dp;
                f3 = -a2;
            } else {
                f3 = a1;
            }
            Vec3d v = dp.cross(n1);
            if (v.norm() < 1e-12) continue;
            v.normalize();
            const Vec3d w = n1.cross(v);
            const double f2 = v.dot(n2), f1 = std::atan2(w.dot(n2), n1.dot(n2));
            h[static_cast<std::size_t>(bin(f1, -pi, pi))] += 1;
            h[static_cast<std::size_t>(11 + bin(f2, -1, 1))] += 1;
            h[static_cast<std::size_t>(22 + bin(f3, -1, 1))] += 1;
            ++count;
        }
        if (count) for (auto& x : h) x *= 100.f / static_cast<float>(count);
    }
    std::vector<Feature> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        Feature acc{};
        acc.fill(0.f);
        for (const auto& [j, dist] : nb[i]) {
            if (j == i || dist <= 1e-12) continue;
            const float wgt = static_cast<float>(1.0 / dist);
            for (std::size_t b = 0; b < 33; ++b) acc[b] += wgt * spfh[j][b];
        }
        for (int blk = 0; blk < 3; ++blk) {
            float s = 0;
            for (int b = 0; b < 11; ++b) s += acc[static_cast<std::size_t>(blk * 11 + b)];
            const float k = s > 0 ? 100.f / s : 0.f;
            for (int b = 0; b < 11; ++b) {
                const auto at = static_cast<std::size_t>(blk * 11 + b);
                out[i][at] = spfh[i][at] + acc[at] * k;
            }
        }
    }
    return out;
}

GlobalRegResult globalRegistration(const TriMesh& moving, const TriMesh& reference, const GlobalRegOptions& o) {
    GlobalRegResult res;
    const auto rb = reference.bounds(), mb = moving.bounds();
    const double size = std::max(rb.diagonal(), mb.diagonal());
    const double voxel = o.voxel > 0 ? o.voxel : std::max(0.05, size / 60.0);
    res.voxel = voxel;
    const SampledCloud A = sampleForRegistration(moving, voxel), B = sampleForRegistration(reference, voxel);
    if (A.points.size() < 20 || B.points.size() < 20) throw std::runtime_error("too few points to compare shapes");
    const std::vector<Feature> fa = fpfh(A, 5 * voxel), fb = fpfh(B, 5 * voxel);

    // Each moving point's look-alike in the reference, kept when the match is mutual.
    Feats ab{&fb}, aa{&fa};
    TreeF tb(33, ab, nanoflann::KDTreeSingleIndexAdaptorParams(16)), ta(33, aa, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    tb.buildIndex();
    ta.buildIndex();
    auto nn = [](const TreeF& t, const Feature& f) {
        std::uint32_t i = 0;
        float d2 = 0;
        t.knnSearch(f.data(), 1, &i, &d2);
        return i;
    };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> all, mutual;
    for (std::uint32_t i = 0; i < fa.size(); ++i) {
        const std::uint32_t j = nn(tb, fa[i]);
        all.emplace_back(i, j);
        if (nn(ta, fb[j]) == i) mutual.emplace_back(i, j);
    }
    const auto& corr = mutual.size() >= 30 ? mutual : all;
    res.correspondences = corr.size();

    Pts3 bp{&B.points};
    Tree3 tree(3, bp, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    tree.buildIndex();
    const double inlierDist = 2.0 * voxel;
    auto countInliers = [&](const Transform& t, std::vector<std::size_t>* which) {
        std::size_t c = 0;
        for (std::size_t k = 0; k < corr.size(); ++k)
            if ((t * A.points[corr[k].first] - B.points[corr[k].second]).norm() < inlierDist) {
                ++c;
                if (which) which->push_back(k);
            }
        return c;
    };
    std::mt19937 rng(o.seed);
    std::uniform_int_distribution<std::size_t> pick(0, corr.size() - 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(o.seconds);
    Transform best = Transform::Identity();
    std::size_t bestCount = 0;
    for (int it = 0; it < o.maxIterations; ++it) {
        if ((it & 63) == 0) checkCancel();
        if ((it & 255) == 0 && std::chrono::steady_clock::now() > deadline) break;
        std::size_t s[3] = {pick(rng), pick(rng), pick(rng)};
        if (s[0] == s[1] || s[1] == s[2] || s[0] == s[2]) continue;
        std::vector<Vec3d> pa, pb;
        bool ok = true;
        for (int u = 0; u < 3 && ok; ++u) {
            pa.push_back(A.points[corr[s[u]].first]);
            pb.push_back(B.points[corr[s[u]].second]);
        }
        // The triangle must have the same sides in both, and not be thin.
        for (int u = 0; u < 3 && ok; ++u) {
            const double la = (pa[static_cast<std::size_t>(u)] - pa[static_cast<std::size_t>((u + 1) % 3)]).norm();
            const double lb = (pb[static_cast<std::size_t>(u)] - pb[static_cast<std::size_t>((u + 1) % 3)]).norm();
            ok = la > 3 * voxel && lb > 3 * voxel && std::min(la, lb) / std::max(la, lb) > 0.9;
        }
        if (!ok) continue;
        Transform t;
        try {
            t = rigidFromPairs(pa, pb);
        } catch (const std::invalid_argument&) {
            continue;
        }
        const std::size_t c = countInliers(t, nullptr);
        if (c > bestCount) {
            bestCount = c;
            best = t;
        }
    }
    if (bestCount < 6) throw std::runtime_error("no consistent match between the two shapes (do they overlap enough?)");
    // Once more from every agreeing pair.
    std::vector<std::size_t> in;
    countInliers(best, &in);
    {
        std::vector<Vec3d> pa, pb;
        for (auto k : in) {
            pa.push_back(A.points[corr[k].first]);
            pb.push_back(B.points[corr[k].second]);
        }
        try {
            best = rigidFromPairs(pa, pb);
        } catch (const std::invalid_argument&) {}
    }
    res.inliers = countInliers(best, nullptr);
    std::size_t close = 0;
    for (const auto& p : A.points) {
        double d = 0;
        nearest(tree, best * p, &d);
        if (d < inlierDist) ++close;
    }
    res.coarseOverlap = static_cast<double>(close) / static_cast<double>(A.points.size());
    if (res.coarseOverlap < 0.1) throw std::runtime_error("the best match covers too little of the moving scan (do they overlap enough?)");
    res.transform = best;
    if (o.refine) {
        TriMesh moved = moving;
        transformMesh(moved, best);
        BestFitOptions bo;
        bo.maxDistance = 3 * voxel;
        res.refined = bestFit(moved, reference, bo);
        res.transform = res.refined.transform * best;
    }
    return res;
}

TriMesh mergeMeshes(const std::vector<const TriMesh*>& meshes) {
    TriMesh out;
    bool normals = true, clouds = false, solids = false;
    for (const TriMesh* m : meshes) {
        normals = normals && m->hasNormals();
        (m->isPointCloud() ? clouds : solids) = true;
    }
    if (clouds && solids) throw std::invalid_argument("a point cloud and a mesh cannot be joined: make the cloud a mesh first, or join clouds with clouds");
    for (const TriMesh* m : meshes) {
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.insert(out.vertices.end(), m->vertices.begin(), m->vertices.end());
        if (normals) out.normals.insert(out.normals.end(), m->normals.begin(), m->normals.end());
        for (const Tri& t : m->faces) out.faces.push_back({t[0] + base, t[1] + base, t[2] + base});
    }
    return out;
}

}  // namespace orv

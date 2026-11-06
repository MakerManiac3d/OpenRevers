// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/points/PointCloud.h"

#include "core/mesh/MeshAnalysis.h"
#include "core/mesh/MeshRepair.h"
#include "core/points/PoissonBridge.h"

#include <Eigen/Eigenvalues>
#include <nanoflann.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace orv {
namespace {

struct CloudAdaptor {
    const std::vector<Vec3f>& pts;
    std::size_t kdtree_get_point_count() const { return pts.size(); }
    float kdtree_get_pt(std::size_t i, std::size_t d) const { return pts[i][static_cast<Eigen::Index>(d)]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};
using KdTree = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, CloudAdaptor>, CloudAdaptor, 3, std::uint32_t>;

std::unique_ptr<KdTree> buildTree(const CloudAdaptor& a) {
    auto t = std::make_unique<KdTree>(3, a, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    t->buildIndex();
    return t;
}

}  // namespace

std::size_t thinPoints(TriMesh& c, float spacing) {
    if (spacing <= 0.f || c.vertices.empty()) return 0;
    struct CellHash {
        std::size_t operator()(const std::array<long long, 3>& k) const noexcept {
            return static_cast<std::size_t>(k[0] * 73856093ll ^ k[1] * 19349663ll ^ k[2] * 83492791ll);
        }
    };
    struct Cell { Vec3d sum{Vec3d::Zero()}; std::uint32_t count{0}; std::uint32_t best{UINT32_MAX}; double bestD{0}; };
    std::unordered_map<std::array<long long, 3>, Cell, CellHash> cells;
    cells.reserve(c.vertices.size() / 4 + 16);
    const double inv = 1.0 / spacing;
    std::vector<std::array<long long, 3>> key(c.vertices.size());
    for (std::size_t i = 0; i < c.vertices.size(); ++i) {
        const Vec3d p = c.vertices[i].cast<double>();
        key[i] = {static_cast<long long>(std::floor(p.x() * inv)), static_cast<long long>(std::floor(p.y() * inv)), static_cast<long long>(std::floor(p.z() * inv))};
        Cell& cell = cells[key[i]];
        cell.sum += p;
        ++cell.count;
    }
    for (std::size_t i = 0; i < c.vertices.size(); ++i) {
        Cell& cell = cells[key[i]];
        const double d = (c.vertices[i].cast<double>() - cell.sum / cell.count).squaredNorm();
        if (cell.best == UINT32_MAX || d < cell.bestD) { cell.best = static_cast<std::uint32_t>(i); cell.bestD = d; }
    }
    std::vector<char> keep(c.vertices.size(), 0);
    for (const auto& [k, cell] : cells) keep[cell.best] = 1;
    const bool n = c.hasNormals();
    std::size_t w = 0;
    for (std::size_t i = 0; i < c.vertices.size(); ++i) {
        if (!keep[i]) continue;
        c.vertices[w] = c.vertices[i];
        if (n) c.normals[w] = c.normals[i];
        ++w;
    }
    const std::size_t removed = c.vertices.size() - w;
    c.vertices.resize(w);
    if (n) c.normals.resize(w);
    return removed;
}

double meanPointSpacing(const TriMesh& c) {
    if (c.vertices.size() < 2) return 0.0;
    CloudAdaptor a{c.vertices};
    auto tree = buildTree(a);
    const std::size_t step = std::max<std::size_t>(1, c.vertices.size() / 20000);
    double sum = 0;
    std::size_t n = 0;
    std::uint32_t idx[2];
    float d2[2];
    for (std::size_t i = 0; i < c.vertices.size(); i += step) {
        if (tree->knnSearch(c.vertices[i].data(), 2, idx, d2) < 2) continue;
        sum += std::sqrt(static_cast<double>(d2[1]));
        ++n;
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}

NormalsResult estimateNormals(TriMesh& c, int neighbours, NormalOrientation orientation) {
    NormalsResult r;
    const std::size_t N = c.vertices.size();
    r.points = N;
    if (N < 3) throw std::runtime_error("too few points");
    const std::size_t k = static_cast<std::size_t>(std::clamp(neighbours, 3, 100));
    const std::vector<Vec3f> before = c.hasNormals() ? c.normals : std::vector<Vec3f>{};
    if (orientation == NormalOrientation::Keep && before.empty()) throw std::runtime_error("no normals to keep the orientation of");

    CloudAdaptor a{c.vertices};
    auto tree = buildTree(a);
    std::vector<std::uint32_t> nbr(N * k);  // neighbour lists, for the orientation graph
    std::vector<std::uint32_t> nbrCount(N, 0);
    std::vector<Vec3f> normals(N, Vec3f::Zero());
    std::vector<std::uint32_t> idx(k + 1);
    std::vector<float> d2(k + 1);
    double spacing = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t found = tree->knnSearch(c.vertices[i].data(), k + 1, idx.data(), d2.data());
        if (found >= 2) spacing += std::sqrt(static_cast<double>(d2[1]));
        Vec3d mean = Vec3d::Zero();
        for (std::size_t j = 0; j < found; ++j) mean += c.vertices[idx[j]].cast<double>();
        mean /= static_cast<double>(std::max<std::size_t>(found, 1));
        Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
        for (std::size_t j = 0; j < found; ++j) {
            const Vec3d d = c.vertices[idx[j]].cast<double>() - mean;
            cov += d * d.transpose();
        }
        std::uint32_t m = 0;
        for (std::size_t j = 0; j < found; ++j)
            if (idx[j] != i) nbr[i * k + m++] = idx[j];
        nbrCount[i] = std::min<std::uint32_t>(m, static_cast<std::uint32_t>(k));
        if (found < 3) { ++r.isolated; continue; }
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
        normals[i] = es.eigenvectors().col(0).normalized().cast<float>();  // smallest eigenvalue
    }
    r.meanSpacing = spacing / static_cast<double>(N);

    auto agree = [&](std::size_t i, const Vec3f& ref) {
        if (normals[i].dot(ref) < 0.f) { normals[i] = -normals[i]; ++r.flipped; }
    };
    if (orientation == NormalOrientation::Keep) {
        for (std::size_t i = 0; i < N; ++i) agree(i, before[i]);
    } else if (orientation == NormalOrientation::Outward) {
        Vec3d centre = Vec3d::Zero();
        for (const auto& p : c.vertices) centre += p.cast<double>();
        centre /= static_cast<double>(N);
        for (std::size_t i = 0; i < N; ++i) agree(i, (c.vertices[i].cast<double>() - centre).cast<float>());
    } else {
        // Minimum spanning tree over the neighbour graph, weight 1 - |ni·nj|:
        // the orientation travels first across the flattest places. Each
        // connected part starts from its highest point, normal turned up (+z).
        std::vector<char> done(N, 0);
        using Item = std::pair<float, std::pair<std::uint32_t, std::uint32_t>>;  // weight, (to, from)
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        std::vector<std::uint32_t> order(N);
        for (std::size_t i = 0; i < N; ++i) order[i] = static_cast<std::uint32_t>(i);
        std::sort(order.begin(), order.end(), [&](std::uint32_t x, std::uint32_t y) { return c.vertices[x].z() > c.vertices[y].z(); });
        for (std::uint32_t seed : order) {
            if (done[seed]) continue;
            agree(seed, Vec3f::UnitZ());
            done[seed] = 1;
            auto push = [&](std::uint32_t from) {
                for (std::uint32_t j = 0; j < nbrCount[from]; ++j) {
                    const std::uint32_t to = nbr[from * k + j];
                    if (!done[to]) heap.push({1.f - std::abs(normals[from].dot(normals[to])), {to, from}});
                }
            };
            push(seed);
            while (!heap.empty()) {
                const auto [w, e] = heap.top();
                heap.pop();
                const auto [to, from] = e;
                if (done[to]) continue;
                done[to] = 1;
                agree(to, normals[from]);
                push(to);
            }
        }
    }
    c.normals = std::move(normals);
    return r;
}

PoissonResult reconstructPoisson(const TriMesh& cloud, const PoissonOptions& o, TriMesh& out) {
    if (!cloud.hasNormals()) throw std::runtime_error("the points have no normals");
    if (cloud.vertices.size() < 10) throw std::runtime_error("too few points");
    poisson::Input in;
    in.points = cloud.vertices.front().data();
    in.normals = cloud.normals.front().data();
    static_assert(sizeof(Vec3f) == 12, "Vec3f must be three packed floats");
    in.count = cloud.vertices.size();
    in.depth = std::clamp(o.depth, 4, 14);
    in.pointWeight = o.pointWeight;
    in.samplesPerNode = o.samplesPerNode;
    poisson::Output res;
    if (const std::string err = poisson::solve(in, res); !err.empty()) throw std::runtime_error(err);
    if (res.faces.empty()) throw std::runtime_error("no surface was found");

    TriMesh m;
    const std::size_t nv = res.density.size();
    m.vertices.resize(nv);
    for (std::size_t i = 0; i < nv; ++i) m.vertices[i] = Vec3f(res.vertices[i * 3], res.vertices[i * 3 + 1], res.vertices[i * 3 + 2]);
    m.faces.reserve(res.faces.size() / 3);
    for (std::size_t f = 0; f + 2 < res.faces.size(); f += 3) m.faces.push_back({res.faces[f], res.faces[f + 1], res.faces[f + 2]});
    PoissonResult r;
    r.facesBeforeTrim = m.faces.size();

    if (o.trimQuantile > 0.f) {
        // Poisson closes the surface everywhere, also where there were no
        // points; there the point density is low. Cut the faces whose
        // corners fall below the chosen quantile of the density.
        std::vector<float> sorted = res.density;
        const auto q = static_cast<std::size_t>(std::clamp(o.trimQuantile, 0.f, 0.5f) * static_cast<float>(sorted.size() - 1));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(q), sorted.end());
        const float cut = sorted[q];
        std::erase_if(m.faces, [&](const Tri& t) { return res.density[t[0]] < cut || res.density[t[1]] < cut || res.density[t[2]] < cut; });
        r.facesTrimmed = r.facesBeforeTrim - m.faces.size();
        if (o.keepLargest && !m.faces.empty()) removeSmallComponents(m, 0, true);
    }
    m.removeUnusedVertices();
    if (m.faces.empty()) throw std::runtime_error("trimming left no surface");
    out = std::move(m);
    return r;
}

}  // namespace orv

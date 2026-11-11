// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fit/Align.h"

#include <Eigen/Dense>
#include <nanoflann.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace orv {

void transformMesh(TriMesh& m, const Transform& t) {
    for (auto& v : m.vertices) v = (t * v.cast<double>()).cast<float>();
    for (auto& n : m.normals) n = (t.linear() * n.cast<double>()).cast<float>();
}

Primitive transformPrimitive(const Primitive& p, const Transform& t) {
    Primitive q = p;
    q.origin = t * p.origin;
    q.axis = (t.linear() * p.axis).normalized();
    return q;
}

Transform principalAlignment(const TriMesh& m) {
    // Area-weighted moments of the surface (independent of how it is meshed).
    double area = 0;
    Vec3d c = Vec3d::Zero();
    for (const Tri& t : m.faces) {
        const Vec3d a = m.vertices[t[0]].cast<double>(), b = m.vertices[t[1]].cast<double>(), d = m.vertices[t[2]].cast<double>();
        const double ar = 0.5 * (b - a).cross(d - a).norm();
        area += ar;
        c += ar * (a + b + d) / 3.0;
    }
    if (area <= 0) {  // a point cloud: plain centroid
        for (const auto& v : m.vertices) c += v.cast<double>();
        c /= std::max<double>(1.0, static_cast<double>(m.vertices.size()));
    } else {
        c /= area;
    }
    Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
    auto addPoint = [&](const Vec3d& p, double w) { C += w * (p - c) * (p - c).transpose(); };
    if (area > 0) {
        for (const Tri& t : m.faces) {
            const Vec3d a = m.vertices[t[0]].cast<double>(), b = m.vertices[t[1]].cast<double>(), d = m.vertices[t[2]].cast<double>();
            const double ar = 0.5 * (b - a).cross(d - a).norm() / 3.0;
            addPoint((a + b) / 2, ar);  // the edge midpoints integrate quadratics exactly on a triangle
            addPoint((b + d) / 2, ar);
            addPoint((d + a) / 2, ar);
        }
    } else {
        for (const auto& v : m.vertices) addPoint(v.cast<double>(), 1.0);
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
    Eigen::Matrix3d R;  // rows: new X, Y, Z in old coordinates
    R.row(0) = es.eigenvectors().col(2).transpose();
    R.row(1) = es.eigenvectors().col(1).transpose();
    // Signs from the third moment along each axis (skewness), then right-handed.
    for (int k = 0; k < 2; ++k) {
        double s = 0;
        for (const auto& v : m.vertices) s += std::pow(R.row(k).dot(v.cast<double>() - c), 3);
        if (s < 0) R.row(k) = -R.row(k);
    }
    R.row(2) = R.row(0).cross(R.row(1));
    Transform t = Transform::Identity();
    t.linear() = R;
    t.translation() = -R * c;
    return t;
}

namespace {

/// The point a datum pins down most naturally (axis point, centre, plane point).
Vec3d datumPoint(const Primitive& p) { return p.origin; }

/// Linear constraints n·x = d that the datum puts on the origin.
void constraints(const Primitive& p, std::vector<std::pair<Vec3d, double>>& out) {
    switch (p.type) {
        case PrimitiveType::Plane: out.push_back({p.axis, p.axis.dot(p.origin)}); break;
        case PrimitiveType::Sphere:
            for (int k = 0; k < 3; ++k) out.push_back({Vec3d::Unit(k), p.origin[k]});
            break;
        case PrimitiveType::Torus:
            for (int k = 0; k < 3; ++k) out.push_back({Vec3d::Unit(k), p.origin[k]});
            break;
        default: {  // an axis: two planes through it
            const Vec3d u = (std::abs(p.axis.x()) < 0.9 ? p.axis.cross(Vec3d::UnitX()) : p.axis.cross(Vec3d::UnitY())).normalized();
            const Vec3d v = p.axis.cross(u);
            out.push_back({u, u.dot(p.origin)});
            out.push_back({v, v.dot(p.origin)});
        }
    }
}

}  // namespace

std::optional<Transform> datumAlignment(const Primitive& A, const std::optional<Primitive>& B, const std::optional<Primitive>& C, std::string* why) {
    auto fail = [&](const char* w) -> std::optional<Transform> { if (why) *why = w; return std::nullopt; };
    if (A.type == PrimitiveType::Sphere) return fail("the primary datum must give a direction (a plane or an axis)");
    // A datum face's normal points out of the material: the part's axes point
    // into it (the part sits on the primary, against the secondary).
    auto dir = [](const Primitive& p) { return p.type == PrimitiveType::Plane ? Vec3d(-p.axis) : p.axis; };
    const Vec3d Z = dir(A).normalized();
    Vec3d X = Vec3d::Zero();
    constexpr double kParallel = 0.9998;  // about 1°
    if (B) {
        if (B->type != PrimitiveType::Sphere && std::abs(B->axis.dot(Z)) < kParallel) {
            X = dir(*B) - dir(*B).dot(Z) * Z;
        } else if (C) {
            // A hole or centre: X runs from it to the tertiary's point (in the primary's plane).
            const Vec3d d = datumPoint(*C) - datumPoint(*B);
            X = d - d.dot(Z) * Z;
        }
    }
    if (X.norm() < 1e-9 && C && C->type != PrimitiveType::Sphere && std::abs(C->axis.dot(Z)) < kParallel) {
        X = dir(*C) - dir(*C).dot(Z) * Z;
    }
    if (X.norm() < 1e-9) return fail("the datums do not fix a direction across the primary (add a secondary plane or axis, or two holes)");
    X.normalize();
    const Vec3d Y = Z.cross(X);

    // Origin: the primary fixes what it can, the secondary what is left, then
    // the tertiary (3-2-1). A constraint along a direction already fixed is
    // skipped: two parallel holes give the origin from the first, the X
    // direction from the second.
    std::vector<std::pair<Vec3d, double>> cons, used;
    auto take = [&](const Primitive& p) {
        cons.clear();
        constraints(p, cons);
        for (const auto& [n, d] : cons) {
            Vec3d rest = n;
            for (const auto& [u, ud] : used) rest -= rest.dot(u) * u;  // u are kept orthonormal
            if (rest.norm() < 0.1 || used.size() >= 3) continue;
            // Re-express the constraint along its new, orthogonal part.
            double dd = d;
            const Vec3d r = rest / rest.norm();
            // n·x = d with n = r·|rest| + Σ (n·u) u, and u·x = ud  →  r·x = (d − Σ (n·u) ud) / |rest|
            for (const auto& [u, ud] : used) dd -= n.dot(u) * ud;
            used.push_back({r, dd / rest.norm()});
        }
    };
    take(A);
    if (B) take(*B);
    if (C) take(*C);
    Eigen::Matrix3d N = 1e-9 * Eigen::Matrix3d::Identity();
    Vec3d rhs = 1e-9 * A.origin;
    for (const auto& [n, d] : used) {
        N += n * n.transpose();
        rhs += n * d;
    }
    const Vec3d o = N.ldlt().solve(rhs);
    if (!o.allFinite()) return fail("the datums do not fix an origin");
    Transform t = Transform::Identity();
    Eigen::Matrix3d R;
    R.row(0) = X.transpose();
    R.row(1) = Y.transpose();
    R.row(2) = Z.transpose();
    t.linear() = R;
    t.translation() = -R * o;
    return t;
}

// ------------------------------------------------------------- best fit --
namespace {

struct Cloud {
    const std::vector<Vec3f>& pts;
    std::size_t kdtree_get_point_count() const { return pts.size(); }
    float kdtree_get_pt(std::size_t i, std::size_t d) const { return pts[i][static_cast<Eigen::Index>(d)]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};
using Tree = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, Cloud>, Cloud, 3, std::uint32_t>;

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

}  // namespace

BestFitResult bestFit(const TriMesh& moving, const TriMesh& reference, const BestFitOptions& o) {
    if (reference.vertices.size() < 10 || moving.vertices.size() < 10) throw std::runtime_error("too few points");
    // Reference: its vertices with normals (meshes) for point-to-plane.
    const std::vector<Vec3f> refN = reference.isPointCloud() ? reference.normals : reference.vertexNormals();
    if (refN.size() != reference.vertices.size()) throw std::runtime_error("the reference needs normals (a mesh, or points with normals)");
    Cloud cloud{reference.vertices};
    Tree tree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(16));
    tree.buildIndex();

    std::vector<Vec3d> src;
    const std::size_t step = std::max<std::size_t>(1, moving.vertices.size() / std::max<std::size_t>(o.samples, 100));
    for (std::size_t i = 0; i < moving.vertices.size(); i += step) src.push_back(moving.vertices[i].cast<double>());

    BestFitResult r;
    Transform T = Transform::Identity();
    if (o.coarseStart) {
        // Match the principal frames; try the four sign choices and keep the best.
        const Transform Pm = principalAlignment(moving), Pr = principalAlignment(reference);
        double best = 1e300;
        for (int k = 0; k < 4; ++k) {
            Transform F = Transform::Identity();
            F.linear() = Eigen::DiagonalMatrix<double, 3>((k & 1) ? -1 : 1, (k & 2) ? -1 : 1, ((k & 1) ^ ((k & 2) >> 1)) ? -1 : 1);
            const Transform cand = Pr.inverse() * F * Pm;
            double s = 0;
            std::uint32_t idx;
            float d2;
            for (std::size_t i = 0; i < src.size(); i += 10) {
                const Vec3f q = (cand * src[i]).cast<float>();
                tree.knnSearch(q.data(), 1, &idx, &d2);
                s += std::sqrt(static_cast<double>(d2));
            }
            if (s < best) { best = s; T = cand; }
        }
    }
    const Vec3f lo = reference.bounds().min, hi = reference.bounds().max;
    const double diag = (hi - lo).cast<double>().norm();
    double cutoff = o.maxDistance > 0 ? o.maxDistance : 0.1 * diag;
    std::vector<double> dist;
    for (int it = 0; it < o.maxIterations; ++it) {
        Eigen::Matrix<double, 6, 6> A = Eigen::Matrix<double, 6, 6>::Zero();
        Eigen::Matrix<double, 6, 1> b = Eigen::Matrix<double, 6, 1>::Zero();
        dist.clear();
        std::size_t used = 0;
        std::vector<std::array<double, 7>> rows;  // J (6) and residual
        rows.reserve(src.size());
        for (const auto& p0 : src) {
            const Vec3d p = T * p0;
            const Vec3f pf = p.cast<float>();
            std::uint32_t idx;
            float d2;
            if (tree.knnSearch(pf.data(), 1, &idx, &d2) < 1) continue;
            if (std::sqrt(static_cast<double>(d2)) > cutoff) continue;
            const Vec3d q = reference.vertices[idx].cast<double>(), n = refN[idx].cast<double>();
            const double res = n.dot(p - q);
            const Vec3d pxn = p.cross(n);
            rows.push_back({pxn.x(), pxn.y(), pxn.z(), n.x(), n.y(), n.z(), res});
            dist.push_back(std::abs(res));
        }
        if (rows.size() < 20) throw std::runtime_error("too little overlap between the meshes");
        // Tukey biweight on the residuals, scale from their median.
        const double sigma = std::max(1.4826 * quantile(dist, 0.5), 1e-6);
        const double c = 4.685 * sigma;
        for (const auto& row : rows) {
            const double u = row[6] / c;
            if (std::abs(u) >= 1) continue;
            const double w = (1 - u * u) * (1 - u * u);
            Eigen::Matrix<double, 6, 1> J;
            J << row[0], row[1], row[2], row[3], row[4], row[5];
            A += w * J * J.transpose();
            b -= w * J * row[6];
            ++used;
        }
        const Eigen::Matrix<double, 6, 1> x = A.ldlt().solve(b);
        if (!x.allFinite()) throw std::runtime_error("the alignment did not converge");
        Transform delta = Transform::Identity();
        const Vec3d w = x.head<3>();
        if (w.norm() > 0) delta.linear() = Eigen::AngleAxisd(w.norm(), w.normalized()).toRotationMatrix();
        delta.translation() = x.tail<3>();
        T = delta * T;
        r.iterations = it + 1;
        r.overlap = static_cast<double>(used) / static_cast<double>(src.size());
        // Tighten the correspondence cut as it converges.
        cutoff = std::max(std::min(cutoff, 10.0 * c), o.maxDistance > 0 ? std::min(o.maxDistance, 3.0 * c) : 3.0 * c);
        if (w.norm() < 1e-7 && x.tail<3>().norm() < 1e-6 * std::max(diag, 1.0)) break;
    }
    // Final statistics with the final transform.
    dist.clear();
    double ss = 0;
    for (const auto& p0 : src) {
        const Vec3f pf = (T * p0).cast<float>();
        std::uint32_t idx;
        float d2;
        tree.knnSearch(pf.data(), 1, &idx, &d2);
        const double d = std::abs(refN[idx].cast<double>().dot((T * p0) - reference.vertices[idx].cast<double>()));
        if (std::sqrt(static_cast<double>(d2)) > cutoff) continue;
        dist.push_back(d);
        ss += d * d;
    }
    r.transform = T;
    r.rms = dist.empty() ? 0 : std::sqrt(ss / static_cast<double>(dist.size()));
    r.median = quantile(dist, 0.5);
    r.p95 = quantile(dist, 0.95);
    r.overlap = static_cast<double>(dist.size()) / static_cast<double>(src.size());
    return r;
}

}  // namespace orv

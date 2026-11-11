// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fit/Primitive.h"

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>

namespace orv {

const char* primitiveName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Plane: return "plane";
        case PrimitiveType::Sphere: return "sphere";
        case PrimitiveType::Cylinder: return "cylinder";
        case PrimitiveType::Cone: return "cone";
        case PrimitiveType::Torus: return "torus";
    }
    return "?";
}

std::optional<PrimitiveType> primitiveFromName(const std::string& n) {
    for (auto t : {PrimitiveType::Plane, PrimitiveType::Sphere, PrimitiveType::Cylinder, PrimitiveType::Cone, PrimitiveType::Torus})
        if (n == primitiveName(t)) return t;
    return std::nullopt;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3d anyPerpendicular(const Vec3d& a) {
    const Vec3d t = std::abs(a.x()) < 0.9 ? Vec3d::UnitX() : Vec3d::UnitY();
    return a.cross(t).normalized();
}

/// Signed distance for the parameters of a type (in whatever frame they are given).
double dist(PrimitiveType type, const Vec3d& o, const Vec3d& a, double r, double r2, double ang, const Vec3d& p) {
    const Vec3d q = p - o;
    switch (type) {
        case PrimitiveType::Plane: return a.dot(q);
        case PrimitiveType::Sphere: return q.norm() - r;
        case PrimitiveType::Cylinder: return (q - q.dot(a) * a).norm() - r;
        case PrimitiveType::Cone: {
            const double h = q.dot(a), d = (q - h * a).norm();
            return d * std::cos(ang) - h * std::sin(ang);
        }
        case PrimitiveType::Torus: {
            const double h = q.dot(a), rho = (q - h * a).norm();
            return std::hypot(rho - r, h) - r2;
        }
    }
    return 0.0;
}

// ------------------------------------------------- Levenberg–Marquardt --
// Minimises the sum of squared residuals; Jacobian by central differences.
// `tidy` puts the parameters back into their canonical form (unit axis…).
using Residuals = std::function<void(const Eigen::VectorXd&, Eigen::VectorXd&)>;

void levenbergMarquardt(Eigen::VectorXd& x, Eigen::Index m, const Residuals& f, const std::function<void(Eigen::VectorXd&)>& tidy,
                        int maxIterations) {
    const Eigen::Index n = x.size();
    Eigen::VectorXd r(m), r2(m), rp(m), rm(m);
    f(x, r);
    double cost = r.squaredNorm();
    double lambda = 1e-3;
    Eigen::MatrixXd J(m, n);
    for (int it = 0; it < maxIterations; ++it) {
        for (Eigen::Index j = 0; j < n; ++j) {
            const double h = 1e-6 * std::max(1.0, std::abs(x[j]));
            Eigen::VectorXd xp = x, xm = x;
            xp[j] += h;
            xm[j] -= h;
            f(xp, rp);
            f(xm, rm);
            J.col(j) = (rp - rm) / (2 * h);
        }
        const Eigen::MatrixXd A = J.transpose() * J;
        const Eigen::VectorXd g = J.transpose() * r;
        bool improved = false;
        for (int tries = 0; tries < 10; ++tries) {
            Eigen::MatrixXd Ad = A;
            for (Eigen::Index j = 0; j < n; ++j) Ad(j, j) += lambda * (A(j, j) + 1e-12);
            const Eigen::VectorXd dx = Ad.ldlt().solve(-g);
            if (!dx.allFinite()) { lambda *= 10; continue; }
            Eigen::VectorXd xn = x + dx;
            tidy(xn);
            f(xn, r2);
            const double c2 = r2.squaredNorm();
            if (std::isfinite(c2) && c2 < cost) {
                const double rel = (cost - c2) / std::max(cost, 1e-300);
                x = xn;
                r = r2;
                cost = c2;
                lambda = std::max(lambda / 3, 1e-12);
                improved = true;
                if (rel < 1e-12 || dx.norm() < 1e-12) return;
                break;
            }
            lambda *= 4;
        }
        if (!improved) return;
    }
}

// ------------------------------------------------------------- framing --
// Fits run in a normalised frame (centroid at 0, unit RMS radius) so that
// tolerances and the finite differences do not depend on the part's size.
struct Frame {
    Vec3d centre{Vec3d::Zero()};
    double scale{1.0};
    std::vector<Vec3d> pts, nrm;
};

Frame frame(const std::vector<Vec3d>& points, const std::vector<Vec3d>& normals, const std::vector<std::size_t>& use) {
    Frame f;
    for (auto i : use) f.centre += points[i];
    f.centre /= static_cast<double>(use.size());
    double s = 0;
    for (auto i : use) s += (points[i] - f.centre).squaredNorm();
    f.scale = std::sqrt(s / static_cast<double>(use.size()));
    if (f.scale <= 0) f.scale = 1;
    f.pts.reserve(use.size());
    for (auto i : use) f.pts.push_back((points[i] - f.centre) / f.scale);
    if (!normals.empty())
        for (auto i : use) f.nrm.push_back(normals[i].normalized());
    return f;
}

/// Parameters packed for LM: o(3) a(3) r r2 ang (unused ones stay put).
struct Params {
    Vec3d o{Vec3d::Zero()}, a{Vec3d::UnitZ()};
    double r{0}, r2{0}, ang{0};
};

Eigen::VectorXd pack(PrimitiveType t, const Params& p) {
    switch (t) {
        case PrimitiveType::Plane: return (Eigen::VectorXd(6) << p.o, p.a).finished();
        case PrimitiveType::Sphere: return (Eigen::VectorXd(4) << p.o, p.r).finished();
        case PrimitiveType::Cylinder: return (Eigen::VectorXd(7) << p.o, p.a, p.r).finished();
        case PrimitiveType::Cone: return (Eigen::VectorXd(7) << p.o, p.a, p.ang).finished();
        case PrimitiveType::Torus: return (Eigen::VectorXd(8) << p.o, p.a, p.r, p.r2).finished();
    }
    return {};
}

Params unpack(PrimitiveType t, const Eigen::VectorXd& x) {
    Params p;
    p.o = x.head<3>();
    switch (t) {
        case PrimitiveType::Plane: p.a = x.segment<3>(3); break;
        case PrimitiveType::Sphere: p.r = x[3]; break;
        case PrimitiveType::Cylinder: p.a = x.segment<3>(3); p.r = x[6]; break;
        case PrimitiveType::Cone: p.a = x.segment<3>(3); p.ang = x[6]; break;
        case PrimitiveType::Torus: p.a = x.segment<3>(3); p.r = x[6]; p.r2 = x[7]; break;
    }
    const double an = p.a.norm();
    p.a = an > 0 ? Vec3d(p.a / an) : Vec3d::UnitZ();
    return p;
}

double distP(PrimitiveType t, const Params& p, const Vec3d& q) { return dist(t, p.o, p.a, p.r, p.r2, p.ang, q); }

void refine(PrimitiveType t, Params& p, const std::vector<Vec3d>& pts, int maxIterations) {
    if (t == PrimitiveType::Plane) return;  // the PCA plane already is the least-squares one
    // At most ~1500 points take part in the iterations; the quality is measured on all.
    std::vector<Vec3d> sample;
    const std::size_t step = std::max<std::size_t>(1, pts.size() / 1500);
    for (std::size_t i = 0; i < pts.size(); i += step) sample.push_back(pts[i]);
    const auto m = static_cast<Eigen::Index>(sample.size());
    Eigen::VectorXd x = pack(t, p);
    auto f = [&](const Eigen::VectorXd& xv, Eigen::VectorXd& r) {
        const Params q = unpack(t, xv);
        for (Eigen::Index i = 0; i < m; ++i) r[i] = distP(t, q, sample[static_cast<std::size_t>(i)]);
    };
    auto tidy = [&](Eigen::VectorXd& xv) {
        if (t == PrimitiveType::Sphere) return;
        Params q = unpack(t, xv);
        // An axis point is free to slide along the axis: keep it at the foot of the centroid.
        if (t == PrimitiveType::Cylinder) q.o -= q.o.dot(q.a) * q.a;  // centroid is 0 in this frame
        xv = pack(t, q);
    };
    levenbergMarquardt(x, m, f, tidy, maxIterations);
    p = unpack(t, x);
}

// ---------------------------------------------------- first estimates --

bool planeInit(const std::vector<Vec3d>& pts, Params& p) {
    if (pts.size() < 3) return false;
    Vec3d c = Vec3d::Zero();
    for (const auto& q : pts) c += q;
    c /= static_cast<double>(pts.size());
    Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
    for (const auto& q : pts) C += (q - c) * (q - c).transpose();
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
    if (es.eigenvalues()[1] <= 1e-12 * es.eigenvalues()[2]) return false;  // points on a line
    p.o = c;
    p.a = es.eigenvectors().col(0).normalized();
    return true;
}

bool sphereInit(const std::vector<Vec3d>& pts, Params& p) {
    if (pts.size() < 4) return false;
    Eigen::MatrixXd A(static_cast<Eigen::Index>(pts.size()), 4);
    Eigen::VectorXd b(static_cast<Eigen::Index>(pts.size()));
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto k = static_cast<Eigen::Index>(i);
        A.row(k) << 2 * pts[i].x(), 2 * pts[i].y(), 2 * pts[i].z(), 1.0;
        b[k] = pts[i].squaredNorm();
    }
    const Eigen::Vector4d s = A.colPivHouseholderQr().solve(b);
    p.o = s.head<3>();
    const double rr = s[3] + p.o.squaredNorm();
    if (!(rr > 0) || !p.o.allFinite()) return false;
    p.r = std::sqrt(rr);
    return true;
}

/// Circle through 2-D points (Kåsa's algebraic fit).
bool circle2d(const std::vector<Eigen::Vector2d>& q, Eigen::Vector2d& c, double& r) {
    if (q.size() < 3) return false;
    Eigen::MatrixXd A(static_cast<Eigen::Index>(q.size()), 3);
    Eigen::VectorXd b(static_cast<Eigen::Index>(q.size()));
    for (std::size_t i = 0; i < q.size(); ++i) {
        const auto k = static_cast<Eigen::Index>(i);
        A.row(k) << 2 * q[i].x(), 2 * q[i].y(), 1.0;
        b[k] = q[i].squaredNorm();
    }
    const Eigen::Vector3d s = A.colPivHouseholderQr().solve(b);
    c = s.head<2>();
    const double rr = s[2] + c.squaredNorm();
    if (!(rr > 0) || !c.allFinite()) return false;
    r = std::sqrt(rr);
    return true;
}

bool cylinderInit(const std::vector<Vec3d>& pts, const std::vector<Vec3d>& nrm, Params& p) {
    if (nrm.size() != pts.size() || pts.size() < 6) return false;
    Eigen::Matrix3d M = Eigen::Matrix3d::Zero();
    for (const auto& n : nrm) M += n * n.transpose();
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(M);
    p.a = es.eigenvectors().col(0).normalized();  // the normals are all across the axis
    const Vec3d u = anyPerpendicular(p.a), v = p.a.cross(u);
    std::vector<Eigen::Vector2d> q;
    q.reserve(pts.size());
    for (const auto& x : pts) q.emplace_back(x.dot(u), x.dot(v));
    Eigen::Vector2d c;
    if (!circle2d(q, c, p.r)) return false;
    p.o = u * c.x() + v * c.y();
    return true;
}

bool coneInit(const std::vector<Vec3d>& pts, const std::vector<Vec3d>& nrm, Params& p) {
    if (nrm.size() != pts.size() || pts.size() < 8) return false;
    // The normals of a cone all make the same angle with its axis.
    Vec3d mean = Vec3d::Zero();
    for (const auto& n : nrm) mean += n;
    mean /= static_cast<double>(nrm.size());
    Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
    for (const auto& n : nrm) C += (n - mean) * (n - mean).transpose();
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
    Vec3d a = es.eigenvectors().col(0).normalized();
    // Every tangent plane passes through the apex: n·A = n·p.
    Eigen::Matrix3d N = Eigen::Matrix3d::Zero();
    Vec3d rhs = Vec3d::Zero();
    for (std::size_t i = 0; i < pts.size(); ++i) {
        N += nrm[i] * nrm[i].transpose();
        rhs += nrm[i] * nrm[i].dot(pts[i]);
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> en(N);
    if (en.eigenvalues()[0] < 1e-4 * en.eigenvalues()[2]) return false;  // no single apex: a cylinder or a plane
    const Vec3d apex = N.ldlt().solve(rhs);
    if (!apex.allFinite()) return false;
    double hs = 0;
    for (const auto& x : pts) hs += (x - apex).dot(a);
    if (hs < 0) a = -a;  // the axis runs from the apex into the cone
    double ang = 0;
    for (const auto& x : pts) {
        const Vec3d q = x - apex;
        const double h = q.dot(a);
        ang += std::atan2((q - h * a).norm(), h);
    }
    ang /= static_cast<double>(pts.size());
    if (!(ang > 0.2 * kPi / 180) || ang > 89.0 * kPi / 180) return false;
    p.o = apex;
    p.a = a;
    p.ang = ang;
    return true;
}

bool torusInit(const std::vector<Vec3d>& pts, const std::vector<Vec3d>& nrm, Params& p) {
    if (nrm.size() != pts.size() || pts.size() < 12) return false;
    // The tube's centres (point minus minor radius along its normal) lie on a
    // flat circle: search the minor radius that makes them so.
    std::vector<Vec3d> q(pts.size());
    auto score = [&](double r2, Params& out) {
        for (std::size_t i = 0; i < pts.size(); ++i) q[i] = pts[i] - r2 * nrm[i];
        Params pl;
        if (!planeInit(q, pl)) return 1e300;
        const Vec3d u = anyPerpendicular(pl.a), v = pl.a.cross(u);
        std::vector<Eigen::Vector2d> q2;
        q2.reserve(q.size());
        for (const auto& x : q) q2.emplace_back((x - pl.o).dot(u), (x - pl.o).dot(v));
        Eigen::Vector2d c;
        double R = 0;
        if (!circle2d(q2, c, R)) return 1e300;
        double e = 0;
        for (std::size_t i = 0; i < q.size(); ++i) {
            const double h = (q[i] - pl.o).dot(pl.a);
            e += h * h + std::pow((q2[i] - c).norm() - R, 2);
        }
        out.o = pl.o + u * c.x() + v * c.y();
        out.a = pl.a;
        out.r = R;
        out.r2 = r2;
        return e;
    };
    double best = 1e300, bestR = 0;
    for (int k = -40; k <= 40; ++k) {
        if (k == 0) continue;
        Params tmp;
        const double r2 = 0.05 * k;  // the frame has unit RMS radius
        const double e = score(r2, tmp);
        if (e < best) { best = e; bestR = r2; }
    }
    // Golden-section refinement around the best grid value.
    double lo = bestR - 0.05, hi = bestR + 0.05;
    const double g = 0.5 * (std::sqrt(5.0) - 1);
    Params tmp;
    for (int it = 0; it < 40; ++it) {
        const double m1 = hi - g * (hi - lo), m2 = lo + g * (hi - lo);
        if (score(m1, tmp) < score(m2, tmp)) hi = m2;
        else lo = m1;
    }
    if (score(0.5 * (lo + hi), p) >= 1e299) return false;
    if (p.r2 < 0) p.r2 = -p.r2;  // normals pointed inward: same torus
    return p.r > 0 && p.r2 > 0;
}

bool init(PrimitiveType t, const Frame& f, Params& p) {
    switch (t) {
        case PrimitiveType::Plane: return planeInit(f.pts, p);
        case PrimitiveType::Sphere: return sphereInit(f.pts, p);
        case PrimitiveType::Cylinder: return cylinderInit(f.pts, f.nrm, p);
        case PrimitiveType::Cone: return coneInit(f.pts, f.nrm, p);
        case PrimitiveType::Torus: return torusInit(f.pts, f.nrm, p);
    }
    return false;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

}  // namespace

double Primitive::distance(const Vec3d& p) const { return dist(type, origin, axis, radius, radius2, angle, p); }

Vec3d Primitive::normal(const Vec3d& p) const {
    const Vec3d q = p - origin;
    switch (type) {
        case PrimitiveType::Plane: return axis;
        case PrimitiveType::Sphere: return q.normalized();
        case PrimitiveType::Cylinder: return (q - q.dot(axis) * axis).normalized();
        case PrimitiveType::Cone: {
            const double h = q.dot(axis);
            const Vec3d radial = (q - h * axis).normalized();
            return (radial * std::cos(angle) - axis * std::sin(angle)).normalized();
        }
        case PrimitiveType::Torus: {
            const double h = q.dot(axis);
            const Vec3d radial = (q - h * axis).normalized();
            return (q - radial * radius).normalized();
        }
    }
    return Vec3d::UnitZ();
}

std::optional<Primitive> fitPrimitive(PrimitiveType type, const std::vector<Vec3d>& points, const std::vector<Vec3d>& normals,
                                      const FitOptions& o) {
    if (points.size() < 3) return std::nullopt;
    if (!normals.empty() && normals.size() != points.size()) return std::nullopt;
    constexpr std::size_t kMaxFit = 3000;
    if (points.size() > kMaxFit) {
        // Large sets: fit on an even subset (plenty for a few parameters), then
        // measure the quality and the outliers on every point.
        const std::size_t step = (points.size() + kMaxFit - 1) / kMaxFit;
        std::vector<Vec3d> sp, sn;
        for (std::size_t i = 0; i < points.size(); i += step) {
            sp.push_back(points[i]);
            if (!normals.empty()) sn.push_back(normals[i]);
        }
        auto r = fitPrimitive(type, sp, sn, o);
        if (!r) return std::nullopt;
        std::vector<double> d(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) d[i] = std::abs(r->distance(points[i]));
        const double cut = o.rejectOutliers ? std::max(3.0 * 1.4826 * median(d), 1e-9) : 1e300;
        double ss = 0, mx = 0;
        std::size_t kept = 0;
        for (double x : d)
            if (x <= cut) { ss += x * x; mx = std::max(mx, x); ++kept; }
        r->points = kept;
        r->outliers = points.size() - kept;
        r->rms = std::sqrt(ss / static_cast<double>(std::max<std::size_t>(kept, 1)));
        r->maxAbs = mx;
        return r;
    }
    std::vector<std::size_t> use(points.size());
    std::iota(use.begin(), use.end(), 0);
    Params p;
    bool started = false;
    Frame f;
    for (int round = 0; round < (o.rejectOutliers ? 3 : 1); ++round) {
        if (use.size() < 3) return std::nullopt;
        // Each round re-frames the kept points; carry the parameters over.
        const Frame nf = frame(points, normals, use);
        if (started) {  // previous parameters, from the old frame to the new one
            p.o = ((p.o * f.scale + f.centre) - nf.centre) / nf.scale;
            p.r *= f.scale / nf.scale;
            p.r2 *= f.scale / nf.scale;
        }
        f = nf;
        if (!started) {
            if (!init(type, f, p)) return std::nullopt;
            started = true;
        }
        if (type == PrimitiveType::Plane) {
            if (!planeInit(f.pts, p)) return std::nullopt;
        } else {
            refine(type, p, f.pts, o.maxIterations);
        }
        if (!p.o.allFinite() || !p.a.allFinite() || !std::isfinite(p.r) || !std::isfinite(p.r2)) return std::nullopt;
        if (!o.rejectOutliers) break;
        // Outliers: beyond 3 robust σ (1.4826 · median absolute distance).
        std::vector<double> d(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) d[i] = std::abs(distP(type, p, (points[i] - f.centre) / f.scale));
        const double sigma = 1.4826 * median(d);
        const double cut = std::max(3.0 * sigma, 1e-9);
        std::vector<std::size_t> keep;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (d[i] <= cut) keep.push_back(i);
        if (keep.size() == use.size()) break;
        use = std::move(keep);
    }
    Primitive r;
    r.type = type;
    r.origin = p.o * f.scale + f.centre;
    r.axis = p.a;
    r.radius = p.r * f.scale;
    r.radius2 = p.r2 * f.scale;
    r.angle = p.ang;
    if (type == PrimitiveType::Cylinder || type == PrimitiveType::Torus || type == PrimitiveType::Sphere) r.radius = std::abs(r.radius);
    if (type == PrimitiveType::Cone && r.angle < 0) { r.angle = -r.angle; }
    if (type == PrimitiveType::Plane && !normals.empty()) {  // normal on the side the points' normals face
        double s = 0;
        for (auto i : use) s += normals[i].dot(r.axis);
        if (s < 0) r.axis = -r.axis;
    }
    if (type == PrimitiveType::Cylinder) r.origin = r.origin + (f.centre - r.origin).dot(r.axis) * r.axis;  // foot of the centroid
    double ss = 0, mx = 0;
    for (auto i : use) {
        const double d = r.distance(points[i]);
        ss += d * d;
        mx = std::max(mx, std::abs(d));
    }
    r.points = use.size();
    r.outliers = points.size() - use.size();
    r.rms = std::sqrt(ss / static_cast<double>(use.size()));
    r.maxAbs = mx;
    return r;
}

std::optional<Primitive> fitBest(const std::vector<Vec3d>& points, const std::vector<Vec3d>& normals, double noise, double slack) {
    struct Candidate { Primitive p; int complexity; double score; };
    std::vector<Candidate> c;
    Vec3d lo = Vec3d::Constant(1e300), hi = Vec3d::Constant(-1e300);
    for (const auto& p : points) { lo = lo.cwiseMin(p); hi = hi.cwiseMax(p); }
    const double extent = (hi - lo).norm();
    for (auto t : {PrimitiveType::Plane, PrimitiveType::Sphere, PrimitiveType::Cylinder, PrimitiveType::Cone, PrimitiveType::Torus}) {
        auto f = fitPrimitive(t, points, normals);
        if (!f) continue;
        // Shapes that are really a simpler one, or absurd for this patch, are left out.
        if ((t == PrimitiveType::Sphere || t == PrimitiveType::Cylinder) && f->radius > 20 * extent) continue;
        if (t == PrimitiveType::Cone && (f->angle < 1.0 * kPi / 180 || f->angle > 88.0 * kPi / 180)) continue;
        if (t == PrimitiveType::Torus && (f->radius > 20 * extent || f->radius2 > 20 * extent)) continue;
        const int complexity = t == PrimitiveType::Plane ? 0 : t == PrimitiveType::Cone ? 2 : t == PrimitiveType::Torus ? 3 : 1;
        const double outFrac = static_cast<double>(f->outliers) / static_cast<double>(points.size());
        c.push_back({*f, complexity, f->rms * (1.0 + 4.0 * outFrac)});
    }
    if (c.empty()) return std::nullopt;
    double best = 1e300;
    for (const auto& x : c) best = std::min(best, x.score);
    const double limit = slack * std::max(best, noise);
    const Candidate* pick = nullptr;
    for (const auto& x : c)
        if (x.score <= limit && (!pick || x.complexity < pick->complexity || (x.complexity == pick->complexity && x.score < pick->score))) pick = &x;
    return pick->p;
}

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
// Basis functions: Cox–de Boor recursion as in Piegl & Tiller, The NURBS Book
// (2nd ed., 1997), algorithms A2.1 and A2.2. Fitting: linear least squares on
// the poles with a second-difference penalty (a discrete thin plate), see
// docs/algoritmos/superficies.md.
#include "core/solid/BSpline.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>

namespace orv {

std::vector<double> clampedKnots(int poles, int degree) {
    std::vector<double> k;
    const int n = poles, p = degree;
    for (int i = 0; i <= p; ++i) k.push_back(0.0);
    for (int i = 1; i < n - p; ++i) k.push_back(static_cast<double>(i) / (n - p));
    for (int i = 0; i <= p; ++i) k.push_back(1.0);
    return k;
}

void knotsAndMultiplicities(const std::vector<double>& full, std::vector<double>& knots, std::vector<int>& mults) {
    knots.clear();
    mults.clear();
    for (double k : full) {
        if (!knots.empty() && std::abs(k - knots.back()) < 1e-12) ++mults.back();
        else {
            knots.push_back(k);
            mults.push_back(1);
        }
    }
}

int basis(const std::vector<double>& U, int p, double t, double* N) {
    const int n = static_cast<int>(U.size()) - p - 1;  // poles
    t = std::clamp(t, 0.0, 1.0);
    // The span: U[s] <= t < U[s + 1], the last one for t = 1.
    int s = n - 1;
    if (t < U[static_cast<std::size_t>(n)]) {
        int lo = p, hi = n;
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            if (t < U[static_cast<std::size_t>(mid)]) hi = mid;
            else lo = mid;
        }
        s = lo;
    }
    double left[8], right[8];
    N[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[j] = t - U[static_cast<std::size_t>(s + 1 - j)];
        right[j] = U[static_cast<std::size_t>(s + j)] - t;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double den = right[r + 1] + left[j - r];
            const double tmp = den != 0 ? N[r] / den : 0.0;
            N[r] = saved + right[r + 1] * tmp;
            saved = left[j - r] * tmp;
        }
        N[j] = saved;
    }
    return s - p;
}

Vec2d evalCurve(const std::vector<Vec2d>& poles, double t) {
    if (poles.empty()) return Vec2d::Zero();
    if (poles.size() == 1) return poles.front();
    const int p = curveDegree(poles.size());
    const auto U = clampedKnots(static_cast<int>(poles.size()), p);
    double N[8];
    const int first = basis(U, p, t, N);
    Vec2d c = Vec2d::Zero();
    for (int k = 0; k <= p; ++k) c += N[k] * poles[static_cast<std::size_t>(first + k)];
    return c;
}

std::vector<Vec2d> sampleCurve(const std::vector<Vec2d>& poles, int count) {
    std::vector<Vec2d> out;
    count = std::max(count, 2);
    for (int i = 0; i < count; ++i) out.push_back(evalCurve(poles, static_cast<double>(i) / (count - 1)));
    return out;
}

namespace {

/// Poles with fixed ends that bring the curve nearest the points at their
/// parameters, with a small penalty on bending.
std::vector<Vec2d> solveCurve(const std::vector<Vec2d>& pts, const std::vector<double>& t, const Vec2d& from, const Vec2d& to, int n, double lambda) {
    const int p = curveDegree(static_cast<std::size_t>(n));
    const auto U = clampedKnots(n, p);
    const int m = n - 2;  // free poles
    Eigen::MatrixXd A = Eigen::MatrixXd::Zero(m, m);
    Eigen::MatrixXd b = Eigen::MatrixXd::Zero(m, 2);
    double N[8];
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const int first = basis(U, p, t[i], N);
        Vec2d r = pts[i];
        for (int k = 0; k <= p; ++k) {
            const int j = first + k;
            if (j == 0) r -= N[k] * from;
            if (j == n - 1) r -= N[k] * to;
        }
        for (int k = 0; k <= p; ++k) {
            const int j = first + k - 1;
            if (j < 0 || j >= m) continue;
            b.row(j) += N[k] * r.transpose();
            for (int l = 0; l <= p; ++l) {
                const int jl = first + l - 1;
                if (jl >= 0 && jl < m) A(j, jl) += N[k] * N[l];
            }
        }
    }
    // Second differences of all poles (the fixed ones on the right-hand side).
    for (int i = 1; i + 1 < n; ++i) {
        const double c[3] = {1, -2, 1};
        Vec2d fixedPart = Vec2d::Zero();
        int idx[3] = {i - 1, i, i + 1};
        for (int a = 0; a < 3; ++a) {
            if (idx[a] == 0) fixedPart += c[a] * from;
            if (idx[a] == n - 1) fixedPart += c[a] * to;
        }
        for (int a = 0; a < 3; ++a) {
            const int ja = idx[a] - 1;
            if (ja < 0 || ja >= m) continue;
            b.row(ja) -= lambda * c[a] * fixedPart.transpose();
            for (int c2 = 0; c2 < 3; ++c2) {
                const int jb = idx[c2] - 1;
                if (jb >= 0 && jb < m) A(ja, jb) += lambda * c[a] * c[c2];
            }
        }
    }
    const Eigen::MatrixXd x = A.ldlt().solve(b);
    std::vector<Vec2d> poles{from};
    for (int j = 0; j < m; ++j) poles.emplace_back(x(j, 0), x(j, 1));
    poles.push_back(to);
    return poles;
}

}  // namespace

CurveFit fitCurve(const std::vector<Vec2d>& points, const Vec2d& from, const Vec2d& to, double tolerance, int maxPoles) {
    CurveFit best;
    if (points.empty()) {
        best.poles = {from, to};
        return best;
    }
    // Chord-length parameters from `from` to `to` through the points.
    std::vector<double> t(points.size());
    double total = (points.front() - from).norm();
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i) total += (points[i] - points[i - 1]).norm();
        t[i] = total;
    }
    total += (to - points.back()).norm();
    for (auto& x : t) x = total > 0 ? x / total : 0.0;
    maxPoles = std::max(4, std::min(maxPoles, static_cast<int>(points.size()) + 2));
    for (int n = 4;; n = std::min(maxPoles, n + std::max(1, n / 4))) {
        std::vector<double> tt = t;
        CurveFit f;
        for (int pass = 0; pass < 3; ++pass) {
            f.poles = solveCurve(points, tt, from, to, n, 1e-6 * static_cast<double>(points.size()));
            // Each point's nearest place on the curve: its parameter for the next pass.
            const int dense = std::max(200, 20 * n);
            const auto c = sampleCurve(f.poles, dense);
            f.maxDeviation = 0;
            std::size_t k = 0;
            for (std::size_t i = 0; i < points.size(); ++i) {
                // Points go along the curve: search forward from the last match.
                double bestD = 1e300;
                std::size_t at = k;
                for (std::size_t j = (k > 20 ? k - 20 : 0); j < c.size(); ++j) {
                    const double d = (c[j] - points[i]).squaredNorm();
                    if (d < bestD) { bestD = d; at = j; }
                    else if (j > at + 40) break;
                }
                k = at;
                // Refine between the neighbouring samples.
                const std::size_t lo = at > 0 ? at - 1 : at, hi = std::min(c.size() - 1, at + 1);
                double d = std::sqrt(bestD);
                for (std::size_t j = lo; j < hi; ++j) {
                    const Vec2d ab = c[j + 1] - c[j];
                    const double s = std::clamp(ab.squaredNorm() > 0 ? (points[i] - c[j]).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
                    d = std::min(d, (points[i] - (c[j] + s * ab)).norm());
                    if (j == at || j + 1 == at) tt[i] = (static_cast<double>(j) + s) / static_cast<double>(dense - 1);
                }
                f.maxDeviation = std::max(f.maxDeviation, d);
            }
        }
        if (best.poles.empty() || f.maxDeviation < best.maxDeviation) best = f;
        if (f.maxDeviation <= tolerance || n >= maxPoles) break;
    }
    return best;
}

// ---- surfaces ----------------------------------------------------------

namespace {
int surfaceDegree(int poles) { return std::min(3, poles - 1); }
std::vector<double> greville(const std::vector<double>& U, int n, int p) {
    std::vector<double> g(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double s = 0;
        for (int k = 1; k <= p; ++k) s += U[static_cast<std::size_t>(i + k)];
        g[static_cast<std::size_t>(i)] = p > 0 ? s / p : 0.0;
    }
    return g;
}
}  // namespace

double HeightSurface::heightAt(double u, double v) const {
    const int pu = surfaceDegree(nu), pv = surfaceDegree(nv);
    const auto U = clampedKnots(nu, pu), V = clampedKnots(nv, pv);
    double Nu[8], Nv[8];
    const int fu = basis(U, pu, u, Nu), fv = basis(V, pv, v, Nv);
    double h = 0;
    for (int b = 0; b <= pv; ++b)
        for (int a = 0; a <= pu; ++a) h += Nu[a] * Nv[b] * heights[static_cast<std::size_t>(fu + a + nu * (fv + b))];
    return h;
}

Vec3d HeightSurface::eval(double u, double v) const {
    return origin + (x0 + u * width) * xDir + (y0 + v * height) * yDir + heightAt(u, v) * normal;
}

std::vector<Vec3d> HeightSurface::poles3() const {
    const int pu = surfaceDegree(nu), pv = surfaceDegree(nv);
    const auto gu = greville(clampedKnots(nu, pu), nu, pu), gv = greville(clampedKnots(nv, pv), nv, pv);
    std::vector<Vec3d> out;
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i)
            out.push_back(origin + (x0 + gu[static_cast<std::size_t>(i)] * width) * xDir + (y0 + gv[static_cast<std::size_t>(j)] * height) * yDir +
                          heights[static_cast<std::size_t>(i + nu * j)] * normal);
    return out;
}

TriMesh HeightSurface::triangles(int n) const {
    TriMesh m;
    n = std::max(n, 1);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) m.vertices.push_back(eval(static_cast<double>(i) / n, static_cast<double>(j) / n).cast<float>());
    auto at = [n](int i, int j) { return static_cast<std::uint32_t>(i + (n + 1) * j); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            m.faces.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            m.faces.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return m;
}

std::vector<std::vector<Vec3f>> HeightSurface::isoLines(int count, int samples) const {
    std::vector<std::vector<Vec3f>> out;
    for (int k = 0; k < count; ++k) {
        const double c = count > 1 ? static_cast<double>(k) / (count - 1) : 0.5;
        std::vector<Vec3f> a, b;
        for (int s = 0; s < samples; ++s) {
            const double t = static_cast<double>(s) / (samples - 1);
            a.push_back(eval(t, c).cast<float>());
            b.push_back(eval(c, t).cast<float>());
        }
        out.push_back(std::move(a));
        out.push_back(std::move(b));
    }
    return out;
}

SurfaceFit fitHeightSurface(const std::vector<Vec3d>& points, const Vec3d& origin, const Vec3d& normalIn, const Vec3d& xIn, int nu, int nv, double margin,
                            double smoothing) {
    SurfaceFit out;
    HeightSurface& s = out.surface;
    s.origin = origin;
    s.normal = normalIn.normalized();
    s.xDir = (xIn - xIn.dot(s.normal) * s.normal).normalized();
    s.yDir = s.normal.cross(s.xDir);
    s.nu = std::max(2, nu);
    s.nv = std::max(2, nv);
    s.heights.assign(static_cast<std::size_t>(s.nu * s.nv), 0.0);
    if (points.empty()) return out;
    // The points in the plane's coordinates, and their rectangle grown by the margin.
    std::vector<Vec3d> local(points.size());
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec3d d = points[i] - origin;
        local[i] = {d.dot(s.xDir), d.dot(s.yDir), d.dot(s.normal)};
        xmin = std::min(xmin, local[i].x());
        xmax = std::max(xmax, local[i].x());
        ymin = std::min(ymin, local[i].y());
        ymax = std::max(ymax, local[i].y());
    }
    s.x0 = xmin - margin;
    s.y0 = ymin - margin;
    s.width = std::max(1e-6, xmax - xmin + 2 * margin);
    s.height = std::max(1e-6, ymax - ymin + 2 * margin);
    const int pu = surfaceDegree(s.nu), pv = surfaceDegree(s.nv);
    const auto U = clampedKnots(s.nu, pu), V = clampedKnots(s.nv, pv);
    const int N = s.nu * s.nv;
    Eigen::MatrixXd A = Eigen::MatrixXd::Zero(N, N);
    Eigen::VectorXd b = Eigen::VectorXd::Zero(N);
    double Nu[8], Nv[8];
    for (const auto& q : local) {
        const double u = (q.x() - s.x0) / s.width, v = (q.y() - s.y0) / s.height;
        const int fu = basis(U, pu, u, Nu), fv = basis(V, pv, v, Nv);
        int idx[16];
        double w[16];
        int k = 0;
        for (int bb = 0; bb <= pv; ++bb)
            for (int a = 0; a <= pu; ++a, ++k) {
                idx[k] = fu + a + s.nu * (fv + bb);
                w[k] = Nu[a] * Nv[bb];
            }
        for (int x = 0; x < k; ++x) {
            b[idx[x]] += w[x] * q.z();
            for (int y = 0; y < k; ++y) A(idx[x], idx[y]) += w[x] * w[y];
        }
    }
    // Bending: second differences along u and v and the twist, weighed so
    // that the weight means the same whatever the number of points.
    const double lambda = smoothing * static_cast<double>(points.size());
    auto addRow = [&](const std::vector<std::pair<int, double>>& row, double weight) {
        for (const auto& [i, ci] : row)
            for (const auto& [j, cj] : row) A(i, j) += weight * ci * cj;
    };
    auto id = [&](int i, int j) { return i + s.nu * j; };
    for (int j = 0; j < s.nv; ++j)
        for (int i = 1; i + 1 < s.nu; ++i) addRow({{id(i - 1, j), 1}, {id(i, j), -2}, {id(i + 1, j), 1}}, lambda);
    for (int i = 0; i < s.nu; ++i)
        for (int j = 1; j + 1 < s.nv; ++j) addRow({{id(i, j - 1), 1}, {id(i, j), -2}, {id(i, j + 1), 1}}, lambda);
    for (int j = 0; j + 1 < s.nv; ++j)
        for (int i = 0; i + 1 < s.nu; ++i) addRow({{id(i, j), 1}, {id(i + 1, j), -1}, {id(i, j + 1), -1}, {id(i + 1, j + 1), 1}}, 2 * lambda);
    // A whisper of plain closeness to zero keeps poles with no points near them defined.
    for (int i = 0; i < N; ++i) A(i, i) += 1e-9 * (1 + lambda);
    const Eigen::VectorXd h = A.ldlt().solve(b);
    for (int i = 0; i < N; ++i) s.heights[static_cast<std::size_t>(i)] = h[i];
    // How far the points are from it.
    std::vector<double> d(local.size());
    double sum2 = 0;
    for (std::size_t i = 0; i < local.size(); ++i) {
        const auto& q = local[i];
        d[i] = std::abs(q.z() - s.heightAt((q.x() - s.x0) / s.width, (q.y() - s.y0) / s.height));
        sum2 += d[i] * d[i];
        out.maxDeviation = std::max(out.maxDeviation, d[i]);
    }
    out.points = local.size();
    out.rms = std::sqrt(sum2 / static_cast<double>(local.size()));
    std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
    out.median = d[d.size() / 2];
    return out;
}

}  // namespace orv

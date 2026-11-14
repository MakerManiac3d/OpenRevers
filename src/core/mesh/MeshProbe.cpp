// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/MeshProbe.h"

#include "core/util/Cancel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>

namespace orv {

namespace {
constexpr double kPi = std::numbers::pi;
const double kNaN = std::numeric_limits<double>::quiet_NaN();

double percentile(std::vector<double>& v, double q) {
    if (v.empty()) return kNaN;
    const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}
}  // namespace

std::vector<RayHit> castRay(const TriMesh& m, const Vec3d& o, const Vec3d& dirIn, double tMin, double tMax) {
    std::vector<RayHit> hits;
    const Vec3d d = dirIn.normalized();
    for (std::size_t f = 0; f < m.faces.size(); ++f) {
        if ((f & 0xFFFF) == 0) checkCancel();
        const Vec3d a = m.vertices[m.faces[f][0]].cast<double>(), b = m.vertices[m.faces[f][1]].cast<double>(), c = m.vertices[m.faces[f][2]].cast<double>();
        // Möller–Trumbore
        const Vec3d e1 = b - a, e2 = c - a, p = d.cross(e2);
        const double det = e1.dot(p);
        if (std::abs(det) < 1e-14) continue;
        const double inv = 1.0 / det;
        const Vec3d s = o - a;
        const double u = s.dot(p) * inv;
        if (u < 0.0 || u > 1.0) continue;
        const Vec3d q = s.cross(e1);
        const double v = d.dot(q) * inv;
        if (v < 0.0 || u + v > 1.0) continue;
        const double t = e2.dot(q) * inv;
        if (t < tMin || t > tMax) continue;
        const Vec3d n = e1.cross(e2);
        hits.push_back({t, o + t * d, n.dot(d) < 0});
    }
    std::sort(hits.begin(), hits.end(), [](const RayHit& x, const RayHit& y) { return x.t < y.t; });
    // A ray through an edge or a vertex hits two triangles at once: keep one.
    std::vector<RayHit> out;
    for (const auto& h : hits)
        if (out.empty() || h.t - out.back().t > 1e-7 || h.entering != out.back().entering) out.push_back(h);
    return out;
}

std::vector<Chord> chordsOf(const std::vector<RayHit>& hits) {
    std::vector<Chord> out;
    const RayHit* open = nullptr;
    for (const auto& h : hits) {
        if (h.entering) {
            if (!open) open = &h;
        } else if (open) {
            out.push_back({open->t, h.t, open->point, h.point});
            open = nullptr;
        }
    }
    return out;
}

void Axis::normalize(const Vec3d* wanted) {
    if (direction.norm() < 1e-12) direction = Vec3d::UnitZ();
    direction.normalize();
    Vec3d r = wanted ? *wanted : reference;
    r -= r.dot(direction) * direction;
    if (r.norm() < 1e-9) r = direction.unitOrthogonal();
    reference = r.normalized();
}

double Axis::radius(const Vec3d& p) const {
    const Vec3d d = p - point;
    return (d - d.dot(direction) * direction).norm();
}

double Axis::angleDeg(const Vec3d& p) const {
    const Vec3d d = p - point;
    const Vec3d perp = d - d.dot(direction) * direction;
    const Vec3d y = direction.cross(reference);
    double a = std::atan2(perp.dot(y), perp.dot(reference)) * 180.0 / kPi;
    if (a < 0) a += 360.0;
    return a >= 360.0 ? 0.0 : a;
}

std::vector<Vec3d> samplePoints(const TriMesh& m, double spacing) {
    std::vector<Vec3d> out;
    out.reserve(m.vertices.size() + m.faces.size());
    for (const auto& v : m.vertices) out.push_back(v.cast<double>());
    constexpr std::size_t kMaxPoints = 30'000'000;
    std::size_t k = 0;
    for (const auto& f : m.faces) {
        if ((++k & 0xFFFF) == 0) checkCancel();
        const Vec3d a = m.vertices[f[0]].cast<double>(), b = m.vertices[f[1]].cast<double>(), c = m.vertices[f[2]].cast<double>();
        int n = 1;
        if (spacing > 0) {
            const double longest = std::max({(b - a).norm(), (c - b).norm(), (a - c).norm()});
            n = std::clamp(static_cast<int>(std::ceil(longest / spacing)), 1, 80);
        }
        if (n == 1) {
            out.push_back((a + b + c) / 3.0);
            continue;
        }
        if (out.size() > kMaxPoints) continue;
        // Rejilla baricentrica interior y de aristas: i + j <= n.
        for (int i = 0; i <= n; ++i)
            for (int j = 0; i + j <= n; ++j) {
                if ((i == 0 || j == 0 || i + j == n) && (i + j == 0 || (i == n) || (j == n) || (i == 0 && j == 0))) continue;  // vertices ya incluidos
                const double u = static_cast<double>(i) / n, v = static_cast<double>(j) / n;
                out.push_back(a + (b - a) * u + (c - a) * v);
            }
    }
    return out;
}

std::vector<ProfileBin> radialProfile(const std::vector<Vec3d>& pts, const Axis& axis, double step, double zMin, double zMax, double maxRadius) {
    if (!(step > 0) || pts.empty()) return {};
    double lo = zMin, hi = zMax;
    if (std::isnan(lo) || std::isnan(hi)) {
        double a = 1e300, b = -1e300;
        for (const auto& p : pts) {
            const double z = axis.along(p);
            a = std::min(a, z);
            b = std::max(b, z);
        }
        if (std::isnan(lo)) lo = a;
        if (std::isnan(hi)) hi = b;
    }
    if (!(hi > lo)) return {};
    const auto n = static_cast<std::size_t>(std::ceil((hi - lo) / step));
    if (n > 20000) return {};
    std::vector<std::vector<double>> radii(n);
    std::size_t k = 0;
    for (const auto& p : pts) {
        if ((++k & 0xFFFFF) == 0) checkCancel();
        const double z = axis.along(p);
        if (z < lo || z >= hi) continue;
        const double r = axis.radius(p);
        if (maxRadius > 0 && r > maxRadius) continue;
        radii[std::min(n - 1, static_cast<std::size_t>((z - lo) / step))].push_back(r);
    }
    std::vector<ProfileBin> bins(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto& r = radii[i];
        ProfileBin& b = bins[i];
        b.z0 = lo + static_cast<double>(i) * step;
        b.z1 = b.z0 + step;
        b.count = r.size();
        if (r.empty()) continue;
        b.rMin = *std::min_element(r.begin(), r.end());
        b.rMax = *std::max_element(r.begin(), r.end());
        b.rInner = percentile(r, 0.05);
        b.rMedian = percentile(r, 0.5);
        b.rOuter = percentile(r, 0.95);
    }
    return bins;
}

std::vector<ProfileZone> profileZones(const std::vector<ProfileBin>& bins, Skin skin, double tol) {
    std::vector<ProfileZone> zones;
    const std::size_t n = bins.size();
    auto value = [&](std::size_t i) { return skin == Skin::Outer ? bins[i].rOuter : bins[i].rInner; };
    auto usable = [&](std::size_t i) { return bins[i].count >= 3; };
    std::size_t i = 0;
    while (i < n) {
        if (!usable(i)) { ++i; continue; }
        // Grow the zone while a straight line (least squares) stays within tol.
        std::size_t j = i + 1;
        double slope = 0, icpt = value(i), worst = 0;
        auto fit = [&](std::size_t last, double& s, double& c, double& w) {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            std::size_t m = 0;
            for (std::size_t k = i; k <= last; ++k) {
                if (!usable(k)) continue;
                const double x = 0.5 * (bins[k].z0 + bins[k].z1), y = value(k);
                sx += x; sy += y; sxx += x * x; sxy += x * y;
                ++m;
            }
            const double den = static_cast<double>(m) * sxx - sx * sx;
            s = std::abs(den) < 1e-12 ? 0.0 : (static_cast<double>(m) * sxy - sx * sy) / den;
            c = (sy - s * sx) / static_cast<double>(m);
            w = 0;
            for (std::size_t k = i; k <= last; ++k)
                if (usable(k)) w = std::max(w, std::abs(value(k) - (c + s * 0.5 * (bins[k].z0 + bins[k].z1))));
        };
        while (j < n) {
            if (!usable(j)) { // a gap ends the zone
                break;
            }
            double s, c, w;
            fit(j, s, c, w);
            if (w > tol) break;
            slope = s; icpt = c; worst = w;
            ++j;
        }
        ProfileZone z;
        const std::size_t last = j - 1;
        z.z0 = bins[i].z0;
        z.z1 = bins[last].z1;
        z.bins = last - i + 1;
        if (z.bins == 1) {
            z.rStart = z.rEnd = value(i);
        } else {
            z.rStart = icpt + slope * z.z0;
            z.rEnd = icpt + slope * z.z1;
        }
        z.halfAngleDeg = std::atan(slope) * 180.0 / kPi;
        z.maxResidual = worst;
        z.kind = z.bins <= 2 ? "step" : (std::abs(z.halfAngleDeg) < 0.5 ? "cylinder" : "cone");
        if (z.kind == "cylinder") { z.halfAngleDeg = 0; z.rStart = z.rEnd = 0.5 * (z.rStart + z.rEnd); }
        zones.push_back(z);
        i = j;
    }
    return zones;
}

Unwrapped unwrap(const std::vector<Vec3d>& pts, const Axis& axis, Skin skin, int nAngle, double dz, double zMin, double zMax, double maxRadius) {
    Unwrapped u;
    nAngle = std::clamp(nAngle, 8, 1440);
    double lo = zMin, hi = zMax;
    if (std::isnan(lo) || std::isnan(hi)) {
        double a = 1e300, b = -1e300;
        for (const auto& p : pts) {
            const double z = axis.along(p);
            a = std::min(a, z);
            b = std::max(b, z);
        }
        if (std::isnan(lo)) lo = a;
        if (std::isnan(hi)) hi = b;
    }
    if (!(hi > lo) || !(dz > 0)) return u;
    const auto nz = static_cast<std::size_t>(std::ceil((hi - lo) / dz));
    if (nz > 4000 || nz * static_cast<std::size_t>(nAngle) > 4000000) return u;
    u.nAngle = nAngle;
    u.nAlong = static_cast<int>(nz);
    u.z0 = lo;
    u.dz = dz;
    std::vector<std::vector<float>> cells(nz * static_cast<std::size_t>(nAngle));
    std::size_t k = 0;
    for (const auto& p : pts) {
        if ((++k & 0xFFFFF) == 0) checkCancel();
        const double z = axis.along(p);
        if (z < lo || z >= hi) continue;
        const double r = axis.radius(p);
        if (maxRadius > 0 && r > maxRadius) continue;
        if (r < 1e-9) continue;
        const std::size_t row = std::min(nz - 1, static_cast<std::size_t>((z - lo) / dz));
        const std::size_t col = std::min(static_cast<std::size_t>(nAngle - 1), static_cast<std::size_t>(axis.angleDeg(p) / 360.0 * nAngle));
        cells[row * static_cast<std::size_t>(nAngle) + col].push_back(static_cast<float>(r));
    }
    u.radius.assign(cells.size(), std::numeric_limits<float>::quiet_NaN());
    for (std::size_t c = 0; c < cells.size(); ++c) {
        if (cells[c].empty()) { ++u.empty; continue; }
        std::vector<double> v(cells[c].begin(), cells[c].end());
        u.radius[c] = static_cast<float>(percentile(v, skin == Skin::Outer ? 0.95 : 0.05));
    }
    u.rowMedian.assign(nz, std::numeric_limits<float>::quiet_NaN());
    for (std::size_t r = 0; r < nz; ++r) {
        std::vector<double> v;
        for (int c = 0; c < nAngle; ++c) {
            const float x = u.radius[r * static_cast<std::size_t>(nAngle) + static_cast<std::size_t>(c)];
            if (!std::isnan(x)) v.push_back(x);
        }
        if (v.size() >= static_cast<std::size_t>(nAngle) / 4 + 1) u.rowMedian[r] = static_cast<float>(percentile(v, 0.5));
    }
    return u;
}

std::vector<Deviation> deviations(const Unwrapped& u, double threshold, std::size_t minCells) {
    std::vector<Deviation> out;
    if (u.nAngle == 0) return out;
    const auto nA = static_cast<std::size_t>(u.nAngle), nZ = static_cast<std::size_t>(u.nAlong);
    std::vector<signed char> mark(nA * nZ, 0);  // 0 none, 1 candidate, 2 taken
    auto dev = [&](std::size_t r, std::size_t c) -> double {
        const float x = u.radius[r * nA + c], m = u.rowMedian[r];
        return std::isnan(x) || std::isnan(m) ? kNaN : static_cast<double>(x) - static_cast<double>(m);
    };
    for (std::size_t r = 0; r < nZ; ++r)
        for (std::size_t c = 0; c < nA; ++c) {
            const double d = dev(r, c);
            if (!std::isnan(d) && std::abs(d) > threshold) mark[r * nA + c] = 1;
        }
    const double dA = 360.0 / static_cast<double>(nA);
    for (std::size_t r0 = 0; r0 < nZ; ++r0)
        for (std::size_t c0 = 0; c0 < nA; ++c0) {
            if (mark[r0 * nA + c0] != 1) continue;
            const bool positive = dev(r0, c0) > 0;
            std::queue<std::pair<std::size_t, std::size_t>> q;
            q.push({r0, c0});
            mark[r0 * nA + c0] = 2;
            Deviation d;
            d.z0 = 1e300; d.z1 = -1e300;
            double sum = 0, rowSum = 0;
            std::vector<char> colsHit(nA, 0);
            std::size_t cnt = 0;
            while (!q.empty()) {
                const auto [r, c] = q.front();
                q.pop();
                const double v = dev(r, c);
                sum += v;
                rowSum += u.rowMedian[r];
                ++cnt;
                if (std::abs(v) > std::abs(d.worstDeviation)) d.worstDeviation = v;
                d.z0 = std::min(d.z0, u.z0 + static_cast<double>(r) * u.dz);
                d.z1 = std::max(d.z1, u.z0 + static_cast<double>(r + 1) * u.dz);
                colsHit[c] = 1;
                const std::pair<std::ptrdiff_t, std::ptrdiff_t> nb[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (auto [dr, dc] : nb) {
                    const std::ptrdiff_t rr = static_cast<std::ptrdiff_t>(r) + dr;
                    if (rr < 0 || rr >= static_cast<std::ptrdiff_t>(nZ)) continue;
                    const auto cc = static_cast<std::size_t>((static_cast<std::ptrdiff_t>(c) + dc + static_cast<std::ptrdiff_t>(nA)) % static_cast<std::ptrdiff_t>(nA));
                    const std::size_t idx = static_cast<std::size_t>(rr) * nA + cc;
                    if (mark[idx] != 1 || (dev(static_cast<std::size_t>(rr), cc) > 0) != positive) continue;
                    mark[idx] = 2;
                    q.push({static_cast<std::size_t>(rr), cc});
                }
            }
            if (cnt < minCells) continue;
            // The angular span: the arc of columns hit that leaves the widest gap outside.
            std::size_t gapStart = 0, gapLen = 0, runStart = 0, run = 0;
            for (std::size_t i = 0; i < 2 * nA; ++i) {
                if (!colsHit[i % nA]) {
                    if (run == 0) runStart = i;
                    ++run;
                    if (run > gapLen && run <= nA) { gapLen = run; gapStart = runStart; }
                } else {
                    run = 0;
                }
            }
            const std::size_t first = gapLen ? (gapStart + gapLen) % nA : 0;
            const std::size_t span = nA - gapLen;
            d.angle0 = static_cast<double>(first) * dA;
            d.angle1 = d.angle0 + static_cast<double>(span) * dA;
            d.cells = cnt;
            d.meanDeviation = sum / static_cast<double>(cnt);
            d.rowRadius = rowSum / static_cast<double>(cnt);
            out.push_back(d);
        }
    std::sort(out.begin(), out.end(), [](const Deviation& a, const Deviation& b) { return a.cells > b.cells; });
    return out;
}

}  // namespace orv

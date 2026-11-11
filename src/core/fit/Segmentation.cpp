// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fit/Segmentation.h"

#include "core/mesh/EdgeMap.h"

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace orv {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct FaceData {
    std::vector<Vec3d> centre, normal;
    std::vector<double> area;
    std::vector<std::array<std::uint32_t, 3>> nbr;  // face across each edge, or UINT32_MAX
};

FaceData faceData(const TriMesh& m) {
    FaceData d;
    const std::size_t F = m.faces.size();
    d.centre.resize(F);
    d.normal.resize(F);
    d.area.resize(F);
    d.nbr.assign(F, {UINT32_MAX, UINT32_MAX, UINT32_MAX});
    for (std::size_t f = 0; f < F; ++f) {
        const Tri& t = m.faces[f];
        const Vec3d a = m.vertices[t[0]].cast<double>(), b = m.vertices[t[1]].cast<double>(), c = m.vertices[t[2]].cast<double>();
        const Vec3d n = (b - a).cross(c - a);
        d.area[f] = 0.5 * n.norm();
        d.normal[f] = n.norm() > 0 ? Vec3d(n.normalized()) : Vec3d::Zero();
        d.centre[f] = (a + b + c) / 3.0;
    }
    const EdgeMap em(m);
    for (std::size_t f = 0; f < F; ++f)
        for (std::size_t k = 0; k < 3; ++k) {
            const auto e = em.edgeOf(f, k);
            if (e == EdgeMap::kNone) continue;
            const auto fs = em.faces(em.edge(e));
            if (fs.size() != 2) continue;  // open rims and non-manifold edges stop growth
            d.nbr[f][k] = fs[0] == f ? fs[1] : fs[0];
        }
    return d;
}

/// Up to `count` faces around `seed`, breadth first, crossing only smooth edges.
std::vector<std::uint32_t> patchAround(const FaceData& d, std::uint32_t seed, std::size_t count, double cosSmooth,
                                       const std::vector<std::int32_t>* taken, std::vector<std::uint32_t>& mark, std::uint32_t stamp) {
    std::vector<std::uint32_t> out{seed};
    mark[seed] = stamp;
    for (std::size_t i = 0; i < out.size() && out.size() < count; ++i) {
        const auto f = out[i];
        for (auto g : d.nbr[f]) {
            if (g == UINT32_MAX || mark[g] == stamp) continue;
            if (taken && (*taken)[g] != Segmentation::kNone) continue;
            if (d.normal[f].dot(d.normal[g]) < cosSmooth) continue;
            mark[g] = stamp;
            out.push_back(g);
            if (out.size() >= count) break;
        }
    }
    return out;
}

void samples(const FaceData& d, const std::vector<std::uint32_t>& faces, std::vector<Vec3d>& p, std::vector<Vec3d>& n) {
    p.clear();
    n.clear();
    for (auto f : faces) {
        p.push_back(d.centre[f]);
        n.push_back(d.normal[f]);
    }
}

bool sameShape(const Primitive& a, const Primitive& b, double tol) {
    if (a.type != b.type) return false;
    const double axisCos = std::abs(a.axis.dot(b.axis));
    switch (a.type) {
        case PrimitiveType::Plane:
            return a.axis.dot(b.axis) > std::cos(1.0 * kPi / 180) && std::abs(a.distance(b.origin)) < tol;
        case PrimitiveType::Sphere:
            return (a.origin - b.origin).norm() < tol && std::abs(a.radius - b.radius) < tol;
        case PrimitiveType::Cylinder: {
            const Vec3d d = b.origin - a.origin;
            // A short bore's axis is uncertain by several degrees: two halves of it
            // may disagree that much; the joint fit decides.
            return axisCos > std::cos(10.0 * kPi / 180) && (d - d.dot(a.axis) * a.axis).norm() < tol && std::abs(a.radius - b.radius) < tol;
        }
        case PrimitiveType::Cone:
            return axisCos > std::cos(1.0 * kPi / 180) && (a.origin - b.origin).norm() < 3 * tol && std::abs(a.angle - b.angle) < 0.5 * kPi / 180;
        case PrimitiveType::Torus:
            return axisCos > std::cos(1.0 * kPi / 180) && (a.origin - b.origin).norm() < tol && std::abs(a.radius - b.radius) < tol &&
                   std::abs(a.radius2 - b.radius2) < tol;
    }
    return false;
}

/// Pieces of one curved face that came out as separate small regions (a
/// shallow hole's wall cut into flat facets or into short cylinders of
/// different radii, a fillet into strips): join neighbouring regions while
/// their union fits one cylinder, cone or sphere within the tolerance, and is
/// really curved (no single plane fits it).
void joinIntoCurved(const TriMesh& m, const FaceData& d, Segmentation& s, double tol) {
    const std::size_t R = s.regions.size(), F = m.faces.size();
    if (R < 2) return;
    std::vector<std::vector<std::uint32_t>> facesOf(R);
    for (std::size_t f = 0; f < F; ++f)
        if (s.faceRegion[f] != Segmentation::kNone) facesOf[static_cast<std::size_t>(s.faceRegion[f])].push_back(static_cast<std::uint32_t>(f));
    // Neighbours: touching, or across a gap of up to three free faces (the
    // strip the growth left between two pieces).
    std::vector<std::vector<std::uint32_t>> nbrs(R);
    {
        std::vector<std::int32_t> owner(F, Segmentation::kNone);  // region that reached a free face first
        std::vector<std::uint32_t> front;
        for (std::size_t f = 0; f < F; ++f)
            if (s.faceRegion[f] != Segmentation::kNone) { owner[f] = s.faceRegion[f]; front.push_back(static_cast<std::uint32_t>(f)); }
        auto link = [&](std::int32_t a, std::int32_t b) {
            if (a == b || a == Segmentation::kNone || b == Segmentation::kNone) return;
            nbrs[static_cast<std::size_t>(a)].push_back(static_cast<std::uint32_t>(b));
            nbrs[static_cast<std::size_t>(b)].push_back(static_cast<std::uint32_t>(a));
        };
        for (int ring = 0; ring <= 3 && !front.empty(); ++ring) {
            std::vector<std::uint32_t> next;
            for (auto f : front)
                for (auto g : d.nbr[f]) {
                    if (g == UINT32_MAX) continue;
                    if (owner[g] == Segmentation::kNone) {
                        if (ring < 3) { owner[g] = owner[f]; next.push_back(g); }
                    } else {
                        link(owner[f], owner[g]);
                    }
                }
            front = std::move(next);
        }
        for (auto& v : nbrs) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }
    }
    double largest = 0;
    for (const auto& r : s.regions) largest = std::max(largest, r.area);

    std::vector<Vec3d> p, n;
    FitOptions fo;
    fo.maxIterations = 30;
    auto extent = [&](const std::vector<std::uint32_t>& faces) {
        Eigen::AlignedBox3d box;
        for (auto f : faces) box.extend(d.centre[f]);
        return box.diagonal().norm();
    };
    // A union worth keeping as one curved shape: fits, few outliers, and is
    // not a nearly flat piece of a huge cylinder or sphere.
    auto curvedFit = [&](const std::vector<std::uint32_t>& faces) -> std::optional<Primitive> {
        samples(d, faces, p, n);
        std::optional<Primitive> best;
        for (auto t : {PrimitiveType::Cylinder, PrimitiveType::Sphere, PrimitiveType::Cone}) {
            auto sh = fitPrimitive(t, p, n, fo);
            if (!sh || sh->rms > tol || sh->outliers * 20 > p.size()) continue;
            const double ext = extent(faces);
            if (t != PrimitiveType::Cone && sh->radius > 2 * ext) continue;
            if (t == PrimitiveType::Cone) {  // apex near the piece, and not nearly a plane or a cylinder
                Vec3d c = Vec3d::Zero();
                for (const auto& q : p) c += q;
                c /= static_cast<double>(p.size());
                if ((sh->origin - c).norm() > 4 * ext || sh->angle < 5 * kPi / 180 || sh->angle > 80 * kPi / 180) continue;
            }
            if (!best || sh->rms < best->rms * 0.8) best = sh;  // the simpler type unless clearly worse
        }
        if (!best) return best;
        auto flat = fitPrimitive(PrimitiveType::Plane, p, n, fo);
        if (flat && flat->rms <= tol) return std::nullopt;  // a plane does: leave it to the plane merge
        return best;
    };

    std::vector<std::int32_t> group(R, -1);  // region -> leader it was joined to
    std::vector<char> alive(R, 1);
    std::vector<std::size_t> order(R);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return s.regions[a].area < s.regions[b].area; });
    bool changed = false;
    for (auto lead : order) {
        if (!alive[lead] || s.regions[lead].area > 0.25 * largest) continue;
        std::vector<std::uint32_t> faces = facesOf[lead], members{static_cast<std::uint32_t>(lead)};
        std::optional<Primitive> shape;
        if (s.regions[lead].shape.type != PrimitiveType::Plane) shape = s.regions[lead].shape;
        bool grew = true;
        while (grew) {
            grew = false;
            std::vector<std::uint32_t> cand;
            for (auto r : members)
                for (auto q : nbrs[r])
                    if (alive[q] && std::find(members.begin(), members.end(), q) == members.end()) cand.push_back(q);
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
            double area = 0;
            for (auto r : members) area += s.regions[r].area;
            for (auto q : cand) {
                if (s.regions[q].area > 0.25 * largest && s.regions[q].area > 4 * area) continue;  // a main face is not part of a fillet
                std::vector<std::uint32_t> both = faces;
                both.insert(both.end(), facesOf[q].begin(), facesOf[q].end());
                auto u = curvedFit(both);
                if (!u) continue;
                faces = std::move(both);
                members.push_back(q);
                shape = u;
                grew = true;
            }
        }
        if (members.size() < 2 || !shape) continue;
        for (auto r : members) {
            if (r == lead) continue;
            alive[r] = 0;
            facesOf[r].clear();
        }
        facesOf[lead] = std::move(faces);
        s.regions[lead].shape = *shape;
        // the joined region is no longer small: its neighbours are the union's
        for (auto r : members)
            if (r != lead) nbrs[lead].insert(nbrs[lead].end(), nbrs[r].begin(), nbrs[r].end());
        changed = true;
    }
    if (!changed) return;
    std::vector<Region> kept;
    std::fill(s.faceRegion.begin(), s.faceRegion.end(), Segmentation::kNone);
    for (std::size_t r = 0; r < R; ++r) {
        if (!alive[r]) continue;
        const auto id = static_cast<std::int32_t>(kept.size());
        Region reg;
        reg.shape = s.regions[r].shape;
        reg.faces = facesOf[r].size();
        for (auto f : facesOf[r]) { reg.area += d.area[f]; s.faceRegion[f] = id; }
        kept.push_back(reg);
    }
    s.regions = std::move(kept);
}

}  // namespace

double estimateNoise(const TriMesh& m) {
    if (m.faces.size() < 50) return 0.0;
    const FaceData d = faceData(m);
    std::mt19937 g(12345);
    std::uniform_int_distribution<std::size_t> pick(0, m.faces.size() - 1);
    std::vector<std::uint32_t> mark(m.faces.size(), 0);
    std::vector<double> sig;
    std::vector<Vec3d> p, n;
    const int tries = static_cast<int>(std::min<std::size_t>(300, m.faces.size() / 10 + 1));
    for (int i = 0; i < tries; ++i) {
        const auto f = static_cast<std::uint32_t>(pick(g));
        auto patch = patchAround(d, f, 60, std::cos(25 * kPi / 180), nullptr, mark, static_cast<std::uint32_t>(i + 1));
        if (patch.size() < 30) continue;
        samples(d, patch, p, n);
        FitOptions o;
        o.maxIterations = 20;
        // Plane and sphere are enough here: on a few dozen faces any smooth spot is close to one of them.
        // A quadratic height field over the patch's plane follows any smooth
        // surface at this size (saddles too); what is left is the noise. It is
        // measured on the vertices, not the face centres (which average three).
        std::vector<std::uint32_t> verts;
        for (auto face : patch)
            for (auto v : m.faces[face]) verts.push_back(v);
        std::sort(verts.begin(), verts.end());
        verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
        if (verts.size() < 20) continue;
        const auto plane = fitPrimitive(PrimitiveType::Plane, p, n, o);
        if (!plane) continue;
        const Vec3d ax = plane->axis, ux = std::abs(ax.x()) < 0.9 ? ax.cross(Vec3d::UnitX()).normalized() : ax.cross(Vec3d::UnitY()).normalized(),
                    vx = ax.cross(ux);
        Eigen::MatrixXd A(static_cast<Eigen::Index>(verts.size()), 6);
        Eigen::VectorXd z(static_cast<Eigen::Index>(verts.size()));
        for (std::size_t k = 0; k < verts.size(); ++k) {
            const Vec3d q = m.vertices[verts[k]].cast<double>() - plane->origin;
            const double x = q.dot(ux), y = q.dot(vx);
            A.row(static_cast<Eigen::Index>(k)) << x * x, x * y, y * y, x, y, 1.0;
            z[static_cast<Eigen::Index>(k)] = q.dot(ax);
        }
        const Eigen::VectorXd c = A.colPivHouseholderQr().solve(z);
        const double ss = (A * c - z).squaredNorm();
        const double best = std::sqrt(ss / static_cast<double>(verts.size() - 6));
        if (best < 1e299) sig.push_back(best);
    }
    if (sig.empty()) return 0.0;
    std::sort(sig.begin(), sig.end());
    const double local = sig[sig.size() / 4];

    // A scanner's software smooths its mesh: at a few faces' size there is
    // almost no noise left, yet the surface still ripples by a few hundredths
    // over a millimetre or two, and that is what a face of the part deviates
    // by. So the noise is also measured as the flatness of the flattest spots
    // a few percent of the part's size across.
    Eigen::AlignedBox3d box;
    double faceArea = 0;
    for (std::size_t f = 0; f < m.faces.size(); ++f) { box.extend(d.centre[f]); faceArea += d.area[f]; }
    faceArea /= static_cast<double>(m.faces.size());
    // Patches stay on one face: every normal within 20° of the seed's, so a
    // patch on a thin part does not fold over a rounded edge. Only patches
    // that grew to their full size count (they fit inside a face).
    const double R = 0.06 * box.diagonal().norm();
    const auto count = static_cast<std::size_t>(std::clamp(kPi * R * R / std::max(faceArea, 1e-12), 50.0, 20000.0));
    std::vector<double> wide;
    std::fill(mark.begin(), mark.end(), 0u);
    const double cone = std::cos(20 * kPi / 180);
    for (int i = 0; i < 300; ++i) {
        const auto f = static_cast<std::uint32_t>(pick(g));
        const auto stamp = static_cast<std::uint32_t>(i + 1);
        std::vector<std::uint32_t> patch{f};
        mark[f] = stamp;
        double area = d.area[f];
        for (std::size_t k = 0; k < patch.size() && patch.size() < 4 * count; ++k)
            for (auto nb : d.nbr[patch[k]]) {
                if (nb == UINT32_MAX || mark[nb] == stamp || d.normal[nb].dot(d.normal[f]) < cone || (d.centre[nb] - d.centre[f]).norm() > R) continue;
                mark[nb] = stamp;
                patch.push_back(nb);
                area += d.area[nb];
            }
        if (area < 0.7 * kPi * R * R) continue;  // the disc does not fit on one face
        std::vector<std::uint32_t> verts;
        for (auto face : patch)
            for (auto v : m.faces[face]) verts.push_back(v);
        std::sort(verts.begin(), verts.end());
        verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
        Vec3d c = Vec3d::Zero();
        for (auto v : verts) c += m.vertices[v].cast<double>();
        c /= static_cast<double>(verts.size());
        Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
        for (auto v : verts) { const Vec3d q = m.vertices[v].cast<double>() - c; C += q * q.transpose(); }
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
        // A quadratic height field takes out the curvature of a curved spot
        // (a part may have no flat face); the ripple, shorter than the patch, stays.
        const Vec3d ax = es.eigenvectors().col(0), ux = ax.unitOrthogonal(), vx = ax.cross(ux);
        Eigen::MatrixXd A(static_cast<Eigen::Index>(verts.size()), 6);
        Eigen::VectorXd z(static_cast<Eigen::Index>(verts.size()));
        for (std::size_t k = 0; k < verts.size(); ++k) {
            const Vec3d q = m.vertices[verts[k]].cast<double>() - c;
            const double x = q.dot(ux), y = q.dot(vx);
            A.row(static_cast<Eigen::Index>(k)) << x * x, x * y, y * y, x, y, 1.0;
            z[static_cast<Eigen::Index>(k)] = q.dot(ax);
        }
        const Eigen::VectorXd coef = A.colPivHouseholderQr().solve(z);
        wide.push_back(std::sqrt((A * coef - z).squaredNorm() / static_cast<double>(verts.size() - 6)));
    }
    if (wide.size() < 10) return local;
    std::sort(wide.begin(), wide.end());
    return std::max(local, wide[wide.size() / 5]);
}

Segmentation segment(const TriMesh& m, const SegmentOptions& o) {
    Segmentation s;
    const std::size_t F = m.faces.size();
    s.faceRegion.assign(F, Segmentation::kNone);
    if (F == 0) return s;
    const FaceData d = faceData(m);
    // Mean edge length bounds the tolerance from below: on a clean (noise-free)
    // mesh the faces' centres still sit off a curved surface by the chord error.
    double edge = 0;
    for (const Tri& t : m.faces) edge += (m.vertices[t[0]] - m.vertices[t[1]]).cast<double>().norm();
    edge /= static_cast<double>(F);
    s.noise = o.noise > 0 ? o.noise : estimateNoise(m);
    const double tol = std::max(o.tolerance * s.noise, 0.02 * edge);
    const double cosAng = std::cos(o.angleDeg * kPi / 180);
    const std::size_t minFaces = o.minFaces ? o.minFaces : std::max<std::size_t>(20, F / 2000);

    // Seeds: the smoothest faces first (least normal change to their neighbours).
    std::vector<std::pair<double, std::uint32_t>> order(F);
    for (std::size_t f = 0; f < F; ++f) {
        double c = 0;
        for (auto g : d.nbr[f]) c += g == UINT32_MAX ? 2.0 : 1.0 - d.normal[f].dot(d.normal[g]);
        order[f] = {c, static_cast<std::uint32_t>(f)};
    }
    std::sort(order.begin(), order.end());

    std::vector<std::uint32_t> mark(F, 0);
    std::uint32_t stamp = 0;
    std::vector<char> tried(F, 0);
    std::vector<Vec3d> p, n;
    for (const auto& [score, seed] : order) {
        if (s.faceRegion[seed] != Segmentation::kNone || tried[seed]) continue;
        tried[seed] = 1;
        auto patch = patchAround(d, seed, 60, std::cos(20 * kPi / 180), &s.faceRegion, mark, ++stamp);
        if (patch.size() < std::min<std::size_t>(minFaces, 30)) continue;
        samples(d, patch, p, n);
        auto shape = fitBest(p, n, s.noise);
        // The shape is fitted on face centres, whose noise is about σ/√3.
        if (!shape || shape->rms > tol) {
            for (auto f : patch) tried[f] = 1;  // this spot is not a primitive: do not seed around it again
            continue;
        }

        // Grow over neighbours on the shape; refit each time the region doubles.
        const auto id = static_cast<std::int32_t>(s.regions.size());
        std::vector<std::uint32_t> region;
        // A face is on the shape when its centre is within the tolerance, its
        // corners within 1.5 times it, and its normal close to the shape's.
        auto fits = [&](std::uint32_t f) {
            if (std::abs(shape->distance(d.centre[f])) > tol) return false;
            const Tri& t = m.faces[f];
            for (auto v : t)
                if (std::abs(shape->distance(m.vertices[v].cast<double>())) > 1.5 * tol) return false;
            return std::abs(shape->normal(d.centre[f]).dot(d.normal[f])) >= cosAng;
        };
        std::vector<std::uint32_t> front;
        for (auto f : patch)
            if (fits(f)) { s.faceRegion[f] = id; region.push_back(f); front.push_back(f); }
        if (region.size() < 3) {
            for (auto f : region) s.faceRegion[f] = Segmentation::kNone;
            for (auto f : patch) tried[f] = 1;
            continue;
        }
        std::size_t nextRefit = region.size() * 2;
        while (!front.empty()) {
            std::vector<std::uint32_t> next;
            for (auto f : front)
                for (auto g : d.nbr[f])
                    if (g != UINT32_MAX && s.faceRegion[g] == Segmentation::kNone && fits(g)) {
                        s.faceRegion[g] = id;
                        region.push_back(g);
                        next.push_back(g);
                    }
            front = std::move(next);
            if (region.size() >= nextRefit || front.empty()) {
                samples(d, region, p, n);
                FitOptions fo;
                fo.maxIterations = 30;
                // A small patch of a cone or torus looks like a cylinder or a
                // sphere: as the region grows, let the type change to the
                // simplest one that still fits all of it.
                // (Only when the current type starts to strain: trying every type is costly.)
                auto refit = fitPrimitive(shape->type, p, n, fo);
                if (!refit || refit->rms > 0.5 * tol || front.empty()) {  // straining, or stopped growing
                    auto best = fitBest(p, n, s.noise);
                    if (best && best->rms <= tol && (!refit || best->rms < refit->rms)) refit = best;
                }
                if (refit && refit->rms <= tol) {
                    const bool changed = refit->rms < shape->rms * 0.999 || region.size() >= nextRefit;
                    shape = refit;
                    if (front.empty() && changed) {  // the better shape may take in more faces: look around the rim once more
                        for (auto f : region)
                            for (auto g : d.nbr[f])
                                if (g != UINT32_MAX && s.faceRegion[g] == Segmentation::kNone) { front.push_back(f); break; }
                    }
                }
                nextRefit = region.size() * 2;
                if (front.size() > region.size()) front.clear();  // safety: never loop forever
            }
        }
        if (region.size() < minFaces) {
            for (auto f : region) { s.faceRegion[f] = Segmentation::kNone; tried[f] = 1; }
            continue;
        }
        Region r;
        samples(d, region, p, n);
        if (auto final = fitPrimitive(shape->type, p, n)) shape = final;
        r.shape = *shape;
        r.faces = region.size();
        for (auto f : region) r.area += d.area[f];
        s.regions.push_back(r);
    }

    if (o.merge && s.regions.size() > 1) {
        // Join regions of the same type whose union still fits one shape
        // within the tolerance (a plane cut by a slot, a cylinder split by a
        // seam). Only plausible pairs are refitted.
        const std::size_t R = s.regions.size();
        std::vector<std::vector<std::uint32_t>> facesOf(R);
        for (std::size_t f = 0; f < F; ++f)
            if (s.faceRegion[f] != Segmentation::kNone) facesOf[static_cast<std::size_t>(s.faceRegion[f])].push_back(static_cast<std::uint32_t>(f));
        std::vector<char> alive(R, 1);
        bool again = true;
        while (again) {
            again = false;
            for (std::size_t i = 0; i < R; ++i) {
                if (!alive[i]) continue;
                for (std::size_t j = i + 1; j < R; ++j) {
                    if (!alive[j] || !alive[i]) continue;
                    const Primitive& A = s.regions[i].shape;
                    const Primitive& B = s.regions[j].shape;
                    if (A.type != B.type) continue;
                    const double maxAxisDeg = A.type == PrimitiveType::Cylinder ? 10.0 : 3.0;
                    if (A.type != PrimitiveType::Sphere && std::abs(A.axis.dot(B.axis)) < std::cos(maxAxisDeg * kPi / 180)) continue;
                    if (!sameShape(A, B, std::max(10 * tol, 0.05 * std::max(A.radius, B.radius))) && A.type != PrimitiveType::Cone) continue;
                    std::vector<std::uint32_t> both = facesOf[i];
                    both.insert(both.end(), facesOf[j].begin(), facesOf[j].end());
                    samples(d, both, p, n);
                    FitOptions fo;
                    fo.maxIterations = 30;
                    auto u = fitPrimitive(A.type, p, n, fo);
                    if (!u || u->rms > tol || u->outliers > both.size() / 20) continue;
                    facesOf[i] = std::move(both);
                    facesOf[j].clear();
                    alive[j] = 0;
                    s.regions[i].shape = *u;
                    again = true;
                }
            }
        }
        std::vector<Region> merged;
        std::fill(s.faceRegion.begin(), s.faceRegion.end(), Segmentation::kNone);
        for (std::size_t i = 0; i < R; ++i) {
            if (!alive[i]) continue;
            const auto id = static_cast<std::int32_t>(merged.size());
            Region r;
            samples(d, facesOf[i], p, n);
            auto shape = fitPrimitive(s.regions[i].shape.type, p, n);
            r.shape = shape ? *shape : s.regions[i].shape;
            r.faces = facesOf[i].size();
            for (auto f : facesOf[i]) { r.area += d.area[f]; s.faceRegion[f] = id; }
            merged.push_back(r);
        }
        s.regions = std::move(merged);
        joinIntoCurved(m, d, s, tol);
    }
    // Final pass with the final shapes: each region takes in the free faces
    // next to it that lie on its shape, and a small region is absorbed by a
    // larger neighbour whose shape fits it (a cylinder fragment caught on the
    // inside of a torus, before the torus was known).
    {
        auto onShape = [&](const Primitive& sh, std::uint32_t f) {
            if (std::abs(sh.distance(d.centre[f])) > tol) return false;
            for (auto v : m.faces[f])
                if (std::abs(sh.distance(m.vertices[v].cast<double>())) > 1.5 * tol) return false;
            return std::abs(sh.normal(d.centre[f]).dot(d.normal[f])) >= cosAng;
        };
        std::vector<std::uint32_t> order2(s.regions.size());
        std::iota(order2.begin(), order2.end(), 0u);
        std::sort(order2.begin(), order2.end(), [&](std::uint32_t a2, std::uint32_t b2) { return s.regions[a2].area < s.regions[b2].area; });
        std::vector<std::vector<std::uint32_t>> facesOf(s.regions.size());
        for (std::size_t f = 0; f < F; ++f)
            if (s.faceRegion[f] != Segmentation::kNone) facesOf[static_cast<std::size_t>(s.faceRegion[f])].push_back(static_cast<std::uint32_t>(f));
        for (auto small : order2) {
            if (facesOf[small].empty()) continue;
            std::vector<std::int32_t> nbrRegions;
            for (auto f : facesOf[small])
                for (auto g : d.nbr[f])
                    if (g != UINT32_MAX && s.faceRegion[g] != Segmentation::kNone && s.faceRegion[g] != static_cast<std::int32_t>(small))
                        nbrRegions.push_back(s.faceRegion[g]);
            std::sort(nbrRegions.begin(), nbrRegions.end());
            nbrRegions.erase(std::unique(nbrRegions.begin(), nbrRegions.end()), nbrRegions.end());
            for (auto big : nbrRegions) {
                const auto b2 = static_cast<std::size_t>(big);
                if (s.regions[b2].area <= s.regions[small].area || facesOf[b2].empty()) continue;
                std::size_t ok = 0;
                for (auto f : facesOf[small]) ok += onShape(s.regions[b2].shape, f) ? 1 : 0;
                if (ok * 20 < facesOf[small].size() * 19) continue;
                for (auto f : facesOf[small]) s.faceRegion[f] = big;
                facesOf[b2].insert(facesOf[b2].end(), facesOf[small].begin(), facesOf[small].end());
                s.regions[b2].area += s.regions[small].area;
                facesOf[small].clear();
                break;
            }
        }
        for (std::size_t r = 0; r < s.regions.size(); ++r) {
            if (facesOf[r].empty()) continue;
            std::vector<std::uint32_t> front = facesOf[r];
            while (!front.empty()) {
                std::vector<std::uint32_t> next;
                for (auto f : front)
                    for (auto g : d.nbr[f])
                        if (g != UINT32_MAX && s.faceRegion[g] == Segmentation::kNone && onShape(s.regions[r].shape, g)) {
                            s.faceRegion[g] = static_cast<std::int32_t>(r);
                            facesOf[r].push_back(g);
                            next.push_back(g);
                        }
                front = std::move(next);
            }
        }
        // Rebuild the list without the absorbed regions, with fresh fits and areas.
        std::vector<Region> kept;
        std::vector<std::int32_t> newIdx(s.regions.size(), Segmentation::kNone);
        for (std::size_t r = 0; r < s.regions.size(); ++r) {
            if (facesOf[r].empty()) continue;
            newIdx[r] = static_cast<std::int32_t>(kept.size());
            Region reg;
            samples(d, facesOf[r], p, n);
            auto sh = fitPrimitive(s.regions[r].shape.type, p, n);
            reg.shape = sh && sh->rms <= std::max(2 * s.regions[r].shape.rms, tol) ? *sh : s.regions[r].shape;
            reg.faces = facesOf[r].size();
            for (auto f : facesOf[r]) reg.area += d.area[f];
            kept.push_back(reg);
        }
        for (auto& f : s.faceRegion)
            if (f != Segmentation::kNone) f = newIdx[static_cast<std::size_t>(f)];
        s.regions = std::move(kept);
    }

    // Largest regions first: they are the part's main faces.
    std::vector<std::size_t> idx(s.regions.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return s.regions[a].area > s.regions[b].area; });
    std::vector<std::int32_t> newId(s.regions.size());
    std::vector<Region> sorted;
    for (std::size_t k = 0; k < idx.size(); ++k) { newId[idx[k]] = static_cast<std::int32_t>(k); sorted.push_back(s.regions[idx[k]]); }
    for (auto& f : s.faceRegion)
        if (f != Segmentation::kNone) f = newId[static_cast<std::size_t>(f)];
    s.regions = std::move(sorted);
    s.unassignedFaces = static_cast<std::size_t>(std::count(s.faceRegion.begin(), s.faceRegion.end(), Segmentation::kNone));
    return s;
}

void regionSamples(const TriMesh& m, const Segmentation& s, std::int32_t region, std::vector<Vec3d>& points, std::vector<Vec3d>& normals) {
    points.clear();
    normals.clear();
    for (std::size_t f = 0; f < m.faces.size(); ++f) {
        if (s.faceRegion[f] != region) continue;
        const Tri& t = m.faces[f];
        const Vec3d a = m.vertices[t[0]].cast<double>(), b = m.vertices[t[1]].cast<double>(), c = m.vertices[t[2]].cast<double>();
        const Vec3d nn = (b - a).cross(c - a);
        if (nn.norm() <= 0) continue;
        points.push_back((a + b + c) / 3.0);
        normals.push_back(nn.normalized());
    }
}

}  // namespace orv

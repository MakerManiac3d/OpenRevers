// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/MeshQuad.h"
#include "core/util/Cancel.h"

#include "core/mesh/MeshAnalysis.h"

#include <optimizer.hpp>
#include <parametrizer.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace orv {

QuadRemeshResult remeshQuads(TriMesh& mesh, const QuadRemeshOptions& o) {
    if (mesh.faces.empty()) throw std::runtime_error("empty mesh");
    if (analyze(mesh).nonManifoldEdges > 0)
        throw std::runtime_error("non-manifold edges");

    // The solver keeps a lot of state: on the heap, never on the stack.
    auto field = std::make_unique<qflow::Parametrizer>();
    qflow::Parametrizer& f = *field;
    f.V.resize(3, static_cast<Eigen::Index>(mesh.vertices.size()));
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        f.V.col(static_cast<Eigen::Index>(i)) = mesh.vertices[i].cast<double>();
    f.F.resize(3, static_cast<Eigen::Index>(mesh.faces.size()));
    for (std::size_t i = 0; i < mesh.faces.size(); ++i)
        for (int k = 0; k < 3; ++k) f.F(k, static_cast<Eigen::Index>(i)) = static_cast<int>(mesh.faces[i][static_cast<std::size_t>(k)]);
    f.flag_preserve_sharp = o.preserveSharp ? 1 : 0;
    f.flag_preserve_boundary = o.preserveBoundary ? 1 : 0;
    f.flag_adaptive_scale = o.adaptive ? 1 : 0;
    f.flag_minimum_cost_flow = o.minimumCostFlow ? 1 : 0;
    f.hierarchy.rng_seed = o.seed;

    checkCancel();
    f.NormalizeMesh();
    f.Initialize(static_cast<int>(std::max<std::size_t>(o.targetFaces, 6)));

    if (f.flag_preserve_boundary) {  // as QuadriFlow's own main.cpp does
        qflow::Hierarchy& h = f.hierarchy;
        h.clearConstraints();
        for (std::uint32_t i = 0; i < 3 * h.mF.cols(); ++i) {
            if (h.mE2E[i] != -1) continue;
            const std::uint32_t i0 = static_cast<std::uint32_t>(h.mF(i % 3, i / 3));
            const std::uint32_t i1 = static_cast<std::uint32_t>(h.mF((i + 1) % 3, i / 3));
            const Eigen::Vector3d p0 = h.mV[0].col(i0), p1 = h.mV[0].col(i1);
            Eigen::Vector3d edge = p1 - p0;
            if (edge.squaredNorm() <= 0) continue;
            edge.normalize();
            h.mCO[0].col(i0) = p0;
            h.mCO[0].col(i1) = p1;
            h.mCQ[0].col(i0) = h.mCQ[0].col(i1) = edge;
            h.mCQw[0][i0] = h.mCQw[0][i1] = h.mCOw[0][i0] = h.mCOw[0][i1] = 1.0;
        }
        h.propagateConstraints();
    }

    checkCancel();
    qflow::Optimizer::optimize_orientations(f.hierarchy);
    checkCancel();
    f.ComputeOrientationSingularities();
    if (f.flag_adaptive_scale == 1) f.EstimateSlope();
    qflow::Optimizer::optimize_scale(f.hierarchy, f.rho, f.flag_adaptive_scale);
    checkCancel();
    f.flag_adaptive_scale = 1;
    qflow::Optimizer::optimize_positions(f.hierarchy, f.flag_adaptive_scale);
    checkCancel();
    f.ComputePositionSingularities();
    f.ComputeIndexMap();
    checkCancel();

    TriMesh out;
    out.vertices.reserve(f.O_compact.size());
    for (const auto& p : f.O_compact) out.vertices.push_back((p * f.normalize_scale + f.normalize_offset).cast<float>());
    std::vector<Quad> quads;
    quads.reserve(f.F_compact.size());
    QuadRemeshResult r;
    const auto nv = static_cast<int>(out.vertices.size());
    for (const auto& q : f.F_compact) {
        bool ok = true;
        for (int a = 0; a < 4 && ok; ++a) {
            if (q[a] < 0 || q[a] >= nv) ok = false;
            for (int b = a + 1; b < 4 && ok; ++b) ok = q[a] != q[b];
        }
        if (!ok) { ++r.droppedDegenerate; continue; }
        quads.push_back({static_cast<std::uint32_t>(q[0]), static_cast<std::uint32_t>(q[1]),
                         static_cast<std::uint32_t>(q[2]), static_cast<std::uint32_t>(q[3])});
    }
    if (quads.empty()) throw std::runtime_error("no quads");
    out.setQuads(std::move(quads));
    out.removeUnusedVertices();
    // Keep the original's orientation: QuadriFlow may return the quads inside out.
    if ((mesh.signedVolume() > 0) != (out.signedVolume() > 0) && analyze(mesh).closed) {
        for (Quad& q : out.quads) std::swap(q[1], q[3]);
        out.setQuads(std::move(out.quads));
    }
    r.quads = out.quads.size();
    mesh = std::move(out);
    return r;
}

namespace {

using Key = std::uint64_t;
Key key(std::uint32_t a, std::uint32_t b) { return (static_cast<Key>(a) << 32) | b; }

}  // namespace

std::string tsmProblem(const TriMesh& m) {
    if (!m.hasQuads()) return "not a quad mesh";
    std::unordered_map<Key, int> half;
    half.reserve(m.quads.size() * 4);
    for (const Quad& q : m.quads)
        for (int k = 0; k < 4; ++k) {
            const auto a = q[static_cast<std::size_t>(k)], b = q[static_cast<std::size_t>((k + 1) % 4)];
            if (++half[key(a, b)] > 1) return "non-manifold or inconsistently oriented edges";
        }
    std::vector<int> valence(m.vertices.size(), 0);
    for (const auto& [k, n] : half) {
        const auto a = static_cast<std::uint32_t>(k >> 32), b = static_cast<std::uint32_t>(k & 0xffffffffu);
        if (!half.count(key(b, a))) return "open: the mesh has boundary edges";
        ++valence[a];
    }
    for (std::size_t v = 0; v < valence.size(); ++v)
        if (valence[v] == 2) return "a vertex has only two edges";
    return {};
}

TsmResult writeTsm(const TriMesh& input, const std::filesystem::path& path) {
    if (std::string p = tsmProblem(input); !p.empty()) throw std::runtime_error(p);
    TriMesh mesh = input;  // every vertex must belong to a quad: the file numbers them in order
    mesh.removeUnusedVertices();
    std::vector<Quad> quads = mesh.quads;
    TsmResult r;
    double vol = 0;  // signed volume over the quads' triangulation
    for (const Tri& t : mesh.faces)
        vol += mesh.vertices[t[0]].cast<double>().dot(mesh.vertices[t[1]].cast<double>().cross(mesh.vertices[t[2]].cast<double>()));
    if (vol < 0) {
        for (Quad& q : quads) std::swap(q[1], q[3]);
        r.flipped = true;
    }
    // Half-edge "links", one per quad corner: prev, next, twin, vertex, face, edge.
    struct Link { std::size_t prev, next, twin, vertex, face, edge; };
    std::vector<Link> links(quads.size() * 4);
    std::unordered_map<Key, std::size_t> at;
    at.reserve(links.size());
    for (std::size_t f = 0; f < quads.size(); ++f)
        for (std::size_t k = 0; k < 4; ++k) {
            const std::size_t l = f * 4 + k;
            links[l] = {f * 4 + (k + 3) % 4, f * 4 + (k + 1) % 4, 0, quads[f][k], f, 0};
            at[key(quads[f][k], quads[f][(k + 1) % 4])] = l;
        }
    std::vector<std::size_t> edgeLink;
    for (std::size_t l = 0; l < links.size(); ++l) {
        const auto a = static_cast<std::uint32_t>(links[l].vertex), b = static_cast<std::uint32_t>(links[links[l].next].vertex);
        links[l].twin = at.at(key(b, a));
        if (l < links[l].twin) {
            links[l].edge = links[links[l].twin].edge = edgeLink.size();
            edgeLink.push_back(l);
        }
    }
    std::vector<std::size_t> vlink(mesh.vertices.size(), SIZE_MAX);
    for (std::size_t l = 0; l < links.size(); ++l)
        if (vlink[links[l].vertex] == SIZE_MAX) vlink[links[l].vertex] = l;

    std::ofstream os(path, std::ios::binary);
    if (!os) throw std::runtime_error("cannot write " + path.string());
    os << "#TS0200\n\ndegree 3\ncap-type G1CAPS\nstar-smoothness 0\nunits 1 meters\n"
          "end-conditions SUBD_CREASES\nstar-knot-rule NURCCS\n";
    for (std::size_t f = 0; f < quads.size(); ++f) os << "f " << f * 4 << " 0\n";
    for (std::size_t e : edgeLink) os << "e " << e << " 1\n";
    for (std::size_t v : vlink) os << "v " << v << " NORTH\n";
    for (const Link& l : links)
        os << "l " << l.prev << ' ' << l.next << ' ' << l.twin << ' ' << l.vertex << ' ' << l.face << ' ' << l.edge << " 0\n";
    char buf[96];
    for (const Vec3f& v : mesh.vertices) {
        const Vec3d p = v.cast<double>() / 10.0;  // mm -> cm, as Fusion reads it
        std::snprintf(buf, sizeof buf, "0g %.10g %.10g %.10g 1\n", p.x(), p.y(), p.z());
        os << buf;
    }
    os << "\ntol 1.00000000000000008e-05\ngeom-tol 1.00000000000000008e-05\nver 998\n"
          "behavior-version 14.2.0\ncompat-version 13.3.0\n\n";
    if (!os) throw std::runtime_error("cannot write " + path.string());
    r.faces = quads.size();
    r.edges = edgeLink.size();
    r.vertices = mesh.vertices.size();
    return r;
}

}  // namespace orv

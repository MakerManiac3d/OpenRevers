// SPDX-License-Identifier: GPL-3.0-or-later
// Screened Poisson reconstruction through PoissonRecon's library interface
// (M. Kazhdan, H. Hoppe, "Screened Poisson Surface Reconstruction", ACM TOG
// 2013; https://github.com/mkazhdan/PoissonRecon, MIT licence).
#include "core/points/PoissonBridge.h"

#include "PreProcessor.h"
#include "Reconstructors.h"

#include <exception>
#include <memory>
#include <type_traits>

namespace orv::poisson {
namespace {

using namespace PoissonRecon;

struct Samples : public Reconstructor::InputOrientedSampleStream<float, 3> {
    const Input& in;
    std::size_t next{0};
    explicit Samples(const Input& i) : in(i) {}
    void reset() override { next = 0; }
    bool read(Point<float, 3>& p, Point<float, 3>& n) override {
        if (next >= in.count) return false;
        for (unsigned d = 0; d < 3; ++d) {
            p[d] = in.points[next * 3 + d];
            n[d] = in.normals[next * 3 + d];
        }
        ++next;
        return true;
    }
};

struct Vertices : public Reconstructor::OutputLevelSetVertexStream<float, 3> {
    Output& out;
    explicit Vertices(Output& o) : out(o) {}
    size_t size() const override { return out.density.size(); }
    size_t write(const Point<float, 3>& p, const Point<float, 3>&, const float& density) override {
        for (unsigned d = 0; d < 3; ++d) out.vertices.push_back(p[d]);
        out.density.push_back(density);
        return out.density.size() - 1;
    }
};

struct Faces : public Reconstructor::OutputFaceStream<2> {
    Output& out;
    explicit Faces(Output& o) : out(o) {}
    size_t size() const override { return out.faces.size() / 3; }
    size_t write(const std::vector<node_index_type>& polygon) override {
        // Triangles are asked for; fan-triangulate anything else to be safe.
        for (size_t i = 1; i + 1 < polygon.size(); ++i) {
            out.faces.push_back(static_cast<std::uint32_t>(polygon[0]));
            out.faces.push_back(static_cast<std::uint32_t>(polygon[i]));
            out.faces.push_back(static_cast<std::uint32_t>(polygon[i + 1]));
        }
        return out.faces.size() / 3 - 1;
    }
};

}  // namespace

std::string solve(const Input& in, Output& out) {
    out = Output{};
    if (in.count == 0 || !in.points || !in.normals) return "no points";
    try {
        using Poisson = Reconstructor::Poisson;
        using FEMSigs = IsotropicUIntPack<3, FEMDegreeAndBType<Poisson::DefaultFEMDegree, Poisson::DefaultFEMBoundary>::Signature>;
        Poisson::SolutionParameters<float> sp;
        sp.verbose = false;
        sp.depth = static_cast<unsigned>(in.depth);
        sp.pointWeight = in.pointWeight;
        sp.samplesPerNode = in.samplesPerNode;
        Samples samples(in);
        auto* raw = Poisson::Solver<float, 3, FEMSigs>::Solve(samples, sp);
        if (!raw) return "the solver gave no result";
        std::unique_ptr<std::remove_pointer_t<decltype(raw)>> implicit(raw);
        Reconstructor::LevelSetExtractionParameters ep;
        ep.outputDensity = true;
        ep.forceManifold = true;
        ep.polygonMesh = false;
        ep.verbose = false;
        Vertices v(out);
        Faces f(out);
        implicit->extractLevelSet(v, f, ep);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
    return {};
}

}  // namespace orv::poisson

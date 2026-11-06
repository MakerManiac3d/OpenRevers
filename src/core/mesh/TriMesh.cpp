// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/TriMesh.h"

#include <Eigen/Geometry>

namespace orv {

Box3 TriMesh::bounds() const {
    Box3 b;
    for (const auto& v : vertices) b.add(v);
    return b;
}

Vec3f TriMesh::faceNormal(std::size_t f) const {
    const Tri& t = faces[f];
    const Vec3f n = (vertices[t[1]] - vertices[t[0]]).cross(vertices[t[2]] - vertices[t[0]]);
    const float len = n.norm();
    return len > 0.f ? Vec3f(n / len) : Vec3f::Zero();
}

std::vector<Vec3f> TriMesh::vertexNormals() const {
    std::vector<Vec3f> n(vertices.size(), Vec3f::Zero());
    for (const Tri& t : faces) {
        const Vec3f fn = (vertices[t[1]] - vertices[t[0]]).cross(vertices[t[2]] - vertices[t[0]]);
        for (auto i : t) n[i] += fn;  // area weighted: |fn| = 2 * area
    }
    for (auto& v : n) {
        const float len = v.norm();
        if (len > 0.f) v /= len;
    }
    return n;
}

double TriMesh::area() const {
    double a = 0.0;
    for (const Tri& t : faces) {
        const Vec3d p0 = vertices[t[0]].cast<double>();
        a += 0.5 * (vertices[t[1]].cast<double>() - p0).cross(vertices[t[2]].cast<double>() - p0).norm();
    }
    return a;
}

double TriMesh::signedVolume() const {
    double v = 0.0;
    for (const Tri& t : faces) {
        v += vertices[t[0]].cast<double>().dot(vertices[t[1]].cast<double>().cross(vertices[t[2]].cast<double>()));
    }
    return v / 6.0;
}

std::size_t TriMesh::removeUnusedVertices() {
    std::vector<std::uint32_t> remap(vertices.size(), UINT32_MAX);
    for (const Tri& t : faces)
        for (auto i : t) remap[i] = 0;
    std::uint32_t next = 0;
    std::vector<Vec3f> kept;
    kept.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (remap[i] == UINT32_MAX) continue;
        remap[i] = next++;
        kept.push_back(vertices[i]);
    }
    const std::size_t removed = vertices.size() - kept.size();
    for (Tri& t : faces)
        for (auto& i : t) i = remap[i];
    vertices = std::move(kept);
    return removed;
}

}  // namespace orv

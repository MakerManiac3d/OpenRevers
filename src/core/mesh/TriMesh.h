// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — triangle mesh: the type every mesh operation reads and writes.
#pragma once

#include <Eigen/Core>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace orv {

using Vec3f = Eigen::Vector3f;
using Vec3d = Eigen::Vector3d;
using Tri = std::array<std::uint32_t, 3>;
using Quad = std::array<std::uint32_t, 4>;

/// Axis-aligned box. Empty until a point is added.
struct Box3 {
    Vec3f min{Vec3f::Constant(std::numeric_limits<float>::max())};
    Vec3f max{Vec3f::Constant(std::numeric_limits<float>::lowest())};
    bool empty() const { return min.x() > max.x(); }
    void add(const Vec3f& p) { min = min.cwiseMin(p); max = max.cwiseMax(p); }
    Vec3f size() const { return empty() ? Vec3f::Zero() : Vec3f(max - min); }
    Vec3f center() const { return empty() ? Vec3f::Zero() : Vec3f((min + max) * 0.5f); }
    float diagonal() const { return size().norm(); }
};

/// An indexed triangle mesh in millimetres: shared vertices, faces of three
/// vertex indices, counter-clockwise seen from outside.
///
/// A quad mesh (from quad remeshing, or read from a file of quads) also keeps
/// its `quads`; `faces` then holds their triangulation, two triangles per
/// quad, so that everything that works on triangles works on it too. An
/// operation that changes the triangles drops the quads (see dropQuads).
struct TriMesh {
    std::vector<Vec3f> vertices;
    std::vector<Tri> faces;
    std::vector<Quad> quads;

    bool hasQuads() const { return !quads.empty(); }
    /// Set the quads and rebuild `faces` from them.
    void setQuads(std::vector<Quad> q);
    void dropQuads() { quads.clear(); }

    std::size_t vertexCount() const { return vertices.size(); }
    std::size_t faceCount() const { return faces.size(); }
    bool empty() const { return faces.empty(); }

    Box3 bounds() const;
    /// Unit normal of face f (zero for a degenerate face).
    Vec3f faceNormal(std::size_t f) const;
    /// Area-weighted vertex normals, one per vertex.
    std::vector<Vec3f> vertexNormals() const;
    /// Surface area in mm².
    double area() const;
    /// Signed volume in mm³ (meaningful for a closed, consistently oriented mesh).
    double signedVolume() const;
    /// Remove vertices no face uses, renumbering the faces.
    std::size_t removeUnusedVertices();
};

}  // namespace orv

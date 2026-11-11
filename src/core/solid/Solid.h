// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — an exact solid (B-rep), backed by Open CASCADE. The rest of
// the program sees only this class: no Open CASCADE header leaks out of
// Solid.cpp. A Solid is immutable and cheap to copy (its shape is shared).
#pragma once

#include "core/mesh/TriMesh.h"
#include "core/solid/Profile.h"

#include <Eigen/Geometry>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace orv {

class SolidError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A solid's triangles for drawing and measuring, with the solid face each
/// triangle comes from (0-based, in the order Solid::faceCount counts them).
struct SolidTessellation {
    TriMesh mesh;
    std::vector<std::uint32_t> faceOf;
};

class Solid {
public:
    Solid();
    ~Solid();
    Solid(const Solid&);
    Solid& operator=(const Solid&);

    bool empty() const;

    // ---- making -------------------------------------------------------------
    /// The profile's loops pushed along the plane's normal, from `start` to
    /// `end` (distances along the normal; start < end). Nested loops alternate
    /// material and holes: a loop inside the outline is a hole, a loop inside
    /// that hole is material again.
    static Solid extrude(const Profile& profile, double start, double end);
    /// The profile's loops turned round an axis lying in its plane (through
    /// `axisPoint` along `axisDir`, both in the plane's 2D coordinates), by
    /// `angleDeg` (360: a full turn).
    static Solid revolve(const Profile& profile, const Vec2d& axisPoint, const Vec2d& axisDir, double angleDeg);
    Solid fused(const Solid& other) const;
    Solid cut(const Solid& other) const;
    Solid common(const Solid& other) const;
    Solid transformed(const Eigen::Isometry3d& t) const;
    /// Rounds the edges whose both ends lie within `tolerance` of the given
    /// points (an edge is picked by a point on it), with radius r.
    Solid filleted(const std::vector<Vec3d>& edgePoints, double r, double tolerance = 0.05) const;

    // ---- exchange ----------------------------------------------------------
    static Solid readStep(const std::filesystem::path& path);
    void writeStep(const std::filesystem::path& path, const std::string& name = {}) const;
    /// Open CASCADE's own text format, for the project file.
    std::string toBrep() const;
    static Solid fromBrep(const std::string& text);

    // ---- looking -----------------------------------------------------------
    /// Triangles within `deflection` of the exact surfaces (and no more than
    /// `angleDeg` between neighbours on a curve).
    SolidTessellation tessellate(double deflection, double angleDeg = 15.0) const;
    double volume() const;
    double area() const;
    Eigen::AlignedBox3d bounds() const;
    int faceCount() const;
    int edgeCount() const;
    int solidCount() const;
    /// Empty when the shape passes Open CASCADE's checks, else why not.
    std::string checkProblem() const;

    struct Impl;
    explicit Solid(std::shared_ptr<const Impl> impl);
    const std::shared_ptr<const Impl>& impl() const { return impl_; }

private:
    std::shared_ptr<const Impl> impl_;
};

/// The version of Open CASCADE in use, e.g. "7.9.3".
std::string occtVersion();

}  // namespace orv

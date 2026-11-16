// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — an exact solid (B-rep), backed by Open CASCADE. The rest of
// the program sees only this class: no Open CASCADE header leaks out of
// Solid.cpp. A Solid is immutable and cheap to copy (its shape is shared).
#pragma once

#include "core/mesh/TriMesh.h"
#include "core/solid/BSpline.h"
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
    /// Each edge of the solid as a polyline within the deflection (0-based,
    /// in the order Solid::edgeCount counts them), and the point halfway
    /// along it (to pick it again, as part.fillet does).
    std::vector<std::vector<Vec3f>> edges;
    std::vector<Vec3d> edgeMiddle;
};

/// What a face or an edge of a solid is, for showing it and acting on it.
struct ShapeInfo {
    std::string type;                // face: plane, cylinder, cone, sphere, torus, bspline, other; edge: line, circle, ellipse, bspline, other
    Vec3d origin{Vec3d::Zero()};     // a point: on the plane, of the axis, the centre
    Vec3d axis{Vec3d::Zero()};       // the plane's normal (outward), the axis' direction
    double radius{0.0};              // cylinder, cone (at origin), sphere, torus (major), circle
    double radius2{0.0};             // torus (minor)
    double angleDeg{0.0};            // cone half angle
    double size{0.0};                // face: area (mm²); edge: length (mm)
};

/// What an edge was when it was picked, to find it again after the model
/// changes: its kind ("line", "circle", ...) and direction (a line's direction,
/// a circle's axis). Empty: only the picked point is known.
struct EdgeHint {
    std::string type;
    Vec3d dir{Vec3d::Zero()};
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
    /// A solid through closed sections, in order (each profile must have one
    /// loop); ruled: flat between sections, else smooth through them.
    static Solid loft(const std::vector<Profile>& sections, bool ruled);
    /// A free-form surface thickened into a solid: offset by `thickness`
    /// along its normal (negative: the other way).
    static Solid thicken(const HeightSurface& surface, double thickness);
    /// The solid with what lies beyond a free-form surface removed: above it
    /// (along its normal) when keepBelow, else below it. The surface must
    /// reach past the solid on every side.
    Solid cutBySurface(const HeightSurface& surface, bool keepBelow) const;
    Solid fused(const Solid& other) const;
    Solid cut(const Solid& other) const;
    Solid common(const Solid& other) const;
    Solid transformed(const Eigen::Isometry3d& t) const;
    /// Rounds the edges picked by points on them (within `tolerance`), with
    /// radius r. When no edge lies at a point (the model changed since it was
    /// picked) and a hint is given, the nearest edge of the same kind and
    /// direction within a quarter of the solid's size is taken instead, if
    /// only one is clearly the nearest.
    Solid filleted(const std::vector<Vec3d>& edgePoints, double r, double tolerance = 0.05, const std::vector<EdgeHint>& hints = {}) const;
    /// Bevels the same kind of picked edges, `d` back along both faces.
    Solid chamfered(const std::vector<Vec3d>& edgePoints, double d, double tolerance = 0.05, const std::vector<EdgeHint>& hints = {}) const;
    /// The hint of the edge at `p` (empty when none lies within `tolerance`).
    EdgeHint edgeHintAt(const Vec3d& p, double tolerance = 0.05) const;
    /// The solid reflected in the plane through `origin` with this normal.
    Solid mirrored(const Vec3d& origin, const Vec3d& normal) const;
    /// The tool of a hole, to cut with: from `origin` (on the surface) along
    /// `axis` (into the material), `depth` deep. A counterbore (cbDiameter > 0,
    /// cbDepth deep) or a countersink (csDiameter > 0, csAngleDeg across the
    /// cone) widens the mouth. The tool starts a little outside the surface.
    static Solid drill(const Vec3d& origin, const Vec3d& axis, double diameter, double depth, double cbDiameter = 0, double cbDepth = 0,
                       double csDiameter = 0, double csAngleDeg = 90);
    /// Index of the face nearest to `p` within `tolerance` (as tessellate
    /// numbers faces), or -1.
    int faceNear(const Vec3d& p, double tolerance = 0.05) const;

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
    /// The face or edge with this index (0-based, as tessellate numbers them).
    ShapeInfo faceInfo(int index) const;
    ShapeInfo edgeInfo(int index) const;
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

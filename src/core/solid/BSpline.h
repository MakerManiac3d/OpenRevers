// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — free-form curves and surfaces: clamped B-splines, evaluated
// and fitted to points here, without Open CASCADE, so that the sketches, the
// editor and the tests see exactly the geometry that Solid builds from them.
//
// A curve is a clamped uniform B-spline of degree min(3, poles - 1): it starts
// at its first pole and ends at its last. A surface is a height field over a
// plane: S(u, v) = origin + (x0 + u·w)·xDir + (y0 + v·h)·yDir + H(u, v)·normal,
// with H a bicubic clamped uniform B-spline. Its poles stand over the grid of
// Greville points, where a B-spline of x is exactly linear in u, so the same
// surface is an ordinary B-spline surface with 3-D poles in Open CASCADE.
#pragma once

#include "core/mesh/TriMesh.h"

#include <Eigen/Core>

#include <vector>

namespace orv {

using Vec2d = Eigen::Vector2d;

/// The full knot vector of a clamped uniform B-spline (degree + 1 equal knots
/// at each end), on [0, 1].
std::vector<double> clampedKnots(int poles, int degree);
/// The distinct knots and their multiplicities (Open CASCADE's form).
void knotsAndMultiplicities(const std::vector<double>& full, std::vector<double>& knots, std::vector<int>& mults);
/// The B-spline basis functions of a clamped uniform knot vector at t: the
/// first non-zero one's index, and the degree + 1 values from it.
int basis(const std::vector<double>& knots, int degree, double t, double* values);

struct CurveFit {
    std::vector<Vec2d> poles;
    double maxDeviation{0};  // of the points from the curve
};

/// Degree of a curve with this many poles.
inline int curveDegree(std::size_t poles) { return poles >= 4 ? 3 : static_cast<int>(poles) - 1; }
Vec2d evalCurve(const std::vector<Vec2d>& poles, double t);
std::vector<Vec2d> sampleCurve(const std::vector<Vec2d>& poles, int count);

/// A curve from `from` to `to` near `points` (in order along it): the fewest
/// poles (4 to maxPoles) that keep every point within `tolerance`, or
/// maxPoles if none does.
CurveFit fitCurve(const std::vector<Vec2d>& points, const Vec2d& from, const Vec2d& to, double tolerance, int maxPoles = 40);

/// A height field over a rectangle of a plane.
struct HeightSurface {
    Vec3d origin{Vec3d::Zero()}, xDir{Vec3d::UnitX()}, yDir{Vec3d::UnitY()}, normal{Vec3d::UnitZ()};
    double x0{0}, y0{0}, width{1}, height{1};  // the rectangle, in the plane's coordinates
    int nu{4}, nv{4};                          // poles across and up
    std::vector<double> heights;               // nu × nv, row by row along u (index i + nu·j)

    Vec3d eval(double u, double v) const;
    double heightAt(double u, double v) const;
    /// The 3-D poles (i + nu·j), for Open CASCADE.
    std::vector<Vec3d> poles3() const;
    /// Triangles of the surface, n × n quads, for drawing and comparing.
    TriMesh triangles(int n) const;
    /// Lines along u and v (count of each), for drawing.
    std::vector<std::vector<Vec3f>> isoLines(int count, int samples) const;
};

struct SurfaceFit {
    HeightSurface surface;
    double maxDeviation{0}, rms{0}, median{0};  // of the points from the surface, along its normal
    std::size_t points{0};
};

/// A smooth surface through `points` seen from a plane (origin, normal; x
/// along xDir): the points are projected on it, and the surface covers their
/// rectangle grown by `margin` on every side. `smoothing` weighs bending
/// against closeness (a little keeps it smooth where points are few).
SurfaceFit fitHeightSurface(const std::vector<Vec3d>& points, const Vec3d& origin, const Vec3d& normal, const Vec3d& xDir, int nu, int nv, double margin,
                            double smoothing = 1e-6);

}  // namespace orv

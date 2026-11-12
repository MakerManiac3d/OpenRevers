// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — from a section of a scan to a sketch: each closed polyline
// becomes a loop of lines and arcs within a tolerance (a whole circle when it
// is one), with sharp corners where two lines meet.
#pragma once

#include "core/solid/Profile.h"

#include <vector>

namespace orv {

struct SketchFitOptions {
    double tolerance{0.05};   // largest distance from a point to its segment
    double minSegment{0.3};   // shorter pieces are merged into their neighbours
    double maxRadius{500.0};  // flatter arcs become lines
    /// Free-form runs (three or more pieces joined without a corner, two of
    /// them arcs at least, as the fit breaks an organic outline) become one
    /// spline each.
    bool splines{false};
};

struct LoopFit {
    Loop loop;
    double maxDeviation{0.0};  // of the section's points from the loop
};

/// One closed polyline (the last point joins the first).
LoopFit fitLoop(const std::vector<Vec2d>& points, const SketchFitOptions& o = {});

}  // namespace orv

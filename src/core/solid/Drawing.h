// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a dimensioned 2D drawing of a solid: orthographic views with
// hidden lines, overall sizes and hole diameters, written as DXF, SVG or PDF.
// No Open CASCADE header here: the views are made from the solid's own
// tessellation and edges.
#pragma once

#include "core/solid/Solid.h"

#include <filesystem>
#include <string>
#include <vector>

namespace orv {

struct DrawSegment {
    Eigen::Vector2d a, b;
    bool hidden{false};
    bool centre{false};  // a centre line
};

/// A dimension: a measured length between a and b, drawn offset from them, or a
/// diameter (circle: centre c, radius r) with a leader.
struct DrawDimension {
    bool diameter{false};
    Eigen::Vector2d a, b;      // length: the two points measured
    Eigen::Vector2d offsetDir; // length: where its line sits, away from the part
    double offset{0};          // length: distance of the dimension line from a-b
    Eigen::Vector2d centre;    // diameter
    double value{0};           // the measured length, or the diameter
};

struct DrawView {
    std::string name;                 // front, top, right, ...
    Eigen::Vector2d origin;           // lower left of the view's box on the sheet, in model mm * scale
    Eigen::Vector2d size;             // the view's box, in model mm
    std::vector<DrawSegment> segments;  // in the view's own coordinates (mm), origin at its box's lower left
    std::vector<DrawDimension> dimensions;
};

struct DrawingOptions {
    std::vector<std::string> views{"front", "top", "right"};
    bool hidden{true};
    bool dimensions{true};
    std::string sheet{"A4"};          // A4, A3, A2
    double scale{0.0};                // 0: the largest that fits the sheet (a standard ratio)
    std::string title;
    double deflection{0.02};
};

struct Drawing {
    std::vector<DrawView> views;
    double sheetW{297}, sheetH{210};  // mm
    double scale{1};
    std::string title;
    std::string scaleText;            // "1:2", "2:1"
};

/// Makes the views of `solid`. Throws SolidError when it is empty or a view name is unknown.
Drawing makeDrawing(const Solid& solid, const DrawingOptions& options);

/// Writes by the extension of `path`: .dxf, .svg or .pdf (millimetres on paper).
void writeDrawing(const Drawing& drawing, const std::filesystem::path& path);

}  // namespace orv

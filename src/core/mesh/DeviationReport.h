// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the deviation report: one page (PDF or SVG) with how far a
// scan lies from the model it was built from: summary figures, a histogram
// of the signed distances, the scan coloured by its distance seen from three
// sides, and the worst faces and zones.
#pragma once

#include "core/mesh/Distance.h"
#include "core/mesh/TriMesh.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace orv {

struct ReportFace {
    std::string label;  // "plane 12"
    std::size_t points{0};
    double mean{0}, scatter{0}, p95{0}, worst{0};
};

struct ReportZone {
    std::array<double, 3> centre{}, size{};
    double percent{0}, meanDistance{0};
};

struct DeviationReport {
    std::string title, part, scan;
    bool spanish{false};
    double tolerance{0.2}, texture{0}, range{1};
    DeviationStats stats;
    std::vector<Vec3f> points;     // the scan's points (the ones measured)
    std::vector<double> distance;  // signed distance of each
    std::vector<ReportFace> faces; // worst first
    std::vector<ReportZone> zones; // largest first
};

/// The colour of a signed distance (the one the viewport shows): green within
/// the tolerance, yellow to red outside the part, cyan to blue inside it.
std::array<float, 3> deviationRgb(double d, double tolerance, double range);

/// Writes the report; the format comes from the extension (.pdf or .svg).
void writeDeviationReport(const DeviationReport& r, const std::filesystem::path& path);

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the shapes found on a body: its segmentation into regions and
// the primitives fitted to them or to a chosen spot. Immutable once made;
// the document swaps whole sets (so undo is a pointer swap).
#pragma once

#include "core/fit/Primitive.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orv {

struct Feature {
    std::uint32_t id{0};        // stable within the body: "f<id>"
    std::string name;           // "Plane 1", "Cylinder 3" or the user's
    Primitive shape;
    std::int32_t region{-1};    // its region in faceRegion, or -1 for a shape fitted on a spot
    std::size_t faces{0};
    double area{0.0};

    std::string ref() const { return "f" + std::to_string(id); }
};

struct Features {
    std::vector<std::int32_t> faceRegion;  // one per face of the body's mesh (empty: not segmented)
    std::vector<Feature> list;
    double noise{0.0};                     // scanner noise found or given at segmentation, mm
    std::uint32_t nextId{1};

    const Feature* find(const std::string& ref) const {
        for (const auto& f : list)
            if (f.ref() == ref || f.name == ref) return &f;
        return nullptr;
    }
};

}  // namespace orv

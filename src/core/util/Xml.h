// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a small XML reader into a tree, for file formats whose XML part
// is small (E57). Elements, attributes, text and CDATA; the five predefined
// entities and numeric character references. No DTDs (a <!DOCTYPE> is
// refused), no external entities, no namespaces processing: a prefix stays in
// the name ("nor:normalX"). Throws std::runtime_error on malformed input.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orv::xml {

struct Element {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string text;  // character data directly inside the element (CDATA included)
    std::vector<std::unique_ptr<Element>> children;

    const std::string* attribute(std::string_view n) const {
        for (const auto& [k, v] : attributes)
            if (k == n) return &v;
        return nullptr;
    }
    const Element* child(std::string_view n) const {
        for (const auto& c : children)
            if (c->name == n) return c.get();
        return nullptr;
    }
};

/// Parse a whole document; returns its root element.
std::unique_ptr<Element> parse(std::string_view text);

}  // namespace orv::xml

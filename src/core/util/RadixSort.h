// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — stable LSD radix sort by a 64-bit key, 16 bits per pass.
// Passes in which every element has the same digit are skipped (vertex
// indices rarely use the top bits). On tens of millions of half-edges it is
// several times faster than std::sort.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orv {

template <class T, class Key>
void radixSort(std::vector<T>& v, Key key) {
    if (v.size() < 2) return;
    std::vector<T> tmp(v.size());
    for (int shift = 0; shift < 64; shift += 16) {
        std::vector<std::size_t> count(65537, 0);
        for (const T& x : v) ++count[((key(x) >> shift) & 0xffffu) + 1];
        bool trivial = false;
        for (std::size_t d = 1; d <= 65536; ++d)
            if (count[d] == v.size()) { trivial = true; break; }
        if (trivial) continue;
        for (std::size_t d = 1; d <= 65536; ++d) count[d] += count[d - 1];
        for (const T& x : v) tmp[count[(key(x) >> shift) & 0xffffu]++] = x;
        v.swap(tmp);
    }
}

}  // namespace orv

// SPDX-License-Identifier: MIT
// Original classifier using exhaustively measured Windows acceptance data.
// It neither calls Windows APIs nor imports encoding implementation code.
#include "cp932.h"
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace dokodemo::core {
namespace {
struct Range { std::uint16_t first, last; };
#include "cp932_ranges.inc"
template<std::size_t N>
bool contains(const Range (&ranges)[N], std::uint16_t value) noexcept {
    const auto item = std::lower_bound(std::begin(ranges), std::end(ranges), value,
        [](const Range& range, std::uint16_t word) { return range.last < word; });
    return item != std::end(ranges) && item->first <= value;
}
}
Cp932Count count_cp932(std::string_view bytes) noexcept {
    Cp932Count result;
    for (std::size_t pos = 0; pos < bytes.size();) {
        const auto lead = static_cast<std::uint8_t>(bytes[pos]);
        if (contains(single_ranges, lead)) { ++pos; ++result.code_units; continue; }
        if (pos + 1 < bytes.size()) {
            const auto trail = static_cast<std::uint8_t>(bytes[pos + 1]);
            const auto pair = static_cast<std::uint16_t>((unsigned(lead) << 8) | trail);
            if (contains(pair_ranges, pair)) { pos += 2; ++result.code_units; continue; }
        }
        result.valid = false; result.error_offset = pos; return result;
    }
    return result;
}
} // namespace dokodemo::core

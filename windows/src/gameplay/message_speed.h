#pragma once

#include <array>

namespace dokodemo::gameplay {
struct MessageTiming {
    int glyph, line, scroll, page;
};

// BOOT 150B8 selects a five-halfword row in data segment + 432C.
// BOOT 160B4 takes column 3 for control 04's delay per displayed line.
constexpr MessageTiming MessageSpeed(unsigned setting) {
    constexpr std::array<MessageTiming, 5> rates{{
        {6, 15, 31, 45}, {5, 12, 27, 37}, {4, 10, 24, 30},
        {3, 7, 20, 22}, {2, 5, 16, 15}
    }};
    return rates.at(setting);
}
}

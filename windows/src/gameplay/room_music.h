#pragma once

#include <cstdint>

namespace dokodemo::gameplay {

// 19F94 clears the previous bank before 19E18 calls 19B6C for the
// replacement scene. The final alternate in each source group cannot
// be reached by the same-choice test with previous bank zero.
constexpr int SelectTravelRoomBank(unsigned period, std::uint32_t draw) {
    switch (period) {
    case 0: case 1: return 6 + int(draw % 2);
    case 2: return 9 + int(draw % 5);
    case 3: return 15 + int(draw % 2);
    case 4: case 5: return 18 + int(draw % 6);
    default: return -1;
    }
}

// Fresh scene 0: 5DC8 clears room+82 before 7398 classifies the clock.
// 177C4 also puts the previous bank outside all four groups.
inline int SelectFirstRoomBank(unsigned hour, unsigned minute, std::uint32_t draw) {
    const unsigned minutes = hour * 60 + minute;
    if (minutes < 4*60 || minutes >= 18*60) return SelectTravelRoomBank(5, draw);
    if (minutes < 10*60) return SelectTravelRoomBank(1, draw);
    if (minutes < 15*60 + 30) return SelectTravelRoomBank(2, draw);
    return SelectTravelRoomBank(3, draw);
}

} // namespace dokodemo::gameplay

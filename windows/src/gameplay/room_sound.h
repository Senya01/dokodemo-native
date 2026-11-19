#pragma once

namespace dokodemo::gameplay {

// BOOT 4AC38 receives bank/group/sequence in a1..a3, volume in t0,
// and the switch between the two source gain laws in t1.
struct RoomSound {
    unsigned bank, group, sequence, volume;
    bool scaled;
};

} // namespace dokodemo::gameplay

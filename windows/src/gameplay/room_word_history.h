#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dokodemo::gameplay {

struct RoomNjCandidate {
    std::string reading, written;
    unsigned left_pos=0, right_pos=0;
};

struct RoomNjFirstSlot {
    int grade = 0;
    std::optional<RoomNjCandidate> candidate;
};

// An admitted two-kana reading whose second kana is a terminal suffix.
// Different admitted suffix stems can share a kana. The preceding class
// selects the resulting class of the winning terminal for that spelling.
struct RoomNjSuffix {
    std::map<unsigned,unsigned> resulting_class;
    std::vector<RoomNjFirstSlot> whole_slots, prefix_slots;
};

// GuiSoftkey owns the two banks. 1FD80 opens an edit session on it.
// Its destructor 1FAA4 -> 98990 releases the banks at owner+8C70.
class RoomWordHistory {
public:
    bool Learn(const RoomNjCandidate& selected);
    bool Register(std::string_view reading, std::string_view written);
    std::vector<std::uint8_t> Save() const;
    bool Restore(const std::vector<std::uint8_t>& data);
    std::vector<RoomNjCandidate> Compose(std::string_view query,
                                         const std::vector<RoomNjCandidate>& predictions,
                                         const std::vector<RoomNjCandidate>& exact,
                                         const RoomNjSuffix& suffix = {}) const;
private:
    std::vector<RoomNjCandidate> learned_, registered_;
};
}

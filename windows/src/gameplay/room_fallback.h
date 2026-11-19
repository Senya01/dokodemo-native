#pragma once

#include "ksc_save_graph.h"
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace dokodemo::gameplay {

// NEKO G5995 holds three pairs. The new-game image contains six nils.
// 27EE passes the subject and the statement separately to its caller.
class RoomRumors {
public:
    using Pair = std::pair<std::optional<std::string>, std::optional<std::string>>;
    using Slots = std::array<Pair, 3>;

    RoomRumors() = default;
    explicit RoomRumors(Slots slots) : slots_(std::move(slots)) {}

    KscSaveValue SaveTable() const;
    void RestoreSaveTable(const KscSaveValue& ref);

    // 2761 stops at the first vacant subject or exact pair it encounters.
    // Only a full table without a match requests a random replacement.
    bool Remember(const Pair& pair) {
        for (auto& slot : slots_) {
            if (!slot.first) { slot=pair; return true; }
            if (slot==pair) return true;
        }
        return false;
    }
    void Replace(unsigned choice, Pair pair) { slots_.at(choice)=std::move(pair); }

    unsigned candidates() const {
        return static_cast<unsigned>(std::count_if(slots_.begin(), slots_.end(), Complete));
    }

    // 2859 draws only when a complete pair exists. The supplied index is
    // relative to the compact array of eligible slot numbers built at 2823.
    Pair Take(unsigned choice) {
        const unsigned count = candidates();
        if (count == 0 && choice == 0) return {};  // 2842..284B
        if (choice >= count) throw std::out_of_range("Invalid NEKO rumor selection");
        for (auto& slot : slots_) {
            if (Complete(slot) && choice-- == 0)
                return std::exchange(slot, Pair{}); // 285F..2890
        }
        throw std::logic_error("NEKO rumor selection lost its slot");
    }

private:
    static bool Complete(const Pair& slot) {
        return slot.first.has_value() && slot.second.has_value();
    }
    Slots slots_{};
};
}

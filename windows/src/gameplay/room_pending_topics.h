#pragma once
#include "recovered/rng_native.h"
#include <array>
#include <optional>
#include <utility>

namespace dokodemo::gameplay {

// NEKO.KSC CT2879 holds three optional procedure references for later conversations.
class RoomPendingTopics {
public:
    void Enqueue(std::optional<unsigned> current_topic) {
        // 249F writes CT2878 to the first nil slot. Only a full list shifts.
        for (auto& slot : slots_) {
            if (!slot) {
                slot = current_topic;
                return;
            }
        }
        slots_[0] = slots_[1];
        slots_[1] = slots_[2];
        slots_[2] = current_topic;
    }

    std::optional<unsigned> Pop(recovered::RngNativeOwner& rng) {
        // 24F4 draws from the indices of non-nil slots, in source order.
        std::array<unsigned, 3> occupied{};
        unsigned count = 0;
        for (unsigned i = 0; i < slots_.size(); ++i)
            if (slots_[i]) occupied[count++] = i;
        if (count == 0) return std::nullopt;
        if (count < slots_.size() && rng.NextC3234() % 100 >= 30) return std::nullopt;
        const unsigned index = occupied[rng.NextC3234() % count];
        // 2573 clears just this slot before the indirect call at 25A5.
        return std::exchange(slots_[index], std::nullopt);
    }

private:
    std::array<std::optional<unsigned>, 3> slots_{};
};

} // namespace dokodemo::gameplay

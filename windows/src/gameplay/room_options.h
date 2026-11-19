#pragma once

#include "profile_fields.h"
#include <array>
#include <cstdint>
#include <vector>

namespace dokodemo::gameplay {

class RoomOptions {
public:
    void RestoreValues(const std::array<int,3>& values);
    void Open(std::uint32_t buttons);
    void Step(std::uint32_t buttons, bool fade_open);
    int cursor() const { return cursor_; }
    const std::array<int,3>& values() const { return values_; }
    bool interactive() const { return phase_ == Phase::edit; }
    bool closing() const { return phase_ == Phase::closing; }
    const ProfileConfirmation& prompt() const { return prompt_; }
    unsigned frame() const { return frame_; }
    const std::array<unsigned,2>& help_lines() const { return help_lines_; }
    float help_progress() const { return cursor_ == 0 ? help_progress_ : 0.f; }
    std::vector<unsigned> TakeEffects();

private:
    enum class Phase { opening, edit, confirmation, closing };
    Phase phase_ = Phase::closing;
    int cursor_ = 0;
    unsigned frame_ = 0;
    std::uint32_t previous_ = 0;
    std::array<unsigned,4> held_{};
    std::array<int,3> values_{2,3,3}, backup_{};
    ProfileConfirmation prompt_;
    std::vector<unsigned> effects_;
    std::array<unsigned,2> help_lines_{};
    unsigned help_line_ = 0, help_delay_ = 0, help_rate_ = 2;
    float help_progress_ = 0;
    void AdvanceHelp();
};

}

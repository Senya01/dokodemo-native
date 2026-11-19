#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace dokodemo::gameplay {

// TITLE 32068's two-choice TRC. Selection stays provisional until the
// prompt has disappeared, as in the 31D44 idle-state test.
class ProfileConfirmation {
public:
    void Open(std::uint32_t buttons);
    void Step(std::uint32_t buttons);
    static constexpr std::string_view question() {
        return "\x82\xb1\x82\xea\x82\xc5\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48";
    }
    bool visible() const { return phase_!=Phase::idle && phase_!=Phase::prepare; }
    bool active() const { return phase_==Phase::active; }
    bool closing() const { return phase_==Phase::closing; }
    float progress() const { return progress_; }
    unsigned cursor() const { return cursor_; }
    unsigned se_mask() const { return se_mask_; }
    const std::optional<int>& result() const { return result_; }
private:
    enum class Phase { idle, prepare, opening, active, closing };
    Phase phase_=Phase::idle;
    std::uint32_t previous_=0;
    std::array<unsigned,2> held_{};
    unsigned cursor_=0, se_mask_=0;
    float progress_=0;
    std::optional<int> result_;
};
}

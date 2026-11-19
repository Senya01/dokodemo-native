#include "room_options.h"
#include "message_speed.h"
#include <algorithm>
#include <utility>
#include <stdexcept>

namespace dokodemo::gameplay {

void RoomOptions::RestoreValues(const std::array<int,3>& values) {
    for (int value : values)
        if (value<0 || value>4) throw std::invalid_argument("Invalid saved room option");
    values_=values;
}

void RoomOptions::Open(std::uint32_t buttons) {
    // 34F1C, 353A8: each open starts on row 0 and snapshots the current settings.
    phase_ = Phase::opening;
    cursor_ = 0;
    frame_ = 0;
    previous_ = buttons;
    held_.fill(0);
    backup_ = values_;
    prompt_ = {};
    help_lines_ = {};
    help_line_ = help_delay_ = 0;
    help_rate_ = values_[0];
    help_progress_ = 0;
    effects_ = {12};
}

std::vector<unsigned> RoomOptions::TakeEffects() {
    return std::exchange(effects_, {});
}

void RoomOptions::AdvanceHelp() {
    // 34D00..34D5C only service the repeating line DA4EC while row 0 owns it.
    if (cursor_ != 0) return;
    if (help_progress_ < 1.f) {
        help_progress_ = std::min(1.f, help_progress_ + .1f);
        if (help_progress_ == 1.f) help_rate_ = values_[0];
        return;
    }
    if (help_delay_ && --help_delay_) return;
    // PH1:432C, five signed halfwords per speed. This sample has nine
    // ordinary CP932 glyphs and one LF. Each new copy takes the live rate.
    if (help_lines_[help_line_] == 9) {
        if (help_line_ == 1) help_lines_[0] = help_lines_[1];
        help_line_ = 1;
        help_lines_[1] = 0;
        help_delay_ = MessageSpeed(help_rate_).line;
        return;
    }
    if (!help_lines_[help_line_]) help_rate_ = values_[0];
    ++help_lines_[help_line_];
    help_delay_ = MessageSpeed(help_rate_).glyph;
}

void RoomOptions::Step(std::uint32_t buttons, bool fade_open) {
    const auto edge = buttons & ~previous_;
    previous_ = buttons;
    ++frame_;
    // 94F90(held,10,2): repeat at counts 10,12,... after the initial edge.
    static constexpr unsigned keys[]{0x40,0x10,0x20,0x80};
    std::array<bool,4> move{};
    for (unsigned i=0; i<held_.size(); ++i) {
        if (buttons & keys[i]) {
            move[i] = (edge & keys[i]) || (held_[i] >= 10 && !(held_[i] & 1));
            held_[i] = held_[i] < 12 ? held_[i] + 1 : 11;
        } else held_[i] = 0;
    }
    if (phase_ == Phase::closing) return;
    if (phase_ == Phase::opening) {
        if (fade_open) phase_ = Phase::edit;
        return;
    }
    if (phase_ == Phase::confirmation) {
        prompt_.Step(buttons);
        const auto mask = prompt_.se_mask();
        for (unsigned cue : {4u,6u,10u,11u})
            if (mask & (1u << cue)) effects_.push_back(cue);
        if (prompt_.result()) {
            // 35338: No keeps all preview values and restores editor focus.
            if (*prompt_.result() == 1) {
                phase_ = Phase::closing;
                effects_.push_back(12);
            } else phase_ = Phase::edit;
        }
    } else if (move[0] || move[1]) {
        const bool down = move[0];
        const int next = cursor_ + (down ? 1 : -1);
        if ((next >= 0 && next < 3) || (edge & keys[down ? 0 : 1])) {
            cursor_ = (next + 3) % 3;
            effects_.push_back(6);
        }
    } else if (move[2] || move[3]) {
        // 3516C/351EC: the three real sliders stop at their endpoints.
        int& value = values_[cursor_];
        const int next = value + (move[2] ? 1 : -1);
        if (next >= 0 && next < 5) {
            value = next;
            effects_.push_back(6);
        }
    } else if (edge & 0x2000) {
        phase_ = Phase::confirmation;
        prompt_.Open(buttons);
        effects_.push_back(4);
    } else if (edge & 0x4000) {
        // 3526C: Cross restores all three entry values before closing.
        values_ = backup_;
        phase_ = Phase::closing;
        effects_.push_back(5);
        effects_.push_back(12);
    }
    AdvanceHelp();
}

}

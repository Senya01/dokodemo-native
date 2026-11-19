#include "profile_fields.h"
#include <algorithm>

namespace dokodemo::gameplay {
void ProfileConfirmation::Open(std::uint32_t buttons) {
    phase_=Phase::prepare; // 48200 -> TRC.state864=-1
    previous_=buttons;
    cursor_=se_mask_=0;
    progress_=0;
    held_.fill(0);
    result_.reset();
}

void ProfileConfirmation::Step(std::uint32_t buttons) {
    const auto pressed=buttons&~previous_;
    previous_=buttons;
    se_mask_=0;
    // 47D10/47DC0 use the same horizontal counters as the character carousel.
    constexpr std::uint32_t keys[]{0x20,0x80};
    std::array<unsigned,2> events{};
    for (unsigned i=0;i<held_.size();++i) {
        if (!(buttons&keys[i])) { held_[i]=0; continue; }
        const auto n=held_[i];
        events[i]=(pressed&keys[i])?1u:(n>=10 && !(n&1)?2u:0u);
        held_[i]=n<12?n+1:11;
    }
    switch (phase_) {
    case Phase::idle: break;
    case Phase::prepare:
        se_mask_=1u<<10; // 47CA4, prompt open
        phase_=Phase::opening;
        break;
    case Phase::opening:
        // 47CC4, native game's single logic quantum per update
        progress_=std::min(1.f,progress_+.1f);
        if (progress_==1.f) phase_=Phase::active;
        break;
    case Phase::active:
        if (events[0]) {
            if (cursor_<1) ++cursor_; else if(events[0]==1) cursor_=0;
            se_mask_=1u<<6;
        } else if (events[1]) {
            if (cursor_) --cursor_; else if(events[1]==1) cursor_=1;
            se_mask_=1u<<6;
        } else if (pressed&0x2000) {
            se_mask_=(1u<<4)|(1u<<11); // 47E50 then 48264
            phase_=Phase::closing;
        }
        break;
    case Phase::closing:
        progress_=std::max(0.f,progress_-.1f);
        if (progress_==0.f) { // 47EA4..47EFC hides root and retires controls
            result_=static_cast<int>(cursor_+1);
            phase_=Phase::idle;
        }
        break;
    }
}
}

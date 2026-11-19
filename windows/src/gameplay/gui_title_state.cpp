#include "gui_title_state.h"
#include "recovered/rng_native.h"
#include <algorithm>
#include <cmath>

namespace dokodemo::gameplay {
namespace {
constexpr std::array<unsigned,5> order{0,4,3,2,1};
constexpr std::array<float,5> carousel{0,2000,4000,-4000,-2000};
constexpr std::uint32_t right = 0x20, left = 0x80, circle = 0x2000, cross = 0x4000;
// Motion table at E9EA4. The run, dance and non-looping selection DAKs
// of all five packaged characters have these durations in source units.
constexpr unsigned run_end = 3840, dance_end = 32000, select_end = 8640;
const float max_radius = std::hypot(240.5f,136.5f);
const float radial_step = max_radius / 30.f;  // D9BC/D9CC

unsigned repeat(bool down, unsigned& ticks, unsigned period) {
    if (!down) { ticks=0; return 0; }
    // 94F90(held,10,period): distinguish the first press from held repeats.
    unsigned event = 0;
    if (ticks == 0) event = 1;
    else if (ticks >= 10 && (ticks-10)%period == 0) event = 2;
    if (ticks == 0x7fffffffu) ticks = 10 + (ticks-10)%period;
    ++ticks;
    return event;
}
}

void GuiTitleState::BeginSwitch(unsigned to, int bank) {
    next_ = to;
    pending_bank_ = bank;
    fade_requested_ = bank >= 0 && panel_ != 5;
    phase_ = Phase::switch_panel;
    if (wipe_ != Wipe::closed) wipe_ = Wipe::closing;
}

void GuiTitleState::InstallPanel() {
    panel_ = next_;
    frame_ = 1; // Single-frame control: 97BB4 binds at 0, first evaluator tick draws 1.
    panel_ticks_ = 0;
    phase_ = Phase::opening;
    wipe_ = Wipe::opening;
    selecting_ = false;
    editor_started_ = false;
    guide_ = -1;
    guide_frame_ = 0;
    if (panel_ == 0) { // 30E70 clears the previous character and profile on return.
        selected_ = 0;
        nickname_present_ = player_present_ = phone_present_ = false;
        gender_value_ = year_ = month_ = day_ = zodiac_ = blood_ = -1;
        list_phase_ = ListPhase::idle;
    }
    if (panel_ == 4) {
        profile_mode_ = ProfileMode::guided;
        EndProfileField(); // 32B40 calls 32184 on the new layout.
        se_mask_ |= 1u<<6;
    }
    countdown_ = 4;
    segments_.fill(0);
    motion_counters_.fill(0);
    for (unsigned i=0; i<actors_.size(); ++i) {
        auto& a = actors_[i];
        a = {};
        a.type = std::min(i,5u);
        if (panel_ == 4) {
            a.x = target_[i] = 0;
        } else if (panel_ == 0) {
            // The package contains Kuro's RUN_L as its only clip (slot 0).
            a.motion = i<5 ? 1 : 0;
            // 336D0 takes bc1fl when the target is nonzero. Its delay
            // slot writes f20 to the initial position. 337A0 then bypasses
            // the slot's ordinary X. 33B90 positions the first visible lap.
            a.x = i<5 ? -4000.f : 2500.f+800.f*order[i%5];
            a.y = 2000.f+500.f*order[i%5];
            a.z = -400.f;
            target_[i] = i<5 ? -4000.f : a.x;
        } else {
            a.visible = i == selected_;
            a.motion = 2;
            a.x = panel_ == 3 ? -600.f : carousel[(5+i-selected_)%5];
            target_[i] = a.x;
        }
    }
}

void GuiTitleState::UpdateActors() {
    constexpr std::array<float,6> speed{25,58,0,33,25,25};
    constexpr std::array<unsigned,3> jun_segment_length{3,19,24};
    for (unsigned i=0; i<actors_.size(); ++i) {
        auto& a = actors_[i];
        if (panel_ == 0 && a.x != target_[i]) {
            float step = speed[a.type];
            if (a.type == 2) {
                auto& segment = segments_[i];
                step = segment==1 ? 52.f : 0.f;
                const unsigned counter = motion_counters_[i]; // 08AF4 uses the prior 3B01C receipt.
                if (counter==1) segment=0;
                else if (counter>1 && counter%jun_segment_length[segment]==1) segment=(segment+1)%3;
            }
            a.x -= std::min(std::max(a.x-target_[i],0.f),step);
        } else if (panel_ != 0) {
            a.x += std::clamp(target_[i]-a.x,-200.f,200.f);
        }
        motion_counters_[i] = a.time / 160;
        if (!a.visible) continue; // The hidden clone's DAK playback time remains at its last pose.
        const auto end = a.motion==3 ? select_end : a.motion==2 ? dance_end : run_end;
        // 89340 consumes the 1280-unit crossfade before the new
        // nonlooping selection clip advances from its first pose.
        if (i == selected_ && selecting_ && blend_remaining_) {
            blend_remaining_ -= 160;
            continue;
        }
        a.time += 160;
        if (a.time >= end) a.time = a.motion==3 ? end : 160;
    }
}

void GuiTitleState::UpdateEntranceVisibility() {
    if (media_phase_ != MediaPhase::ready) return;
    // 33B90 / 33D8C. Reaching an endpoint restarts the primary or its paired
    // Kuro. The 64-way draw consumes the boot-owned RNG only after countdown.
    for (unsigned i=0; i<5; ++i) {
        auto& kuro = actors_[5+i];
        auto& primary = actors_[i];
        if (kuro.visible) {
            if (kuro.x == -4000.f) kuro.visible = false;
            continue;
        }
        primary.visible = true;
        if (primary.x != -4000.f) continue;
        if (i==0 && countdown_) --countdown_;
        const bool choose_kuro = !countdown_ && rng_ && rng_->NextC3234()%64 == 0;
        auto index = choose_kuro ? i+5 : i;
        primary.visible = !choose_kuro;
        actors_[index].visible = true;
        actors_[index].x = 2500.f + 800.f*order[i];
        target_[index] = -4000.f;
        segments_[index] = 0;
        motion_counters_[index] = 0;
    }
}

void GuiTitleState::SubmitSlotAbsent(bool absent) {
    if (media_request_ != GuiTitleMediaRequest::slot) return;
    media_request_ = GuiTitleMediaRequest::none;
    media_phase_ = absent ? MediaPhase::check_space : MediaPhase::blocked;
}

void GuiTitleState::SubmitLoadedSlot() {
    if (media_request_ != GuiTitleMediaRequest::slot) return;
    media_request_ = GuiTitleMediaRequest::none;
    media_phase_ = MediaPhase::finish;
    menu_variant_ = 2; // 31638, saved room selected
}

void GuiTitleState::EnterSavedRoom() {
    phase_ = Phase::finished; // 1916C requests resource 15 directly
}

void GuiTitleState::ReturnToSelectMenu() {
    auto* rng = rng_;
    *this = {};
    rng_ = rng;
    next_ = 0;
    pending_bank_ = 0;
    InstallPanel();
}

void GuiTitleState::SubmitFreeBytes(std::uintmax_t available) {
    if (media_request_ != GuiTitleMediaRequest::free_space) return;
    media_request_ = GuiTitleMediaRequest::none;
    media_phase_ = available>=800u*1024u ? MediaPhase::finish : MediaPhase::blocked;
}

void GuiTitleState::AdvanceColdMedia() {
    // The startup mount is present. GuiTitle issues its own savedata query
    // when 31274 first services the menu. This is separate from boot's probe.
    // 33B90 suppresses entrance arrivals until +124==7 and +130==0.
    switch (media_phase_) {
    case MediaPhase::inserted: // 31384, pending insertion selects mode 0.
        media_phase_ = MediaPhase::begin;
        break;
    case MediaPhase::begin: // 34018 -> 8
        media_phase_ = MediaPhase::wait_hidden;
        break;
    case MediaPhase::wait_hidden: // 3428C -> 1 after all ten clones are hidden.
        if (std::none_of(actors_.begin(),actors_.end(),[](const GuiTitleActor& a){return a.visible;}))
            media_phase_ = MediaPhase::read_slot;
        break;
    case MediaPhase::read_slot: // 34084 starts the request, mode 2 waits on its completion.
        media_request_ = GuiTitleMediaRequest::slot;
        media_phase_ = MediaPhase::wait_slot;
        break;
    case MediaPhase::check_space: // Source mode 4, 34130..34194.
        media_request_ = GuiTitleMediaRequest::free_space;
        break;
    case MediaPhase::finish: // 341A8 -> 7 (no-data profile).
        media_phase_ = MediaPhase::ready;
        break;
    default:
        break;
    }
}

void GuiTitleState::MoveCarousel(int direction) {
    arrow_frames_[direction>0 ? 1 : 0] = 2; // 31A3C / 31A6C controls 8 and 7.
    selected_ = (selected_ + (direction>0 ? 1 : 4)) % 5;
    for (unsigned i=0; i<5; ++i) {
        auto k = (5+i-selected_)%5;
        actors_[i].visible = k==0 || k==1 || k==4;
        target_[i] = carousel[k];
        if (!actors_[i].visible) actors_[i].x=target_[i];
    }
}

void GuiTitleState::FinishNickname(bool confirmed_nonempty) {
    if (panel_ == 3 && editor_started_ && phase_ == Phase::active) {
        if (confirmed_nonempty) nickname_present_ = true;
        BeginSwitch(confirmed_nonempty ? 4 : 2, -1);
    } else if (EditingRow(0)) {
        if (confirmed_nonempty) nickname_present_ = true;
        EndProfileField();
    }
}

unsigned GuiTitleState::profile_present_mask() const {
    return unsigned(nickname_present_) | (unsigned(player_present_)<<1) |
           (unsigned(gender_value_!=-1)<<2) | (unsigned(year_!=-1)<<3) |
           (unsigned(zodiac_!=-1)<<4) | (unsigned(blood_!=-1)<<5) |
           (unsigned(phone_present_)<<6);
}

bool GuiTitleState::EditingRow(unsigned row) const {
    return panel_==4 && phase_==Phase::active && editor_started_ && profile_row_==row;
}

void GuiTitleState::SelectProfileRow(unsigned row) {
    if (profile_row_ != row) se_mask_ |= 1u<<6;
    profile_row_ = row;
}

void GuiTitleState::EndProfileField() {
    editor_started_ = false;
    editor_request_ = false;
    guide_ = -1;
    guide_frame_ = 0;
    // 32184 counts the seven populated source fields. A completed form
    // retains the current row while 32068 opens the Yes/No dialog.
    unsigned present = 0;
    for (auto mask=profile_present_mask();mask;mask>>=1) present += mask&1u;
    if (present==7) profile_mode_ = ProfileMode::request_confirmation;
    else {
        profile_mode_ = ProfileMode::guided;
        SelectProfileRow(present);
    }
}

void GuiTitleState::FinishPlayerName(bool confirmed_nonempty) {
    if (!EditingRow(1)) return;
    // 323D0 restores the previous value on cancel; it is empty on a new profile.
    // 32184 counts populated fields and selects the first incomplete row.
    if (confirmed_nonempty) player_present_ = true;
    EndProfileField();
}

void GuiTitleState::FinishDate(int year, int month, int day, int zodiac) {
    if (!EditingRow(3)) return;
    // The date owner returns all four accepted fields together, including
    // 145E0's sign. Its cancelled year restores its saved fields.
    if (year>0 && month>=1 && month<=12 && day>=1 && day<=31 && zodiac>=0 && zodiac<12) {
        year_=year; month_=month; day_=day; zodiac_=zodiac;
    }
    EndProfileField();
}

void GuiTitleState::FinishPhone(bool confirmed_nonempty) {
    if (!EditingRow(6)) return;
    if (confirmed_nonempty) phone_present_ = true; // 32AE0 restores old text on null.
    EndProfileField();
}

void GuiTitleState::FinishConfirmation(bool yes) {
    if (profile_mode_ == ProfileMode::confirming)
        profile_mode_ = yes ? ProfileMode::adopting : ProfileMode::review;
}

void GuiTitleState::FinishAdoption() {
    if (profile_mode_ != ProfileMode::adopting) return;
    // 32020 sets D0=-1. 311B4 closes this root. 30D7C then publishes CC3.
    profile_mode_ = ProfileMode::leaving;
    fade_requested_ = true;
    phase_ = Phase::switch_panel;
    wipe_ = Wipe::closing;
    guide_ = -1;
    guide_frame_ = 0;
}

void GuiTitleState::UpdateGuide(bool editor_editing, bool list_editing, int date_guide) {
    // 30ED4 runs after the panel handler. 3338C only accepts the first guide
    // in the current panel. Its one-tick activation counter advances in CC1.
    if (EditingRow(3) && date_guide != guide_) {
        guide_ = -1;
        guide_frame_ = 0;
    }
    if (guide_ == -1) {
        if (panel_ == 2) guide_ = 0;
        else if (panel_ == 3 && editor_editing) guide_ = 1;
        else if (EditingRow(0) && editor_editing) guide_ = 8;
        else if (EditingRow(1) && editor_editing) guide_ = 2;
        else if (EditingRow(2) && (list_editing || list_phase_ == ListPhase::active)) guide_ = 9;
        else if (EditingRow(3) && date_guide>=3 && date_guide<=5) guide_ = date_guide;
        else if (EditingRow(5) && list_phase_ == ListPhase::active) guide_ = 6;
        else if (EditingRow(6) && editor_editing) guide_ = 7;
    }
    if (guide_ >= 0 && (guide_frame_ || phase_ == Phase::active))
        guide_frame_ = std::min(guide_frame_ + 1, 5u);
}

void GuiTitleState::AdvanceProfileList(std::uint32_t pressed, unsigned down, unsigned up) {
    // SELECT.CB2A mode 1 binds root 1 to clip 3 on open, 4 on close.
    // Both reach their end at 10. The selector consumes buttons in phase 1.
    constexpr unsigned end_frame = 10;
    if (list_phase_ == ListPhase::opening || list_phase_ == ListPhase::closing) {
        if (++list_frame_ < end_frame) return;
        if (list_phase_ == ListPhase::opening) {
            list_phase_ = ListPhase::active;
        } else {
            list_phase_ = ListPhase::idle;
            // 32420 and 32990 consume the completed selector's one-based
            // result. A cancelled re-edit restores the previous value.
            if (!list_cancelled_) {
                if (profile_row_==2) gender_value_ = list_cursor_==0 ? 2 : 1;
                else blood_ = int(list_cursor_);
            }
            EndProfileField();
        }
        return;
    }
    if (list_phase_ != ListPhase::active) return;
    // 10BCC wraps only on a new press at an endpoint. A repeated
    // direction stays at the endpoint. Down has priority over Up.
    const unsigned old_cursor = list_cursor_, last = profile_row_==2 ? 1u : 3u;
    if (down) {
        if (list_cursor_<last) ++list_cursor_;
        else if (down==1) list_cursor_=0;
    } else if (up) {
        if (list_cursor_) --list_cursor_;
        else if (up==1) list_cursor_=last;
    }
    if (list_cursor_ != old_cursor) se_mask_ |= 1u<<6;
    if (pressed & circle) {
        se_mask_ |= (1u<<4) | (1u<<11); // Accept, then the SELECT close at 1032C.
        list_phase_ = ListPhase::closing;
        list_frame_ = 0;
    } else if (list_cancel_enabled_ && (pressed & cross)) {
        se_mask_ |= (1u<<5) | (1u<<11);
        list_cancelled_ = true;
        list_phase_ = ListPhase::closing;
        list_frame_ = 0;
    }
}

void GuiTitleState::OpenProfileField() {
    editor_request_ = editor_started_ = true;
    guide_ = -1;
    guide_frame_ = 0;
    if (profile_row_==2 || profile_row_==5) {
        se_mask_ |= 1u<<10; // FFFC binds the opening clip and plays SE_SYS 10.
        list_phase_ = ListPhase::opening;
        list_frame_ = 0;
        list_cancel_enabled_ = profile_mode_ == ProfileMode::review;
        list_cancelled_ = false;
        list_cursor_ = profile_row_==2 ? unsigned(gender_value_==1) : unsigned(std::max(0,blood_));
    }
}

void GuiTitleState::AdvanceProfile(std::uint32_t pressed, unsigned down, unsigned up) {
    if (list_phase_ != ListPhase::idle) {
        AdvanceProfileList(pressed,down,up);
        return;
    }
    if (editor_started_) return;
    if (profile_mode_==ProfileMode::request_confirmation) {
        confirmation_request_ = true;
        profile_mode_ = ProfileMode::confirming;
    } else if (profile_mode_==ProfileMode::guided) {
        OpenProfileField();
    } else if (profile_mode_==ProfileMode::review) {
        // 31E04..31EB8: the derived zodiac row is skipped in both
        // directions. Fresh navigation wraps; held repeats stop at the edge.
        if (down) {
            if (profile_row_<6) SelectProfileRow(profile_row_==3 ? 5 : profile_row_+1);
            else if (down==1) SelectProfileRow(0);
        } else if (up) {
            if (profile_row_>0) SelectProfileRow(profile_row_==5 ? 3 : profile_row_-1);
            else if (up==1) SelectProfileRow(6);
        } else if (pressed & circle) {
            se_mask_ |= 1u<<4;
            OpenProfileField();
        } else if (pressed & cross) {
            se_mask_ |= 1u<<5;
            BeginSwitch(3,-1);
        }
    }
}

void GuiTitleState::Advance(std::uint32_t buttons, bool selection_voice_playing) {
    se_mask_ = 0;
    editor_request_ = false;
    confirmation_request_ = false;
    fade_requested_ = false;
    submitted_bank_ = -1;
    if (phase_ == Phase::finished) return; // CC3 no longer updates the GuiTitle actors.
    voice_duck_ = selecting_ && selection_voice_playing;
    ++panel_ticks_;
    for (auto& frame : arrow_frames_) if (frame>1 && frame<7) ++frame;
    // The displayed surface trails the fade update by two submissions:
    // D754 draws the updated owner, AFF74 publishes the prior draw surface.
    display_radius_ = draw_radius_;
    draw_radius_ = radius_;
    display_wipe_ = draw_wipe_;
    draw_wipe_ = wipe_ != Wipe::open;
    const auto pressed = buttons & ~previous_buttons_;
    previous_buttons_ = buttons;
    const bool next = repeat((buttons & right)!=0,right_ticks_,3)!=0;
    const bool prior = repeat((buttons & left)!=0,left_ticks_,3)!=0;
    const auto down = repeat((buttons & 0x40)!=0,down_ticks_,2);
    const auto up = repeat((buttons & 0x10)!=0,up_ticks_,2);
    submitted_bank_ = queued_bank_;
    queued_bank_ = -1;

    if (panel_ == 5 && phase_ == Phase::active) {
        ++frame_;
        if (frame_ == 319) BeginSwitch(0,0);  // Clip length 320 includes frame 0.
        return;
    }
    if (wipe_ == Wipe::opening) {
        radius_ = std::min(radius_ + radial_step,max_radius);
        if (radius_ == max_radius) wipe_ = Wipe::open;
    } else if (wipe_ == Wipe::closing) {
        radius_ = std::max(radius_ - radial_step,-50.f);
        if (radius_ == -50.f) wipe_ = Wipe::closed;
    }
    if (panel_ != 5) UpdateActors();
    if (phase_ == Phase::switch_panel && wipe_ == Wipe::closed) {
        if (profile_mode_ == ProfileMode::leaving) phase_ = Phase::finished;
        else InstallPanel(); // 30D44 resets root + control; 30E48 requests expansion.
    } else if (phase_ == Phase::opening && wipe_ == Wipe::open) {
        // At the first open submission phase 1's callback can service the
        // pending request. The SndWave manager submits it on its next tick.
        phase_ = Phase::active;
        queued_bank_ = pending_bank_;
        pending_bank_ = -1;
    } else if (phase_ == Phase::active) {
        queued_bank_ = pending_bank_;
        pending_bank_ = -1;
        if (panel_ == 0) {
            const bool menu_input_enabled = media_phase_ == MediaPhase::ready;
            AdvanceColdMedia();
            UpdateEntranceVisibility(); // 30ED4 runs after 31274.
            if (menu_input_enabled && menu_variant_ != 1) {
                if ((down && menu_variant_ == 2) || (up && menu_variant_ == 3) ||
                    down == 1 || up == 1) {
                    menu_variant_ = 5 - menu_variant_; // 312DC/31698, fresh press wraps
                    se_mask_ = 1u << 6;
                    return;
                }
                if (pressed & circle) {
                    if (menu_variant_ == 3) {
                        overwrite_requested_ = true; // 314E4, separate confirmation
                        return;
                    }
                    se_mask_ = 1u << 4;
                    profile_mode_ = ProfileMode::leaving;
                    fade_requested_ = true;
                    phase_ = Phase::switch_panel;
                    wipe_ = Wipe::closing;
                }
                return;
            }
            // No saved profile: 315E4 enforces F4=1 even on vertical navigation.
            if (menu_input_enabled && (pressed & circle)) {
                se_mask_ = (1u<<4) | 1u; // 31408 confirm and 31210 transition.
                BeginSwitch(2,1);
            }
        } else if (panel_ == 2 && actors_[selected_].x == target_[selected_]) {
            if (selecting_) {
                // 31BE4 needs both the finished nonlooping motion and a retired
                // handle for the SE submitted by 31BA0.
                if (!selection_voice_playing && actors_[selected_].time == select_end) BeginSwitch(3,-1);
            } else if (next || prior) {
                se_mask_ = 1u<<2;
                MoveCarousel(next ? 1 : -1);
            } else if (pressed & cross) {
                se_mask_ = 1u<<5;
                BeginSwitch(0,0);
            } else if (pressed & circle) {
                actors_[selected_].motion = 3;
                actors_[selected_].time = 0;
                selecting_ = true;
                voice_duck_ = true;
                blend_remaining_ = 1280;
                se_mask_ = 1u<<3;
            }
        } else if (panel_ == 4) {
            AdvanceProfile(pressed,down,up);
        } else if (panel_ == 3 && !editor_started_) {
            // 31C40 opens the nickname editor once CC1 is serviced.
            editor_started_ = true;
            editor_request_ = true;
            guide_ = -1;
            guide_frame_ = 0;
        }
    }

}
} // namespace dokodemo::gameplay

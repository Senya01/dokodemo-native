#pragma once
#include <array>
#include <cstdint>

namespace dokodemo::recovered { class RngNativeOwner; }
namespace dokodemo::gameplay {

enum class GuiTitleMediaRequest { none, slot, free_space };

// Positions are in TITLE model units. Time is the source DAK clock (4800 Hz).
struct GuiTitleActor {
    unsigned type = 0, motion = 0, time = 0;
    float x = 0, y = 0, z = 0;
    bool visible = false;
};

class GuiTitleState {
public:
    // One 30-Hz game update; raw positive PSP button bits.
    void Advance(std::uint32_t buttons, bool selection_voice_playing = false);
    GuiTitleMediaRequest media_request() const { return media_request_; }
    bool media_blocked() const { return media_phase_ == MediaPhase::blocked; }
    void SubmitSlotAbsent(bool absent);
    void SubmitLoadedSlot();
    void EnterSavedRoom();
    void ReturnToSelectMenu();
    unsigned menu_variant() const { return menu_variant_; }
    bool overwrite_requested() const { return overwrite_requested_; }
    void SubmitFreeBytes(std::uintmax_t available);
    void FinishNickname(bool confirmed_nonempty);
    void FinishPlayerName(bool confirmed_nonempty);
    void FinishDate(int year, int month, int day, int zodiac);
    void FinishPhone(bool confirmed_nonempty);
    void FinishConfirmation(bool yes);
    void FinishAdoption();
    void UpdateGuide(bool editor_editing, bool list_editing = false, int date_guide = -1);
    int guide_index() const { return guide_frame_ ? guide_ : -1; }
    unsigned guide_frame() const { return guide_frame_; }
    unsigned arrow_frame(unsigned clip) const { return arrow_frames_.at(clip - 7); }
    bool nickname_requested() const { return editor_request_ && (panel_ == 3 || (panel_ == 4 && profile_row_==0)); }
    bool player_name_requested() const { return editor_request_ && panel_ == 4 && active_profile_row()==1; }
    bool gender_requested() const { return editor_request_ && panel_ == 4 && active_profile_row()==2; }
    bool date_requested() const { return editor_request_ && panel_ == 4 && profile_row_==3; }
    bool phone_requested() const { return editor_request_ && panel_ == 4 && profile_row_==6; }
    bool confirmation_requested() const { return confirmation_request_; }
    bool profile_yes_requested() const { return profile_mode_ == ProfileMode::adopting; }
    bool title_complete() const { return phase_ == Phase::finished; }
    unsigned active_profile_row() const { return profile_row_; }
    unsigned profile_present_mask() const;
    bool gender_list_visible() const { return profile_row_==2 && list_phase_ != ListPhase::idle; }
    bool blood_list_visible() const { return profile_row_==5 && list_phase_ != ListPhase::idle; }
    bool profile_list_closing() const { return list_phase_ == ListPhase::closing; }
    unsigned profile_list_frame() const { return list_frame_; }
    unsigned profile_list_cursor() const { return list_cursor_; }
    bool gender_list_closing() const { return profile_list_closing(); }
    unsigned gender_list_frame() const { return profile_list_frame(); }
    unsigned gender_cursor() const { return profile_list_cursor(); }
    int gender_value() const { return gender_value_; }
    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }
    int zodiac() const { return zodiac_; }
    int blood_value() const { return blood_; }
    unsigned se_mask() const { return se_mask_; }
    bool selection_in_progress() const { return selecting_; }
    bool duck_bgm() const { return voice_duck_; }
    void BindRng(recovered::RngNativeOwner& rng) { rng_ = &rng; }
    unsigned panel() const { return panel_; }
    unsigned frame() const { return frame_; }
    unsigned selected_actor() const { return selected_; }
    const GuiTitleActor& actor(unsigned i) const { return actors_.at(i); }
    float actor_x(unsigned i) const { return actor(i).x; }
    bool actor_visible(unsigned i) const { return actor(i).visible; }
    float radius() const { return display_radius_; }
    bool bgm_fade_requested() const { return fade_requested_; }
    unsigned panel_ticks() const { return panel_ticks_; }
    bool wipe_visible() const { return panel_ != 5 && display_wipe_; }
    int bgm_bank() const { return submitted_bank_; }
    bool bgm_requested() const { return submitted_bank_ >= 0; }
    bool interactive() const { return phase_ == Phase::active && panel_ != 5; }
private:
    enum class Phase { opening, active, switch_panel, finished };
    enum class Wipe { closed, opening, open, closing };
    enum class ListPhase { idle, opening, active, closing };
    enum class ProfileMode { guided, request_confirmation, confirming, review, adopting, leaving };
    enum class MediaPhase { inserted, begin, wait_hidden, read_slot, wait_slot, check_space, finish, ready, blocked };
    void InstallPanel();
    void UpdateActors();
    void UpdateEntranceVisibility();
    void AdvanceColdMedia();
    void BeginSwitch(unsigned panel, int bank);
    void MoveCarousel(int direction);
    void AdvanceProfileList(std::uint32_t pressed, unsigned down, unsigned up);
    void AdvanceProfile(std::uint32_t pressed, unsigned down, unsigned up);
    void OpenProfileField();
    void EndProfileField();
    void SelectProfileRow(unsigned row);
    bool EditingRow(unsigned row) const;
    Phase phase_ = Phase::active;
    Wipe wipe_ = Wipe::closed;
    unsigned menu_variant_ = 1;
    bool overwrite_requested_ = false;
    unsigned panel_ = 5, next_ = 5, frame_ = 0, selected_ = 0;
    float radius_ = -50;
    int pending_bank_ = -1, queued_bank_ = -1, submitted_bank_ = -1;
    std::uint32_t previous_buttons_ = 0;
    unsigned left_ticks_ = 0, right_ticks_ = 0, countdown_ = 4;
    unsigned down_ticks_ = 0, up_ticks_ = 0;
    bool selecting_ = false, fade_requested_ = false;
    unsigned panel_ticks_ = 0, blend_remaining_ = 0, se_mask_ = 0;
    bool voice_duck_ = false;
    bool editor_started_ = false, editor_request_ = false;
    bool nickname_present_ = false, player_present_ = false, phone_present_ = false;
    ProfileMode profile_mode_ = ProfileMode::guided;
    unsigned profile_row_ = 0;
    bool confirmation_request_ = false, list_cancel_enabled_ = false, list_cancelled_ = false;
    ListPhase list_phase_ = ListPhase::idle;
    unsigned list_frame_ = 0, list_cursor_ = 0;
    int gender_value_ = -1;
    int year_ = -1, month_ = -1, day_ = -1, zodiac_ = -1, blood_ = -1;
    MediaPhase media_phase_ = MediaPhase::inserted;
    GuiTitleMediaRequest media_request_ = GuiTitleMediaRequest::none;
    int guide_ = -1;
    unsigned guide_frame_ = 0;
    std::array<unsigned,2> arrow_frames_{1,1};
    float draw_radius_ = -50, display_radius_ = -50;
    bool draw_wipe_ = true, display_wipe_ = true;
    recovered::RngNativeOwner* rng_ = nullptr;
    std::array<GuiTitleActor, 10> actors_{};
    std::array<float,10> target_{};
    std::array<unsigned,10> segments_{};
    std::array<unsigned,10> motion_counters_{};
};
} // namespace dokodemo::gameplay

#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dokodemo::gameplay {

struct ProfileDate {
    int year = -1, month = -1, day = -1, zodiac = -1;
};

int ProfileMonthDays(int year, int month);
int ProfileZodiac(int month, int day);
std::string ProfileDateText(const ProfileDate& date); // CP932, includes any already committed prefix.
std::string_view ProfileZodiacText(int zodiac);

// GuiTitle's year -> month -> day owner. Raw PSP masks, one tick per update.
class ProfileDateEditor {
public:
    void Open(int current_year, ProfileDate previous, std::uint32_t buttons, bool form_complete = false);
    void Step(std::uint32_t buttons);
    unsigned mode() const { return mode_; }
    int cursor() const { return cursor_; }
    int base_year() const { return base_; }
    int count() const;
    int first_visible() const { return static_cast<int>(scroll_); }
    float scroll_rows() const { return scroll_; }
    unsigned frame() const { return frame_; }
    bool editing() const { return phase_ == Phase::input; }
    bool closing() const { return phase_ == Phase::closing; }
    bool cancelled() const { return cancelled_; }
    unsigned se_mask() const { return sounds_; }
    const ProfileDate& visible_date() const { return staged_; }
    const std::optional<ProfileDate>& result() const { return result_; }

private:
    enum class Phase { idle, opening, input, closing };
    Phase phase_ = Phase::idle;
    std::uint32_t last_buttons_ = 0;
    std::array<unsigned,4> held_{};
    int base_ = 0, cursor_ = 0, top_ = 0;
    float scroll_ = 0;
    unsigned mode_ = 0, frame_ = 0, sounds_ = 0;
    bool can_cancel_year_ = false, backing_up_ = false, cancelled_ = false;
    ProfileDate prior_, staged_;
    std::optional<ProfileDate> result_;
    void Begin(unsigned mode);
    void Move(unsigned direction, bool fresh);
    void Finish();
};

} // namespace dokodemo::gameplay

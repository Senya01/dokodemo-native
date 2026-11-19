#include "profile_date.h"
#include <algorithm>

namespace dokodemo::gameplay {

int ProfileMonthDays(int year, int month) {
    constexpr int days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    const bool leap = year%4 == 0 && (year%100 != 0 || year%400 == 0);
    return days[month-1] + (month == 2 && leap);
}

int ProfileZodiac(int month, int day) {
    // 14538 compares packed MMDD against the first day of the next sign.
    constexpr int change[]{20,19,21,20,21,22,23,23,23,24,22,22};
    if (month < 1 || month > 12 || day < 1 || day > ProfileMonthDays(2000,month)) return -1;
    return (month+8+(day >= change[month-1]))%12;
}

std::string_view ProfileZodiacText(int zodiac) {
    // 14510 selects one CP932 name from the table at PH1+42F8.
    constexpr std::string_view names[]{
        "\x82\xa8\x82\xd0\x82\xc2\x82\xb6\x8d\xc0", "\x82\xa8\x82\xa4\x82\xb5\x8d\xc0",
        "\x82\xd3\x82\xbd\x82\xb2\x8d\xc0", "\x82\xa9\x82\xc9\x8d\xc0",
        "\x82\xb5\x82\xb5\x8d\xc0", "\x82\xa8\x82\xc6\x82\xdf\x8d\xc0",
        "\x82\xc4\x82\xf1\x82\xd1\x82\xf1\x8d\xc0", "\x82\xb3\x82\xbb\x82\xe8\x8d\xc0",
        "\x82\xa2\x82\xc4\x8d\xc0", "\x82\xe2\x82\xac\x8d\xc0",
        "\x82\xdd\x82\xb8\x82\xaa\x82\xdf\x8d\xc0", "\x82\xa4\x82\xa8\x8d\xc0", "\x81\x48"
    };
    return zodiac >= 0 && zodiac <= 12 ? names[zodiac] : std::string_view{};
}

std::string ProfileDateText(const ProfileDate& date) {
    std::string text;
    auto append = [&](int number, unsigned width, std::string_view suffix) {
        if (number == -1) return;
        const auto digits = std::to_string(number);
        if (width > digits.size()) text.append(width-digits.size(),' ');
        text += digits;
        text += suffix;
    };
    append(date.year,4,"\x94\x4e");
    append(date.month,2,"\x8c\x8e");
    append(date.day,2,"\x93\xfa");
    return text;
}

void ProfileDateEditor::Open(int current_year, ProfileDate previous, std::uint32_t buttons,
                             bool form_complete) {
    prior_ = previous;
    can_cancel_year_ = prior_.zodiac != -1 || form_complete;
    if (prior_.year == -1) prior_.year = 1980; // 32674 updates the actual backup.
    base_ = current_year-100;
    staged_ = {};
    cancelled_ = false;
    last_buttons_ = buttons;
    held_.fill(0);
    result_.reset();
    sounds_ = 0;
    Begin(3);
}

int ProfileDateEditor::count() const {
    if (mode_ == 3) return 101;
    if (mode_ == 4) return 12;
    if (mode_ == 5) return ProfileMonthDays(staged_.year,staged_.month);
    return 0;
}

void ProfileDateEditor::Begin(unsigned mode) {
    sounds_ |= 1u<<10; // SELECT open, 10178.
    mode_ = mode;
    frame_ = 0;
    phase_ = Phase::opening;
    if (mode == 3) {
        staged_.year = -1;
        cursor_ = std::clamp(prior_.year-base_,0,100);
        top_ = cursor_ < 5 ? 0 : std::min(cursor_+4,100)-8;
    } else if (mode == 4) {
        staged_.month = -1;
        cursor_ = std::clamp(prior_.month-1,0,11);
        top_ = std::max(0,cursor_-8);
    } else {
        staged_.day = -1;
        cursor_ = std::clamp(prior_.day-1,0,count()-1);
        top_ = 0;
    }
    scroll_ = static_cast<float>(top_);
}

void ProfileDateEditor::Move(unsigned direction, bool fresh) {
    const int last = count()-1;
    const int previous = cursor_;
    if (mode_ == 5 && direction < 2) {
        // 11514 uses a fixed five-row grid. A missing cell snaps to the
        // final legal day, even when the month occupies only four rows.
        int row = cursor_/7;
        const int column = cursor_%7;
        if (direction == 0) row = row == 4 ? (fresh ? 0 : 4) : row+1;
        else row = row == 0 ? (fresh ? 4 : 0) : row-1;
        cursor_ = std::min(last,7*row+column);
    } else {
        if (mode_ != 5 && direction >= 2) return;
        const bool forward = direction == 0 || direction == 2;
        if (forward) cursor_ = cursor_ == last ? (fresh ? 0 : last) : cursor_+1;
        else cursor_ = cursor_ == 0 ? (fresh ? last : 0) : cursor_-1;
        if (mode_ != 5) top_ = std::clamp(top_,cursor_-8,cursor_);
    }
    if (previous != cursor_) sounds_ |= 1u<<6;
}

void ProfileDateEditor::Finish() {
    if (backing_up_) {
        if (mode_ > 3) {
            Begin(mode_-1);
            return;
        }
        staged_ = prior_;
        cancelled_ = true;
    } else if (mode_ == 3) {
        staged_.year = cursor_+base_;
        Begin(4);
        return;
    } else if (mode_ == 4) {
        staged_.month = cursor_+1;
        Begin(5);
        return;
    } else {
        staged_.day = cursor_+1;
        staged_.zodiac = ProfileZodiac(staged_.month,staged_.day);
    }
    mode_ = 0;
    phase_ = Phase::idle;
    result_ = staged_;
}

void ProfileDateEditor::Step(std::uint32_t buttons) {
    const auto pressed = buttons & ~last_buttons_;
    last_buttons_ = buttons;
    sounds_ = 0;
    constexpr unsigned directions[]{0x40,0x10,0x20,0x80};
    std::array<bool,4> move{};
    for (unsigned i=0; i<held_.size(); ++i) {
        if (!(buttons & directions[i])) {
            held_[i] = 0;
            continue;
        }
        const unsigned counter = held_[i];
        move[i] = counter == 0 || (counter >= 10 && counter%2 == 0);
        held_[i] = counter < 12 ? counter+1 : 11;
    }
    if (phase_ == Phase::idle) return;
    if (phase_ != Phase::input) {
        // SELECT clips 5 through 10 all end at animation frame 10.
        if (++frame_ == 10) {
            if (closing()) Finish();
            else phase_ = Phase::input;
        }
        return;
    }
    for (unsigned direction=0; direction<move.size(); ++direction) {
        if (move[direction]) {
            Move(direction,(pressed & directions[direction]) != 0);
            break;
        }
    }
    scroll_ += (static_cast<float>(top_)-scroll_)*0.125f;
    const bool confirm = (pressed & 0x2000) != 0;
    const bool cancel = (pressed & 0x4000) && (mode_ != 3 || can_cancel_year_);
    if (confirm || cancel) {
        backing_up_ = !confirm;
        sounds_ |= 1u<<(confirm ? 4 : 5);
        sounds_ |= 1u<<11; // SELECT close, 1040C.
        phase_ = Phase::closing;
        frame_ = 0;
    }
}

} // namespace dokodemo::gameplay

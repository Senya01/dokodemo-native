#include "game_profile_native.h"
#include <algorithm>
#include <charconv>

namespace dokodemo::recovered {
int32_t GameProfile::Days(int32_t y, int32_t m) noexcept {
    constexpr int32_t d[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12)
        return 0;
    return d[m - 1] + int(m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
void GameProfile::Count() noexcept {
    state_.present = uint32_t(!state_.nickname.empty()) + uint32_t(!state_.player.empty()) +
                     uint32_t(state_.gender != -1) + uint32_t(state_.year != -1) +
                     uint32_t(state_.zodiac != -1) + uint32_t(state_.blood != -1) +
                     uint32_t(!state_.comment.empty());
}
GameProfileField GameProfile::Next() const noexcept {
    if (state_.editor_field)
        return GameProfileField(state_.editor_field);
    // 320B8 uses E6+1. This is deliberately a count,not first-missing search.
    constexpr GameProfileField next[]{GameProfileField::Nickname, GameProfileField::Player,
                                      GameProfileField::Gender,   GameProfileField::Year,
                                      GameProfileField::Year,     GameProfileField::Blood,
                                      GameProfileField::Comment,  GameProfileField::Confirmation};
    return next[state_.present <= 7 ? state_.present : 7];
}
void GameProfile::Begin(GameProfileField field) {
    if (!state_.editor_field)
        backup_ = state_;
    state_.editor_field = uint32_t(field);
}
bool GameProfile::Result(const std::string *value, int32_t current_year, std::string &error) {
    const auto field = GameProfileField(state_.editor_field);
    if (field == GameProfileField::None || field == GameProfileField::Confirmation) {
        error = "No field result requested";
        return false;
    }
    if (!value) {
        ++state_.cancelled_results;
        if (field == GameProfileField::Month) {
            state_.month = -1;
            state_.editor_field = uint32_t(GameProfileField::Year);
            return true;
        }
        if (field == GameProfileField::Day) {
            state_.day = -1;
            state_.editor_field = uint32_t(GameProfileField::Month);
            return true;
        }
        const auto accepted = state_.accepted_results, cancelled = state_.cancelled_results;
        state_ = backup_;
        state_.accepted_results = accepted;
        state_.cancelled_results = cancelled;
        state_.editor_field = 0;
        Count();
        return true;
    }
    int32_t n = 0;
    if (field != GameProfileField::Nickname && field != GameProfileField::Player &&
        field != GameProfileField::Comment) {
        const auto parsed = std::from_chars(value->data(), value->data() + value->size(), n);
        if (parsed.ec != std::errc{} || parsed.ptr != value->data() + value->size()) {
            error = "Numeric selection required";
            return false;
        }
    }
    switch (field) {
    case GameProfileField::Nickname:
        state_.nickname = *value;
        break;
    case GameProfileField::Player:
        state_.player = *value;
        break;
    case GameProfileField::Gender:
        if (n < 1 || n > 2) {
            error = "Gender result range";
            return false;
        }
        state_.gender = n == 1 ? 2 : 1;
        break;
    case GameProfileField::Year:
        if (n < current_year - 100 || n > current_year) {
            error = "Birthday year outside source calendar range";
            return false;
        }
        state_.year = n;
        state_.editor_field = uint32_t(GameProfileField::Month);
        ++state_.accepted_results;
        return true;
    case GameProfileField::Month:
        if (n < 1 || n > 12) {
            error = "Birthday month range";
            return false;
        }
        state_.month = n;
        state_.editor_field = uint32_t(GameProfileField::Day);
        ++state_.accepted_results;
        return true;
    case GameProfileField::Day: {
        if (n < 1 || n > Days(state_.year, state_.month)) {
            error = "Invalid Gregorian birthday";
            return false;
        }
        state_.day = n;
        // 145E0: traditional12-sign result,month/day boundary table.
        constexpr int32_t ends[]{19, 18, 20, 19, 20, 21, 22, 22, 22, 23, 21, 21};
        state_.zodiac = (state_.month + 8 + int(n > ends[state_.month - 1])) % 12;
        break;
    }
    case GameProfileField::Blood:
        if (n < 1 || n > 4) {
            error = "Blood result range";
            return false;
        }
        state_.blood = n - 1;
        break;
    case GameProfileField::Comment:
        state_.comment = *value;
        break;
    default:
        error = "Unsupported field";
        return false;
    }
    ++state_.accepted_results;
    state_.editor_field = 0;
    Count();
    error.clear();
    return true;
}
uint32_t GameProfile::PackDate(int32_t year, int32_t month, int32_t day) noexcept {
    if (year < 1900)
        return UINT32_MAX;
    const auto p = [](uint32_t n) {
        return (((n / 1000) % 10) << 16) | (((n / 100) % 10) << 8) | (((n / 10) % 10) << 4) | (n % 10);
    };
    return (p(uint32_t(year - 1900)) << 16) | (p(uint32_t(month)) << 8) | p(uint32_t(day));
}
bool GameProfile::Confirm(bool yes, uint32_t actor, const std::array<uint32_t, 7> &calendar) {
    if (state_.present != 7 || actor > 4)
        return false;
    state_.confirmed = yes;
    if (!yes)
        return true;
    GameProfileRecord next;
    next.live = true;
    next.actor = actor;
    next.gender = uint32_t(state_.gender);
    next.blood = uint32_t(state_.blood);
    next.date = PackDate(state_.year, state_.month, state_.day);
    next.calendar = calendar;
    next.nickname = state_.nickname;
    next.player = state_.player;
    next.comment = state_.comment;
    const auto raw = [](const std::string &s, std::array<uint8_t, 16> &out) {
        // Native safety padding for bytes beyond the logical NUL. A long
        // comment retains its full native string;this16B projection is DATA,
        // never decoded as CP932 or published as a purported PSP savedata.
        std::copy_n(reinterpret_cast<const uint8_t *>(s.data()), (std::min)(s.size(), out.size()),
                    out.begin());
    };
    raw(next.nickname, next.source_nickname);
    raw(next.player, next.source_player);
    raw(next.comment, next.source_comment);
    state_.stored = std::move(next);
    state_.applied = true;
    return true;
}
} // namespace dokodemo::recovered

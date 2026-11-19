#include "profile_adoption.h"

#include <algorithm>
#include <cstring>

namespace dokodemo::gameplay {

bool ProfileText::Assign(std::string_view cp932) noexcept {
    if (cp932.size() >= bytes.size() || cp932.find('\0') != cp932.npos) return false;
    if (!cp932.empty()) std::memmove(bytes.data(), cp932.data(), cp932.size());
    bytes[cp932.size()] = '\0';
    return true;
}

std::string_view ProfileText::text() const noexcept {
    const auto end = std::find(bytes.begin(), bytes.end(), '\0');
    return {bytes.data(), static_cast<std::size_t>(end - bytes.begin())};
}

namespace {
// BOOT+4224 places the decimal digits at bits 0,4,8,16.
std::uint32_t CalendarDigits(unsigned value) noexcept {
    return (value % 10) | ((value / 10 % 10) << 4) |
           ((value / 100 % 10) << 8) | ((value / 1000 % 10) << 16);
}
}

AcceptedProfile AdoptProfile(unsigned actor, const ProfileText& nickname, const ProfileText& player,
                             int gender, int year, int month, int day, int blood,
                             const ProfileText& phone, const std::array<std::uint32_t, 7>& calendar) noexcept {
    // 31F70 calls 13F70 before the 24B94..24D3C setters. The form owns
    // month/day validation; 13F70's early return is for years before 1900.
    const std::uint32_t packed = year < 1900 ? UINT32_MAX :
        (CalendarDigits(static_cast<unsigned>(year - 1900)) << 16) |
        (CalendarDigits(static_cast<unsigned>(month)) << 8) | CalendarDigits(static_cast<unsigned>(day));
    // 24F50 copies seven consumed words into slot zero. No save write is
    // issued by this sequence, and the star is derived from the birthday.
    return {actor, nickname, player, gender, packed, blood, phone, calendar};
}
} // namespace dokodemo::gameplay

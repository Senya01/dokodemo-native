#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace dokodemo::gameplay {

// GuiTitle owns three 16-byte buffers; its consumers copy all 16 bytes.
struct ProfileText {
    std::array<char, 16> bytes{};
    bool Assign(std::string_view cp932) noexcept;
    std::string_view text() const noexcept;
};

struct AcceptedProfile {
    unsigned actor;
    ProfileText nickname;
    ProfileText player;
    int gender;
    std::uint32_t packed_birthday;
    int blood;
    ProfileText phone;
    std::array<std::uint32_t, 7> adopted_at;
};

AcceptedProfile AdoptProfile(unsigned actor, const ProfileText& nickname, const ProfileText& player,
                             int gender, int year, int month, int day, int blood,
                             const ProfileText& phone, const std::array<std::uint32_t, 7>& calendar) noexcept;

} // namespace dokodemo::gameplay

#pragma once

#include "profile_adoption.h"
#include <cstddef>
#include <optional>
#include <string>

namespace dokodemo::gameplay {

// The live profile copied by BOOT 4DA28, before the packed KSC stream.
// These operations do not create a PSP utility file.
class RetailSaveProfile {
public:
    static constexpr std::size_t kSize = 0x10b50;
    using Calendar = std::array<std::uint32_t, 7>;

    RetailSaveProfile() noexcept;
    void Adopt(const AcceptedProfile& profile) noexcept;
    void SetOptions(const std::array<int, 3>& values) noexcept;
    void EnterFirstDayRoom(unsigned actor) noexcept;
    void SetFullMenuMode() noexcept;
    bool SetFirstDayDiary(std::string_view text, const std::array<unsigned, 3>& date,
                          bool revealed) noexcept;
    void StampForSave(const Calendar& now) noexcept;
    bool RestoreFromUtility(const std::uint8_t* data, std::size_t length) noexcept;
    AcceptedProfile ReadProfile() const noexcept;
    Calendar SavedAt() const noexcept;
    std::array<int,3> Options() const noexcept;
    struct DiaryPage {
        std::string text;
        std::array<unsigned,3> date;
        bool revealed;
    };
    std::optional<DiaryPage> FirstDayDiary() const;
    const std::array<std::uint8_t, kSize>& bytes() const noexcept { return bytes_; }

private:
    std::array<std::uint8_t, kSize> bytes_{};
};

} // namespace dokodemo::gameplay

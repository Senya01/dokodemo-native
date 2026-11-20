#include "retail_save_profile.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "profile_date.h"

namespace dokodemo::gameplay {
namespace {
constexpr std::uint32_t version = 0x0011076a;
constexpr std::size_t adopted_time = 0xb0, save_time = 0xcc;
constexpr std::size_t diary_base = 0x114, bank_size = 0x1834, row_size = 0xdc;
static_assert(diary_base + 11 * bank_size == RetailSaveProfile::kSize);

void WriteWord(std::uint8_t* dest, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        dest[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint32_t ReadWord(const std::uint8_t* src) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(src[i]) << (8 * i);
    return value;
}

void ResetDiaryBank(std::uint8_t* bank) noexcept {
    std::fill_n(bank,bank_size,std::uint8_t{0});
    for (std::size_t j=0;j<28;++j) {
        auto* row=bank+row_size*j;
        WriteWord(row+4,UINT32_MAX);
        WriteWord(row+0xd0,UINT32_MAX);
        WriteWord(row+0xd4,UINT32_MAX);
        WriteWord(row+0xd8,UINT32_MAX);
    }
    WriteWord(bank+0x1830,UINT32_MAX);
}

void WriteCalendar(std::uint8_t* dest, const RetailSaveProfile::Calendar& clock) noexcept {
    for (std::size_t i = 0; i < clock.size(); ++i)
        WriteWord(dest + 4 * i, clock[i]);
}

RetailSaveProfile::Calendar ReadCalendar(const std::uint8_t* src) noexcept {
    RetailSaveProfile::Calendar clock;
    for (std::size_t i = 0; i < clock.size(); ++i)
        clock[i] = ReadWord(src + 4 * i);
    return clock;
}
} // namespace

RetailSaveProfile::RetailSaveProfile() noexcept {
    // Fresh process BSS and 249D0. 39B80/39B20/39D64 initialize each
    // empty diary row, including the signed -1 date and photo fields.
    auto* base = bytes_.data();
    WriteWord(base + 0x10, 2);
    WriteWord(base + 0x14, 3);
    WriteWord(base + 0x18, 3);
    for (std::size_t i=0;i<11;++i)
        ResetDiaryBank(base+diary_base+bank_size*i);
}

void RetailSaveProfile::Adopt(const AcceptedProfile& profile) noexcept {
    // 31F70..32000. Fixed-width copies include bytes after the terminator.
    auto* base = bytes_.data();
    WriteWord(base + 0x24, profile.actor);
    std::memcpy(base + 0x28, profile.nickname.bytes.data(), 16);
    std::memcpy(base + 0x38, profile.player.bytes.data(), 16);
    WriteWord(base + 0x48, profile.gender);
    WriteWord(base + 0x4c, profile.packed_birthday);
    WriteWord(base + 0x50, profile.blood);
    std::memcpy(base + 0x54, profile.phone.bytes.data(), 16);
    WriteCalendar(base + adopted_time, profile.adopted_at);
}

void RetailSaveProfile::AdoptReplacing(const AcceptedProfile& profile) noexcept {
    Adopt(profile);
    ResetDiaryBank(bytes_.data()+diary_base); // 32014 -> 39BCC -> 39B20(0)
    RestrictRoomMenus();
}

void RetailSaveProfile::SetOptions(const std::array<int, 3>& values) noexcept {
    for (std::size_t i = 0; i < values.size(); ++i)
        WriteWord(bytes_.data() + 0x10 + 4 * i, values[i]);
}

void RetailSaveProfile::EnterFirstDayRoom(unsigned actor) noexcept {
    // LoadMan transitions 1 (17BCC) and 5 (17C20). Only valid game actors.
    if (actor >= 5) return;
    for (unsigned bit : {0u, actor + 21, 26u, 29u, 1u, 2u, actor + 3, 31u})
        bytes_[0x104 + bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
}

void RetailSaveProfile::SetFullMenuMode() noexcept {
    // NEKO 8DDF5 -> GAME72(1) -> BOOT 25098.
    WriteWord(bytes_.data() + 8, 1);
}

void RetailSaveProfile::RestrictRoomMenus() noexcept {
    WriteWord(bytes_.data()+8,0); // 89669 -> GAME72(0) -> 25098.
}

unsigned RetailSaveProfile::MainDiaryCount() const noexcept {
    // GAME30 -> 3A314: count occupied illustrations in the 28 main-bank rows.
    unsigned count=0;
    for (unsigned row=0;row<28;++row)
        count+=ReadWord(bytes_.data()+diary_base+row_size*row+4)!=UINT32_MAX;
    return count;
}

bool RetailSaveProfile::SetFirstDayDiary(std::string_view text, const std::array<unsigned, 3>& date,
                                         bool revealed) noexcept {
    if (text.empty() || text.size() >= 200 || text.find('\0') != text.npos) return false;
    auto* row = bytes_.data() + diary_base;
    const auto id = ReadWord(row + 4);
    if (id != UINT32_MAX && (id != 12 || ReadWord(row + 0xd0) != date[0] ||
        ReadWord(row + 0xd4) != date[1] || ReadWord(row + 0xd8) != date[2])) return false;
    // Toro's initial A519/GAME71 record; BOOT 3A074 selects the first
    // available row. The native string owns its text and initializes the
    // unused tail; stale PSP heap bytes after the terminator are not data.
    row[0] = revealed ? 1 : 0;
    WriteWord(row + 4, 12);
    std::memset(row + 8, 0, 200);
    std::memcpy(row + 8, text.data(), text.size());
    for (std::size_t i = 0; i < date.size(); ++i)
        WriteWord(row + 0xd0 + 4 * i, date[i]);
    return true;
}

void RetailSaveProfile::StampForSave(const Calendar& now) noexcept {
    WriteWord(bytes_.data(), version); // 25064
    WriteCalendar(bytes_.data() + save_time, now); // 4E1D0 -> 24F50(1)
}

bool RetailSaveProfile::RestoreFromUtility(const std::uint8_t* data, std::size_t length) noexcept {
    if (!data || length < kSize || ReadWord(data) != version) return false;
    // 4E324 does field copies: padding, A4/A8 and bits 104..113 are
    // outside its copy ranges. The rest of the payload is loaded by 7CE00.
    auto* dst = bytes_.data();
    std::memmove(dst, data, 0x0d);
    std::memmove(dst + 0x10, data + 0x10, 0xa4 - 0x10);
    std::memmove(dst + 0xac, data + 0xac, 0x104 - 0xac);
    std::memmove(dst + diary_base, data + diary_base, kSize - diary_base);
    return true;
}

AcceptedProfile RetailSaveProfile::ReadProfile() const noexcept {
    const auto* base = bytes_.data();
    AcceptedProfile out{};
    out.actor = ReadWord(base + 0x24);
    std::memcpy(out.nickname.bytes.data(), base + 0x28, 16);
    std::memcpy(out.player.bytes.data(), base + 0x38, 16);
    out.gender = static_cast<int>(ReadWord(base + 0x48));
    out.packed_birthday = ReadWord(base + 0x4c);
    out.blood = static_cast<int>(ReadWord(base + 0x50));
    std::memcpy(out.phone.bytes.data(), base + 0x54, 16);
    out.adopted_at = ReadCalendar(base + adopted_time);
    return out;
}

std::array<int,3> RetailSaveProfile::Options() const noexcept {
    return {int(ReadWord(bytes_.data()+0x10)),int(ReadWord(bytes_.data()+0x14)),int(ReadWord(bytes_.data()+0x18))};
}

std::optional<RetailSaveProfile::DiaryPage> RetailSaveProfile::FirstDayDiary() const {
    const auto* row=bytes_.data()+diary_base;
    const auto id=ReadWord(row+4);
    if (id==UINT32_MAX) return {};
    const auto* end=std::find(row+8,row+0xd0,0);
    const std::array<unsigned,3> date{ReadWord(row+0xd0),ReadWord(row+0xd4),ReadWord(row+0xd8)};
    if (id!=12 || row[0]>1 || end==row+0xd0 || end==row+8 || date[0]>9999 ||
        date[1]<1 || date[1]>12 || date[2]<1 || date[2]>unsigned(ProfileMonthDays(date[0],date[1])))
        throw std::invalid_argument("Unsupported saved first-day diary page");
    return DiaryPage{std::string(row+8,end), date, row[0]!=0};
}

RetailSaveProfile::Calendar RetailSaveProfile::SavedAt() const noexcept {
    return ReadCalendar(bytes_.data() + save_time);
}

} // namespace dokodemo::gameplay

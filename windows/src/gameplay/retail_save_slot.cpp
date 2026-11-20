#include "retail_save_slot.h"
#include "retail_savedata_envelope.h"
#include "profile_date.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <vector>

namespace dokodemo::gameplay {
namespace {
constexpr std::string_view history_magic="DOKONJ01";
constexpr std::size_t history_footer_size=history_magic.size()+4;
constexpr std::size_t max_history_size=36*1024;

// 13E68 and 14334, for valid RTC calendar words. 14334 subtracts the
// individual dates in a 32-bit register, including the sub-second field.
bool CalendarTicks(const RetailSaveProfile::Calendar& c, std::uint32_t& ticks) {
    const auto year = c[6], month = c[5], day = c[4];
    if (year < 1900 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > static_cast<unsigned>(ProfileMonthDays(year, month)) ||
        c[3] > 23 || c[2] > 59 || c[1] > 59 || c[0] >= 30) return false;
    const auto leaps = [](unsigned y) { return y/4 - y/100 + y/400; };
    auto days = (year - 1900) * 365 + leaps(year - 1) - leaps(1899) + day - 1;
    for (unsigned m = 1; m < month; ++m) days += ProfileMonthDays(year, m);
    ticks = (((days * 24 + c[3]) * 60 + c[2]) * 60 + c[1]) * 30 + c[0];
    return true;
}

bool HasFirstDaySignature(const RetailSaveProfile& profile) {
    const auto& bytes = profile.bytes();
    return bytes[0] == 0x6a && bytes[1] == 0x07 && bytes[2] == 0x11 && bytes[3] == 0;
}

// 8D1FD binds the date and the 26 script globals in a single array.
void RequireFirstDayRoot(const KscSaveValue& actor) {
    if (actor.word != 0x9000001bu || actor.children.size() != 27 ||
        actor.children.front().word >> 28 != 3 ||
        !std::all_of(actor.children.begin()+1, actor.children.end(), [](const KscSaveValue& value) {
            return value.word == 0xb0000000u && value.children.size() == 1;
        }))
        throw std::invalid_argument("Expected the bound first-day actor graph");
}

} // namespace

std::vector<std::uint8_t> BuildRetailUtilityRequest(const RetailSaveProfile& profile,
                                                    const KscSaveValue& actor,
                                                    const std::vector<std::uint8_t>& word_history) {
    RequireFirstDayRoot(actor);
    const auto& header = profile.bytes();
    if (!HasFirstDaySignature(profile))
        throw std::invalid_argument("Expected the version stamped by System Save");
    const auto graph = EncodeKscSaveGraph(actor);
    if (graph.size() > kRetailUtilitySize-header.size())
        throw std::length_error("Actor graph exceeds the utility buffer");
    // 4D96C(0) clears the buffer; 4DA28 and 7CBC0 then append the two live owners.
    std::vector<std::uint8_t> request(kRetailUtilitySize);
    auto graph_begin = std::copy(header.begin(), header.end(), request.begin());
    std::copy(graph.begin(), graph.end(), graph_begin);
    if(!word_history.empty()) {
        const auto room_for_history=request.size()-header.size()-graph.size();
        if(word_history.size()>max_history_size || word_history.size()+history_footer_size>room_for_history)
            throw std::length_error("NJ history exceeds the remaining utility buffer");
        // The length-marked native extension lives in the authenticated padding.
        // The profile and the VM graph retain their original format and offsets.
        auto footer=request.end()-history_footer_size;
        std::copy(word_history.begin(),word_history.end(),footer-word_history.size());
        std::copy(history_magic.begin(),history_magic.end(),footer);
        for(unsigned i=0;i<4;++i)
            footer[history_magic.size()+i]=static_cast<std::uint8_t>(word_history.size()>>(8*i));
    }
    return request;
}

RetailUtilityContents DecodeRetailUtilityRequest(const std::vector<std::uint8_t>& request,
                                                const RetailSaveProfile& current_profile) {
    if (request.size()!=kRetailUtilitySize)
        throw std::invalid_argument("Unexpected first-day utility buffer size");
    RetailUtilityContents contents{current_profile,{},0,{}};
    if (!contents.profile.RestoreFromUtility(request.data(),request.size()))
        throw std::invalid_argument("Unexpected first-day utility signature");
    contents.actor=DecodeKscSaveGraph(request.data()+RetailSaveProfile::kSize,
                                      request.size()-RetailSaveProfile::kSize,contents.graph_size);
    RequireFirstDayRoot(contents.actor);
    const auto footer=request.end()-history_footer_size;
    if(std::equal(history_magic.begin(),history_magic.end(),footer)) {
        std::uint32_t size=0;
        for(unsigned i=0;i<4;++i) size|=std::uint32_t(footer[history_magic.size()+i])<<(8*i);
        const auto graph_end=RetailSaveProfile::kSize+contents.graph_size;
        const auto history_end=request.size()-history_footer_size;
        if(size && size<=max_history_size && graph_end<=history_end && size<=history_end-graph_end)
            contents.word_history.assign(footer-size,footer);
    }
    return contents;
}

bool IsLoadedTitleClockCurrent(const RetailSaveProfile& profile,const RetailSaveProfile::Calendar& now) {
    std::uint32_t current, saved;
    if (!CalendarTicks(now,current) || !CalendarTicks(profile.SavedAt(),saved)) return false;
    const auto delta = current - saved;
    return delta != 0 && delta < 0x80000000u; // 344EC: signed current minus saved > 0
}

LoadedBootRoute SelectLoadedBootRoute(const RetailSaveProfile& profile,
                                      const RetailSaveProfile::Calendar& now) {
    const auto& header = profile.bytes();
    if (!HasFirstDaySignature(profile))
        return LoadedBootRoute::invalid_header;
    std::uint32_t saved_ticks, now_ticks;
    if (!CalendarTicks(profile.SavedAt(), saved_ticks) || !CalendarTicks(now, now_ticks))
        return LoadedBootRoute::unsupported_calendar;
    const auto delta = saved_ticks - now_ticks;
    if (delta != 0 && delta < 0x80000000u) return LoadedBootRoute::clock_notice;
    if (header[8] || header[9] || header[10] || header[11]) return LoadedBootRoute::resume_15;
    return LoadedBootRoute::title_11;
}

RetailSlotStatus InspectRetailSaveSlot(const std::filesystem::path& slot_directory) {
    std::error_code ec;
    if (!std::filesystem::exists(slot_directory, ec))
        return ec ? RetailSlotStatus::unreadable : RetailSlotStatus::absent;
    if (!std::filesystem::is_directory(slot_directory, ec))
        return ec ? RetailSlotStatus::unreadable : RetailSlotStatus::unrecognized;

    const auto data_path = slot_directory / "DATA.BIN";
    const auto sfo_path = slot_directory / "PARAM.SFO";
    for (const auto* path : {&data_path, &sfo_path}) {
        if (!std::filesystem::is_regular_file(*path, ec)) {
            if (ec && ec != std::errc::no_such_file_or_directory)
                return RetailSlotStatus::unreadable;
            return RetailSlotStatus::unrecognized;
        }
    }
    const auto data_size = std::filesystem::file_size(data_path, ec);
    if (ec) return RetailSlotStatus::unreadable;
    std::ifstream sfo(sfo_path, std::ios::binary | std::ios::ate);
    const auto size = sfo.tellg();
    if (!sfo || size < 0) return RetailSlotStatus::unreadable;
    // A bounded metadata read; larger custom SFOs have unknown structure here.
    if (size > 64 * 1024) return RetailSlotStatus::unrecognized;
    std::vector<std::uint8_t> metadata(static_cast<std::size_t>(size));
    sfo.seekg(0);
    if (!metadata.empty() &&
        !sfo.read(reinterpret_cast<char*>(metadata.data()), static_cast<std::streamsize>(size)))
        return RetailSlotStatus::unreadable;
    return IsRetailProtectedSlot(metadata, data_size) ? RetailSlotStatus::protected_slot
                                                      : RetailSlotStatus::unrecognized;
}

const char* RetailSlotReadBoundary(RetailSlotStatus status) {
    switch (status) {
    case RetailSlotStatus::unreadable: return "Portable PSP save slot could not be read";
    case RetailSlotStatus::unrecognized: return "Incomplete or unrecognized UCJS10002000 save slot";
    case RetailSlotStatus::protected_slot: return "Recognized UCJS10002000 envelope; payload requires authenticated read";
    case RetailSlotStatus::absent: return nullptr;
    }
    return nullptr;
}

} // namespace dokodemo::gameplay

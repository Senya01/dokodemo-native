#include "room_calendar_items.h"
#include "core_hash.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
bool RoomCalendarItems::Load(const std::filesystem::path& directory, std::string& error) {
    std::ifstream stream(directory / "calendar-items.bin", std::ios::binary | std::ios::ate);
    std::array<std::uint8_t,4 + 29 * 6 * 4> bytes{};
    if (!stream || stream.tellg() != static_cast<std::streamoff>(bytes.size()) ||
        !stream.seekg(0).read(reinterpret_cast<char*>(bytes.data()),bytes.size()) ||
        core::sha256_hex(bytes.data(), bytes.size()) !=
            "ddf794e953165935cc477cd85add19dd90b1c0fc1d0adb9aa713e7771989e2a7") {
        error = "Missing or incorrect NEKO calendar placement rules";
        return false;
    }
    std::size_t at = 4;
    const auto next = [&]() {
        std::uint32_t value = 0;
        for (unsigned i=0; i<4; ++i)
            value |= std::uint32_t(bytes[at++]) << (8*i);
        return value;
    };
    for (auto& rule : rules_)
        rule = {next(),next(),next(),next(),next(),next()};
    rows_ = {};
    loaded_ = true;
    return true;
}

unsigned RoomCalendarItems::Update(std::uint32_t selection_bcd, std::uint32_t today_bcd,
                                   std::uint32_t birthday_bcd) {
    if (!loaded_) throw std::logic_error("Updating unloaded NEKO calendar");
    constexpr std::uint32_t integer = 0x20000000;
    const auto day = selection_bcd & 0xffff;
    unsigned count = 0;
    for (const auto& rule : rules_) {
        const auto first = (rule.first ? rule.first : birthday_bcd) & 0xffff;
        const auto last = (rule.last ? rule.last : birthday_bcd) & 0xffff;
        if (day < first || day > last) continue;
        auto& row = rows_.at(count++);
        // 3161 writes only these cells. Field 2 of every selected item's
        // template is nil, so 31EF never takes its word-selection branch.
        row[0] = rule.item;
        row[3] = rule.property;
        row[4] = integer | (today_bcd & 0x0fffffff);
        row[6] = 0;
        row[9] = integer;
        // 3B4D..3B70 assign the lowest set placement bit and its descriptor.
        row[5] = row[7] = rule.spot;
        row[8] = rule.layout;
    }
    // 3B79..3B97 call 2105 on each remaining row.
    std::fill(rows_.begin()+count, rows_.end(), Row{});
    return count;
}
} // namespace dokodemo::gameplay

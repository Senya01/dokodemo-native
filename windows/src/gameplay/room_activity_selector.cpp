#include "room_activity_selector.h"
#include "core_hash.h"
#include "recovered/rng_native.h"
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
unsigned ReadU16(const unsigned char* p) {
    return unsigned(p[0]) | (unsigned(p[1]) << 8);
}

unsigned Bit(int n) {
    // KSC E3 returns a tagged 28-bit integer after the MIPS variable shift.
    return (1u << (static_cast<unsigned>(n) & 31)) & 0x0fffffff;
}
}

bool RoomActivitySelector::Load(const std::filesystem::path& directory, std::string& error) {
    std::ifstream file(directory / "toro-room-activities.bin", std::ios::binary | std::ios::ate);
    constexpr std::size_t bytes = 21330;
    if (!file || file.tellg() != static_cast<std::streamoff>(bytes)) {
        error = "Missing NEKO room activity list";
        return false;
    }
    std::vector<unsigned char> data(bytes);
    if (!file.seekg(0).read(reinterpret_cast<char*>(data.data()), bytes) ||
        core::sha256_hex(data.data(), data.size()) !=
            "392ac172a3dd9cc030b2190e015880cba52d0ba4cc8c38e0a5f8449cca695a8e") {
        error = "Incorrect NEKO room activity list";
        return false;
    }
    std::vector<std::array<std::uint16_t, 7>> rows(ReadU16(data.data() + 4));
    std::size_t offset = 8;
    for (auto& row : rows) {
        for (auto& cell : row) {
            cell = static_cast<std::uint16_t>(ReadU16(data.data() + offset));
            offset += 2;
        }
    }
    rows_ = std::move(rows);
    capacity_ = ReadU16(data.data() + 6);
    return true;
}

RoomActivitySelection RoomActivitySelector::Select(const RoomActivityInputs& input,
                                                  recovered::RngNativeOwner& rng) const {
    if (rows_.empty()) throw std::logic_error("Room activity list was not loaded");
    // 134CE executes even if the first half of the KSC C8 conjunction is false.
    const auto roll = rng.NextC3234() % 2;
    const int field58 = !input.first_after_cleanup && roll == 0 ? 0 : input.actor_field58;
    const std::array<unsigned, 4> mask{
        Bit(input.actor_field50), Bit(input.period), Bit(input.mood), Bit(input.game_flag28)};
    RoomActivitySelection out;
    out.effective_field58 = field58;
    for (std::size_t i = 0; i != rows_.size(); ++i) {
        const auto& row = rows_[i];
        if (row[0] != input.scene || row[2] != field58) continue;
        bool matches = true;
        for (std::size_t k = 0; k != mask.size(); ++k) {
            if ((row[3 + k] & mask[k]) != mask[k]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            out.rows.push_back(static_cast<std::uint16_t>(i));
            if (out.rows.size() == capacity_) break;
        }
    }
    // 13539 returns nil for an empty list. Repeated commands in distinct
    // rows retain their separate chances in 13546.
    if (!out.rows.empty()) {
        const auto row = out.rows[rng.NextC3234() % out.rows.size()];
        out.selected_row = row;
        out.command = rows_[row][1];
    }
    return out;
}
}

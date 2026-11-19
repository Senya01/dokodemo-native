#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace dokodemo::gameplay {
std::optional<std::vector<std::uint8_t>> LoadRetailSlot(const std::filesystem::path& slot_directory);
std::vector<std::uint8_t> ReadRetailSlot(const std::filesystem::path& slot_directory);
void WriteRetailSlot(const std::filesystem::path& slot_directory, const std::filesystem::path& game_directory,
                     const std::vector<std::uint8_t>& utility_request);
}

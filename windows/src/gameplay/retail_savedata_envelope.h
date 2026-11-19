#pragma once
#include <cstdint>
#include <vector>

namespace dokodemo::gameplay {

// Identifies this game's PARAM.SFO + DATA.BIN layout only. Metadata cannot
// authenticate the file contents or be used to populate a loaded game.
bool IsRetailProtectedSlot(const std::vector<std::uint8_t>& sfo,
                           std::uintmax_t data_size) noexcept;

} // namespace dokodemo::gameplay

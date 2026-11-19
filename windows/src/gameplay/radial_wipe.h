#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dokodemo::gameplay {

// Draw GuiTitle's centered black radial transition into its RGBA8888 draw buffer.
void ApplyRadialWipe(float radius, std::vector<std::uint8_t>& vram, std::size_t offset);

} // namespace dokodemo::gameplay

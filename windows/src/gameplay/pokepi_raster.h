#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dokodemo::gameplay {
struct PokepiVertex {
    std::array<float, 4> clip{};
    float u = 0, v = 0;
    uint32_t color = 0xffffffff; // bytes RGBA (same order as EDRAM)
    std::array<float, 3> specular{};
};
struct PokepiTexture {
    unsigned width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
bool DrawPokepiTriangle(const std::array<PokepiVertex, 3> &vertices, const PokepiTexture *texture,
                        unsigned repeat_axes, bool two_sided, std::vector<uint8_t> &vram, size_t offset);
} // namespace dokodemo::gameplay

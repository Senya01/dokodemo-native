#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dokodemo::gameplay {

// BOOT B0FF0 produces 16-byte records. ADB10 consumes them before AFF74
// presents the old draw buffer. One queue belongs to each swap-buffer index.
struct OverlayGlyph {
    std::int32_t x;
    std::int32_t y;
    std::uint32_t rgba;
    std::int8_t glyph; // B1098 stores character minus 32.
};
using OverlayFont = std::array<std::uint8_t, 96 * 8>; // BOOT VA E1E50

enum class OverlayStatus { ok, unknown_queue, unsupported_glyph, short_buffer };

// Known queue records are supplied by the caller; nullptr represents unknown
// BSS. The original draw allocation and its previous pixels must be owned
// by the caller. This operation leaves unpainted pixels untouched.
OverlayStatus ApplyPreSwapGlyphs(
    std::vector<OverlayGlyph>* queue, const OverlayFont& font,
    std::vector<std::uint8_t>& vram, std::size_t draw_offset);

class StartupLogos {
public:
    // Title directory contains the ISO-derived CB2A and two packed RGBA8888 textures.
    bool Load(const std::filesystem::path& title_directory, std::string& error);
    bool Load(const std::vector<std::uint8_t>& title_cb2a,
              const std::filesystem::path& title_directory, std::string& error);
    // TITLE.CB2A animation 5, visible frames 1..319. Output is the RGB
    // scanout in a PSP stride-512 surface. Preserve the caller's stencil byte
    // and row padding; neither is displayed on the LCD.
    bool Render(unsigned animation_frame, std::vector<std::uint8_t>& vram,
                std::size_t draw_offset) const;
private:
    struct Key { unsigned frame, value; };
    struct Logo {
        unsigned width, height, x, y;
        std::vector<std::uint8_t> pixels;
        std::vector<Key> colors;
        std::vector<Key> sprites;
        unsigned sprite;
    };
    std::vector<Logo> logos_;
};

} // namespace dokodemo::gameplay

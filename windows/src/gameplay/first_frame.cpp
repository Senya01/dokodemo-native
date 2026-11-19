#include "first_frame.h"
#include "display.h"

namespace dokodemo::gameplay {
namespace {
bool Visible(const OverlayGlyph& glyph) {
    return glyph.x >= 0 && glyph.y >= 0 &&
        static_cast<unsigned>(glyph.x) <= Display::width - 8 &&
        static_cast<unsigned>(glyph.y) <= Display::height - 8;
}
}

OverlayStatus ApplyPreSwapGlyphs(std::vector<OverlayGlyph>* queue, const OverlayFont& font,
        std::vector<std::uint8_t>& vram, std::size_t draw_offset) {
    if (!queue) return OverlayStatus::unknown_queue;
    constexpr std::size_t stride = 512;
    for (const auto& glyph : *queue) {
        if (!Visible(glyph)) continue;
        if (glyph.glyph < 0 || glyph.glyph >= 96) return OverlayStatus::unsupported_glyph;
        const std::size_t required =
            (stride * (static_cast<unsigned>(glyph.y) + 7) + static_cast<unsigned>(glyph.x) + 8) * 4;
        if (draw_offset > vram.size() || required > vram.size() - draw_offset)
            return OverlayStatus::short_buffer;
    }
    for (const auto& glyph : *queue) {
        if (!Visible(glyph)) continue;
        for (unsigned y = 0; y < 8; ++y) {
            const auto row = font[static_cast<unsigned>(glyph.glyph) * 8 + y];
            for (unsigned x = 0; x < 8; ++x) {
                if (!(row & (1u << x))) continue;
                const auto at = draw_offset + 4 * (stride * (static_cast<unsigned>(glyph.y) + y) +
                    static_cast<unsigned>(glyph.x) + x);
                // ADCD4 high half, ADC4C low half. Little-endian final bytes.
                vram[at + 2] = static_cast<std::uint8_t>(glyph.rgba >> 16);
                vram[at + 3] = static_cast<std::uint8_t>(glyph.rgba >> 24);
                vram[at] = static_cast<std::uint8_t>(glyph.rgba);
                vram[at + 1] = static_cast<std::uint8_t>(glyph.rgba >> 8);
            }
        }
    }
    queue->clear(); // ADC98 clears the active count after processing.
    return OverlayStatus::ok;
}
}

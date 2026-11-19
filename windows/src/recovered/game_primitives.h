#pragma once

#include <cstddef>
#include <cstdint>

// Original v1.01 BOOT.BIN SHA-256:
// 2b361beb4c55e08af2828713861429e768a1140f284a11f606fba5f580cd483c
// These are native C++ functions, with no CPU, PSP OS, or ELF runtime.
namespace dokodemo::recovered {

struct Glyph {
    std::uint32_t index;
    std::uint8_t consumed;
};

// VA 0x941C8..0x9429C (file 0x94248..0x9431C).
// Returns the game's font index, not a Unicode codepoint. Original behavior
// consumes two bytes for every lead >= 0x80, even malformed SJIS and halfwidth
// kana. That behavior is deliberately retained. NUL returns {0,1}.
// XFT atlas lookup requires index >= 0x20 and index - 0x20 < glyph_count.
// Merely accepting a well-formed SJIS pair does not guarantee an atlas index.
// Handle NUL and layout controls before lookup. Widths are signed int8_t.
Glyph decode_glyph(std::uint8_t lead, std::uint8_t trail = 0) noexcept;

// Bounded native adapter: false means truncated input; result is unchanged.
// It does not read a missing trail byte. This adapter is new, not recovered.
// Success does not imply a valid SJIS sequence or a present XFT glyph.
bool read_glyph(const std::uint8_t* bytes, std::size_t available, Glyph& result) noexcept;

// VA 0x76960..0x76990. Only a tag-2 value can equal the supplied signed integer.
// Its payload is a sign-extended 28-bit integer.
bool integer_equals(std::uint32_t value, std::int32_t integer) noexcept;

// VA 0x76990..0x769F4: pure value form of the original in-place helper.
// Bool becomes true; tags 2 and 3 increment with 28-bit wrap; others retain bits.
std::uint32_t increment_value(std::uint32_t value) noexcept;

} // namespace dokodemo::recovered

#include "game_primitives.h"

namespace dokodemo::recovered {

Glyph decode_glyph(std::uint8_t lead, std::uint8_t trail) noexcept {
    if (lead < 0x80) {
        return {lead, 1};
    }

    std::uint32_t row;
    std::uint32_t column = trail;
    if (lead < 0xE0) {
        row = (2u * lead - 1u) & 0xFFu;
        if (lead >= 0xA0) {
            // Original jumps directly to the upper-trail adjustment with
            // this replacement pair; it has already consumed two bytes.
            row = 1;
            column = 0xAC;
        }
    } else {
        row = (2u * lead + (lead < 0xF0 ? 0x7Fu : 0x29u)) & 0xFFu;
    }

    if ((lead >= 0xA0 && lead < 0xE0) || column >= 0x9F) {
        const auto original_column = column;
        row = (row + 1u) & 0xFFu;
        column = (column + 0x62u) & 0xFFu;
        if (original_column >= 0xFD) {
            row = 2;
            column = 0x0E;
        }
    } else {
        column = (column - (column >= 0x80 ? 0x40u : 0x3Fu)) & 0xFFu;
    }
    return {96u * row + column + 0x20u, 2};
}

bool read_glyph(const std::uint8_t* bytes, std::size_t available, Glyph& result) noexcept {
    if (bytes == nullptr || available == 0 || (bytes[0] >= 0x80 && available < 2)) {
        return false;
    }
    result = decode_glyph(bytes[0], bytes[0] < 0x80 ? 0 : bytes[1]);
    return true;
}

bool integer_equals(std::uint32_t value, std::int32_t integer) noexcept {
    const auto payload = static_cast<std::int32_t>(value & 0x07FFFFFFu)
                       - static_cast<std::int32_t>(value & 0x08000000u);
    return value >> 28 == 2 && payload == integer;
}

std::uint32_t increment_value(std::uint32_t value) noexcept {
    const auto tag = value >> 28;
    if (tag == 1) {
        return 0x10000001u;
    }
    if (tag == 2 || tag == 3) {
        return (value & 0xF0000000u) | ((value + 1u) & 0x0FFFFFFFu);
    }
    return value;
}

} // namespace dokodemo::recovered

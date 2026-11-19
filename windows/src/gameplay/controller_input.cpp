#include "controller_input.h"

namespace dokodemo::gameplay {
namespace {

// BOOT 52DE4..52E70 maps PSP direction, face, shoulder and system bits.
std::uint32_t GameButtons(std::uint32_t raw) {
    return ((raw >> 4) & 0x001u) | ((raw >> 5) & 0x006u) |
           ((raw >> 2) & 0x008u) | ((raw >> 9) & 0x030u) |
           ((raw >> 6) & 0x040u) | ((raw >> 8) & 0x080u) |
           (raw & 0x300u) | ((raw & 1u) << 10) | ((raw & 8u) << 8);
}

} // namespace

std::optional<std::uint32_t> PollController(
    bool enabled, ControllerSource& source, std::optional<ControllerSample>& retained,
    ControllerFields& fields) {
    if (!enabled) return 0;
    const auto read = source.ReadOne();
    if (!read) return std::nullopt;
    // 52DC4 branches only on zero. A negative return can therefore consume
    // stack bytes. If they are unknown the retained sample also becomes unknown.
    if (read->returned != 0) retained = read->sample;
    if (!retained) return std::nullopt;
    fields.axes = {128, 128, retained->analog_x, retained->analog_y};
    fields.buttons = GameButtons(retained->buttons);
    return fields.buttons;
}

} // namespace dokodemo::gameplay

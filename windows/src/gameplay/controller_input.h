#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace dokodemo::gameplay {

struct ControllerSample {
    std::uint32_t buttons;
    std::uint8_t analog_x;
    std::uint8_t analog_y;
};

// Result of one sample request (BOOT 52DB4, sceCtrlReadBufferPositive).
// A native device failure is distinct from a source return value of zero.
struct ControllerRead {
    std::int32_t returned;
    std::optional<ControllerSample> sample;
};

class ControllerSource {
public:
    virtual ~ControllerSource() = default;
    virtual std::optional<ControllerRead> ReadOne() = 0;
};

struct ControllerFields {
    std::array<std::uint8_t, 4> axes; // input object +5..+8
    std::uint32_t buttons;            // input object +C
};

// Source binding: research/stage3/title-menu/instruction-evidence.txt,
// BOOT 95508 -> 950DC -> 52E7C -> 52DA8. Caller owns the poll-enable byte,
// retained globals and destination fields. Separate storage corresponds to
// the registered input object, distinct from the global sample and stack.
// A disabled poll returns 0 and leaves fields unchanged. A missing result
// suspends this operation before publishing fields; it has no game return.
std::optional<std::uint32_t> PollController(
    bool enabled, ControllerSource& source, std::optional<ControllerSample>& retained,
    ControllerFields& fields);

} // namespace dokodemo::gameplay

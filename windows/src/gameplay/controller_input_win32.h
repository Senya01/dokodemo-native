#pragma once

#include "controller_input.h"

namespace dokodemo::gameplay {

// Samples one explicitly selected XInput user (0..3). Scheduling remains
// with the caller. A disconnected device supplies no observation.
class XInputController final : public ControllerSource {
public:
    explicit XInputController(std::uint32_t user_index) : user_index_(user_index) {}
    std::optional<ControllerRead> ReadOne() override;
private:
    std::uint32_t user_index_;
};

} // namespace dokodemo::gameplay

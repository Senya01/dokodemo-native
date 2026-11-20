#include "controller_input_win32.h"

#include <windows.h>
#include <Xinput.h>

namespace dokodemo::gameplay {

ControllerSample MapXInputGamepad(std::uint16_t host_buttons,std::int16_t left_x,std::int16_t left_y) {
    // Match physical button positions. This is a Windows device mapping.
    static constexpr struct { WORD host; std::uint32_t psp; } mapping[] = {
        {XINPUT_GAMEPAD_BACK, 0x0001}, {XINPUT_GAMEPAD_START, 0x0008},
        {XINPUT_GAMEPAD_DPAD_UP, 0x0010}, {XINPUT_GAMEPAD_DPAD_RIGHT, 0x0020},
        {XINPUT_GAMEPAD_DPAD_DOWN, 0x0040}, {XINPUT_GAMEPAD_DPAD_LEFT, 0x0080},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, 0x0100}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, 0x0200},
        {XINPUT_GAMEPAD_Y, 0x1000}, {XINPUT_GAMEPAD_B, 0x2000},
        {XINPUT_GAMEPAD_A, 0x4000}, {XINPUT_GAMEPAD_X, 0x8000}
    };
    std::uint32_t buttons = 0;
    for (const auto& button : mapping) {
        if ((host_buttons & button.host) != 0) buttons |= button.psp;
    }
    // Both axes rest at 128. PSP analog Y increases downward.
    const auto x = static_cast<std::uint8_t>((32768 + int(left_x)) / 256);
    const int y_up = left_y;
    const auto y = static_cast<std::uint8_t>(
        y_up == -32768 ? 255 : (32768 - y_up) / 256);
    return {buttons,x,y};
}

std::optional<ControllerRead> XInputController::ReadOne() {
    XINPUT_STATE state{};
    if (user_index_ >= XUSER_MAX_COUNT ||
        XInputGetState(user_index_, &state) != ERROR_SUCCESS) return std::nullopt;
    const auto& pad=state.Gamepad;
    return ControllerRead{1,MapXInputGamepad(pad.wButtons,pad.sThumbLX,pad.sThumbLY)};
}

} // namespace dokodemo::gameplay

#include "gameplay/controller_input.h"
#include "gameplay/controller_input_win32.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>

#include <cstdio>
#include <stdexcept>

using namespace dokodemo::gameplay;

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class SampleSource final : public ControllerSource {
public:
    unsigned calls = 0;
    std::optional<ControllerRead> next;
    std::optional<ControllerRead> ReadOne() override { ++calls; return next; }
};

// One original raw source bit per game bit, low to high. Derived from
// BOOT 52DE4..52E70; Stage3 independently checked all low16 values against
// the original instruction words (controller_mask_original_word_vs_pure).
std::uint32_t ExpectedButtons(std::uint32_t raw) {
    constexpr unsigned source_bits[] = {4, 6, 7, 5, 13, 14, 12, 15, 8, 9, 0, 3};
    std::uint32_t result = 0;
    for (unsigned bit = 0; bit < 12; ++bit)
        if (raw & (1u << source_bits[bit])) result |= 1u << bit;
    return result;
}

void CheckWindowsPad() {
    constexpr std::pair<std::uint16_t,std::uint32_t> cases[]{
        {XINPUT_GAMEPAD_BACK,0x0001},{XINPUT_GAMEPAD_START,0x0008},
        {XINPUT_GAMEPAD_DPAD_UP,0x0010},{XINPUT_GAMEPAD_DPAD_RIGHT,0x0020},
        {XINPUT_GAMEPAD_DPAD_DOWN,0x0040},{XINPUT_GAMEPAD_DPAD_LEFT,0x0080},
        {XINPUT_GAMEPAD_LEFT_SHOULDER,0x0100},{XINPUT_GAMEPAD_RIGHT_SHOULDER,0x0200},
        {XINPUT_GAMEPAD_Y,0x1000},{XINPUT_GAMEPAD_B,0x2000},
        {XINPUT_GAMEPAD_A,0x4000},{XINPUT_GAMEPAD_X,0x8000},
    };
    for (unsigned raw=0;raw<65536;++raw) {
        std::uint32_t expected=0;
        for (const auto& [physical,game]:cases)
            if (raw&physical) expected|=game;
        const auto mapped=MapXInputGamepad(std::uint16_t(raw),0,0);
        Require(mapped.buttons==expected && mapped.analog_x==128 && mapped.analog_y==128,
                "Physical XInput positions mapped incorrectly");
    }
    const auto low=MapXInputGamepad(0,-32768,32767), high=MapXInputGamepad(0,32767,-32768);
    Require(low.analog_x==0 && low.analog_y==0 && high.analog_x==255 && high.analog_y==255,
            "Wrong XInput endpoints or Y sign");

    ControllerButtonLatch pad;
    for (unsigned i=0;i<3;++i) {
        const auto confirm=MapXInputGamepad(XINPUT_GAMEPAD_B,0,0).buttons;
        Require(pad.Read(confirm)==0x2000,"XInput confirmation lost");
        pad.Reset(); // focus loss or suspend
        Require(pad.Read({})==0 && pad.Read(confirm)==0 && pad.Read(confirm)==0,
                "Missing OS sample armed a held button after reconnect");
        Require(pad.Read(0)==0 && pad.Read(confirm)==0x2000,
                "Neutral hardware sample did not re-arm controls");
        Require(pad.Read({})==0 && pad.Read(confirm)==0 && pad.Read(0)==0,
                "Disconnect did not suppress stale controls");
    }
    unsigned attached=0;
    for (unsigned index=0;index<XUSER_MAX_COUNT;++index) {
        XINPUT_STATE state{};
        const auto before=XInputGetState(index,&state);
        XInputController controller(index);
        const auto sample=controller.ReadOne();
        const auto after=XInputGetState(index,&state);
        if (before==ERROR_DEVICE_NOT_CONNECTED && after==before)
            Require(!sample,"Disconnected Windows device fabricated neutral input");
        if (sample) {
            Require(sample->returned==1 && sample->sample.has_value(),"Invalid Windows sample shape");
            ++attached;
        }
    }
    Require(!XInputController(XUSER_MAX_COUNT).ReadOne(),"Out of range Windows index was admitted");
    std::printf("Windows XInput API sampled once per index, attached devices %u/4. No device input was injected.\n",attached);
}

void Run() {
    SampleSource source;
    std::optional<ControllerSample> retained;
    ControllerFields fields{{1, 2, 3, 4}, 0xCAFE};
    auto poll = [&] { return PollController(true, source, retained, fields); };
    auto fieldsUntouched = [&] {
        return fields.axes == std::array<std::uint8_t, 4>{1, 2, 3, 4}
            && fields.buttons == 0xCAFE;
    };
    Require(PollController(false, source, retained, fields) == 0u && !source.calls &&
        fieldsUntouched(), "disabled input called host or published fields");
    Require(!poll() && source.calls == 1 && fieldsUntouched(), "unavailable device");
    source.next = ControllerRead{0, ControllerSample{0, 127, 128}};
    Require(!poll() && !retained && fieldsUntouched(), "used sample from a zero return");
    for (std::uint32_t low = 0; low <= 65535; ++low) {
        source.next = ControllerRead{1, ControllerSample{0xFFFF0000u | low, 255, 0}};
        auto value = poll();
        Require(value == ExpectedButtons(low) && fields.buttons == *value &&
            fields.axes == std::array<std::uint8_t, 4>{128, 128, 255, 0},
            "source button permutation/axis transfer");
    }
    source.next = ControllerRead{-1, ControllerSample{0x2010, 100, 202}};
    Require(poll() == 0x11u && fields.axes[2] == 100 && fields.axes[3] == 202,
        "source consumes supplied bytes on any nonzero return");
    source.next.reset();
    Require(!poll() && retained && retained->buttons == 0x2010, "host failure changed retention");
    source.next = ControllerRead{0, ControllerSample{0, 50, 60}};
    Require(poll() == 0x11u && fields.axes[2] == 100 && fields.axes[3] == 202,
        "zero result did not retain the prior sample");
    source.next = ControllerRead{-1, std::nullopt};
    Require(!poll() && !retained && fields.buttons == 0x11 && fields.axes[2] == 100,
        "unknown bytes fabricated on failed nonzero read");
}
}

int main() {
    try {
        Run();
        CheckWindowsPad();
        std::puts("Controller poll passed: 65536 Windows and 65536 source button combinations, retention and reconnect.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

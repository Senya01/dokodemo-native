#pragma once
#include <array>
#include <cstdint>

namespace dokodemo::recovered {
enum ControllerButton : uint32_t {
    Select = 0x000001,
    Start = 0x000008,
    Up = 0x000010,
    Right = 0x000020,
    Down = 0x000040,
    Left = 0x000080,
    LTrigger = 0x000100,
    RTrigger = 0x000200,
    Triangle = 0x001000,
    Circle = 0x002000,
    Cross = 0x004000,
    Square = 0x008000
};
struct ControllerSample {
    uint32_t buttons = 0, pressed = 0, released = 0;
    uint8_t analog_x = 128, analog_y = 128;
    uint64_t timestamp_us = 0;
};
class ControllerState {
  public:
    int32_t SetSamplingMode(int32_t mode) noexcept;
    int32_t SetSamplingCycle(int32_t cycle) noexcept;
    ControllerSample Update(uint32_t buttons, uint8_t x, uint8_t y, uint64_t timestamp_us) noexcept;
    int32_t SamplingMode() const noexcept {
        return mode_;
    }
    int32_t SamplingCycle() const noexcept {
        return cycle_;
    }

  private:
    int32_t mode_ = 0, cycle_ = 0;
    uint32_t buttons_ = 0;
};
struct GameInputFrame {
    uint32_t buttons = 0;
    bool focused = false, test_injected = false;
    uint64_t sequence = 0;
};
// One prompt consumes only a focused release followed by a fresh press. A held
// button at prompt creation/refocus cannot confirm it or a following prompt.
class GameButtonGate {
  public:
    void Begin() noexcept {
        active_ = true;
        armed_ = false;
    }
    void Stop() noexcept {
        active_ = armed_ = false;
    }
    bool Observe(const GameInputFrame &frame, uint32_t mask) noexcept;
    bool Armed() const noexcept {
        return armed_;
    }

  private:
    bool active_ = false, armed_ = false;
};
// Caller synchronizes the mailbox. Overflow inserts a focus-loss resync rather
// than silently retaining an edge that could acknowledge the wrong prompt.
class GameInputMailbox {
  public:
    void Push(GameInputFrame frame) noexcept;
    bool Pop(GameInputFrame &frame) noexcept;
    void Clear() noexcept {
        head_ = count_ = 0;
        resync_ = false;
    }

  private:
    std::array<GameInputFrame, 128> frames_{};
    uint32_t head_ = 0, count_ = 0;
    bool resync_ = false;
};
} // namespace dokodemo::recovered

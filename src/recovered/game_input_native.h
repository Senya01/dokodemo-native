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
    int32_t SamplingMode() const noexcept { return mode_; }
    int32_t SamplingCycle() const noexcept { return cycle_; }

  private:
    int32_t mode_ = 0, cycle_ = 0;
    uint32_t buttons_ = 0;
};
struct GameInputFrame {
    uint32_t buttons = 0;
    bool focused = false, test_injected = false;
    uint64_t sequence = 0;
    // Published Windows post-modal context at sampling time. Queued frames
    // from an earlier boundary cannot arm a newly created interaction.
    uint64_t interaction_generation = 0;
    uint64_t message_context = 0, message_owner_generation = 0;
    bool overflow_resync=false;
    uint64_t returned_owner=0,returned_generation=0;
};
// One prompt consumes only a focused release followed by a fresh press. A held
// button at prompt creation/refocus cannot confirm it or a following prompt.
class GameButtonGate {
  public:
    void Begin() noexcept {
        active_ = true;
        armed_ = false;
    }
    void Stop() noexcept { active_ = armed_ = false; }
    bool Observe(const GameInputFrame &frame, uint32_t mask) noexcept;
    bool Armed() const noexcept { return armed_; }

  private:
    bool active_ = false, armed_ = false;
};
// Native message boundaries use the published context at sampling time.
// A queued release/press for an older message cannot arm its successor.
class GameMessageInput {
  public:
    void Begin(uint64_t context,uint64_t owner)noexcept{context_=context;owner_=owner;active_=true;gate_.Begin();}
    void Stop()noexcept{active_=false;gate_.Stop();}
    bool Observe(const GameInputFrame& frame)noexcept{
        if(frame.overflow_resync){if(active_)gate_.Begin();++rejected_;return false;}
        if(frame.sequence<=sequence_){++rejected_;return false;}
        sequence_=frame.sequence;
        if(!active_||frame.message_context!=context_||frame.message_owner_generation!=owner_){++rejected_;return false;}
        if(!gate_.Observe(frame,Circle))return false;
        Stop();return true;
    }
    bool Armed()const noexcept{return gate_.Armed();}
    uint32_t Rejected()const noexcept{return rejected_;}
  private:
    GameButtonGate gate_;
    bool active_=false;
    uint64_t context_=0,owner_=0,sequence_=0;
    uint32_t rejected_=0;
};
// Worker-owned native input sampler for source counter consumers such as94F90.
// Raw queued frames are observed in order; counters advance once per game tick.
// A new UI context/refocus needs a fresh release. No stored dialog edge can
// become a title confirmation. Queued quick presses survive until one tick.
class GameMenuInput {
  public:
    GameMenuInput() { BeginContext(); }
    void BeginContext() noexcept;
    void Observe(const GameInputFrame &) noexcept;
    void Tick() noexcept;
    int32_t Counter(uint32_t single_button) const noexcept;
    bool First(uint32_t mask) const noexcept;
    uint32_t Repeat(uint32_t mask, uint32_t delay, uint32_t period) const noexcept;
    bool Focused() const noexcept { return focused_; }

  private:
    std::array<int32_t, 24> counters_{};
    uint32_t buttons_ = 0, armed_ = 0, active_ = 0, pending_ = 0;
    bool focused_ = false;
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

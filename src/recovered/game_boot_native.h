#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace dokodemo::recovered {
// Readable non-matching game boot. Source PCs identify the recovered caller;
// these are native objects, not PSP registers or addresses.
enum ControllerButton : uint32_t {
    Select = 0x000001, Start = 0x000008,
    Up = 0x000010, Right = 0x000020, Down = 0x000040, Left = 0x000080,
    LTrigger = 0x000100, RTrigger = 0x000200,
    Triangle = 0x001000, Circle = 0x002000, Cross = 0x004000, Square = 0x008000
};
struct ControllerSample {
    uint32_t buttons = 0, pressed = 0, released = 0;
    uint8_t analog_x = 128, analog_y = 128;
    uint64_t timestamp_us = 0;
};
class ControllerState {
public:
    // Native configuration returns the previous value. PSP timing and error
    // numbers are not claimed by this Windows adapter.
    int32_t SetSamplingMode(int32_t mode) noexcept;
    int32_t SetSamplingCycle(int32_t cycle) noexcept;
    ControllerSample Update(uint32_t buttons, uint8_t x, uint8_t y,
                            uint64_t timestamp_us) noexcept;
    int32_t SamplingMode() const noexcept { return mode_; }
    int32_t SamplingCycle() const noexcept { return cycle_; }
private:
    int32_t mode_ = 0, cycle_ = 0;
    uint32_t buttons_ = 0;
};
struct GameServiceResult {
    bool returned = false;
    int32_t value = 0;
};
class GameInputServices {
public:
    virtual ~GameInputServices() = default;
    virtual GameServiceResult SetControllerSamplingMode(int32_t mode) = 0;
    virtual GameServiceResult SetControllerSamplingCycle(int32_t cycle) = 0;
};
// 52D58..52D78: SetSamplingMode(1); SetSamplingCycle(0). The first
// numeric result is ignored. The caller at1004 ignores this function's result.
GameServiceResult InitializeGameInput52D58(GameInputServices& services);

// Native free-list allocator over an owning platform allocation's borrowed
// span. This replaces PSP libc/arena layout; it does not manufacture PSP
// pointers, metadata or knowledge of original padding bytes.
class GameArena {
public:
    bool Initialize(uint8_t* bytes, uint32_t capacity);
    void* Allocate(uint32_t bytes, uint32_t alignment = 16);
    bool Release(void* pointer);
    void Reset() noexcept;
    uint32_t Capacity() const noexcept { return capacity_; }
    uint32_t Used() const noexcept { return used_; }
private:
    struct Region { uint32_t offset, size; bool free; };
    uint8_t* bytes_ = nullptr;
    uint32_t capacity_ = 0, used_ = 0;
    std::vector<Region> regions_;
};
class GameMemoryServices {
public:
    virtual ~GameMemoryServices() = default;
    virtual GameServiceResult PrepareGameArena(uint32_t index, uint32_t bytes) = 0;
    virtual GameServiceResult SelectDefaultGameArena(uint32_t index) = 0;
};
// Recovered1234 allocation order; native services own the backing/controller
// objects. Final helper7CE28 must complete before the source return1 is present.
GameServiceResult InitializeGameMemory1234(GameMemoryServices& services);

struct GameArenaObservation {
    bool live = false;
    uint32_t requested_bytes = 0, source_coordinate = 0;
    uint64_t host_address = 0, committed_bytes = 0;
};
struct GameBootObservation {
    bool controller_setup_entered = false, controller_setup_returned = false;
    int32_t controller_setup_result = 0, sampling_mode = 0, sampling_cycle = 0;
    bool memory_initialization_entered = false, memory_initialization_returned = false;
    bool memory_initialization_succeeded = false;
    uint32_t default_arena = UINT32_MAX;
    std::array<GameArenaObservation, 3> arenas{};
};
} // namespace dokodemo::recovered

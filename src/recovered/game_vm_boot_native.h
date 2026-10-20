#pragma once
#include <array>
#include <chrono>
#include <cstdint>

namespace dokodemo::recovered {
class GameArena;
struct GameVmBootSnapshot {
    bool initialized = false, arena_live = false, timer_live = false, timer_started = false;
    bool singletons_reset = false, manager_ready = false;
    uint32_t arena_bytes = 0, manager_slots = 0, workspace_capacity = 0;
    uint64_t timer_elapsed_us = 0;
};
// Actual elapsed-time owner for the reached CreateVTimer/StartVTimer domain.
// No background timer, handler, PSP numeric UID or thread is fabricated.
class GameVmBoot final {
public:
    GameVmBoot() = default;
    GameVmBoot(const GameVmBoot&) = delete;
    GameVmBoot& operator=(const GameVmBoot&) = delete;
    ~GameVmBoot();
    bool Initialize74EF4(GameArena& parent);
    bool InitializeManager93404();
    void* Allocate(uint32_t bytes);
    bool Release(void* pointer);
    uint32_t AcquireTask();
    bool ReleaseTask(uint32_t index);
    GameVmBootSnapshot Snapshot() const;
    bool Close(); // after consumers finish; refuses live VM allocations/tasks
private:
    using Clock = std::chrono::steady_clock;
    struct Task { uint32_t next = UINT32_MAX; int32_t slot = -1; bool live = false; };
    struct Work { uint64_t object = 0; uint32_t operation = 0, argument = 0; };
    GameArena* parent_ = nullptr;
    void* backing_ = nullptr;
    // Defined out of line to keep platform/game arena ownership separate.
    struct ArenaOwner;
    ArenaOwner* arena_ = nullptr;
    Clock::time_point started_{};
    std::array<void*, 5> source_singletons_{};
    std::array<Task, 16> tasks_{};
    std::array<Work, 32> workspace_{};
    uint32_t free_task_ = UINT32_MAX;
    GameVmBootSnapshot state_;
};
} // namespace dokodemo::recovered

#include "game_vm_boot_native.h"
#include "game_boot_native.h"
#include <memory>

namespace dokodemo::recovered {
struct GameVmBoot::ArenaOwner { GameArena arena; };
GameVmBoot::~GameVmBoot() { (void)Close(); }
bool GameVmBoot::Initialize74EF4(GameArena& parent) {
    static_assert(Clock::is_steady, "VM timer requires a monotonic clock");
    if (parent_) return false;
    auto controller = std::make_unique<ArenaOwner>();
    auto* backing = parent.Allocate(0x40000);
    if (!backing) return false;
    parent_ = &parent; backing_ = backing; // ownership before fallible free-list creation
    arena_ = controller.release();
    if (!arena_->arena.Initialize(static_cast<uint8_t*>(backing), 0x40000)) { (void)Close(); return false; }
    source_singletons_.fill(nullptr); // reached74F10/28/30/38/40 reset, not old BSS inference
    started_ = Clock::now();
    state_ = {}; state_.initialized = state_.arena_live = state_.singletons_reset = true;
    state_.arena_bytes = 0x40000;
    state_.timer_live = state_.timer_started = true; return true;
}
bool GameVmBoot::InitializeManager93404() {
    if (!state_.initialized || state_.manager_ready) return false;
    for (uint32_t i = 0; i < tasks_.size(); ++i) tasks_[i] = {i + 1 < tasks_.size() ? i + 1 : UINT32_MAX, -1, false};
    free_task_ = 0;
    // Typed native control/work records replace PSP pointer and ctor layouts.
    // Their native zero initialization grants no PSP payload/padding knowledge.
    workspace_ = {};
    state_.manager_ready = true; state_.manager_slots = 16; state_.workspace_capacity = 32; return true;
}
void* GameVmBoot::Allocate(uint32_t bytes) { return state_.arena_live ? arena_->arena.Allocate(bytes) : nullptr; }
bool GameVmBoot::Release(void* pointer) { return state_.arena_live && arena_->arena.Release(pointer); }
uint32_t GameVmBoot::AcquireTask() {
    if (!state_.manager_ready || free_task_ == UINT32_MAX) return UINT32_MAX;
    const auto index = free_task_;
    auto& task = tasks_[index]; free_task_ = task.next; task.live = true; task.next = UINT32_MAX; return index;
}
bool GameVmBoot::ReleaseTask(uint32_t index) {
    if (!state_.manager_ready || index >= tasks_.size() || !tasks_[index].live) return false;
    auto& task = tasks_[index]; task.live = false; task.next = free_task_; task.slot = -1; free_task_ = index; return true;
}
GameVmBootSnapshot GameVmBoot::Snapshot() const {
    auto state = state_;
    if (state.timer_started)
        state.timer_elapsed_us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_).count());
    return state;
}
bool GameVmBoot::Close() {
    if (arena_ && arena_->arena.Used()) return false;
    for (const auto& task : tasks_) if (task.live) return false;
    if (parent_ && backing_ && !parent_->Release(backing_)) return false;
    state_ = Snapshot();
    state_.timer_started = state_.timer_live = state_.arena_live = state_.manager_ready = false;
    state_.arena_bytes = state_.manager_slots = state_.workspace_capacity = 0;
    delete arena_; arena_ = nullptr; parent_ = nullptr; backing_ = nullptr;
    source_singletons_.fill(nullptr); free_task_ = UINT32_MAX; return true;
}
} // namespace dokodemo::recovered

#include "game_boot_native.h"
#include <algorithm>
#include <cstddef>
#include <utility>

namespace dokodemo::recovered {
int32_t ControllerState::SetSamplingMode(int32_t mode) noexcept {
    if (mode != 0 && mode != 1) return -1;
    const auto previous = mode_;
    mode_ = mode;
    return previous;
}
int32_t ControllerState::SetSamplingCycle(int32_t cycle) noexcept {
    if (cycle != 0) return -1; // only the game's reached cycle0 is supported
    const auto previous = cycle_;
    cycle_ = cycle;
    return previous;
}
ControllerSample ControllerState::Update(uint32_t buttons, uint8_t x, uint8_t y,
                                        uint64_t timestamp_us) noexcept {
    ControllerSample out;
    out.buttons = buttons;
    out.pressed = buttons & ~buttons_;
    out.released = buttons_ & ~buttons;
    out.analog_x = mode_ == 1 ? x : uint8_t(128);
    out.analog_y = mode_ == 1 ? y : uint8_t(128);
    out.timestamp_us = timestamp_us;
    buttons_ = buttons;
    return out;
}
GameServiceResult InitializeGameInput52D58(GameInputServices& services) {
    const auto mode = services.SetControllerSamplingMode(1); //52D60; delay52D64
    if (!mode.returned) return {};
    return services.SetControllerSamplingCycle(0); //52D68; delay52D6C
}
bool GameArena::Initialize(uint8_t* bytes, uint32_t capacity) {
    if (bytes_ || !bytes || !capacity) return false;
    std::vector<Region> initial{{0, capacity, true}};
    regions_ = std::move(initial);
    bytes_ = bytes; capacity_ = capacity; used_ = 0;
    return true;
}
void* GameArena::Allocate(uint32_t bytes, uint32_t alignment) {
    if (!bytes_ || !bytes || !alignment || (alignment & (alignment - 1)) || alignment > 4096)
        return nullptr;
    for (size_t i = 0; i < regions_.size(); ++i) {
        const auto old = regions_[i];
        if (!old.free) continue;
        const auto address = reinterpret_cast<uintptr_t>(bytes_) + old.offset;
        const uint32_t padding = uint32_t((alignment - (address & (alignment - 1))) & (alignment - 1));
        if (uint64_t(padding) + bytes > old.size) continue;
        const uint32_t offset = old.offset + padding;
        // Work on a candidate so allocation failure cannot corrupt the list.
        auto candidate = regions_;
        candidate.erase(candidate.begin() + static_cast<ptrdiff_t>(i));
        auto at = candidate.begin() + static_cast<ptrdiff_t>(i);
        if (padding) { at = candidate.insert(at, {old.offset, padding, true}); ++at; }
        at = candidate.insert(at, {offset, bytes, false});
        const uint32_t tail = old.size - padding - bytes;
        if (tail) candidate.insert(at + 1, {offset + bytes, tail, true});
        regions_.swap(candidate); used_ += bytes;
        return bytes_ + offset;
    }
    return nullptr;
}
bool GameArena::Release(void* pointer) {
    if (!pointer) return true;
    const auto address = reinterpret_cast<uintptr_t>(pointer);
    const auto base = reinterpret_cast<uintptr_t>(bytes_);
    if (!bytes_ || address < base || address - base >= capacity_) return false;
    const auto offset = uint32_t(address - base);
    for (size_t i = 0; i < regions_.size(); ++i) {
        auto& region = regions_[i];
        if (region.offset != offset || region.free) continue;
        used_ -= region.size; region.free = true;
        if (i + 1 < regions_.size() && regions_[i + 1].free) {
            region.size += regions_[i + 1].size;
            regions_.erase(regions_.begin() + static_cast<ptrdiff_t>(i + 1));
        }
        if (i && regions_[i - 1].free) {
            regions_[i - 1].size += regions_[i].size;
            regions_.erase(regions_.begin() + static_cast<ptrdiff_t>(i));
        }
        return true;
    }
    return false;
}
void GameArena::Reset() noexcept {
    regions_.clear(); bytes_ = nullptr; capacity_ = 0; used_ = 0;
}
GameServiceResult InitializeGameMemory1234(GameMemoryServices& services) {
    constexpr std::array<uint32_t, 3> sizes{{0xB94000, 0x74E000, 0x46000}};
    for (uint32_t i = 0; i < sizes.size(); ++i) {
        const auto result = services.PrepareGameArena(i, sizes[i]);
        if (!result.returned) return {};
        if (!result.value) return {true, 0};
    }
    const auto selected = services.SelectDefaultGameArena(1); //1360 ->7CE28, PPK controller
    if (!selected.returned) return {};
    return {true, 1}; //136C
}
} // namespace dokodemo::recovered

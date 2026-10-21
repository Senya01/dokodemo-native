#include "recovered/game_boot_native.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct InputServices : GameInputServices {
    std::vector<int32_t> calls;
    GameServiceResult mode{true, 0}, cycle{true, 0};
    GameServiceResult SetControllerSamplingMode(int32_t value) override {
        calls.push_back(value);
        return mode;
    }
    GameServiceResult SetControllerSamplingCycle(int32_t value) override {
        calls.push_back(value);
        return cycle;
    }
};
struct MemoryServices : GameMemoryServices {
    int fail_at = -1, incomplete_at = -1;
    bool selected = false, selection_returns = true;
    std::vector<uint32_t> sizes;
    GameServiceResult PrepareGameArena(uint32_t index, uint32_t bytes) override {
        Check(index == sizes.size(), "arena order");
        sizes.push_back(bytes);
        return {int(index) != incomplete_at, int(index) != fail_at ? 1 : 0};
    }
    GameServiceResult SelectDefaultGameArena(uint32_t index) override {
        Check(index == 1, "7CE28 selects PPK, not main");
        selected = true;
        return {selection_returns, 0};
    }
};
void ControllerTests() {
    GameMenuInput menu;
    menu.Observe({Circle, true, false, 1});
    menu.Tick();
    Check(!menu.First(Circle), "TRC-held Circle cannot select a new TITLE context");
    menu.Observe({0, true, false, 2});
    menu.Observe({Circle, true, false, 3});
    menu.Tick();
    Check(menu.First(Circle) && menu.Counter(Circle) == 0, "source counter zero is a new first press");
    menu.Observe({Circle, true, false, 4});
    menu.Observe({Circle, true, false, 5});
    Check(menu.Counter(Circle) == 0, "polling frequency does not advance game counters");
    menu.Tick();
    Check(!menu.First(Circle) && menu.Counter(Circle) == 1, "held next game frame cannot reconfirm");
    menu.BeginContext();
    menu.Observe({Circle, true, false, 6});
    menu.Tick();
    Check(!menu.First(Circle), "context switch discards old first press");
    menu.Observe({0, true, false, 7});
    menu.Observe({Down, true, false, 8});
    menu.Tick();
    Check(menu.Repeat(Down, 10, 2) == 1, "94F90 first navigation press");
    for (unsigned i = 1; i <= 12; ++i) {
        menu.Tick();
        Check(menu.Repeat(Down, 10, 2) == (i == 10 || i == 12 ? 2u : 0u),
              "navigation delay/repeat uses game frames");
    }
    menu.Observe({0, false, false, 9});
    menu.Observe({Down, true, false, 10});
    menu.Tick();
    Check(!menu.First(Down), "held on refocus is suppressed");
    menu.Observe({0, true, false, 11});
    menu.Observe({Right, true, false, 12});
    menu.Observe({0, true, false, 13});
    menu.Tick();
    Check(menu.First(Right) && menu.Counter(Circle) == -1,
          "queued quick edge retained once,independent buttons remain released");
    menu.Tick();
    Check(!menu.First(Right), "quick release does not repeat");
    GameButtonGate gate;
    gate.Begin();
    Check(!gate.Observe({Circle, true, false, 1}, Circle), "held before prompt cannot confirm");
    Check(!gate.Observe({0, false, false, 2}, Circle) && !gate.Observe({Circle, true, false, 3}, Circle),
          "focus loss/refocus still held cannot confirm");
    Check(!gate.Observe({0, true, false, 4}, Circle) && gate.Armed(), "focused release arms prompt");
    Check(gate.Observe({Circle, true, false, 5}, Circle) && !gate.Observe({Circle, true, false, 6}, Circle),
          "fresh edge consumes exactly one prompt");
    gate.Begin();
    Check(!gate.Observe({Circle, true, false, 7}, Circle), "held key cannot accept next prompt");
    GameInputMailbox mailbox;
    for (uint64_t i = 0; i < 129; ++i)
        mailbox.Push({Circle, true, true, i});
    GameInputFrame frame;
    Check(mailbox.Pop(frame) && !frame.focused, "overflow requires safe input resync");
    Check(mailbox.Pop(frame) && frame.focused && frame.test_injected && frame.sequence == 128,
          "mailbox retains latest actual sample after resync");
    mailbox.Clear();
    Check(!mailbox.Pop(frame), "retired/next-prompt queue clears old edges");
    InputServices input;
    Check(InitializeGameInput52D58(input).returned && input.calls == std::vector<int32_t>{1, 0},
          "52D58 calls and delays");
    input.calls.clear();
    input.mode = {true, -1};
    Check(InitializeGameInput52D58(input).returned && input.calls.size() == 2,
          "numeric first return ignored");
    input.calls.clear();
    input.mode = {};
    Check(!InitializeGameInput52D58(input).returned && input.calls.size() == 1,
          "uncompleted service cannot return");
    input.calls.clear();
    input.mode = {true, 0};
    input.cycle = {};
    Check(!InitializeGameInput52D58(input).returned && input.calls.size() == 2,
          "uncompleted cycle cannot return");
    ControllerState state;
    Check(state.SetSamplingMode(1) == 0 && state.SetSamplingCycle(0) == 0, "previous native configuration");
    auto sample = state.Update(Cross | Right, 255, 1, 100);
    Check(sample.pressed == (Cross | Right) && !sample.released && sample.analog_x == 255 &&
              sample.timestamp_us == 100,
          "pressed and analog");
    sample = state.Update(Cross, 128, 128, 200);
    Check(!sample.pressed && sample.released == Right && sample.buttons == Cross, "held vs released");
    sample = state.Update(0, 128, 128, 300);
    Check(sample.released == Cross && !sample.buttons, "focus loss releases held keys");
    Check(state.SetSamplingMode(2) == -1 && state.SamplingMode() == 1, "invalid config leaves state");
    Check(state.SetSamplingCycle(123) == -1 && state.SamplingCycle() == 0, "unimplemented timing rejected");
    state.SetSamplingMode(0);
    sample = state.Update(0, 0, 255, 400);
    Check(sample.analog_x == 128 && sample.analog_y == 128, "digital mode centers analog");
}
void ArenaTests() {
    std::array<uint8_t, 4097> storage{};
    GameArena arena;
    Check(arena.Initialize(storage.data() + 1, 4096), "unaligned native backing accepted");
    auto *a = static_cast<uint8_t *>(arena.Allocate(100, 64));
    auto *b = static_cast<uint8_t *>(arena.Allocate(200, 128));
    Check(a && b && !(reinterpret_cast<uintptr_t>(a) % 64) && !(reinterpret_cast<uintptr_t>(b) % 128),
          "aligned disjoint allocation");
    Check(a + 100 <= b && arena.Used() == 300, "allocation extents");
    a[0] = 17;
    b[199] = 91;
    Check(!arena.Release(a + 1) && !arena.Release(storage.data()), "foreign/interior pointers refused");
    Check(!arena.Allocate(4097) && !arena.Allocate(1, 3) && arena.Used() == 300,
          "bounded failure preserves live allocations");
    Check(arena.Release(a) && !arena.Release(a) && b[199] == 91,
          "double free refused without disturbing peer");
    Check(arena.Release(b) && arena.Used() == 0, "free coalescing");
    auto *whole = arena.Allocate(4096, 1);
    Check(whole == storage.data() + 1 && !arena.Allocate(1), "coalesced whole capacity and exhaustion");
    Check(arena.Release(whole), "whole release");
    arena.Reset();
    Check(!arena.Capacity() && !arena.Allocate(1), "reset invalidates allocation API");
}
void MemoryTests() {
    MemoryServices memory;
    const auto result = InitializeGameMemory1234(memory);
    Check(result.returned && result.value == 1 && memory.selected &&
              memory.sizes == std::vector<uint32_t>{0xB94000, 0x74E000, 0x46000},
          "1234 native arena initialization");
    for (int i = 0; i < 3; ++i) {
        MemoryServices failed;
        failed.fail_at = i;
        const auto failure = InitializeGameMemory1234(failed);
        Check(failure.returned && failure.value == 0 && !failed.selected &&
                  failed.sizes.size() == size_t(i + 1),
              "allocation failure stops later producers");
        MemoryServices stopped;
        stopped.incomplete_at = i;
        Check(!InitializeGameMemory1234(stopped).returned && !stopped.selected,
              "host interruption has no source result");
    }
    MemoryServices incomplete;
    incomplete.selection_returns = false;
    Check(!InitializeGameMemory1234(incomplete).returned, "7CE28 must complete before return1");
}
} // namespace
int main() {
    try {
        ControllerTests();
        ArenaTests();
        MemoryTests();
        std::cout << "game boot: controller, allocation ownership and startup "
                     "order PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

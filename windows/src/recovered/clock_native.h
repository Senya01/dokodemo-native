#pragma once
#include <cstdint>
#include <functional>

namespace dokodemo::recovered {
// BOOT15C4 returns v1:v0 from the CURRENT cache F5D88/F5D8C. This is
// the already shifted PSP microsecond tick, after E08 TickAddHours(+9).
// It is not a raw UTC sample and ClockScalar7C694 applies no timezone shift.
// returned=false is a native readiness boundary, never a fabricated PSP error.
struct ClockCallbackReturn {
    bool returned = false;
    uint64_t cached_psp_tick_us = 0;
};
using ClockProvider = std::function<ClockCallbackReturn()>;

// Separate premises for EAD64+segment1 callback slot and the installed15C4
// getter. A supplied function alone does not establish original initialization.
struct ClockDependency {
    bool callback_slot_mapped = false;
    bool installed_getter_returns_cache = false;
    ClockProvider provider;
    bool available() const {
        return callback_slot_mapped && installed_getter_returns_cache &&
               static_cast<bool>(provider);
    }
};

// Unsigned v1:v0 / 60000000; low32 quotient + C15558A0, wrapping32.
// The original then packs tag3 and low28 in builtin09's argc0 path.
uint32_t ClockScalar7C694(uint64_t cached_psp_tick_us);
uint32_t ClockWord689B8(uint64_t cached_psp_tick_us);
} // namespace dokodemo::recovered

#pragma once

#include "recovered/clock_native.h"
#include <array>
#include <cstdint>

namespace dokodemo::platform {

// This is a native platform adapter, with the PSP RTC year1/microsecond epoch
// as an explicit imported ABI premise. No resident firmware equivalence is
// claimed. E08's fixed+9h is applied once, before the15C4-shaped cached getter.
bool FileTimeUtcToCachedPspTick(std::uint64_t filetime_100ns,
                              std::uint64_t& cached_tick_us) noexcept;

struct WindowsClockSnapshot {
    bool cache_ready = false;
    std::uint64_t filetime_100ns = 0;
    std::uint64_t cached_psp_tick_us = 0;
    std::uint64_t updates = 0;
    // SetTick16B: sixu16 fields thenu32 microseconds; DE0's28B calendar is
    // [zero,second,minute,hour,day,month,year]. Separate from profile getters.
    std::array<std::uint8_t,16> psp_date_bytes{};
    std::array<std::uint32_t,7> calendar_words{};
};

// One application-thread-owned cache. Reading it does not read the OS clock
// again. The default is unavailable; a real Windows sample explicitly updates
// this owned cache. This installs no PSP globals and enables no HostAbi flags.
class WindowsClockCache final {
public:
    bool RefreshFromWindows() noexcept;
    recovered::ClockCallbackReturn ReadCached() const noexcept;
    WindowsClockSnapshot Snapshot() const noexcept { return state_; }
private:
    WindowsClockSnapshot state_{};
};

} // namespace dokodemo::platform

#pragma once
#include <array>
#include <cstdint>

namespace dokodemo::platform {
// Imported RTC ABI, not a resident PSP firmware proof. Unlike E08 clock cache,
// initialization101C converts UTC to local time before GetTime_t and C321C.
struct RngSeedConversion {
    std::uint64_t utc_tick_us = 0;
    std::uint64_t local_tick_us = 0;
    std::uint32_t local_civil_epoch_seconds = 0;
    std::array<std::uint8_t,4> source_sp20_bytes{};
};
// Successful Gregorian local civil time1970..2037 only. Offset comes from an
// actual timezone conversion at this instant; it is never a fixed+9 default.
// On refusal output is unchanged. No state is seeded by this conversion alone.
bool RngSeedFromUtcFileTimeAndOffset(std::uint64_t filetime_100ns,
                                    std::int32_t effective_offset_minutes,
                                    RngSeedConversion& output) noexcept;

struct WindowsRngInitializationSample {
    bool available = false;
    bool precise_sample_observed = false; // void Win32 API, no fabricated status
    bool utc_date_returned = false;
    bool timezone_returned = false;
    bool local_date_returned = false;
    bool civil_filetime_returned = false;
    std::uint32_t timezone_query_result = UINT32_MAX;
    std::uint32_t last_error = 0;
    std::uint64_t filetime_100ns = 0;
    std::int32_t effective_offset_minutes = 0;
    std::int32_t bias_minutes = 0;
    std::int32_t standard_bias_minutes = 0;
    std::int32_t daylight_bias_minutes = 0;
    bool dynamic_daylight_disabled = false;
    std::array<char,512> timezone_key_utf8{};
    std::array<std::uint16_t,8> standard_transition{};
    std::array<std::uint16_t,8> daylight_transition{};
    std::array<std::uint8_t,16> utc_date_bytes{};
    std::array<std::uint8_t,16> local_date_bytes{};
    RngSeedConversion conversion{};
};
// Reads one real UTC instant and dynamic Windows timezone. Does not install
// game globals, initialize an RNG owner or infer intervening callback lifetime.
// Native error refusal differs from source ignored-status partial writes.
bool CaptureWindowsRngInitialization(WindowsRngInitializationSample& output) noexcept;
} // namespace dokodemo::platform

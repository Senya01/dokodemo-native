#pragma once

#include <cstdint>

namespace dokodemo::save {

struct ClockReading {
    std::int64_t utc_ms = 0; // Unix epoch milliseconds, UTC.
    std::uint64_t monotonic_ms = 0; // Only meaningful within one clock-source session.
    std::int32_t utc_offset_minutes = 0;
};

ClockReading ReadSystemClock();

struct CivilTime {
    std::int32_t year = 1970;
    std::uint32_t month = 1, day = 1, hour = 0, minute = 0, second = 0, millisecond = 0;
    std::uint32_t weekday = 4; // Sunday=0. Proleptic Gregorian calendar.
};

// Offset is explicit; the module does not guess a historical timezone/DST rule.
// Returns false for offsets outside [-840,840] or addition overflow.
bool ToCivilTime(std::int64_t utc_ms, std::int32_t utc_offset_minutes, CivilTime& output);

struct ClockObservation {
    std::uint64_t elapsed_monotonic_ms = 0;
    bool first_sample = true;
    bool monotonic_reset = false;
    bool wall_clock_went_backwards = false;
    bool wall_clock_discontinuity = false;
    bool utc_offset_changed = false;
};

// Ephemeral tracker. Start fresh after loading a save: a persisted monotonic
// reading is not reused. No offline rewards, daily events, aging or diary rules.
class ClockTracker {
public:
    ClockObservation Observe(const ClockReading& reading, std::uint64_t tolerance_ms = 2000);
    void Reset() noexcept { has_last_ = false; }
private:
    bool has_last_ = false;
    ClockReading last_{};
};

} // namespace dokodemo::save

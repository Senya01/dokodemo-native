#include "save_time.h"

#include <chrono>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dokodemo::save {

ClockReading ReadSystemClock() {
    ClockReading reading;
    reading.utc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    reading.monotonic_ms = elapsed < 0 ? 0 : static_cast<std::uint64_t>(elapsed);
#ifdef _WIN32
    TIME_ZONE_INFORMATION zone{};
    const auto status = GetTimeZoneInformation(&zone);
    if (status != TIME_ZONE_ID_INVALID) {
        const auto adjustment = status == TIME_ZONE_ID_DAYLIGHT ? zone.DaylightBias :
                                status == TIME_ZONE_ID_STANDARD ? zone.StandardBias : 0;
        reading.utc_offset_minutes = -(zone.Bias + adjustment);
    }
#endif
    return reading;
}

bool ToCivilTime(std::int64_t utc_ms, std::int32_t offset, CivilTime& output) {
    if (offset < -840 || offset > 840) return false;
    const std::int64_t shift = static_cast<std::int64_t>(offset) * 60000;
    if ((shift > 0 && utc_ms > std::numeric_limits<std::int64_t>::max()-shift) ||
        (shift < 0 && utc_ms < std::numeric_limits<std::int64_t>::min()-shift)) return false;
    const auto local = utc_ms + shift;
    std::int64_t days = local / 86400000;
    std::int64_t day_ms = local % 86400000;
    if (day_ms < 0) { day_ms += 86400000; --days; }
    CivilTime civil;
    civil.millisecond = static_cast<std::uint32_t>(day_ms % 1000);
    civil.second = static_cast<std::uint32_t>(day_ms / 1000 % 60);
    civil.minute = static_cast<std::uint32_t>(day_ms / 60000 % 60);
    civil.hour = static_cast<std::uint32_t>(day_ms / 3600000);
    civil.weekday = static_cast<std::uint32_t>((days % 7 + 11) % 7);
    // Count days in 400-year Gregorian cycles, with March as the first month.
    const auto march_days = days + 719468;
    const auto era = (march_days >= 0 ? march_days : march_days - 146096) / 146097;
    const auto cycle_day = march_days - era * 146097;
    const auto cycle_year = (cycle_day - cycle_day/1460 + cycle_day/36524 - cycle_day/146096) / 365;
    auto year = cycle_year + era * 400;
    const auto year_day = cycle_day - (365 * cycle_year + cycle_year/4 - cycle_year/100);
    const auto march_month = (5 * year_day + 2) / 153;
    civil.day = static_cast<std::uint32_t>(year_day - (153 * march_month+2)/5 + 1);
    civil.month = static_cast<std::uint32_t>(march_month + (march_month < 10 ? 3 : -9));
    year += civil.month <= 2;
    if (year < std::numeric_limits<std::int32_t>::min() || year > std::numeric_limits<std::int32_t>::max()) return false;
    civil.year = static_cast<std::int32_t>(year);
    output = civil;
    return true;
}

ClockObservation ClockTracker::Observe(const ClockReading& reading, std::uint64_t tolerance) {
    ClockObservation observation;
    if (has_last_) {
        observation.first_sample = false;
        observation.monotonic_reset = reading.monotonic_ms < last_.monotonic_ms;
        observation.elapsed_monotonic_ms = observation.monotonic_reset ? 0 : reading.monotonic_ms-last_.monotonic_ms;
        observation.wall_clock_went_backwards = reading.utc_ms < last_.utc_ms;
        observation.utc_offset_changed = reading.utc_offset_minutes != last_.utc_offset_minutes;
        // Ordered signed instants can be subtracted in unsigned arithmetic even
        // at INT64 extrema; the result spans [0, UINT64_MAX].
        const auto magnitude = observation.wall_clock_went_backwards ?
            static_cast<std::uint64_t>(last_.utc_ms)-static_cast<std::uint64_t>(reading.utc_ms) :
            static_cast<std::uint64_t>(reading.utc_ms)-static_cast<std::uint64_t>(last_.utc_ms);
        const auto elapsed = observation.elapsed_monotonic_ms;
        const auto difference = magnitude > elapsed ? magnitude-elapsed : elapsed-magnitude;
        observation.wall_clock_discontinuity = observation.wall_clock_went_backwards ||
            observation.monotonic_reset || difference > tolerance;
    }
    last_ = reading;
    has_last_ = true;
    return observation;
}

} // namespace dokodemo::save

#pragma once
#include <array>
#include <cstdint>

namespace dokodemo::recovered {
// Exact caller record at BOOT689B8: year little-endian u16, then four u8
// fields and byte6 zero (68B7C delay slot). No host calendar interpretation.
using CalendarRecord = std::array<uint8_t, 7>;
CalendarRecord CalendarRecord689B8(const std::array<int32_t, 5>& fields);
// Source VA names, unrelocated BOOT coordinates. Inputs are raw record bytes;
// invalid civil dates are deliberately not normalized or rejected by these
// arithmetic functions because the source does neither.
uint32_t CalendarScalar7C580(const CalendarRecord& record);
uint32_t CalendarScalar7C660(const CalendarRecord& record);
} // namespace dokodemo::recovered

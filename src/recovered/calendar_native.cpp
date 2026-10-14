#include "calendar_native.h"

namespace dokodemo::recovered {
namespace {
int64_t signed32(uint32_t word) {
    return word & 0x80000000u ? static_cast<int64_t>(word) - 0x100000000LL : word;
}
uint32_t asr(uint32_t word, unsigned bits) {
    // Defined arithmetic shift, including negative values; no signed overflow
    // or implementation-defined right shift/conversion is used.
    return (word >> bits) | (word & 0x80000000u ? (~uint32_t(0) << (32 - bits)) : 0u);
}
uint32_t mult_hi(uint32_t left, uint32_t right) {
    const uint64_t product = static_cast<uint64_t>(signed32(left) * signed32(right));
    return static_cast<uint32_t>(product >> 32);
}
}
CalendarRecord CalendarRecord689B8(const std::array<int32_t, 5>& fields) {
    const uint32_t year = static_cast<uint32_t>(fields[0]);
    return {static_cast<uint8_t>(year), static_cast<uint8_t>(year >> 8),
            static_cast<uint8_t>(fields[1]), static_cast<uint8_t>(fields[2]),
            static_cast<uint8_t>(fields[3]), static_cast<uint8_t>(fields[4]), 0};
}
uint32_t CalendarScalar7C580(const CalendarRecord& record) {
    uint32_t month = record[2];
    uint32_t year = uint32_t(record[0]) | (uint32_t(record[1]) << 8);
    if (month < 3) { --year; month += 12; } //59C delay loads unsigned year.
    const uint32_t year_hi = mult_hi(year, 0x51EB851Fu); //5AC,5C0
    const uint32_t year_sign = asr(year, 31); //5BC
    const uint32_t month3 = month * 3u + 3u; //5B4..5C8
    uint32_t value = year * 365u + asr(year + (year_sign >> 30), 2);
    value -= asr(year_hi, 5) - year_sign;
    value += asr(year_hi, 7) - year_sign;
    value += month * 30u;
    value = value - 30u + (asr(mult_hi(month3, 0x66666667u), 1) - asr(month3, 31));
    value += record[3];
    value -= 3u; //62C
    value = value * 24u + record[4]; //634..640
    value = value * 60u + record[5]; //648..65C (JR delay returns v0)
    return value;
}
uint32_t CalendarScalar7C660(const CalendarRecord& record) {
    return CalendarScalar7C580(record) + 0xC14D4DE0u; //67C, wrap32
}
} // namespace dokodemo::recovered

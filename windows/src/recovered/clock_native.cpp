#include "clock_native.h"

namespace dokodemo::recovered {
uint32_t ClockScalar7C694(uint64_t cached_psp_tick_us) {
    return static_cast<uint32_t>(cached_psp_tick_us / UINT64_C(60000000)) +
           UINT32_C(0xC15558A0);
}
uint32_t ClockWord689B8(uint64_t cached_psp_tick_us) {
    return UINT32_C(0x30000000) |
           (ClockScalar7C694(cached_psp_tick_us) & UINT32_C(0x0FFFFFFF));
}
} // namespace dokodemo::recovered

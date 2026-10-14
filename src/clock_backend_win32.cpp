#include "clock_backend_win32.h"

#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#endif

namespace dokodemo::platform {

bool FileTimeUtcToCachedPspTick(std::uint64_t filetime_100ns,
                              std::uint64_t& cached_tick_us) noexcept {
    // Gregorian days in years1..1600:1600*365+400-16+4=584388.
    // FILETIME uses100ns since1601UTC; RTC ABI uses microseconds sinceyear1.
    constexpr std::uint64_t epoch_us = UINT64_C(50491123200000000);
    constexpr std::uint64_t source_hours9_us = UINT64_C(32400000000);
    const auto since1601_us = filetime_100ns / UINT64_C(10);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (since1601_us > maximum - epoch_us - source_hours9_us) return false;
    cached_tick_us = since1601_us + epoch_us + source_hours9_us;
    return true;
}

bool WindowsClockCache::RefreshFromWindows() noexcept {
#ifdef _WIN32
    FILETIME utc{};
    // The Win32 API returns void. Do not invent a GetLastError/PSP status code.
    GetSystemTimePreciseAsFileTime(&utc);
    const auto filetime = (static_cast<std::uint64_t>(utc.dwHighDateTime) << 32) |
                          static_cast<std::uint64_t>(utc.dwLowDateTime);
    std::uint64_t cached = 0;
    if (!FileTimeUtcToCachedPspTick(filetime, cached)) {
        state_.cache_ready = false;
        return false;
    }
    constexpr auto hours9_100ns = UINT64_C(324000000000);
    if (filetime > std::numeric_limits<std::uint64_t>::max() - hours9_100ns) {
        state_.cache_ready = false;
        return false;
    }
    const auto shifted_filetime = filetime + hours9_100ns;
    FILETIME shifted{};
    shifted.dwLowDateTime = static_cast<DWORD>(shifted_filetime);
    shifted.dwHighDateTime = static_cast<DWORD>(shifted_filetime >> 32);
    SYSTEMTIME calendar{};
    // Native admitted conversion uses the documented Win32 UTC decoder for
    // an explicitly shifted value. It never consults local timezone or DST.
    if (!FileTimeToSystemTime(&shifted, &calendar)) {
        state_.cache_ready = false;
        return false;
    }
    std::array<std::uint8_t,16> date{};
    const std::uint16_t fields[] = {calendar.wYear,calendar.wMonth,calendar.wDay,
        calendar.wHour,calendar.wMinute,calendar.wSecond};
    for (std::size_t i=0; i<6; ++i) {
        date[i*2]=static_cast<std::uint8_t>(fields[i]);
        date[i*2+1]=static_cast<std::uint8_t>(fields[i]>>8);
    }
    const auto microsecond=static_cast<std::uint32_t>(cached % UINT64_C(1000000));
    for (std::size_t i=0; i<4; ++i) date[12+i]=static_cast<std::uint8_t>(microsecond>>(8*i));
    state_.filetime_100ns = filetime;
    state_.cached_psp_tick_us = cached;
    state_.psp_date_bytes = date;
    state_.calendar_words = {0,calendar.wSecond,calendar.wMinute,calendar.wHour,
        calendar.wDay,calendar.wMonth,calendar.wYear};
    state_.cache_ready = true;
    ++state_.updates;
    return true;
#else
    // No Linux/backend support inferred from compiling the portable core.
    // Prior bytes are retained for diagnostics, but no callback value is given.
    state_.cache_ready = false;
    return false;
#endif
}

recovered::ClockCallbackReturn WindowsClockCache::ReadCached() const noexcept {
    return {state_.cache_ready, state_.cached_psp_tick_us};
}

} // namespace dokodemo::platform

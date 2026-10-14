#include "rng_backend_win32.h"
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
bool RngSeedFromUtcFileTimeAndOffset(std::uint64_t filetime_100ns,
                                    std::int32_t offset_minutes,
                                    RngSeedConversion& output) noexcept {
    constexpr std::uint64_t rtc_at1601 = UINT64_C(50491123200000000);
    constexpr std::uint64_t rtc_at1970 = UINT64_C(62135596800000000);
    constexpr std::uint64_t seconds_until2038 = UINT64_C(2145916800);
    if (filetime_100ns >= UINT64_C(0x8000000000000000) ||
        offset_minutes < -1440 || offset_minutes > 1440) return false;
    const auto utc = filetime_100ns / 10 + rtc_at1601;
    const auto delta = static_cast<std::int64_t>(offset_minutes) * INT64_C(60000000);
    std::uint64_t local = utc;
    if (delta < 0) {
        const auto magnitude = static_cast<std::uint64_t>(-delta);
        if (local < magnitude) return false;
        local -= magnitude;
    } else {
        const auto magnitude = static_cast<std::uint64_t>(delta);
        if (local > std::numeric_limits<std::uint64_t>::max() - magnitude) return false;
        local += magnitude;
    }
    if (local < rtc_at1970) return false;
    const auto seconds = (local - rtc_at1970) / UINT64_C(1000000);
    if (seconds >= seconds_until2038) return false;
    RngSeedConversion candidate{};
    candidate.utc_tick_us = utc;
    candidate.local_tick_us = local;
    candidate.local_civil_epoch_seconds = static_cast<std::uint32_t>(seconds);
    for (std::size_t i=0; i<4; ++i)
        candidate.source_sp20_bytes[i] = static_cast<std::uint8_t>(seconds >> (i*8));
    output = candidate;
    return true;
}

#ifdef _WIN32
namespace {
std::uint64_t Bits(const FILETIME& ft) noexcept {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime)<<32) | ft.dwLowDateTime;
}
std::array<std::uint16_t,8> Transition(const SYSTEMTIME& t) noexcept {
    return {t.wYear,t.wMonth,t.wDayOfWeek,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds};
}
std::array<std::uint8_t,16> Date16(const SYSTEMTIME& t,std::uint32_t microsecond) noexcept {
    std::array<std::uint8_t,16> result{};
    const std::uint16_t fields[]={t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond};
    for (std::size_t i=0;i<6;++i) {
        result[2*i]=static_cast<std::uint8_t>(fields[i]);
        result[2*i+1]=static_cast<std::uint8_t>(fields[i]>>8);
    }
    for (std::size_t i=0;i<4;++i)
        result[12+i]=static_cast<std::uint8_t>(microsecond>>(8*i));
    return result;
}
} // namespace
#endif

bool CaptureWindowsRngInitialization(WindowsRngInitializationSample& output) noexcept {
    WindowsRngInitializationSample candidate{};
#ifdef _WIN32
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    candidate.precise_sample_observed=true;
    candidate.filetime_100ns=Bits(ft);
    SYSTEMTIME utc{},local{};
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    if (!FileTimeToSystemTime(&ft,&utc)) {
        candidate.last_error=GetLastError(); output=candidate; return false;
    }
    candidate.utc_date_returned=true;
    candidate.timezone_query_result=GetDynamicTimeZoneInformation(&zone);
    if (candidate.timezone_query_result==TIME_ZONE_ID_INVALID) {
        candidate.last_error=GetLastError(); output=candidate; return false;
    }
    candidate.timezone_returned=true;
    candidate.bias_minutes=zone.Bias;
    candidate.standard_bias_minutes=zone.StandardBias;
    candidate.daylight_bias_minutes=zone.DaylightBias;
    candidate.dynamic_daylight_disabled=zone.DynamicDaylightTimeDisabled!=FALSE;
    candidate.standard_transition=Transition(zone.StandardDate);
    candidate.daylight_transition=Transition(zone.DaylightDate);
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,zone.TimeZoneKeyName,-1,
            candidate.timezone_key_utf8.data(),static_cast<int>(candidate.timezone_key_utf8.size()),
            nullptr,nullptr)) {
        candidate.last_error=GetLastError(); output=candidate; return false;
    }
    if (!SystemTimeToTzSpecificLocalTimeEx(&zone,&utc,&local)) {
        candidate.last_error=GetLastError(); output=candidate; return false;
    }
    candidate.local_date_returned=true;
    // Treat already-local calendar fields as civil fields on the epoch axis.
    // Converting back through local->UTC here would cancel the source offset.
    FILETIME utc_civil{},local_civil{};
    if (!SystemTimeToFileTime(&utc,&utc_civil) || !SystemTimeToFileTime(&local,&local_civil)) {
        candidate.last_error=GetLastError(); output=candidate; return false;
    }
    candidate.civil_filetime_returned=true;
    const auto u=Bits(utc_civil),l=Bits(local_civil);
    if (u>=UINT64_C(0x8000000000000000) || l>=UINT64_C(0x8000000000000000)) {
        output=candidate; return false;
    }
    const auto delta=static_cast<std::int64_t>(l)-static_cast<std::int64_t>(u);
    constexpr std::int64_t minute100ns=INT64_C(600000000);
    if (delta%minute100ns!=0 || delta/minute100ns < -1440 || delta/minute100ns > 1440) {
        output=candidate; return false;
    }
    candidate.effective_offset_minutes=static_cast<std::int32_t>(delta/minute100ns);
    const auto microseconds=static_cast<std::uint32_t>((candidate.filetime_100ns/10)%1000000);
    candidate.utc_date_bytes=Date16(utc,microseconds);
    candidate.local_date_bytes=Date16(local,microseconds);
    if (!RngSeedFromUtcFileTimeAndOffset(candidate.filetime_100ns,
            candidate.effective_offset_minutes,candidate.conversion)) {
        output=candidate; return false;
    }
    candidate.available=true;
#endif
    output=candidate;
    return candidate.available;
}
} // namespace dokodemo::platform

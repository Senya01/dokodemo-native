#include "save_native.h"
#include "save_time.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace dokodemo::save;
namespace {
unsigned checks = 0;
void Check(bool test, const char* detail) {
    ++checks;
    if (!test) throw std::runtime_error(detail);
}
void Good(const Result& result) {
    ++checks;
    if (!result) throw std::runtime_error(result.message + " (OS=" + std::to_string(result.os_error) + ")");
}
std::vector<std::uint8_t> Bytes(const FoundationState& state) {
    std::vector<std::uint8_t> bytes; Good(Encode(state, bytes)); return bytes;
}
void Same(const FoundationState& a, const FoundationState& b) { Check(Bytes(a) == Bytes(b), "State differs after roundtrip"); }
FoundationState Sample() {
    FoundationState state;
    state.player_name_utf8 = u8"Сеня🌏";
    state.companion_name_utf8 = u8"トロ";
    state.character_key_utf8 = "NEKO";
    state.learned_words = {{17, u8"ありがとう", u8"ありがとう", -1},
                           {0xffffffffffffffffull, u8"ёжик", u8"е\u0308жик", 1791936000123ll}};
    state.diary_entries = {{3, 0, u8"今日は晴れ。\nПривет, 🌏!"}};
    state.last_observed_utc_ms = 1791936000123ll;
    state.last_observed_utc_offset_minutes = 180;
    state.observed_session_ms = 12345;
    return state;
}
void PatchCrc(std::vector<std::uint8_t>& bytes) {
    // Test-side checksum enables structurally invalid, checksum-valid fixtures.
    for (unsigned i = 20; i < 24; ++i) bytes[i] = 0;
    std::uint32_t crc = 0xffffffff;
    for (auto b : bytes) {
        crc ^= b;
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    crc ^= 0xffffffff;
    for (unsigned i = 0; i < 4; ++i) bytes[20+i] = static_cast<std::uint8_t>(crc >> (8*i));
}
void CodecTests(const FoundationState& sample) {
    auto bytes = Bytes(sample);
    FoundationState output;
    Good(Decode(bytes, output)); Same(sample, output);
    Check(output.learned_words[1].reading_utf8 == u8"е\u0308жик", "Unicode normalization changed reading");
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto corrupt = bytes; corrupt[i] ^= 1;
        output = sample;
        Check(!Decode(corrupt, output), "A single-byte corruption was accepted"); Same(sample, output);
    }
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin()+length);
        Check(!Decode(truncated, output), "Truncated state accepted");
    }
    auto future = bytes; future[8] = 2; PatchCrc(future);
    Check(Decode(future, output).error == Error::UnsupportedVersion, "Future schema accepted");
    auto flags = bytes; flags[16] = 2; PatchCrc(flags);
    Check(Decode(flags, output).error == Error::InvalidFormat, "Non-foundation flags accepted");
    auto invalid_length = bytes;
    for (unsigned i = 64; i < 68; ++i) invalid_length[i] = 255;
    PatchCrc(invalid_length);
    Check(Decode(invalid_length, output).error == Error::InvalidFormat, "Oversized string length accepted");
    auto invalid_utf8 = bytes; invalid_utf8[68] = 0xc0; PatchCrc(invalid_utf8);
    Check(Decode(invalid_utf8, output).error == Error::InvalidUtf8, "Invalid UTF-8 accepted");
    auto invalid = sample;
    for (const auto& value : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
                             std::string("\xf4\x90\x80\x80", 4), std::string("a\0b", 3), std::string("\xf0\x9f", 2)}) {
        invalid.player_name_utf8 = value;
        auto unchanged = bytes;
        Check(Encode(invalid, unchanged).error == Error::InvalidUtf8, "Invalid Unicode input accepted");
        Check(bytes == unchanged, "Failed encode changed output");
    }
    invalid = sample; invalid.last_observed_utc_offset_minutes = 841;
    Check(Validate(invalid).error == Error::InvalidState, "Invalid offset accepted");
    invalid = sample; invalid.player_name_utf8.assign(kMaxStringBytes+1, 'a');
    Check(Validate(invalid).error == Error::LimitExceeded, "Oversized input accepted");
    invalid = sample; invalid.learned_words.resize(kMaxRecords+1);
    Check(Validate(invalid).error == Error::LimitExceeded, "Too many records accepted");
    FoundationState empty;
    Good(Decode(Bytes(empty), output)); Same(empty, output);
    auto extreme = sample;
    extreme.last_observed_utc_ms = std::numeric_limits<std::int64_t>::min();
    extreme.learned_words[0].recorded_at_utc_ms = std::numeric_limits<std::int64_t>::max();
    extreme.observed_session_ms = std::numeric_limits<std::uint64_t>::max();
    extreme.last_observed_utc_offset_minutes = -840;
    Good(Decode(Bytes(extreme), output)); Same(extreme, output);
    std::mt19937 generator(0x53415645);
    for (unsigned i = 0; i < 500; ++i) {
        auto fuzz = bytes;
        const auto index = 32+generator()%(fuzz.size()-32);
        fuzz[index] ^= static_cast<std::uint8_t>(1+generator()%255);
        PatchCrc(fuzz);
        FoundationState parsed;
        auto result = Decode(fuzz, parsed);
        if (result) Good(Validate(parsed));
    }
    std::cout << "PASS codec: UTF-8 roundtrip, corruption/truncation, schema, limits, fuzz\n";
}
void DiskTests(const std::filesystem::path& directory, const FoundationState& sample) {
    const auto path = directory / std::filesystem::u8path(u8"профиль-トロ.dknstate");
    Good(SaveAtomic(path, sample));
    FoundationState output; Good(Load(path, output)); Same(sample, output);
    auto replacement = sample; replacement.player_name_utf8 = u8"新しい名前";
    Good(SaveAtomic(path, replacement)); Good(Load(path, output)); Same(replacement, output);
    auto invalid = sample; invalid.player_name_utf8.assign("\xed\xa0\x80", 3);
    Check(!SaveAtomic(path, invalid), "Invalid state saved");
    Good(Load(path, output)); Same(replacement, output);
    HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(lock != INVALID_HANDLE_VALUE, "Could not lock target");
    const auto denied = SaveAtomic(path, sample);
    CloseHandle(lock);
    Check(denied.error == Error::IoError && denied.os_error != 0, "Locked publication did not fail explicitly");
    Good(Load(path, output)); Same(replacement, output);
    Check(!SaveAtomic(directory / "missing-parent" / "state.dknstate", sample), "Missing directory silently created");
    FoundationState sentinel = sample;
    Check(!Load(directory / "missing.dknstate", sentinel), "Missing file loaded"); Same(sentinel, sample);
    const auto directory_target = directory / "directory-target";
    std::filesystem::create_directories(directory_target);
    Check(!SaveAtomic(directory_target, sample), "Directory was replaced by native save");
    Check(std::filesystem::is_directory(directory_target), "Failed save removed a directory");
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        Check(entry.path().extension() != ".tmp", "Failed save leaked temporary file");
    Good(SaveAtomic(directory / "fixture-v1.dknstate", sample));
    std::cout << "PASS disk: Unicode path, create/replace, locked target preserves prior file, cleanup\n";
}
void AtomicReaderTest(const std::filesystem::path& directory, const FoundationState& sample) {
    const auto path = directory / "concurrent.dknstate";
    auto alternate = sample; alternate.diary_entries[0].text_utf8.assign(16384, 'x');
    const auto a = Bytes(sample), b = Bytes(alternate);
    Good(SaveAtomic(path, sample));
    std::atomic<bool> done{false}; std::atomic<unsigned> reads{0}, failures{0};
    std::atomic<std::uint32_t> read_error{0}, write_error{0};
    std::thread reader([&] {
        while (!done.load()) {
            FoundationState current;
            const auto loaded = Load(path, current);
            if (!loaded) { read_error = loaded.os_error; ++failures; continue; }
            std::vector<std::uint8_t> data;
            if (!Encode(current, data) || (data != a && data != b)) ++failures;
            ++reads;
        }
    });
    for (unsigned i = 0; i < 40; ++i) {
        const auto saved = SaveAtomic(path, (i & 1) ? sample : alternate);
        if (!saved) { write_error = saved.os_error; ++failures; }
    }
    done = true; reader.join();
    Check(reads > 0, "Atomic reader did not observe any file");
    if (failures) std::cerr << "Atomic test failures=" << failures << " read OS=" << read_error << " write OS=" << write_error << '\n';
    Check(failures == 0, "Reader saw a partial/corrupt state during replacement");
    std::cout << "PASS atomic reader: " << reads << " complete snapshots during 40 replacements\n";
}
void TimeTests() {
    CivilTime civil;
    Check(ToCivilTime(0, 0, civil) && civil.year == 1970 && civil.month == 1 && civil.day == 1 && civil.weekday == 4,
          "Epoch civil time failed");
    Check(ToCivilTime(-1, 0, civil) && civil.year == 1969 && civil.month == 12 && civil.day == 31 &&
          civil.hour == 23 && civil.minute == 59 && civil.second == 59 && civil.millisecond == 999,
          "Negative epoch flooring failed");
    Check(ToCivilTime(1709164800000ll, 0, civil) && civil.year == 2024 && civil.month == 2 && civil.day == 29,
          "Leap day failed");
    Check(ToCivilTime(4107542400000ll, 0, civil) && civil.year == 2100 && civil.month == 3 && civil.day == 1,
          "Century non-leap day failed");
    Check(ToCivilTime(0, -60, civil) && civil.day == 31 && civil.hour == 23, "Timezone previous-day crossing failed");
    Check(ToCivilTime(86399000, 180, civil) && civil.day == 2 && civil.hour == 2 && civil.second == 59,
          "Timezone next-day crossing failed");
    Check(!ToCivilTime(0, 841, civil), "Out-of-range timezone accepted");
    Check(!ToCivilTime(std::numeric_limits<std::int64_t>::max(), 1, civil), "UTC addition overflow accepted");
    Check(!ToCivilTime(std::numeric_limits<std::int64_t>::min(), -1, civil), "UTC addition underflow accepted");
    Check(ToCivilTime(std::numeric_limits<std::int64_t>::min(), 0, civil), "Minimum epoch failed");
    Check(ToCivilTime(std::numeric_limits<std::int64_t>::max(), 0, civil), "Maximum epoch failed");
    ClockTracker tracker;
    Check(tracker.Observe({100000, 1000, 180}).first_sample, "First observation failed");
    auto observation = tracker.Observe({101000, 2000, 180});
    Check(!observation.first_sample && observation.elapsed_monotonic_ms == 1000 && !observation.wall_clock_discontinuity,
          "Normal monotonic elapsed failed");
    observation = tracker.Observe({100000, 3000, 180});
    Check(observation.wall_clock_went_backwards && observation.elapsed_monotonic_ms == 1000,
          "Backward wall clock damaged monotonic elapsed");
    observation = tracker.Observe({100001, 1, 120});
    Check(observation.monotonic_reset && observation.elapsed_monotonic_ms == 0 && observation.utc_offset_changed,
          "Clock reset/offset change failed");
    observation = tracker.Observe({200001, 2, 120});
    Check(observation.wall_clock_discontinuity, "Forward wall clock jump unreported");
    tracker.Reset();
    Check(tracker.Observe({-1, 0, 0}).first_sample, "Tracker reset failed");
    observation = tracker.Observe({std::numeric_limits<std::int64_t>::max(), 1, 0});
    Check(observation.wall_clock_discontinuity, "Extreme signed wall delta overflowed");
    auto reading = ReadSystemClock();
    Check(reading.utc_ms > 0 && reading.utc_offset_minutes >= -840 && reading.utc_offset_minutes <= 840,
          "Windows system clock sample failed");
    Check(ReadSystemClock().monotonic_ms >= reading.monotonic_ms, "System steady clock decreased");
    std::cout << "PASS time: epoch/leap/century/offset/overflow and explicit clock changes\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("research/saves/testdata");
        std::filesystem::create_directories(directory);
        const auto sample = Sample();
        CodecTests(sample); DiskTests(directory, sample); AtomicReaderTest(directory, sample); TimeTests();
        std::ofstream receipt(directory / "test-result.json", std::ios::binary);
        receipt << "{\"status\":\"pass\",\"checks\":" << checks+1 << ",\"native_foundation_only\":true}\n";
        Check(static_cast<bool>(receipt), "Could not write test receipt");
        std::cout << "PASS " << checks << " checks; native foundation only, no PSP save parity\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}

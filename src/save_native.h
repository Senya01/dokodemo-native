#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dokodemo::save {

// Native research foundation only. These structs are NOT original PSP layouts.
// UTF-8 is preserved byte-for-byte, without normalization or lossy conversion.
struct LearnedWord {
    std::uint64_t native_record_id = 0; // App-owned identity; not a PSP dictionary ID.
    std::string spelling_utf8;
    std::string reading_utf8;
    std::int64_t recorded_at_utc_ms = 0;
};

struct DiaryEntry {
    std::uint64_t native_record_id = 0;
    std::int64_t recorded_at_utc_ms = 0;
    std::string text_utf8; // Supplied by caller; this module never generates diary text.
};

struct FoundationState {
    std::string player_name_utf8;
    std::string companion_name_utf8;
    std::string character_key_utf8; // e.g. resource tag "NEKO"; no original enum assumed.
    std::vector<LearnedWord> learned_words;
    std::vector<DiaryEntry> diary_entries;
    std::int64_t last_observed_utc_ms = 0;
    std::int32_t last_observed_utc_offset_minutes = 0;
    std::uint64_t observed_session_ms = 0; // Native measured uptime, not game progress.
};

enum class Error {
    None, InvalidUtf8, LimitExceeded, InvalidState, BadMagic,
    UnsupportedVersion, InvalidFormat, ChecksumMismatch, IoError
};

struct Result {
    Error error = Error::None;
    std::string message;
    std::uint32_t os_error = 0;
    explicit operator bool() const noexcept { return error == Error::None; }
};

inline constexpr std::uint16_t kFormatVersion = 1;
inline constexpr std::uint32_t kMaxFileBytes = 16 * 1024 * 1024;
inline constexpr std::uint32_t kMaxRecords = 4096;
inline constexpr std::uint32_t kMaxStringBytes = 64 * 1024;

Result Validate(const FoundationState& state);
// Output is unchanged on validation/decode failure. CRC32 detects accidental
// corruption; it is not authentication. Future versions fail explicitly.
Result Encode(const FoundationState& state, std::vector<std::uint8_t>& output);
Result Decode(const std::vector<std::uint8_t>& bytes, FoundationState& output);
Result Load(const std::filesystem::path& path, FoundationState& output);
// Windows: exclusive temp creation in destination directory, full write,
// FlushFileBuffers, then MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH).
// Sharing/access rename failures are retried for at most 31ms.
// No directory creation. On failure the previous target is not deliberately
// deleted/truncated. Concurrent writers are last-successful-rename-wins.
Result SaveAtomic(const std::filesystem::path& path, const FoundationState& state);

} // namespace dokodemo::save

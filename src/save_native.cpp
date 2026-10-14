#include "save_native.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <utility>

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
namespace {
constexpr std::array<std::uint8_t, 8> magic{{'D','K','N','S','T','A','T','E'}};
constexpr std::array<std::uint8_t, 32> source_iso{{
    0xc7,0x8d,0x09,0x74,0xf6,0x60,0xcd,0x92,0xc1,0xb1,0x0a,0x14,0x66,0xac,0x27,0x86,
    0x25,0xd3,0x8e,0x4d,0xd9,0x3b,0xfb,0x48,0xf6,0x6f,0x09,0x0f,0xf7,0xa4,0xc1,0x19}};
constexpr std::uint32_t header_size = 32;
constexpr std::uint32_t foundation_flag = 1;

Result Fail(Error code, const char* text) { return {code, text, 0}; }

bool Utf8(const std::string& value) {
    std::size_t at = 0;
    while (at < value.size()) {
        const auto lead = static_cast<unsigned char>(value[at++]);
        if (lead == 0) return false;
        if (lead < 0x80) continue;
        unsigned count = 0;
        std::uint32_t point = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; point = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; point = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; point = lead & 7; minimum = 0x10000; }
        else return false;
        if (value.size() - at < count) return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto next = static_cast<unsigned char>(value[at++]);
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}

std::uint32_t Crc(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t c = 0xffffffff;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto b = (i >= 20 && i < 24) ? 0 : bytes[i];
        c ^= b;
        for (unsigned bit = 0; bit < 8; ++bit) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return c ^ 0xffffffff;
}

void Put(std::vector<std::uint8_t>& bytes, std::uint64_t value, unsigned length) {
    for (unsigned i = 0; i < length; ++i) { bytes.push_back(static_cast<std::uint8_t>(value)); value >>= 8; }
}
void Text(std::vector<std::uint8_t>& bytes, const std::string& value) {
    Put(bytes, value.size(), 4);
    bytes.insert(bytes.end(), value.begin(), value.end());
}
void Patch32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) { bytes[offset+i] = static_cast<std::uint8_t>(value); value >>= 8; }
}
std::uint64_t Peek(const std::vector<std::uint8_t>& bytes, std::size_t offset, unsigned length) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < length; ++i) value |= std::uint64_t(bytes[offset+i]) << (8*i);
    return value;
}
// Explicit two's-complement interpretation avoids implementation-defined casts.
std::int64_t Signed64(std::uint64_t value) {
    if (value <= std::uint64_t(std::numeric_limits<std::int64_t>::max())) return static_cast<std::int64_t>(value);
    return -1 - static_cast<std::int64_t>(~value);
}
std::int32_t Signed32(std::uint32_t value) {
    if (value <= std::uint32_t(std::numeric_limits<std::int32_t>::max())) return static_cast<std::int32_t>(value);
    return -1 - static_cast<std::int32_t>(~value);
}

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t at;
    bool number(std::uint64_t& out, unsigned size) {
        if (bytes.size()-at < size) return false;
        out = Peek(bytes, at, size); at += size; return true;
    }
    bool string(std::string& out) {
        std::uint64_t length = 0;
        if (!number(length, 4) || length > kMaxStringBytes || length > bytes.size()-at) return false;
        out.assign(reinterpret_cast<const char*>(bytes.data()+at), static_cast<std::size_t>(length));
        at += static_cast<std::size_t>(length); return true;
    }
};

std::uint64_t Size(const FoundationState& state) {
    std::uint64_t size = header_size + source_iso.size() + 8 + 4 + 8 + 4 + 4;
    size += 12 + state.player_name_utf8.size() + state.companion_name_utf8.size() + state.character_key_utf8.size();
    for (const auto& w : state.learned_words) size += 24 + w.spelling_utf8.size() + w.reading_utf8.size();
    for (const auto& d : state.diary_entries) size += 20 + d.text_utf8.size();
    return size;
}

#ifdef _WIN32
Result Io(const char* action, DWORD error) { return {Error::IoError, action, error}; }
class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    bool close() {
        if (value_ == INVALID_HANDLE_VALUE) return true;
        const auto value = value_; value_ = INVALID_HANDLE_VALUE;
        return CloseHandle(value) != 0;
    }
private:
    HANDLE value_;
};
#endif
} // namespace

Result Validate(const FoundationState& state) {
    if (state.learned_words.size() > kMaxRecords || state.diary_entries.size() > kMaxRecords)
        return Fail(Error::LimitExceeded, "Native record limit exceeded");
    if (state.last_observed_utc_offset_minutes < -840 || state.last_observed_utc_offset_minutes > 840)
        return Fail(Error::InvalidState, "UTC offset outside [-840,840] minutes");
    auto check = [](const std::string& value) -> Result {
        if (value.size() > kMaxStringBytes) return Fail(Error::LimitExceeded, "Native string byte limit exceeded");
        if (!Utf8(value)) return Fail(Error::InvalidUtf8, "String must be strict UTF-8 without NUL");
        return {};
    };
    for (const auto* value : {&state.player_name_utf8, &state.companion_name_utf8, &state.character_key_utf8}) {
        auto result = check(*value); if (!result) return result;
    }
    for (const auto& word : state.learned_words) {
        auto result = check(word.spelling_utf8); if (!result) return result;
        result = check(word.reading_utf8); if (!result) return result;
    }
    for (const auto& diary : state.diary_entries) {
        auto result = check(diary.text_utf8); if (!result) return result;
    }
    if (Size(state) > kMaxFileBytes) return Fail(Error::LimitExceeded, "Native file byte limit exceeded");
    return {};
}

Result Encode(const FoundationState& state, std::vector<std::uint8_t>& output) {
    auto result = Validate(state); if (!result) return result;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(Size(state)));
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    Put(bytes, kFormatVersion, 2); Put(bytes, header_size, 2);
    Put(bytes, Size(state)-header_size, 4); Put(bytes, foundation_flag, 4);
    Put(bytes, 0, 4); Put(bytes, 0, 8);
    bytes.insert(bytes.end(), source_iso.begin(), source_iso.end());
    Text(bytes, state.player_name_utf8); Text(bytes, state.companion_name_utf8); Text(bytes, state.character_key_utf8);
    Put(bytes, static_cast<std::uint64_t>(state.last_observed_utc_ms), 8);
    Put(bytes, static_cast<std::uint32_t>(state.last_observed_utc_offset_minutes), 4);
    Put(bytes, state.observed_session_ms, 8);
    Put(bytes, state.learned_words.size(), 4);
    for (const auto& word : state.learned_words) {
        Put(bytes, word.native_record_id, 8); Put(bytes, static_cast<std::uint64_t>(word.recorded_at_utc_ms), 8);
        Text(bytes, word.spelling_utf8); Text(bytes, word.reading_utf8);
    }
    Put(bytes, state.diary_entries.size(), 4);
    for (const auto& diary : state.diary_entries) {
        Put(bytes, diary.native_record_id, 8); Put(bytes, static_cast<std::uint64_t>(diary.recorded_at_utc_ms), 8);
        Text(bytes, diary.text_utf8);
    }
    Patch32(bytes, 20, Crc(bytes));
    output.swap(bytes);
    return {};
}

Result Decode(const std::vector<std::uint8_t>& bytes, FoundationState& output) {
    if (bytes.size() > kMaxFileBytes) return Fail(Error::LimitExceeded, "Native file byte limit exceeded");
    if (bytes.size() < header_size) return Fail(Error::InvalidFormat, "Truncated native header");
    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) return Fail(Error::BadMagic, "Not native foundation state");
    if (Peek(bytes, 8, 2) != kFormatVersion) return Fail(Error::UnsupportedVersion, "Unsupported native state version");
    if (Peek(bytes, 10, 2) != header_size || Peek(bytes, 12, 4) != bytes.size()-header_size ||
        Peek(bytes, 16, 4) != foundation_flag || Peek(bytes, 24, 8) != 0)
        return Fail(Error::InvalidFormat, "Invalid native header/length/flags");
    if (Peek(bytes, 20, 4) != Crc(bytes)) return Fail(Error::ChecksumMismatch, "Native CRC32 mismatch");
    if (bytes.size() < header_size + source_iso.size() ||
        !std::equal(source_iso.begin(), source_iso.end(), bytes.begin()+header_size))
        return Fail(Error::InvalidFormat, "Native state source ISO mismatch");
    FoundationState state;
    Reader reader{bytes, header_size+source_iso.size()};
    std::uint64_t value = 0;
    auto malformed = [] { return Fail(Error::InvalidFormat, "Truncated/oversized native payload or trailing bytes"); };
    if (!reader.string(state.player_name_utf8) || !reader.string(state.companion_name_utf8) ||
        !reader.string(state.character_key_utf8) || !reader.number(value, 8)) return malformed();
    state.last_observed_utc_ms = Signed64(value);
    if (!reader.number(value, 4)) return malformed();
    state.last_observed_utc_offset_minutes = Signed32(static_cast<std::uint32_t>(value));
    if (!reader.number(value, 8)) return malformed();
    state.observed_session_ms = value;
    if (!reader.number(value, 4)) return malformed();
    if (value > kMaxRecords) return Fail(Error::LimitExceeded, "Native word record limit exceeded");
    const auto word_count = static_cast<std::uint32_t>(value);
    for (std::uint32_t i = 0; i < word_count; ++i) {
        LearnedWord word;
        if (!reader.number(word.native_record_id, 8) || !reader.number(value, 8)) return malformed();
        word.recorded_at_utc_ms = Signed64(value);
        if (!reader.string(word.spelling_utf8) || !reader.string(word.reading_utf8)) return malformed();
        state.learned_words.push_back(std::move(word));
    }
    if (!reader.number(value, 4)) return malformed();
    if (value > kMaxRecords) return Fail(Error::LimitExceeded, "Native diary record limit exceeded");
    const auto diary_count = static_cast<std::uint32_t>(value);
    for (std::uint32_t i = 0; i < diary_count; ++i) {
        DiaryEntry diary;
        if (!reader.number(diary.native_record_id, 8) || !reader.number(value, 8)) return malformed();
        diary.recorded_at_utc_ms = Signed64(value);
        if (!reader.string(diary.text_utf8)) return malformed();
        state.diary_entries.push_back(std::move(diary));
    }
    if (reader.at != bytes.size()) return malformed();
    auto result = Validate(state); if (!result) return result;
    output = std::move(state);
    return {};
}

Result Load(const std::filesystem::path& path, FoundationState& output) {
#ifdef _WIN32
    Handle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) return Io("CreateFileW for native load failed", GetLastError());
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.get(), &size)) return Io("GetFileSizeEx failed", GetLastError());
    if (size.QuadPart < 0 || size.QuadPart > kMaxFileBytes) return Fail(Error::LimitExceeded, "Native file byte limit exceeded");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    std::size_t at = 0;
    while (at < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(handle.get(), bytes.data()+at, static_cast<DWORD>(bytes.size()-at), &read, nullptr))
            return Io("ReadFile failed", GetLastError());
        if (!read) return Fail(Error::InvalidFormat, "Native file was truncated during read");
        at += read;
    }
    return Decode(bytes, output);
#else
    (void)path; (void)output;
    return Fail(Error::IoError, "Native disk backend requires Windows");
#endif
}

Result SaveAtomic(const std::filesystem::path& path, const FoundationState& state) {
    std::vector<std::uint8_t> bytes;
    auto result = Encode(state, bytes); if (!result) return result;
#ifdef _WIN32
    if (path.empty() || path.filename().empty()) return Fail(Error::InvalidState, "Native save path must name a file");
    std::error_code error;
    const auto target = std::filesystem::absolute(path, error);
    if (error) return {Error::IoError, "Cannot resolve native save path", static_cast<std::uint32_t>(error.value())};
    static std::atomic<std::uint64_t> sequence{0};
    std::filesystem::path temporary;
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = target;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) +
                     L"." + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
        raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (raw != INVALID_HANDLE_VALUE) break;
        const auto code = GetLastError();
        if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
            return Io("CreateFileW for exclusive native temporary failed", code);
    }
    if (raw == INVALID_HANDLE_VALUE) return Io("Native temporary-name attempts exhausted", ERROR_FILE_EXISTS);
    Handle handle(raw);
    auto abandon = [&](const char* action, DWORD code) -> Result {
        handle.close(); DeleteFileW(temporary.c_str()); return Io(action, code);
    };
    std::size_t at = 0;
    while (at < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(handle.get(), bytes.data()+at, static_cast<DWORD>(bytes.size()-at), &written, nullptr))
            return abandon("WriteFile for native save failed", GetLastError());
        if (!written) return abandon("Zero-length native save write", ERROR_WRITE_FAULT);
        at += written;
    }
    if (!FlushFileBuffers(handle.get())) return abandon("FlushFileBuffers for native save failed", GetLastError());
    if (!handle.close()) return abandon("CloseHandle for native save failed", GetLastError());
    // A replaced file object can remain open in a reader briefly. Windows may
    // report ACCESS_DENIED/SHARING_VIOLATION on the following rename until that
    // object is closed. Retry only those transient failures, for at most 31ms.
    // The previous target is never explicitly unlinked or overwritten in place.
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return {};
        const auto code = GetLastError();
        if ((code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION) || attempt == 31)
            return abandon("MoveFileExW for native save publication failed", code);
        Sleep(1);
    }
    return abandon("Native publication retry limit reached", ERROR_SHARING_VIOLATION);
#else
    (void)path;
    return Fail(Error::IoError, "Native disk backend requires Windows");
#endif
}

} // namespace dokodemo::save

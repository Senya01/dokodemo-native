#include "game_dictionary_native.h"
#include "game_boot_native.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::recovered {
GameDictionary::~GameDictionary() {
    (void)Close();
}
uint32_t GameDictionary::ReadBE32(uint32_t at) const {
    if (at > state_.bytes || state_.bytes - at < 4)
        throw std::out_of_range("NJ dictionary word");
    uint32_t word = 0;
    for (unsigned i = 0; i < 4; ++i)
        word = (word << 8) | (buffer_ ? buffer_[at + i] : data_.ReadU8(at + i));
    return word;
}
GameFsStatus GameDictionary::Attach(PackageView data, uint32_t mode) {
    if (state_.attached || (mode != 0 && mode != 3))
        return {GameFsCode::Busy, 0, "NJ binding mode/owner"};
    try {
        const auto bytes = data.Describe().size;
        if (bytes < 40 || bytes > UINT32_MAX)
            return {GameFsCode::MalformedDirectory, 0, "NJ file extent"};
        data_ = std::move(data);
        state_.bytes = uint32_t(bytes);
        const auto kind = ReadBE32(8);
        const bool supported =
            kind <= 3 || kind == 0x10000 || kind == 0x20001 || kind == 0x20002 || kind == 0xF0000;
        if (ReadBE32(0) != 0x4E4A4443 || ReadBE32(4) != 0x10000 ||
            !supported
            // 999CC/99AA8 read byte offsets20/24 DECIMAL (0x14/0x18),
            // not0x20/0x24. Those latter words are legitimate table offsets.
            || uint64_t(ReadBE32(12)) + ReadBE32(16) + 28 != bytes || ReadBE32(20) > 40 ||
            ReadBE32(24) > 40 || ReadBE32(uint32_t(bytes) - 4) != 0x4E4A4443) {
            data_.Reset();
            state_ = {};
            return {GameFsCode::MalformedDirectory, 0, "NJ header/version/kind/length validation"};
        }
        state_.attached = state_.header_valid = true;
        state_.mode = mode;
        state_.kind = kind;
        return {};
    } catch (const std::exception &error) {
        data_.Reset();
        state_ = {};
        return {GameFsCode::MalformedDirectory, 0, error.what()};
    }
}
bool GameDictionary::InitializeMutable(GameArena &arena, uint32_t mode) {
    if (state_.attached || (mode != 1 && mode != 2))
        return false;
    const uint32_t bytes = mode == 1 ? 32768u : 8762u;
    const uint32_t capacity = (bytes - 62) / (mode == 1 ? 34u : 87u), stride = mode == 1 ? 32u : 85u;
    auto *memory = static_cast<uint8_t *>(arena.Allocate(bytes));
    if (!memory)
        return false;
    arena_ = &arena;
    buffer_ = memory;
    state_ = {};
    state_.bytes = bytes;
    // Explicit native safety policy for bytes not touched by99694/9DBF4.
    // It is not a claim about the original allocator or unused PSP padding.
    std::fill_n(memory, bytes, uint8_t(0));
    const auto word = [memory](uint32_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            memory[at + i] = uint8_t(value >> (24 - i * 8));
    };
    word(0, 0x4E4A4443);
    word(4, 0x10000);
    word(8, mode == 1 ? 0x80020000 : 0x80030000);
    word(12, bytes - 56);
    word(16, 28);
    word(20, 40);
    word(24, 40);
    word(28, 56);
    word(32, 58 + 2 * capacity);
    word(36, 0);
    word(40, capacity);
    word(44, stride);
    word(48, 0);
    word(52, 0);
    word(bytes - 4, 0x4E4A4443);
    state_.attached = state_.mutable_owner = state_.header_valid = true;
    state_.kind = mode == 1 ? 0x80020000 : 0x80030000;
    state_.mode = mode;
    state_.capacity = capacity;
    state_.record_stride = stride;
    return true;
}
bool GameDictionary::Close() {
    data_.Reset();
    if (buffer_ && !arena_->Release(buffer_))
        return false;
    buffer_ = nullptr;
    arena_ = nullptr;
    state_ = {};
    return true;
}
} // namespace dokodemo::recovered

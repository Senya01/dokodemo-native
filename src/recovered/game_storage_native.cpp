#include "game_storage_native.h"
#include <algorithm>
#include <stdexcept>
namespace dokodemo::recovered {
namespace {
constexpr uint8_t magic[]{'D', 'D', 'I', 'N', 'M', 'W', 'P', '1'};
uint32_t Crc(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}
uint32_t Word(const std::vector<uint8_t> &b, size_t at) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= uint32_t(b.at(at + i)) << (8 * i);
    return v;
}
void Write(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = uint8_t(v >> (8 * i));
}
} // namespace
std::vector<uint8_t> EncodeNativeSave(const std::vector<uint8_t> &payload) {
    if (payload.size() > 0x48000)
        throw std::length_error("native save payload exceeds reached buffer capacity");
    std::vector<uint8_t> out(16 + payload.size());
    std::copy(std::begin(magic), std::end(magic), out.begin());
    Write(out, 8, uint32_t(payload.size()));
    Write(out, 12, Crc(payload.data(), payload.size()));
    std::copy(payload.begin(), payload.end(), out.begin() + 16);
    return out;
}
GameStorageStatus DecodeNativeSave(const std::vector<uint8_t> &file, std::vector<uint8_t> &payload) {
    if (file.size() < 16 || !std::equal(std::begin(magic), std::end(magic), file.begin()))
        return {GameStorageCode::Corrupt, 0, "not a DDI NONMATCHING WIP v1 native envelope"};
    const auto size = Word(file, 8);
    if (size > 0x48000 || uint64_t(size) + 16 != file.size() || Word(file, 12) != Crc(file.data() + 16, size))
        return {GameStorageCode::Corrupt, 0, "native envelope length/checksum mismatch"};
    payload.assign(file.begin() + 16, file.end());
    return {};
}
const char *GameStorageCodeName(GameStorageCode code) {
    switch (code) {
    case GameStorageCode::Ok:
        return "ok";
    case GameStorageCode::Missing:
        return "missing";
    case GameStorageCode::Unavailable:
        return "unavailable";
    case GameStorageCode::ReadOnly:
        return "read-only";
    case GameStorageCode::Full:
        return "full";
    case GameStorageCode::Corrupt:
        return "corrupt";
    case GameStorageCode::UnsupportedOriginal:
        return "unsupported-original-save";
    case GameStorageCode::UnmappedNativeState:
        return "unmapped-native-state";
    case GameStorageCode::Exists:
        return "exists";
    case GameStorageCode::Bounds:
        return "bounds";
    case GameStorageCode::IoFailure:
        return "io-failure";
    }
    return "unknown";
}
void GameAutoload::Begin(bool dialog_required) {
    state_ = {};
    buffer_.clear();
    state_.phase = dialog_required ? GameAutoloadPhase::Dialog : GameAutoloadPhase::Begin;
    state_.source_pc = dialog_required ? 0x18FCC : 0x4E100;
}
bool GameAutoload::DialogCompleted(int32_t dialog_state) {
    if (state_.phase != GameAutoloadPhase::Dialog || dialog_state != 3)
        return false;
    state_.phase = GameAutoloadPhase::Begin;
    state_.source_pc = 0x4E100;
    return true;
}
void GameAutoload::Tick(GameStorage &storage) {
    if (state_.phase != GameAutoloadPhase::Begin)
        return;
    // 4D96C(1) actually allocates/clears this capacity before utility mode0.
    buffer_.assign(state_.buffer_capacity, 0);
    state_.buffer_live = true;
    auto status = storage.Probe();
    if (status) {
        std::vector<uint8_t> opaque;
        status = storage.LoadNative(opaque);
        if (status) {
            // No reached producer/format adapter maps native opaque payload
            // onto249D0 state. Do not report a completed original utility load.
            status = {GameStorageCode::UnmappedNativeState, 0,
                      "native envelope read;game-state field import is not recovered"};
        }
    }
    state_.storage = storage.Snapshot();
    state_.status = status;
    if (status.code == GameStorageCode::Missing && state_.storage.free_bytes >= 800 * 1024) {
        state_.phase = GameAutoloadPhase::TitleRequested;
        state_.source_pc = 0x18160;
    } else {
        state_.phase = GameAutoloadPhase::Blocked;
        state_.source_pc = 0x4DF18;
        if (status.code == GameStorageCode::Missing)
            state_.status = {GameStorageCode::Full, 0,
                             "missing save;actual native free space is below source800KiB threshold"};
    }
}
void GameAutoload::Close() {
    std::vector<uint8_t>().swap(buffer_);
    state_.buffer_live = false;
}
} // namespace dokodemo::recovered

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
enum class GameStorageCode {
    Ok,
    Missing,
    Unavailable,
    ReadOnly,
    Full,
    Corrupt,
    UnsupportedOriginal,
    UnmappedNativeState,
    Exists,
    Bounds,
    IoFailure
};
struct GameStorageStatus {
    GameStorageCode code = GameStorageCode::Ok;
    uint32_t native_error = 0;
    std::string detail;
    explicit operator bool() const {
        return code == GameStorageCode::Ok;
    }
};
struct GameStorageSnapshot {
    bool available = false, writable = false, native_record = false, opaque_record_read = false;
    uint64_t free_bytes = 0, read_bytes = 0, written_bytes = 0;
    uint32_t probe_calls = 0, load_calls = 0, publish_calls = 0;
    GameStorageStatus status;
};
// Explicit NONMATCHING native envelope for future native state producers.
// This never labels bytes as decrypted PSP DATA.BIN or invents a PSP save format.
std::vector<uint8_t> EncodeNativeSave(const std::vector<uint8_t> &payload);
GameStorageStatus DecodeNativeSave(const std::vector<uint8_t> &file, std::vector<uint8_t> &payload);
class GameStorage {
  public:
    virtual ~GameStorage() = default;
    virtual GameStorageStatus Probe() = 0;
    virtual GameStorageStatus LoadNative(std::vector<uint8_t> &out) = 0;
    virtual GameStorageStatus PublishNative(const std::vector<uint8_t> &payload, bool replace_existing) = 0;
    virtual GameStorageSnapshot Snapshot() const = 0;
};
const char *GameStorageCodeName(GameStorageCode code);
enum class GameAutoloadPhase {
    Idle,
    Dialog,
    Begin,
    TitleRequested,
    Blocked
};
struct GameAutoloadSnapshot {
    GameAutoloadPhase phase = GameAutoloadPhase::Idle;
    uint32_t mode = 0, parameter_bytes = 0x5C8, buffer_capacity = 0x48000, source_pc = 0;
    bool buffer_live = false, source_fields_applied = false;
    GameStorageSnapshot storage;
    GameStorageStatus status;
};
// 4E100/4DF18 AUTOLOAD only. Missing plus actual800KiB free selects TITLE.
// A checked native envelope remains opaque until a field producer is recovered.
class GameAutoload {
  public:
    void Begin(bool dialog_required);
    bool DialogCompleted(int32_t dialog_state);
    void Tick(GameStorage &storage);
    GameAutoloadSnapshot Snapshot() const {
        return state_;
    }
    void Close();

  private:
    std::vector<uint8_t> buffer_;
    GameAutoloadSnapshot state_;
};
} // namespace dokodemo::recovered

#pragma once
#include "game_filesystem_native.h"

namespace dokodemo::recovered {
class GameArena;
struct GameDictionarySnapshot {
    bool attached = false, mutable_owner = false, header_valid = false;
    bool query_engine_implemented = false;
    uint32_t bytes = 0, kind = 0, capacity = 0, record_stride = 0, mode = 0;
};
// Cold NJ data binding/99694 initialization only. Japanese query/conversion,
// learning and persistence APIs are deliberately not advertised implemented.
class GameDictionary final {
  public:
    GameDictionary() = default;
    GameDictionary(const GameDictionary &) = delete;
    GameDictionary &operator=(const GameDictionary &) = delete;
    ~GameDictionary();
    GameFsStatus Attach(PackageView data, uint32_t mode);
    bool InitializeMutable(GameArena &arena, uint32_t mode);
    uint32_t ReadBE32(uint32_t at) const;
    GameDictionarySnapshot Snapshot() const {
        return state_;
    }
    bool Close();

  private:
    PackageView data_;
    GameArena *arena_ = nullptr;
    uint8_t *buffer_ = nullptr;
    GameDictionarySnapshot state_;
};
} // namespace dokodemo::recovered

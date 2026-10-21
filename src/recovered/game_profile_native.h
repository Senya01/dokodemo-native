#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dokodemo::recovered {
enum class GameProfileField : uint32_t {
    None,
    Nickname,
    Player,
    Gender,
    Year,
    Month,
    Day,
    Blood,
    Comment, // historical native name: source type3 field labelled 電話番号 (telephone)
    Confirmation
};
struct GameProfileRecord {
    bool live = false;
    uint32_t actor = UINT32_MAX, gender = UINT32_MAX, date = UINT32_MAX, blood = UINT32_MAX;
    // comment is the legacy member name for the source type3/telephone field.
    // Its original repertoire/telephone validation is not recovered.
    std::string nickname, player, comment; // safe native strings,not an opaque PSP save
    std::array<uint8_t, 16> source_nickname{}, source_player{}, source_comment{};
    std::array<uint32_t, 7> calendar{};
};
struct GameProfileSnapshot {
    std::string nickname, player, comment;
    int32_t gender = -1, year = -1, month = -1, day = -1, zodiac = -1, blood = -1;
    uint32_t present = 0, editor_field = 0, accepted_results = 0, cancelled_results = 0;
    bool confirmed = false, applied = false;
    GameProfileRecord stored;
};
// Native typed replacement for TITLE fields80..C8 and32184's COUNT. This is
// not a PSP savedata serializer or a profile-loaded marker.
class GameProfile final {
  public:
    void Nickname(std::string value) {
        state_.nickname = std::move(value);
        Count();
    }
    GameProfileField Next() const noexcept;
    void Begin(GameProfileField);
    bool Result(const std::string *value, int32_t current_year, std::string &error);
    bool Confirm(bool yes, uint32_t actor = 0, const std::array<uint32_t, 7> &calendar = {});
    static uint32_t PackDate(int32_t year, int32_t month, int32_t day) noexcept;
    GameProfileSnapshot Snapshot() const { return state_; }
    static int32_t Days(int32_t year, int32_t month) noexcept;

  private:
    void Count() noexcept;
    GameProfileSnapshot state_, backup_;
};
} // namespace dokodemo::recovered

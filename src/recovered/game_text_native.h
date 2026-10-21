#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dokodemo::recovered {
// The source20A30 bound counts calls to941C8,not bytes or UTF16 code units.
// Windows text is accepted only where strict CP932 and source-decoder boundaries
// agree. Unsupported single-byte nonASCII cannot silently eat the next letter.
class GameNameBuffer final {
  public:
    bool Assign(std::string_view initial, uint32_t maximum);
    bool Insert(std::string_view committed);
    bool Backspace();
    bool Delete();
    void Left() noexcept {
        if (cursor_)
            --cursor_;
    }
    void Right() noexcept {
        if (cursor_ < Units())
            ++cursor_;
    }
    void Home() noexcept { cursor_ = 0; }
    void End() noexcept { cursor_ = Units(); }
    void Clear() noexcept {
        bytes_.clear();
        error_.clear();
        boundaries_.resize(1);
        boundaries_[0] = 0;
        cursor_ = maximum_ = 0;
    }
    uint32_t Cursor() const noexcept { return cursor_; }
    uint32_t Units() const noexcept { return uint32_t(boundaries_.size() - 1); }
    uint32_t Maximum() const noexcept { return maximum_; }
    const std::string &Bytes() const noexcept { return bytes_; }
    const std::string &Error() const noexcept { return error_; }
    std::string Prefix() const { return bytes_.substr(0, boundaries_[cursor_]); }

  private:
    bool Replace(std::string candidate, uint32_t cursor);
    std::string bytes_, error_;
    std::vector<size_t> boundaries_{0};
    uint32_t cursor_ = 0, maximum_ = 0;
};
enum class GameTextKind : uint8_t {
    Commit,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Confirm,
    Cancel,
    FocusGained,
    FocusLost,
    CompositionBegin,
    CompositionEnd,
    Resync,
    InvalidEncoding
};
struct GameTextEvent {
    uint64_t context = 0;
    GameTextKind kind = GameTextKind::Commit;
    uint16_t bytes = 0;
    bool test_injected = false;
    std::array<char, 64> text{};
};
// Caller synchronizes. Overflow discards pending text/confirm together and
// publishes a focus-loss resync,never a retained accidental confirmation.
class GameTextMailbox final {
  public:
    bool Push(const GameTextEvent &) noexcept;
    bool Pop(GameTextEvent &) noexcept;
    void Clear() noexcept {
        head_ = count_ = 0;
        resync_ = false;
        context_ = 0;
    }

  private:
    std::array<GameTextEvent, 128> events_{};
    uint32_t head_ = 0, count_ = 0;
    uint64_t context_ = 0;
    bool resync_ = false;
};
// UI-thread gate for Windows keys/WM_CHAR. New contexts/refocus discard key
// admission. A held/repeated old key cannot start editing/confirming the new one.
class GameTextKeyGate final {
  public:
    void Context(uint64_t id, bool focused) noexcept;
    void Focus(bool focused) noexcept;
    bool KeyDown(uint32_t key, bool repeat) noexcept;
    void KeyUp(uint32_t key) noexcept {
        if (key < keys_.size())
            keys_[key] = false;
    }
    bool Character(uint32_t key) const noexcept {
        return focused_ && context_ && key < keys_.size() && keys_[key];
    }
    uint64_t Id() const noexcept { return context_; }

  private:
    uint64_t context_ = 0;
    bool focused_ = false;
    std::array<bool, 256> keys_{};
};

struct GameNameEntrySnapshot {
    bool live = false, focused = false, composing = false, result_ready = false, accepted = false,
         cancelled = false;
    uint32_t source_state = 3, maximum_units = 0, units = 0, native_caret_units = 0;
    uint32_t edits = 0, rejections = 0, confirmations = 0, cancellations = 0;
    uint64_t context = 0;
    std::string initial, staged, source120, result, error;
};
// Finite mode0/type1 adapter: source0(open)->1(edit)->2(close)->3(result).
// Only actual UI control completion2 advances open/close. No result on timer,
// opening,composition or a queued event from a previous editor context.
class GameNameEntry final {
  public:
    bool Begin(std::string_view initial, uint32_t source_limit = 4);
    bool Apply(const GameTextEvent &);
    bool Reject(std::string message) {
        state_.error = std::move(message);
        ++state_.rejections;
        return false;
    }
    bool SetStaged(std::string_view value);
    void ControlCompleted(uint8_t actual_mode);
    GameNameEntrySnapshot Snapshot() const;
    const std::string *Result() const noexcept;
    void Retire() noexcept;

  private:
    GameNameBuffer buffer_;
    GameNameEntrySnapshot state_;
    uint64_t serial_ = 0;
};
} // namespace dokodemo::recovered

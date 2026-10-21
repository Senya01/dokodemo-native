#pragma once
#include "game_graphics_native.h"
#include "game_ui_resource_native.h"
#include "game_dictionary_native.h"
#include "game_input_native.h"
#include <array>

namespace dokodemo::recovered {
struct GameTransitionSnapshot {
    bool live = false;
    uint32_t state = 0, group = 5, duration = 30;
    float x = 0, y = 0, radius = -50, maximum_radius = 0, increment = 0;
};
// D54C/D838/D9BC/D67C/D708, with typed native ownership and host math.
class GameTransition final {
  public:
    bool Initialize(const std::array<float, 2> &produced_center);
    bool SetCenter(const std::array<float, 2> &center);
    void SetDuration(uint32_t ticks);
    void Open(bool immediate = false);      // D9E4; source state1 expands to2
    void CloseWipe(bool immediate = false); // contracting state3 to0
    void Update();
    bool Draw(GameGraphics &graphics) const;
    GameTransitionSnapshot Snapshot() const {
        return state_;
    }
    void Retire() noexcept {
        state_.live = false;
    }

  private:
    GameTransitionSnapshot state_;
};

class GameArena;
class GameVmBoot;
class GameFontResource;
struct GameDialogSnapshot {
    int32_t state = 3, result = 0;
    uint32_t mode = 1, selection = 0, confirmations = 0;
    float progress = 0;
};
// 47E0C/48224/47EA4: first press produces selection+1, then state2 really
// contracts to0/hides the root before state3. No forced completion/autoack.
class GameDialogController {
  public:
    bool Begin(uint32_t mode = 1, uint32_t selection = 0) noexcept;
    bool Input(const GameInputFrame &frame) noexcept;
    void Tick() noexcept;
    void Retire() noexcept {
        gate_.Stop();
        state_.state = 3;
    }
    GameDialogSnapshot Snapshot() const noexcept {
        return state_;
    }

  private:
    GameButtonGate gate_;
    GameDialogSnapshot state_;
};
struct GameSceneUiSnapshot {
    bool live = false, constructors_returned = false, scene1_returned = false;
    bool ui_render_implemented = false, text_drawn = false, mixer_implemented = false;
    uint32_t owners = 0, records = 0, ui_resources = 0, ui_nodes = 0, ui_clips = 0;
    uint32_t textures = 0, texture_pixels = 0, borrowed_dictionaries = 0, mutable_dictionaries = 0;
    uint32_t text_capacity_bytes = 0, visible_roots = 0, native_frames = 0;
    bool storage_dialog = false, awaiting_confirmation = false;
    uint32_t storage_error = 0, dialog_glyphs = 0;
    float dialog_progress = 0;
    int32_t dialog_state = 3, dialog_result = 0;
    uint32_t confirmations = 0;
    GameTransitionSnapshot transition;
};
// 18EE8..18F38: grouped typed native owners. Immutable views borrow DATA.BPM;
// mutable NJ/text/control state is owned and retired before that package/font.
class GameSceneUiBootstrap final {
  public:
    GameFsStatus Initialize(GameResourceSource &source, GameArena &arena, GameVmBoot &manager,
                            const GameFontResource &font);
    bool DrawColdFrame(GameGraphics &graphics);
    GameFsStatus ShowStorageDialog(const GameFontResource &font, uint32_t native_error);
    bool HandleDialogInput(const GameInputFrame &frame);
    GameSceneUiSnapshot Snapshot() const;
    bool Close();
    ~GameSceneUiBootstrap();

  private:
    struct Owner {
        uint32_t state = 3, group = 0;
    };
    GameArena *arena_ = nullptr;
    GameVmBoot *manager_ = nullptr;
    std::array<Owner *, 5> owners_{};
    std::array<uint32_t, 5> tasks_{{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}};
    std::array<void *, 3> text_buffers_{};
    std::array<std::shared_ptr<GameUiResource>, 3> resources_;
    std::array<GameUiInstance, 3> clones_;
    std::array<GameDictionary, 9> dictionaries_;
    std::shared_ptr<GameImage> moving_;
    GameTransition transition_;
    GameDialogController dialog_;
    std::vector<uint32_t> dialog_pixels_;
    uint32_t dialog_width_ = 0, dialog_height_ = 0;
    GameSceneUiSnapshot state_;
};

struct GameSoundBankSnapshot {
    bool metadata_ready = false, mixer_ready = false;
    uint32_t image_bytes = 0, chunks = 0, pool_capacity = 64;
    int32_t native_metadata_slot = -1;
    bool regions = false, waves = false, sequences = false, names = false;
};
// Native metadata pool adaptation of551E4/55208/5536C. This owns a live source
// image and a real bounded slot; it does not pretend to implement SGX playback.
class GameSoundBank final {
  public:
    GameFsStatus Register(PackageView image);
    GameSoundBankSnapshot Snapshot() const {
        return state_;
    }
    void Close() {
        image_.Reset();
        slots_.fill(false);
        state_ = {};
    }

  private:
    PackageView image_;
    std::array<bool, 64> slots_{};
    GameSoundBankSnapshot state_;
};
} // namespace dokodemo::recovered

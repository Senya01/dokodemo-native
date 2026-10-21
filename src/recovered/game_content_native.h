#pragma once
#include "game_filesystem_native.h"
#include <array>
#include <vector>

namespace dokodemo::recovered {
class GameArena;
class GameVmBoot;
struct GameFontTexture {
    PackageView pixels;
    uint8_t format = 0;
    uint16_t width = 0, height = 0;
};
struct GameFontSnapshot {
    bool ready = false;
    uint32_t image_bytes = 0, texture_count = 0, texture_bytes = 0;
    uint32_t auxiliary_div8 = 0, auxiliary_offset = 0;
    int32_t glyph_width = 0, glyph_height = 0;
    float scale_x = 1, scale_y = 1, spacing_x = 2, spacing_y = 2;
};
// E364/937FC/9435C: live borrowed XFT image and typed native texture records.
// No rendered text, GPU upload or invented title is implied by initialization.
class GameFontResource final {
public:
    GameFsStatus Initialize(PackageView image);
    const GameFontTexture* Texture(uint32_t index) const;
    PackageView AuxiliaryData() const { return auxiliary_; }
    GameFontSnapshot Snapshot() const { return state_; }
    void Close();
private:
    PackageView image_, auxiliary_;
    std::vector<GameFontTexture> textures_;
    GameFontSnapshot state_;
};
struct GameContentSnapshot {
    bool vsync_owner = false, loadman_owner = false, bb8_returned = false;
    bool state_reset = false, import_callback_configured = false, import_callback_registered = false;
    bool font_ready = false, package_mounted = false, blocked = false, closed = false;
    uint32_t registered_records = 0, loading_ticks = 0, load_state = 1, mount_state = 0;
    int32_t previous_scene = -1, current_scene = -1, next_scene = -1;
    uint32_t requested_bits = 0, owned_bits = 0, completed_bits = 0;
    uint32_t package_bytes = 0, package_slots = 0, frontier_pc = 0;
    GameFontSnapshot font;
    GameFsStatus status;
};
// Nonmatching cold-loading driver. It runs the recovered phase0 state machines
// in source order; synchronous native I/O replaces the PSP async file pump.
// CVSync phases9..12, interrupt delivery and the general game loop are NOT
// implemented by this bounded driver. Their descriptors remain metadata only.
class GameContentBoot final {
public:
    GameContentBoot() = default;
    GameContentBoot(const GameContentBoot&) = delete;
    GameContentBoot& operator=(const GameContentBoot&) = delete;
    ~GameContentBoot();
    bool InitializeBB8(GameArena& arena, GameVmBoot& manager);
    bool Reset24994();
    bool ConfigureImportCallback5334C(); // unavailable native interrupt/sound policy, no fake registration
    uint32_t StateWord(uint32_t byte_offset) const;
    int32_t ResetLookupWord(uint32_t index) const;
    // One native loading tick; false means a real I/O/format/lifetime failure.
    // A successful font consumer stops BEFORE the next source constructor.
    bool TickLoading(GameFilesystem& filesystem);
    GameContentSnapshot Snapshot() const { return state_; }
    const GameFontResource& Font() const { return font_; }
    bool Close();
private:
    struct Vsync { uint32_t interval = 160; bool render = true, finish = true, paused = false; };
    struct LoadMan { std::array<char, 35> configured_script{}; };
    GameArena* arena_ = nullptr;
    GameVmBoot* manager_ = nullptr;
    Vsync* vsync_ = nullptr;
    LoadMan* loadman_ = nullptr;
    uint32_t vsync_task_ = UINT32_MAX, load_task_ = UINT32_MAX;
    std::array<uint8_t, 0x10B50> reset_bytes_{};
    std::array<int32_t, 28> reset_lookup_{};
    PackageView package_;
    BpDirectory directory_;
    GameFontResource font_;
    GameContentSnapshot state_;
};
} // namespace dokodemo::recovered

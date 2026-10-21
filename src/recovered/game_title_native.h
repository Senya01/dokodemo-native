#pragma once
#include "game_model_native.h"
#include "game_scene_ui_native.h"

namespace dokodemo::recovered {
class GameArena;
class GameVmBoot;
// Explicit host boot policy for the unrecovered7E65C producer. Neither option
// is described as a PSP BSS observation. Menu5 rendering remains unsupported.
enum class NativeTitleLayout {
    CharacterEntrance,
    Menu5
};
uint32_t TitleMovementStep(uint32_t type, uint32_t previous_counter, uint32_t &segment);
struct GameTitleActorSnapshot {
    uint32_t type = 0, animation_time = 0, movement_counter = 0;
    bool desired_visible = false, visible = false, moving = false,target_produced=false;
    float x = 0, y = 0, z = -400, target_x = -4000, target_y = 0;
    uint64_t local_pose_digest = 0;
};
struct GameTitleSnapshot {
    bool live = false, constructor_returned = false, camera_ready = false, frame_drawn = false;
    bool initial_layout_native_policy = true, menu_input_implemented = false, scene3_completed = false;
    bool arrival_branch_pending=false;
    uint32_t state = 2, layout = 0, updates = 0, frames = 0, visible_actors = 0;
    uint32_t model_assets = 0, motions = 0, clones = 0, vertices = 0, triangles = 0, approximate_curves = 0;
    uint32_t native_tick_units = 80, frontier_pc = 0x30188;
    uint64_t pixel_writes = 0;
    float field_of_view = .34906587f, near_plane = 100, far_plane = 10400;
    std::array<float, 3> camera_position{{0, -4400, 560}}, camera_lookpoint{{0, -4399, 560}};
    std::array<GameTitleActorSnapshot, 10> actors;
};
// Actual30188/33484 owners and30C34/30ED4 character branch. This is a game
// consumer driven by CLoadMan, not a model viewer or an emulated PSP draw list.
class GameTitleScene final {
  public:
    GameFsStatus Initialize(GameResourceSource &, GameArena &, GameVmBoot &, NativeTitleLayout,
                            uint32_t source_profile_date);
    GameFsStatus TickAndDraw(GameGraphics &, GameTransition &);
    GameTitleSnapshot Snapshot() const {
        return state_;
    }
    bool Close();
    ~GameTitleScene();

  private:
    struct Actor {
        GameModelClone model;
        uint32_t time = 0, segment = 0;
    };
    GameFsStatus ResetEntrance();
    std::array<std::shared_ptr<GameModelAsset>, 7> assets_;
    std::array<std::vector<GameModelMotion>, 6> motions_;
    std::array<Actor, 10> actors_;
    GameMatrix camera_{};
    GameArena *arena_ = nullptr;
    GameVmBoot *manager_ = nullptr;
    void *owner_ = nullptr;
    void *text_ = nullptr;
    uint32_t task_ = UINT32_MAX;
    GameTitleSnapshot state_;
};
} // namespace dokodemo::recovered

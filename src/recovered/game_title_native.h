#pragma once
#include "game_model_native.h"
#include "game_scene_ui_native.h"
#include "rng_native.h"
#include "game_profile_native.h"

namespace dokodemo::recovered {
class GameArena;
class GameVmBoot;
class GameFontResource;
// Explicit host boot policy for the unrecovered7E65C producer. Neither option
// is described as a PSP BSS observation.
enum class NativeTitleLayout { CharacterEntrance, Menu5 };
uint32_t TitleMovementStep(uint32_t type, uint32_t previous_counter, uint32_t &segment);
struct GameTitleActorSnapshot {
    uint32_t type = 0, animation_time = 0, movement_counter = 0, motion = 1, movement_epoch = 0;
    bool desired_visible = false, visible = false, moving = false, target_produced = false;
    float x = 0, y = 0, z = -400, target_x = -4000, target_y = 0;
    uint64_t local_pose_digest = 0;
};
struct GameTitleSnapshot {
    bool live = false, constructor_returned = false, camera_ready = false, frame_drawn = false;
    bool initial_layout_native_policy = true, menu_input_implemented = false, scene3_completed = false;
    bool arrival_branch_pending = false;
    bool softkey_requested = false, selection_moving = false, selection_motion_complete = false;
    int32_t selection = -1, pending_root = 0;
    uint32_t menu_row = 1, selected_actor = 0, arrival_countdown = 4, media_state = 0;
    uint32_t primary_arrivals = 0, primary_restarts = 0, kuro_selections = 0, rng_calls = 0;
    uint32_t confirmations = 0, navigation_events = 0, ui_quads = 0, ui_callbacks = 0, visible_roots = 0;
    uint32_t state = 2, layout = 0, updates = 0, frames = 0, visible_actors = 0;
    uint32_t model_assets = 0, motions = 0, clones = 0, vertices = 0, triangles = 0, approximate_curves = 0;
    uint32_t native_tick_units = 80, frontier_pc = 0x30188;
    uint64_t pixel_writes = 0;
    uint64_t model_pixel_writes = 0;
    float field_of_view = .34906587f, near_plane = 100, far_plane = 10400;
    std::array<float, 3> camera_position{{0, -4400, 560}}, camera_lookpoint{{0, -4399, 560}};
    std::array<GameTitleActorSnapshot, 10> actors;
    GameProfileSnapshot profile;
};
// Later phase11 consumer33B90. Positions/animation counters are produced by
// phase6 first. Only this function changes desired visibility/restart targets.
void TitleArrivalCycle(GameTitleSnapshot &, RngNativeOwner *, bool game_flag = false,
                       bool both_triggers = false);
// Actual30188/33484 owners and30C34/30ED4 character branch. This is a game
// consumer driven by CLoadMan, not a model viewer or an emulated PSP draw list.
class GameTitleScene final : private GameUiCallbacks {
  public:
    GameFsStatus Initialize(GameResourceSource &, GameArena &, GameVmBoot &, NativeTitleLayout,
                            uint32_t source_profile_date);
    GameFsStatus TickAndDraw(GameGraphics &, GameTransition &);
    GameFsStatus AttachMenu(GameUiInstance &, const GameFontResource &, RngNativeOwner &,
                           GameSceneUiBootstrap* scene_ui=nullptr,int32_t calendar_year=0);
    void Input(const GameInputFrame &frame) { input_.Observe(frame); }
    void SetCalendar(const std::array<uint32_t,7>& value){calendar_=value;}
    GameTitleSnapshot Snapshot() const { return state_; }
    bool Close();
    ~GameTitleScene();

  private:
    struct Actor {
        GameModelClone model;
        uint32_t time = 0, segment = 0;
        uint32_t movement_epoch = 0;
        float carousel_step = 0;
        bool complete = false;
        uint32_t blend_left = 0;
        std::vector<GameModelPose> blend_from;
    };
    GameFsStatus ResetEntrance();
    bool DrawUiCallback(uint32_t, uint32_t, const GameUiMatrix &, uint32_t, GameGraphics &,
                        std::string &) override;
    GameFsStatus MenuTick(GameTransition &);
    GameFsStatus SetupLayout();
    void BeginRoot(uint32_t, GameTransition &);
    GameUiInstance *ui_ = nullptr;
    const GameFontResource *font_ = nullptr;
    RngNativeOwner *rng_ = nullptr;
    GameMenuInput input_;
    std::array<uint32_t, 30> controls_{};
    uint32_t current_control_ = UINT32_MAX, prior_row_ = 1;
    bool switching_root_ = false;
    GameSceneUiBootstrap* scene_ui_=nullptr;
    int32_t calendar_year_=0;
    std::array<uint32_t,7> calendar_{};
    GameProfile profile_;
    bool editor_waiting_=false,confirmation_waiting_=false,leaving_profile_=false;
    bool confirmation_declined_=false;
    GameFsStatus ProfileTick(GameTransition&);
    bool carousel_moving_ = false, selection_motion_ = false;
    std::string nickname_;
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

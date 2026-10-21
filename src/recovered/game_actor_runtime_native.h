#pragma once
#include "game_filesystem_native.h"
#include "game_model_native.h"
#include "game_position_native.h"
#include "game_scene_ui_native.h"

namespace dokodemo::recovered {
// 7398 compares five adjacent intervals from six signed16 (hour,minute)
// endpoints. Native civil-clock validation is explicit; the source result
// values are 5,0,1,2,3 and fallback4, not a success Boolean.
int32_t ClassifySceneClock7398(const std::array<int16_t,12>& endpoints,
                              uint32_t hour,uint32_t minute);
std::array<int16_t,12> SceneClockEndpoints7398(uint32_t row);
uint32_t SceneSeason709C(uint32_t month,uint32_t day);
struct GameActorRuntimeSnapshot {
  bool live = false, camera_follows_actor = false, pose_dirty = false;
  bool actor_special_ready = false, command_complete = true,
       command_ready = false;
  bool manager_request8 = false; // 1B188 zeroed6C owner,1C210 request
  int32_t active_command = -1, camera_mode = 0;
  int32_t source_field5C = 0; //9480/94F8 explicit produced base+4C
  bool region_owners_live=false;
  std::array<uint32_t,3> region_counts{};
  uint32_t position_queries=0,position_fallbacks=0;
  bool position_result3C0=false;
  GameVector4 position3B0{{0,0,0,1}};
  int32_t source_field50 = 1, source_field54 = 0; // AC60 AD5C/AD64 stores
  uint32_t actor = 0, camera_bindings = 0, transition_requests = 0,
           loader_interval = 0;
  uint32_t model_slots = 0, model_nodes = 0, model_vertices = 0,
           model_triangles = 0;
  uint32_t motion_templates = 0, model_frames = 0;
  uint64_t model_pixel_writes = 0;
  uint32_t visible_pixels = 0;
  bool actor_model_live = false;
  uint32_t actor_model_nodes = 0, actor_model_vertices = 0,
           actor_model_triangles = 0;
  int32_t requested_command = -1;
  bool actor_request_pending = false, selected_extension_pending = false;
  uint32_t event_requests = 0;
  bool command_blocked = false, command_flag25A = false, heading_known = false;
  uint32_t command_pc = 0, command_effects = 0, actor_phases = 0,
           actor_math_mode = 0, command_frontier = 0;
  int32_t source_field58 = 0;
  std::array<float, 2> logical_xy{};
  float logical_heading = 0;
  int32_t pending_logical_motion = -1;
  std::array<int32_t, 2> channel_motion{{-1, -1}};
  std::array<GamePlaybackState, 2> channels{};
  uint32_t channel_admissions = 0, first_channel_motion = UINT32_MAX;
  uint32_t actor_draw_frames = 0, actor_visible_pixels = 0, motion_samples = 0;
  uint64_t actor_pixel_writes = 0;
  bool actor_control_ready = false, command_wait_motion = false,
       angular_pending = false;
  uint32_t angular_updates = 0, angular_completions = 0,
           command_completions = 0;
  std::array<uint32_t, 16> effect_order{};
  GameVector4 actor_root{{0, 0, 0, 1}};
  float angular_target = 0;
  bool scene_model_live = false, asc176_ready = false, asc176_complete = false;
  bool scene_clock_ready=false,light_on_control_present=false;
  int32_t source_flag2A980=0;
  int32_t source_flag2A97C=0; // actual24A2C reset;param6 remains checked unsupported
  uint32_t light_toggle_calls=0;
  bool movement_pending=false;
  std::array<float,2> movement_target{};
  uint32_t movement_updates=0,movement_completions=0;
  int32_t last_completed_command=-1;
  std::array<int32_t,8> first_completed_commands{{-1,-1,-1,-1,-1,-1,-1,-1}};
  uint32_t season_controls=0,scene_period_table52=0,manager_scene14=0;
  std::array<bool,7> season_present{};
  std::array<GamePlaybackState,7> season_playback{};
  GamePlaybackState light_on_playback{};
  uint32_t scene_light_nodes=0,scene_light_color14=0,scene_light_color_writes=0;
  uint32_t scene_light_scalar_writes=0,scene_light_vector_writes=0;
  uint32_t scene_controls = 0, scene_pair_calls = 0, scene_pair3_calls = 0,
           scene_frames = 0;
  uint32_t scene_nodes = 0, scene_vertices = 0, scene_triangles = 0,
           scene_particle_emitters = 0, scene_secondary_textures = 0,
           scene_deferred_tracks = 0;
  std::array<int32_t, 2> scene_last_pair{{-1, -1}};
  std::array<GamePlaybackState, 14> scene_playback{};
  uint32_t motion123_admissions = 0, motion123_completions = 0;
  std::string command_error;
  int32_t last_model_slot = -1;
  std::string last_model_path;
  std::array<float, 4> actor_position{{0, 0, 600, 1}};
  std::array<float, 4> camera_up{{0, 0, 1, 1}}, camera_fallback{{1, 0, 0, 1}};
  std::array<float, 4> camera_look{{0, 0, 650, 1}},
      camera_position{{0, -4400, 980, 1}};
};
// Finite source7650/9480/23FD0 control owners required by72/BCFC. This is
// deliberately not a declaration that wholeAC60,182F4 or scene4 models ran.
class GameActorRuntime final {
public:
  GameFsStatus Initialize(uint32_t actor, PackageView selection,
                          GameTransition &transition,
                          GameResourceSource *resources = nullptr);
  GameFsStatus BindCamera(int32_t mode);
  GameFsStatus InitializeScene0(const std::array<uint32_t,7>& calendar);
  GameFsStatus QueryScenePeriod(uint32_t hour,uint32_t minute,int32_t& result);
  GameFsStatus QueryActorPosition(bool& result);
  GameFsStatus StartTransition();
  bool WaitReady() const;
  GameFsStatus SetField54(int32_t value);
  GameFsStatus RequestManager();
  GameFsStatus RequestActorEvent(int32_t first, int32_t second);
  GameFsStatus AdvanceActorFrame();
  GameFsStatus CreateModel(int32_t resource, int32_t category, int32_t motion,
                           float x, int32_t &slot);
  GameFsStatus Draw(GameGraphics &);
  GameActorRuntimeSnapshot Snapshot() const { return state_; }
  void Close();

private:
  bool Live() const;
  PackageView selection_;
  GameTransition *transition_ = nullptr;
  GameResourceSource *resources_ = nullptr;
  struct ModelSlot {
    bool active = false;
    int32_t resource = -1, category = 0;
    std::shared_ptr<GameModelAsset> asset;
    GameModelClone clone;
    std::vector<GameModelMotion> motions;
  };
  std::array<ModelSlot, 100> models_;
  GameActorRuntimeSnapshot state_;
  PackageView actor_asc_;
  std::vector<std::string> actor_motion_names_;
  std::shared_ptr<GameModelAsset> actor_asset_;
  GameModelClone actor_clone_;
  GameFsStatus PrepareActor();
  PackageView command_image_;
  PackageView coordinate_image_;
  bool coordinate_loaded_ = false;
  struct ActorChannel {
    int32_t motion = -1;
    GameModelMotion data;
    GameModelPlayback playback;
    std::vector<GameModelPose> previous_pose;
    uint32_t blend = 0, blend_time = 0;
  };
  std::array<ActorChannel, 2> channels_;
  GameFsStatus BindActorMotion(int32_t motion, uint32_t blend);
  int32_t LogicalMotion(uint32_t record) const;
  GameFsStatus UpdateActorPose();
  struct SceneControl {
    bool present=false;
    GameModelMotion data;
    GameModelPlayback playback;
  };
  std::shared_ptr<GameModelAsset> scene_asset_;
  GameModelClone scene_clone_;
  GamePositionRegions regions_;
  std::vector<SceneControl> scene_controls_,season_controls_;
  SceneControl light_on_control_;
  std::array<uint32_t,7> scene_calendar_{};
  GameFsStatus ApplySceneLightToggle(int32_t value);
  GameFsStatus ApplyScenePair(uint32_t pair);
  GameFsStatus UpdateScene();
};
} // namespace dokodemo::recovered

#pragma once
#include <optional>
#include "game_filesystem_native.h"
#include "game_model_native.h"
#include "game_position_native.h"
#include "game_scene_ui_native.h"
#include <functional>
#include <map>

namespace dokodemo {struct StartupRuntimeTestAccess;}
namespace dokodemo::recovered {
// A690 receives actor+10, so its +44 source is the actor's field54.
int32_t GameActorMotionSelectorA690(int32_t field54) noexcept;
// 7398 compares five adjacent intervals from six signed16 (hour,minute)
// endpoints. Native civil-clock validation is explicit; the source result
// values are 5,0,1,2,3 and fallback4, not a success Boolean.
int32_t ClassifySceneClock7398(const std::array<int16_t,12>& endpoints,
                              uint32_t hour,uint32_t minute);
std::array<int16_t,12> SceneClockEndpoints7398(uint32_t row);
uint32_t SceneSeason709C(uint32_t month,uint32_t day);
struct GameActorLoopFrame { uint32_t start=0,remaining=0; };
struct GameActorLoopStep {
  uint32_t pc=0,depth=0;
  bool set_ready258=false,repeated=false;
};
//NativeSceneAnchorLocalX: source4865C indices, native world * local-X.
//Original parent+10/view-space and source48FFC scheduling remain non-matching.
GameFsStatus RoomModelRootNativeSceneAnchorLocalX(int32_t selector,float x,
    const std::vector<GameModelPose>& scene_pose,GameMatrix& root,int32_t& anchor);
struct GameRoomModelSlotSnapshot {
  bool active=false;
  uint32_t retirement=0; //490C8 request,490E0 disable,then499F8 release
  uint64_t generation=0;
  int32_t resource=-1,category=0,selector=0;
  float source_x=0;
  int32_t anchor_node=-1;
  GameMatrix native_root=ModelIdentity();
  uint64_t asset_identity=0,clone_identity=0,draw_frames=0,draw_pixel_writes=0;
};
// Source24300..24358: infinite repeat or final root pop produces258.
// This does not complete the command or produce25E/VM54.
GameActorLoopStep AdvanceActorLoop8021(std::vector<GameActorLoopFrame>& frames,uint32_t pc);
//Passive native owner history;no strong resource/lease references escape.
struct GameModelRetirementWitness {
  uint64_t request=0,generation=0,requested_phase=0,disabled_phase=0,destroyed_phase=0;
  int32_t slot=-1,resource=-1;
  uint32_t nodes_at_disable=0,motions_at_disable=0;
  bool assets_retained_at_disable=false,clone_retained_at_disable=false;
  bool asset_owner_expired=false,clone_owner_cleared=false;
};
class GameContentBoot;
class GameInventory;
class GameNotebook;
class GameWordMemo;
class GameDiary;
class GameSystemOptionFrontier;
class GameOutingCommand;
struct GameDestinationPreparationSnapshot {
  bool attempted=false,prepared=false,failed=false;
  bool adopted=false,source_scene_allocated=false,source_variant_committed=false;
  std::optional<uint32_t> current_selector24AA4;
  uint32_t current_selector_writer=0;
  bool old_source_retired=false,old_render_retired=false;
  bool arrival_event_posted=false,moving_hide_requested=false,scene_dispatch_completed=false;
  uint64_t arrival_receipt=0;
  uint32_t camera_ready_writer=0;
  bool camera_readyB0=false,camera_holdB3=false;
  uint32_t placement_pc=0,regions_pc=0,adoption_pc=0;
  uint64_t request=0,epoch=0;
  uint32_t candidate=0,selector=0,period=0,resource=0,phase=0;
  GameFsStatus status;
};
struct GameLoadManSnapshot {
  bool current=false,ui_current=false,rendered_current=false;
  uint64_t module_epoch=0,dispatches=0;
  int16_t transition50=0; //177F8 signed HALF, not manager word50/menu/actor50
  uint32_t ui464=0;
  uint64_t ui_generation=0;
  int32_t application20=-1,next24=-1;
  uint32_t phase28=1;
  uint32_t load_phase2C=0,scene_phase44=0; //177F8, DISTINCT from phase28
  int16_t counter42=0;
  bool change40=false,ui_change461=false;
  bool application_flag_known=false;
  uint8_t application_flagEA0=0; //19AA8 sb a2: byte, not a bool/gate
  std::array<uint32_t,3> application_request_order{};
  uint32_t load_writer=0,change_writer=0;
  bool initial_scene6_handoff=false;
  uint32_t initial_store188_pc=0;
  std::optional<uint8_t> initial_state188;
  std::optional<uint8_t> render_state20,render_state21;
  std::optional<uint32_t> game_interval2C; // other owner, NOT load_phase2C/VM time
  int32_t source_selector4DC=-1; // logical source lifetime vs retained Windows render
  uint64_t source_scene_generation=0;
  uint32_t current_variant4A8=1,staged_variant4AC=1,cached_period4B4=2;
  int32_t stored_bgm4BC=0,rendered_selector4DC=-1;
  uint32_t transition50_writer=0,ui464_writer=0;
  std::optional<uint8_t> cleanup4B8,cleanup54; // actual unconditional188F8/18908 writes, no invented prior values
};
struct GameManagerSwitchSnapshot {
  bool live=false,bound=false,pending=false;
  uint64_t epoch=0,requests=0,consumed=0;
  int32_t current14=0,requested18=0;
  std::array<uint32_t,2> last_request_store_pcs{};
  GameLoadManSnapshot loadman;
  uint32_t publication_phase=0,old_resource=0,new_resource=0;
  bool old_bit_known=false,old_bit=false,new_bit_known=false,new_bit=false;
  std::array<uint32_t,6> publication_order{};
};
// WindowsOrdinarySceneOwners co-owns DISTINCT source1B188 manager and177F8
// LoadMan fields. Content constructs it once; the actor borrows that instance.
// This native layout is not PSP layout. UI15 and ordinary continuation refuse;
// target adoption is conditional on the actual issued command and owned job.
class GameManagerSwitchControl : public std::enable_shared_from_this<GameManagerSwitchControl> {
public:
  GameManagerSwitchControl()=default;
  GameManagerSwitchControl(const GameManagerSwitchControl&)=delete;
  GameManagerSwitchControl& operator=(const GameManagerSwitchControl&)=delete;
  GameManagerSwitchControl(GameManagerSwitchControl&&)=delete;
  GameManagerSwitchControl& operator=(GameManagerSwitchControl&&)=delete;
  bool Initialize(uint64_t epoch,std::shared_ptr<const void> module={});
  bool CurrentBorrow(const std::shared_ptr<GameManagerSwitchControl>&)const;
  bool ResourceRequest(uint32_t index,bool& value)const;
  bool BindScene0(uint64_t epoch);
  bool RequestSame0(uint64_t epoch,int32_t target);
  bool RequestPending12(uint64_t epoch);
  bool ConsumeSame0(uint64_t epoch);
  void Retire();
  GameManagerSwitchSnapshot Snapshot()const;
private:
  friend class GameContentBoot;
  friend struct ::dokodemo::StartupRuntimeTestAccess;
  friend class GameActorRuntime;
  friend class GameSceneBgm;
  bool Current()const;
  bool BindActor(uint64_t epoch);
  void ReleaseActor(uint64_t epoch);
  bool RequestApplication(int32_t scene,std::optional<uint32_t> flag={});
  bool CommitApplication();
  bool Phase(uint32_t phase);
  bool OrdinaryUiIdle()const;
  void BindUi(const GameInventory&,const GameNotebook&,const GameWordMemo&,
              const GameDiary&,const GameSystemOptionFrontier&);
  std::weak_ptr<const void> module_;
  std::shared_ptr<const void> standalone_module_;
  uint64_t last_module_epoch_=0;
  const GameInventory* inventory_=nullptr;
  const GameNotebook* notebook_=nullptr;
  const GameWordMemo* wordmemo_=nullptr;
  const GameDiary* diary_=nullptr;
  const GameSystemOptionFrontier* options_=nullptr;
  const GameSceneBgm* bgm_=nullptr;
  std::weak_ptr<const void> bgm_lifetime_;
  std::shared_ptr<int32_t> stored_bgm4BC_;
  std::map<uint32_t,bool> resource_requests_; // only genuinely published bits known
  GameManagerSwitchSnapshot state_;
  std::map<uint32_t,std::shared_ptr<const GameModelAsset>> loaded_models_;
};
enum class GameContextActorPresence:uint32_t { Unknown, Absent, Present };
struct GameContextActorSnapshot {
  GameContextActorPresence presence=GameContextActorPresence::Unknown;
  uint64_t epoch=0,serial=0,model_generation=0;
  uint32_t constructor_actor=0,moves=0,hides=0;
  bool current=false,model_visible=false;
  std::array<float,4> logical{}; // source20/24/28/30, not a fabricated rendered pose
  std::array<uint8_t,32> parameters_sha256{};
};
struct GameActorRuntimeSnapshot {
  GameManagerSwitchSnapshot manager_switch;
  GameSupportRegistrationSnapshot support_registration;
  GameContextActorSnapshot context_actor;
  uint32_t context_actor_bindings=0; // constructor captures; pruned at next bind
  uint64_t owner_epoch=0;
  bool live = false, camera_follows_actor = false, pose_dirty = false;
  bool actor_special_ready = false, command_complete = true,
       command_ready = false,command_repeat_ready258=false;
  bool manager_request8 = false; // 1B188 zeroed6C owner,1C210 request
  bool manager_request9 = false; //GAME3 1C244/1C254;separate async request
  int32_t manager_camera_request4C = -1; //1B204 sentinel;GAME44 is a setter
  uint32_t camera_requests=0;
  uint32_t camera_requests_consumed=0,camera_transition_updates=0,camera_completions=0;
  uint32_t camera_manager_state28=0,camera_transition_mode1C=0;
  bool camera_complete2C=false;
  float camera_fov_degrees=30,camera_target_fov=30,camera_ratio=0;
  bool camera_target_current=false;
  uint64_t camera_target_epoch=0;
  std::array<float,4> camera_target_position{}; // copied readback of current primary+40, not render root
  uint32_t command_loop_depth=0,command_loop_repeats=0;
  bool intro_live=false;uint32_t intro_requests=0,intro_closes=0,intro_state34=3,intro_state38=3;
  uint32_t intro_nodes=0,intro_quads=0,intro_updates=0;
  std::array<GameUiControl,3> intro_controls{};
  GameDestinationPreparationSnapshot destination;
  int32_t active_command = -1, camera_mode = 0;
  std::optional<uint8_t> camera_byteB0,camera_byteB3; // current camera18668/18670 writers
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
  uint32_t model_publications=0,model_updates=0,model_retire_requests=0,model_retirements=0;
  uint64_t model_asset_acquisitions=0,model_clone_initializations=0;
  std::array<GameRoomModelSlotSnapshot,100> room_models{};
  uint64_t room_owner_phases=0;
  uint32_t model_retirement_history_dropped=0;
  std::vector<GameModelRetirementWitness> model_retirement_history; //last16 requests
  uint32_t motion_templates = 0, model_frames = 0;
  uint64_t model_pixel_writes = 0;
  uint32_t visible_pixels = 0;
  bool actor_model_live = false;
  uint32_t actor_model_nodes = 0, actor_model_vertices = 0,
           actor_model_triangles = 0;
  int32_t requested_command = -1;
  bool actor_request_pending = false, selected_extension_pending = false;
  uint32_t event_requests = 0;
  uint32_t event_pending_replacements = 0;
  bool command_blocked = false, command_flag25A = false, heading_known = false;
  uint32_t command_pc = 0, command_effects = 0, actor_phases = 0,
           actor_math_mode = 0, command_frontier = 0;
  int32_t source_field58 = 0;
  std::array<float, 2> logical_xy{};
  float logical_heading = 0,logical_z=0;
  bool source_placement_bound=false;uint32_t source_placement_scene=0,source_placement_writes=0;
  std::array<float,4> source_placement20{};
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
                         GameResourceSource *resources = nullptr,std::shared_ptr<GameBackgroundRuntime> background={},
                         std::shared_ptr<GameManagerSwitchControl> ordinary_owners={});
  bool BackgroundModuleMatches(const std::shared_ptr<GameBackgroundRuntime>&)const;
  GameFsStatus BindCamera(int32_t mode);
  GameFsStatus BindCamera(script::Vm&,int32_t mode);
  GameFsStatus RefreshCamera7980();
  GameFsStatus InitializeScene0(const std::array<uint32_t,7>& calendar,bool defer_regions=false);
  GameFsStatus CompileSceneRegions26B80();
  GameFsStatus ApplyScenePlacementBD28(const std::shared_ptr<GameBackgroundRuntime>&,uint32_t scene,PackageView row,PackageView heading);
  GameFsStatus InitializeScene0Clock(const std::array<uint32_t,7>& calendar);
  GameFsStatus ReadInitialScenePeriod7398(int32_t&)const;
  GameFsStatus QueryScenePeriod(uint32_t hour,uint32_t minute,int32_t& result);
  GameFsStatus QueryActorPosition(bool& result);
  GameFsStatus StartTransition();
  GameFsStatus FinishTransitionGame7();
  GameFsStatus AdvanceDestinationLoad(script::Vm&,const GameOutingCommand&,GameSceneUiBootstrap&,GameSceneBgm&);
  GameFsStatus AdoptPreparedDestination(script::Vm&,const GameOutingCommand&);
  GameFsStatus TickDestinationContinuation(script::Vm&,GameOutingCommand&,GameSceneUiBootstrap&,GameSceneBgm&,bool loader469);
  bool WaitReady() const;
  GameFsStatus SetField54(int32_t value);
  GameFsStatus RequestManager();
  GameFsStatus RequestManager9();
  GameFsStatus RegisterSupportData16(script::Vm&,const script::Value&);
  const GameSupportRegistration& SupportRegistration()const{return support_registration_;}
  // Finite Windows typed1A108/1C0FC actor5 control owner. Actual model paths
  // and six source constants are held user inputs; no default/null authority.
  // Constants: initialX,initialHeading,requestX,requestHeading,divisor,multiplier.
  // Full AC60 animation/callback behavior and scene adoption are not admitted.
  GameFsStatus CreateContextActor5(GameResourceSource&,PackageView model_path,
                                  PackageView texture_prefix,PackageView constants);
  GameFsStatus CreateOrdinaryContextActor5();
  GameFsStatus ClearContextActor1A1F4();
  GameFsStatus BindScriptContextActor1C0FC(script::Vm&);
  GameFsStatus RequestPendingScene12(script::Vm&);
  GameFsStatus RequestSameManager0(script::Vm&);
  GameFsStatus RequestSameManager0(int32_t target);
  GameFsStatus ConsumeSameManager0();
  GameFsStatus BindInitialSceneBgmOwner(GameSceneBgm&);
  // Source1B238 publication ONLY. Target4A8/4DC/adoption/wake still refuse.
  GameFsStatus PublishPendingScene1B238(GameSceneBgm&);
  GameFsStatus PreparePendingDestination(script::Vm&,const GameOutingCommand&,PackageView module,
      const std::shared_ptr<const GameOutingData>&,GameSceneBgm&,std::unique_ptr<GameFileSource>);
  GameDestinationPreparationSnapshot DestinationPreparation()const{return destination_state_;}
  GameFsStatus CloseDestination();
  GameFsStatus ClearManager8();
  GameFsStatus ClearManager9();
  bool OwnersLive()const{return Live();}
  GameFsStatus RequestCamera(int32_t value);
  GameFsStatus ShowIntro(int32_t value);
  GameFsStatus RequestActorEvent(int32_t first, int32_t second);
  GameFsStatus AdvanceActorFrame(script::Vm* support_vm=nullptr);
  GameFsStatus CreateModel(int32_t resource, int32_t category, int32_t motion,
                           float x, int32_t &slot);
  GameFsStatus UpdateModel(int32_t slot,int32_t selector,float x);
  GameFsStatus RetireModel(int32_t slot);
  GameFsStatus Draw(GameGraphics &,const std::function<GameFsStatus(GameGraphics&)>& overlay={},script::Vm* support_vm=nullptr);
  GameActorRuntimeSnapshot Snapshot() const;
  void Close();

private:
  friend class GameOutingCommand;
  friend struct ::dokodemo::StartupRuntimeTestAccess;
  std::shared_ptr<GameBackgroundPreparation> destination_;
  std::shared_ptr<const GameOutingData> destination_inputs_;
  std::weak_ptr<const GameModelAsset> destination_registration_;
  GameDestinationPreparationSnapshot destination_state_;
  bool old_scene_visible_=true;
  bool old_source_retired_=false;
  bool adopting_destination_=false;
  uint64_t last_destination_request_=0;
  std::optional<uint32_t> world_selector_;
  bool Live() const;
  PackageView selection_;
  GameTransition *transition_ = nullptr;
  GameResourceSource *resources_ = nullptr;
  std::weak_ptr<GameBackgroundRuntime> background_module_;
  struct ModelSlot {
    bool active = false;
    int32_t resource = -1, category = 0;
    std::shared_ptr<GameModelAsset> asset;
    GameModelClone clone;
    std::vector<GameModelMotion> motions;
    std::weak_ptr<GameModelAsset> retirement_watch;
    uint64_t retirement_request=0;
  };
  std::array<ModelSlot, 100> models_;
  GameActorRuntimeSnapshot state_;
  std::shared_ptr<GameManagerSwitchControl> manager_switch_;
  bool ordinary_manager_borrow_=false;
  std::shared_ptr<const void> support_manager_;
  GameSupportRegistration support_registration_;
  struct ContextActorData;
  std::shared_ptr<ContextActorData> context_actor_;
  uint64_t context_actor_serial_=0;
  struct PrimaryCameraTarget;
  std::shared_ptr<PrimaryCameraTarget> primary_target_;
  std::weak_ptr<PrimaryCameraTarget> camera_target_;
  bool ReadCameraTarget(std::array<float,4>&)const;
  struct ContextActorBinding {
    std::weak_ptr<script::NativeScriptContext> context;
    std::weak_ptr<ContextActorData> actor;
    uint64_t run=0;
    bool captured_nil=false;
    std::array<char,65> module{};
    std::weak_ptr<PrimaryCameraTarget> primary;
  };
  std::vector<ContextActorBinding> context_actors_;
  GameFsStatus RequestContextScene(script::Vm&,int32_t);
  PackageView actor_asc_;
  std::vector<std::string> actor_motion_names_;
  std::shared_ptr<GameModelAsset> actor_asset_;
  GameModelClone actor_clone_;
  GameFsStatus PrepareActor();
  PackageView command_image_;
  PackageView pending_command_image_;
  std::vector<GameActorLoopFrame> command_loops_;
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
  GameFsStatus UpdatePrimaryRoot99A8();
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
  bool initial_scene_clock_=false;
  GameFsStatus ApplySceneLightToggle(int32_t value);
  GameFsStatus ApplyScenePair(uint32_t pair);
  GameFsStatus UpdateScene();
  GameFsStatus RoomModelRoot(int32_t selector,float x,GameMatrix& root,int32_t& anchor) const;
  GameFsStatus UpdateRoomModels();
  GameFsStatus UpdateCamera();
  std::shared_ptr<GameUiResource> intro_resource_;
  GameUiInstance intro_clone_;
  float camera_from_fov_=30,camera_from_z_=980,camera_from_x_=0,camera_z_=980,camera_x_=0,camera_out_target_x_=0;
};
} // namespace dokodemo::recovered

#include "game_actor_runtime_native.h"
#include "game_model_resources_native.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace dokodemo::recovered {
GameFsStatus RoomModelRootNativeSceneAnchorLocalX(int32_t selector,float x,
    const std::vector<GameModelPose>& scene_pose,GameMatrix& root,int32_t& anchor){
  constexpr std::array<int32_t,9> nodes{{54,55,56,141,61,57,58,59,60}};
  if(selector<0||selector>int32_t(nodes.size())||!std::isfinite(x))
    return {GameFsCode::Unavailable,0,"NativeSceneAnchorLocalX selector/scalar domain"};
  const int32_t selected=selector?nodes[size_t(selector-1)]:-1;
  if(selected>=0&&size_t(selected)>=scene_pose.size())
    return {GameFsCode::Unavailable,0,"NativeSceneAnchorLocalX authored scene anchor absent"};
  auto basis=selected<0?ModelIdentity():scene_pose[size_t(selected)].world;
  for(float v:basis)if(!std::isfinite(v))return {GameFsCode::Unavailable,0,"NativeSceneAnchorLocalX nonfinite authored pose"};
  auto local=ModelIdentity();local[12]=x;
  const auto result=ModelMultiply(basis,local);
  for(float v:result)if(!std::isfinite(v))return {GameFsCode::Unavailable,0,"NativeSceneAnchorLocalX transform overflow"};
  root=result;anchor=selected;return {};
}
GameFsStatus GameActorRuntime::RoomModelRoot(int32_t selector,float x,GameMatrix& root,int32_t& anchor) const{
  if(selector&&(!state_.scene_model_live||!scene_asset_||!scene_asset_->Live()))
    return {GameFsCode::Unavailable,0,"Room model authored scene owner expired"};
  return RoomModelRootNativeSceneAnchorLocalX(selector,x,scene_clone_.Pose(),root,anchor);
}
GameFsStatus GameActorRuntime::UpdateRoomModels(){
  for(size_t i=0;i<models_.size();++i){
    auto& slot=models_[i];auto& owner=state_.room_models[i];
    if(!slot.active||owner.retirement>=2)continue;
    GameMatrix root;int32_t anchor=-1;auto status=RoomModelRoot(owner.selector,owner.source_x,root,anchor);
    if(!status)return status;
    status=slot.clone.Update(root);if(!status)return status;
    owner.native_root=root;owner.anchor_node=anchor;
  }
  return {};
}
namespace {
std::atomic<uint64_t> next_room_owner_epoch{0};
PackageView AscRecord(const PackageView &image, uint32_t index) {
  const auto size = image.Describe().size;
  if (size < 32 || image.ReadLE32(0) != 0x00435341 || image.ReadLE32(4) != 16 ||
      index > 4095)
    throw std::runtime_error("Invalid/capped actual ASC header/index");
  uint64_t at = 16;
  for (uint32_t i = 0; i <= index; ++i) {
    if (at > size || size - at < 16 || image.ReadLE32(at + 4) != 16)
      throw std::runtime_error("ASC ordinal header outside live resource");
    const auto count = image.ReadLE32(at + 8);
    at += 16;
    if (!count || count > size - at)
      throw std::runtime_error("ASC ordinal payload outside live resource");
    if (i == index)
      return image.Subview(at, count);
    at += count;
  }
  throw std::runtime_error("ASC ordinal not found");
}
float WrapAngle(float angle) {
  constexpr float pi = 3.14159265358979323846f;
  while (angle > pi)
    angle -= 2 * pi;
  while (angle < -pi)
    angle += 2 * pi;
  return angle;
}
// PH1 E3D18..E3D88,selected by actual phase5 request31/selector0.
constexpr std::array<const char *, 14> Scene0Names{
    {"tv_on.dak", "tv_off.dak", "fusuma_on.dak", "fusuma_off.dak",
     "reizoko_on.dak", "reizoko_off.dak", "toilet_on.dak", "toilet_off.dak",
     "tuzura_big_on.dak", "tuzura_big_off.dak", "tuzura_big_open.dak",
     "tuzura_small_on.dak", "tuzura_small_off.dak", "tuzura_small_open.dak"}};
constexpr std::array<int32_t, 14> Scene0Stops{
    {1, 0, 3, 2, 5, 4, 7, 6, 9, 10, 8, 12, 13, 11}};
} // namespace
int32_t GameActorMotionSelectorA690(int32_t field54) noexcept {
  if(field54>=0&&field54<=3)return field54;
  if(field54==4)return 3;
  if(field54==5)return 2;
  return 0;
}
GameActorLoopStep AdvanceActorLoop8021(std::vector<GameActorLoopFrame>& frames,uint32_t pc) {
  if(frames.empty())throw std::runtime_error("ASC8021 without live owning loop frame");
  auto& frame=frames.back();
  if(frame.remaining==1){
    frames.pop_back();
    return {pc+4,uint32_t(frames.size()),frames.empty(),false};
  }
  if(frame.remaining>1)--frame.remaining;
  return {frame.start,uint32_t(frames.size()),frame.remaining==0,true};
}
std::array<int16_t,12> SceneClockEndpoints7398(uint32_t row) {
  static constexpr std::array<std::array<int16_t,12>,7> rows{{
    {{0,0,4,0,6,0,10,0,15,30,18,0}},{{0,0,3,50,5,50,10,0,15,40,18,10}},
    {{0,0,3,40,5,40,10,0,15,50,18,20}},{{0,0,3,30,5,30,10,0,16,0,18,30}},
    {{0,0,3,20,5,20,10,0,16,10,18,40}},{{0,0,3,10,5,10,10,0,16,20,18,50}},
    {{0,0,3,0,5,0,10,0,16,30,19,0}}
  }};
  if(row>=rows.size())throw std::runtime_error("Unadmitted scene clock table index");
  return rows[row];
}
uint32_t SceneSeason709C(uint32_t month,uint32_t day) {
  if(month<1||month>12||day<1||day>31)throw std::runtime_error("Scene709C civil month/day unavailable");
  static constexpr std::array<uint32_t,12> rows{{1,2,3,4,5,6,5,4,3,2,1,0}};
  return rows[(month+12-(day>=7?1:2))%12];
}
int32_t ClassifySceneClock7398(const std::array<int16_t,12>& endpoints,
                              uint32_t hour,uint32_t minute) {
  if(hour>=24||minute>=60)
    throw std::runtime_error("Scene7398 requires valid native civil hour/minute");
  const int32_t now=int32_t(hour*60+minute);
  // Both sides are multiplied by2 in7398;the common factor cancels.
  for(unsigned i=0;i<5;++i) {
    const int32_t begin=int32_t(endpoints[i*2])*60+endpoints[i*2+1];
    const int32_t end=int32_t(endpoints[i*2+2])*60+endpoints[i*2+3];
    if(now>=begin&&now<end)return i==0?5:int32_t(i)-1;
  }
  return 4;
}
GameFsStatus GameActorRuntime::Initialize(uint32_t actor, PackageView selection,
                                          GameTransition &transition,
                                          GameResourceSource *resources) {
  Close();
  state_ = {};
  state_.owner_epoch=next_room_owner_epoch.fetch_add(1,std::memory_order_relaxed)+1;
  try {
    if (actor >= 5 || !selection.Describe().readable ||
        !transition.Snapshot().live)
      throw std::runtime_error(
          "Actor control owners need live selected package and transition");
    selection_ = std::move(selection);
    transition_ = &transition;
    resources_ = resources;
    state_.actor = actor;
    // Source9480: actor40=(0,0,600,1);source23FD0:258=1,25E=0,248=-1.
    // The camera7650 vectors and these explicit constructor stores are
    // typed owned values,not borrowed PSP/BSS coordinates or fake models.
    state_.live = true;
    return {};
  } catch (const std::exception &e) {
    return {GameFsCode::Unavailable, 0, e.what()};
  }
}
bool GameActorRuntime::Live() const {
  if (!state_.live || !transition_ || !transition_->Snapshot().live)
    return false;
  try {
    return selection_.Describe().readable;
  } catch (const std::exception &) {
    return false;
  }
}
GameFsStatus GameActorRuntime::InitializeScene0(const std::array<uint32_t,7>& calendar) {
  if (!Live() || !resources_ || state_.scene_model_live)
    return {GameFsCode::Unavailable, 0,
            "Scene4DC admission requires live fresh native owner"};
  if(calendar[5]<1||calendar[5]>12||calendar[4]<1||calendar[4]>31||
     calendar[3]>=24||calendar[2]>=60||calendar[1]>=60)
    return {GameFsCode::Unavailable,0,"Scene seasonal initializer requires valid native civil calendar"};
  // 17C20 requests31/variant1 and explicitly sets selector0. 19DD4/5F58
  // constructs this owner BEFORE script startup. Native typed fields omit
  // unrelated effects owners; no claim of complete5F58/182F4 is made.
  auto asset = std::make_shared<GameModelAsset>();
  auto status = asset->Load(*resources_, "BG_ROOM\\BG_ROOM\\model01.DVS",
                            "BG_ROOM\\BG_ROOM\\gim\\");
  if (!status)
    return status;
  GameModelClone clone;
  status = clone.Initialize(asset);
  if (!status)
    return status;
  std::vector<SceneControl> controls;
  for (const auto name : Scene0Names) {
    SceneControl control;
    status = control.data.Load(
        *resources_, std::string("BG_ROOM\\BG_ROOM\\") + name, *asset);
    if (!status)
      return status;
    const auto stats = control.data.Statistics();
    control.playback.Configure(stats.duration, stats.loop);
    control.present=true;controls.push_back(std::move(control));
  }
  std::vector<SceneControl> seasons(7);SceneControl light_on;
  for(unsigned i=0;i<7;++i){
    PackageView image;
    const auto path=std::string("BG_ROOM\\BG_ROOM\\light0")+char('1'+i)+".DAK";
    status=resources_->OpenResource(path,image);
    if(!status){if(status.code==GameFsCode::NotFound)continue;return status;}
    status=seasons[i].data.Decode(std::move(image),*asset);if(!status)return status;
    const auto stats=seasons[i].data.Statistics();seasons[i].playback.Configure(stats.duration,stats.loop);
    seasons[i].present=true;
  }
  {
    PackageView image;status=resources_->OpenResource("BG_ROOM\\BG_ROOM\\light_on.dak",image);
    if(status){status=light_on.data.Decode(std::move(image),*asset);if(!status)return status;
      const auto stats=light_on.data.Statistics();light_on.playback.Configure(stats.duration,stats.loop);light_on.present=true;}
    else if(status.code!=GameFsCode::NotFound)return status;
  }
  scene_calendar_=calendar;
  const auto season=SceneSeason709C(calendar[5],calendar[4]);
  if(calendar[3]>=24||calendar[2]>=60||calendar[1]>=60)
    return {GameFsCode::Unavailable,0,"Scene seasonal initializer needs real civil calendar"};
  if(seasons[season].present){
    if(seasons[season].data.Statistics().deferred_sections)
      return {GameFsCode::Unavailable,0,"Reached seasonal light motion track not implemented"};
    const auto time=calendar[3]*216000+calendar[2]*3600+calendar[1]*60;
    seasons[season].playback.SetSpeed(30);seasons[season].playback.Start(time);
    state_.scene_period_table52=season;
  }
  season_controls_=std::move(seasons);light_on_control_=std::move(light_on);
  state_.manager_scene14=0; // actual17C84 ->1839C/1B2DC,not a BSS premise
  state_.scene_clock_ready=true;
  state_.light_on_control_present=light_on_control_.present;
  for(size_t i=0;i<season_controls_.size();++i){state_.season_present[i]=season_controls_[i].present;
    if(season_controls_[i].present)++state_.season_controls;}
  scene_asset_ = std::move(asset);
  scene_clone_ = std::move(clone);
  scene_controls_ = std::move(controls);
  const auto stats = scene_asset_->Statistics();
  state_.scene_nodes = stats.nodes;
  state_.scene_vertices = stats.vertices;
  state_.scene_triangles = stats.triangles;
  state_.scene_particle_emitters = stats.particle_emitters;
  state_.scene_secondary_textures = stats.secondary_textures;
  for (const auto &c : scene_controls_)
    state_.scene_deferred_tracks += c.data.Statistics().deferred_sections;
  state_.scene_controls = uint32_t(scene_controls_.size());
  state_.scene_model_live = true;
  // 68E8 actual selected startup list[10,13,-1]. Stop before start in6D6C.
  status = ApplyScenePair(10);
  if (status)
    status = ApplyScenePair(13);
  if(status) {
    // 19EA0 compiles once from the actual scene clone; no per-frame fake
    // list rebuild. Source26B30 owns three independent region arrays.
    status=regions_.Build(scene_clone_);
    if(status){state_.region_owners_live=true;
      for(size_t i=0;i<3;++i)state_.region_counts[i]=uint32_t(regions_.Lists()[i].size());}
  }
  return status;
}
GameFsStatus GameActorRuntime::QueryActorPosition(bool& result) {
  if(!Live()||!state_.actor_model_live||!state_.region_owners_live)
    return {GameFsCode::Unavailable,0,"GAME19 requires live actor/root and compiled scene regions"};
  // C0A8/A420 copies the actual model root into3B0; the query never uses
  // source actor40 or changes logical/model position on fallback.
  state_.position3B0=state_.actor_root;
  bool inside=false;auto status=regions_.ContainsSecond(state_.position3B0,inside);
  if(!status)return status;
  state_.position_result3C0=inside;++state_.position_queries;
  if(!inside){status=regions_.FallbackSecond(state_.position3B0,state_.position3B0);
    if(!status)return status;++state_.position_fallbacks;}
  result=inside;return {};
}
GameFsStatus GameActorRuntime::ApplySceneLightToggle(int32_t value) {
  if(!Live()||!state_.scene_clock_ready)return {GameFsCode::Unavailable,0,"Light toggle scene owner unavailable"};
  state_.source_flag2A980=value; // commandOwner38 points to this real game-state field
  if(light_on_control_.present){
    if(value){
      if(light_on_control_.data.Statistics().deferred_sections)
        return {GameFsCode::Unavailable,0,"Reached light_on motion unsupported"};
      for(auto& c:season_controls_)if(c.present)c.playback.Stop();
      light_on_control_.playback.Start(0);
    }else{
      light_on_control_.playback.Stop();
      const auto season=SceneSeason709C(scene_calendar_[5],scene_calendar_[4]);
      for(auto& c:season_controls_)if(c.present)c.playback.Stop();
      if(season_controls_[season].present){
        auto& c=season_controls_[season];
        if(c.data.Statistics().deferred_sections)return {GameFsCode::Unavailable,0,"Reached seasonal motion unsupported"};
        c.playback.SetSpeed(30);c.playback.Start(scene_calendar_[3]*216000+scene_calendar_[2]*3600+scene_calendar_[1]*60);
        state_.scene_period_table52=season;
      }
    }
  }
  ++state_.light_toggle_calls;return {};
}
GameFsStatus GameActorRuntime::QueryScenePeriod(uint32_t hour,uint32_t minute,int32_t& result) {
  if(!Live()||!state_.scene_model_live||!state_.scene_clock_ready||state_.manager_scene14!=0)
    return {GameFsCode::Unavailable,0,"GAME36 requires live selected scene0 manager/clock owner"};
  try {scene_calendar_[3]=hour;scene_calendar_[2]=minute;result=ClassifySceneClock7398(SceneClockEndpoints7398(state_.scene_period_table52),hour,minute);return {};}
  catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::ApplyScenePair(uint32_t pair) {
  if (!state_.scene_model_live ||
      scene_controls_.size() != Scene0Names.size() ||
      pair >= Scene0Names.size())
    return {GameFsCode::Unavailable, 0,
            "Scene pair requires actual admitted14-control scene owner"};
  const auto stop = Scene0Stops[pair];
  if (scene_controls_[pair].data.Statistics().deferred_sections)
    return {GameFsCode::Unavailable, 0,
            "Unimplemented reached active scene motion track"};
  scene_controls_[size_t(stop)].playback.Stop();
  scene_controls_[pair].playback.Start(0);
  for (size_t i = 0; i < scene_controls_.size(); ++i)
    state_.scene_playback[i] = scene_controls_[i].playback.Snapshot();
  state_.scene_last_pair = {{int32_t(pair), stop}};
  ++state_.scene_pair_calls;
  if (pair == 3)
    ++state_.scene_pair3_calls;
  return {};
}
GameFsStatus GameActorRuntime::UpdateScene() {
  if (!state_.scene_model_live)
    return {};
  // Head-inserted newer controls precede the fourteen pair controls.
  if(light_on_control_.present){
    const auto status=light_on_control_.playback.Apply(light_on_control_.data,scene_clone_,160);if(!status)return status;
    state_.light_on_playback=light_on_control_.playback.Snapshot();
  }
  for(size_t i=season_controls_.size();i-->0;){auto& c=season_controls_[i];if(!c.present)continue;
    const auto status=c.playback.Apply(c.data,scene_clone_,160);if(!status)return status;
    state_.season_playback[i]=c.playback.Snapshot();
  }
  // 88728 inserts controls at clone-list HEAD:14 construction ->13..0.
  for (size_t n = scene_controls_.size(); n > 0; --n) {
    const auto i = n - 1;
    auto &c = scene_controls_[i];
    const auto status = c.playback.Apply(c.data, scene_clone_, 160);
    if (!status)
      return status;
    state_.scene_playback[i] = c.playback.Snapshot();
  }
  state_.scene_light_nodes=uint32_t(scene_clone_.Lights().type5_nodes.size());
  state_.scene_light_color14=scene_clone_.Lights().packed_color14;
  state_.scene_light_color_writes=scene_clone_.Lights().color_writes;
  state_.scene_light_scalar_writes=state_.scene_light_vector_writes=0;
  for(const auto& p:scene_clone_.Pose()){if(p.source210_written)++state_.scene_light_scalar_writes;if(p.source230_written)++state_.scene_light_vector_writes;}
  return scene_clone_.Update(
      ModelIdentity()); // named native canonical world basis
}
GameFsStatus GameActorRuntime::BindCamera(int32_t mode) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Camera72 target owner expired"};
  // 1CF44/25098: retain raw scalar and link camera90 to LIVE actor40.
  // The zero branch is a symbolic static131D0 target,never a null vector.
  state_.camera_mode = mode;
  state_.camera_follows_actor = mode != 0;
  state_.pose_dirty = mode != 0;
  ++state_.camera_bindings;
  return {};
}
GameFsStatus GameActorRuntime::StartTransition() {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Game transition owner expired"};
  state_.loader_interval = 2000;
  // Actual source17B0->15F8 produced the zero point;not code bytes10D80.
  if (!transition_->StartGameOpen())
    return {GameFsCode::Unavailable, 0,
            "Game transition rejected live request"};
  ++state_.transition_requests;
  return {};
}
bool GameActorRuntime::WaitReady() const {
  if (!Live())
    return false;
  // BCFC checks actual produced flags. Future active commands need their
  // own completion producer;there is no generic one-frame auto-completion.
  return !state_.actor_request_pending && !state_.selected_extension_pending &&
         (state_.actor_special_ready || state_.command_complete ||
          state_.command_ready||state_.command_repeat_ready258);
}
GameFsStatus GameActorRuntime::SetField54(int32_t value) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor field54 owner expired"};
  state_.source_field54 = value;
  return {};
}
GameFsStatus GameActorRuntime::RequestManager9(){
  if(!Live())return {GameFsCode::Unavailable,0,"GAME3 manager owner/lease retired"};
  state_.manager_request9=true;return {};
}
GameFsStatus GameActorRuntime::ClearManager8(){
  if(!Live())return {GameFsCode::Unavailable,0,"Manager8 clear owner retired"};
  state_.manager_request8=false;return {};
}
GameFsStatus GameActorRuntime::ClearManager9(){
  if(!Live())return {GameFsCode::Unavailable,0,"Manager9 clear owner retired"};
  state_.manager_request9=false;return {};
}
GameFsStatus GameActorRuntime::RequestManager() {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Game manager0 owner expired"};
  state_.manager_request8 = true;
  return {}; // 1C210 sets8;not manager-update completion
}
GameFsStatus GameActorRuntime::RequestCamera(int32_t value) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "GAME44 camera manager owner expired"};
  state_.manager_camera_request4C=value;
  ++state_.camera_requests;
  return {}; //1C8C4 writes4C only;23488 consumes on a later owner phase
}
GameFsStatus GameActorRuntime::UpdateCamera() {
  if(state_.manager_camera_request4C!=-1){
    const auto request=state_.manager_camera_request4C;
    //23488 consumes the manager cell BEFORE starting camera7844. Setter
    //has already returned. Request0 takes234F8(out);1 takes2352C(in).
    if(request!=1&&request!=0)return {GameFsCode::Unavailable,0,"Unimplemented reached camera request consumer"};
    if(!state_.camera_bindings||!state_.camera_follows_actor)
      return {GameFsCode::Unavailable,0,"Camera request has no live following target"};
    state_.manager_camera_request4C=-1;++state_.camera_requests_consumed;
    state_.camera_manager_state28=4;state_.camera_transition_mode1C=request==1?1:2;
    state_.camera_complete2C=false;state_.camera_ratio=request==1?0.0f:1.0f;
    camera_from_fov_=state_.camera_fov_degrees;camera_from_x_=state_.camera_position[0];camera_from_z_=state_.camera_position[2];
    //79D8 relocation-backed scene curve. Named native direct piecewise
    //interpolation replaces sourceVFPU quantization. Actual y=-300 lies
    //exactly on its tenth-unit grid. Actor40 dynamic producer stays unknown.
    const float y=state_.actor_root[1];
    constexpr float ys[]{-1000,0,2000,3000},fs[]{23,18,13,11};
    state_.camera_target_fov=y<=ys[0]?fs[0]:fs[3];
    for(unsigned i=0;i<3;++i)if(y>=ys[i]&&y<ys[i+1]){
      state_.camera_target_fov=fs[i]+(fs[i+1]-fs[i])*(y-ys[i])/(ys[i+1]-ys[i]);break;
    }
    if(request==0){
      state_.camera_target_fov=30; //native semantic endpoint;7EE0 literalD3178
      camera_out_target_x_=(std::clamp)(state_.actor_root[0],-950.0f,950.0f); //7844 captures7C3C once
    }
  }else if(state_.camera_manager_state28==4&&state_.camera_complete2C)
    state_.camera_manager_state28=0; //23564 polls completed camera on laterphase
  if(state_.camera_transition_mode1C==2&&!state_.camera_complete2C){
    const float t=(std::clamp)(state_.camera_ratio,0.0f,1.0f),s=t*t*(3-2*t);
    //7EEC/7F04/7F24 evaluate endpoint->captured point at decreasingratio.
    //7C3C finite scene0/default clamp;B1/special scene policies unimplemented.
    state_.camera_fov_degrees=30+(camera_from_fov_-30)*s;
    camera_x_=camera_out_target_x_+(camera_from_x_-camera_out_target_x_)*s;
    camera_z_=980+(camera_from_z_-980)*s;
    //D98CC exactfloat5/6;NativeFixedGameTickScalar2,as for camera-in.
    state_.camera_ratio-=1.0f/(0.8333333730697632f*30.0f);++state_.camera_transition_updates;
    if(state_.camera_ratio<0&&state_.camera_fov_degrees>=30){
      state_.camera_ratio=0;state_.camera_complete2C=true;state_.camera_transition_mode1C=0;++state_.camera_completions;
    }
  }else if(state_.camera_transition_mode1C==1&&!state_.camera_complete2C){
    const float t=(std::clamp)(state_.camera_ratio,0.0f,1.0f);
    const float s=t*t*(3-2*t); //4308 cubic Hermite,zero endpoint tangents
    state_.camera_fov_degrees=camera_from_fov_+(state_.camera_target_fov-camera_from_fov_)*s;
    camera_x_=camera_from_x_+(state_.actor_root[0]-camera_from_x_)*s;
    camera_z_=camera_from_z_+(state_.actor_position[2]-camera_from_z_)*s;
    //Named native fixed game-tick scalar2;duration=2/3 fromD98D0,
    //source7844 rate=1/(duration*(60/2)). No setter/frame-count rendezvous.
    state_.camera_ratio+=1.0f/((2.0f/3.0f)*30.0f);++state_.camera_transition_updates;
    if(state_.camera_ratio>1&&state_.camera_fov_degrees<=state_.camera_target_fov){
      state_.camera_ratio=1;state_.camera_complete2C=true;++state_.camera_completions;
    }
  }else if(state_.camera_complete2C){camera_x_=state_.actor_root[0];camera_z_=state_.actor_position[2];}
  return {};
}
GameFsStatus GameActorRuntime::ShowIntro(int32_t value) {
  if(!Live()||!resources_||(value!=1&&value!=0))return {GameFsCode::Unavailable,0,"GAME43 reached owner/argument domain"};
  if(!intro_resource_){
    auto resource=std::make_shared<GameUiResource>();
    const auto loaded=resource->Load(*resources_,"data\\gui\\syoukai\\SYOUKAI.CB2A","data\\gui\\syoukai\\");
    if(!loaded)return loaded;
    GameUiInstance pending;if(!pending.Clone(resource))return {GameFsCode::Unavailable,0,"CGuiIntro clone refused"};
    for(uint32_t i=0;i<3;++i){
      if(!pending.Bind(i,i,0))return {GameFsCode::Unavailable,0,"CGuiIntro actual channel base refused"};
      auto* control=pending.AddControl(i);if(!control)return {GameFsCode::Unavailable,0,"CGuiIntro owned channel unavailable"};
      control->frame=i?1:0;control->mode=i?2:0;control->active=i?1:0;
    }
    intro_clone_=std::move(pending);intro_resource_=std::move(resource);state_.intro_live=true;
    state_.intro_nodes=uint32_t(intro_clone_.Nodes().size());
  }
  if(value==0){
    auto* c=intro_clone_.Control(1);c->frame=1;c->mode=2;c->active=1;state_.intro_state38=3;
    //Reached other branch remains cold3.3E9E8/964F8 hides actual root0;
    //control0 is disabled. This is immediate source hide,not fade completion.
    if(!intro_clone_.ShowRoot(0,false))return {GameFsCode::Unavailable,0,"CGuiIntro root hide refused"};
    c=intro_clone_.Control(0);c->mode=c->active=0;state_.intro_state34=3;++state_.intro_closes;
    return {};
  }
  if(state_.intro_state34==3){
    if(!intro_clone_.ShowRoot(0)||!intro_clone_.Bind(0,0,0))return {GameFsCode::Unavailable,0,"CGuiIntro actual root activation refused"};
    auto* c=intro_clone_.Control(0);c->frame=0;c->mode=c->active=1;state_.intro_state34=1;
  }
  auto* c=intro_clone_.Control(1);c->frame=1;c->mode=c->active=1;
  state_.intro_state38=1;++state_.intro_requests;
  return {}; //1C890 returns dispatcherFALSE;not waiting for CB2A end
}
GameFsStatus GameActorRuntime::PrepareActor() {
  if (state_.actor_model_live)
    return {};
  try {
    if (!Live() || !resources_ || state_.actor != 0)
      throw std::runtime_error(
          "Actor assets need admitted NEKO source binding");
    // 8E64/8FC0:actual actor0 name and mount21 are relocation-bound NEKO.
    // Native lazy resource ownership replaces eager207-DAK construction.
    PackageView asc;
    auto status = resources_->OpenResource("NEKO\\NEKO\\NEKO.ASC", asc);
    if (!status)
      return status;
    const auto names = AscRecord(asc, 0);
    const auto count = asc.ReadLE32(8);
    if (!count || count > 4096)
      throw std::runtime_error("ASC motion-name count cap");
    std::vector<std::string> parsed;
    uint64_t cursor = 0;
    for (uint32_t i = 0; i < count; ++i) {
      std::string name;
      bool ended = false;
      while (cursor < names.Describe().size) {
        const auto c = names.ReadU8(cursor++);
        if (!c) {
          ended = true;
          break;
        }
        if (c < 32 || c > 126 || name.size() >= 127 || c == '\\' || c == '/' ||
            c == ':')
          throw std::runtime_error(
              "ASC motion component outside native safe domain");
        name.push_back(char(c));
      }
      if (!ended || name.empty())
        throw std::runtime_error("ASC motion name missing/empty");
      parsed.push_back(std::move(name));
    }
    // 98F0/F4's distinct body-name table0D58[0] is R32-bound TORO.
    auto asset = std::make_shared<GameModelAsset>();
    status =
        asset->Load(*resources_, "NEKO\\NEKO\\TORO.DVS", "NEKO\\NEKO\\gim\\");
    if (!status)
      return status;
    GameModelClone clone;
    status = clone.Initialize(asset);
    if (!status)
      return status;
    const auto stats = asset->Statistics();
    actor_asc_ = std::move(asc);
    actor_motion_names_ = std::move(parsed);
    actor_asset_ = std::move(asset);
    actor_clone_ = std::move(clone);
    state_.actor_model_live = true;
    state_.actor_model_nodes = stats.nodes;
    state_.actor_model_vertices = stats.vertices;
    state_.actor_model_triangles = stats.triangles;
    return {};
  } catch (const std::exception &e) {
    return {GameFsCode::Unavailable, 0, e.what()};
  }
}
GameFsStatus GameActorRuntime::RequestActorEvent(int32_t first,
                                                 int32_t second) {
  const auto prepared = PrepareActor();
  if (!prepared)
    return prepared;
  try {
    if (first <= 0 || second != 0)
      throw std::runtime_error(
          "Actor event outside reached positive-command/zero-followup domain");
    if(state_.movement_pending||state_.angular_pending||state_.pending_logical_motion>=0)
      throw std::runtime_error("Actor replacement during an active pose operation is outside the finite native contract");
    auto command = AscRecord(actor_asc_, uint32_t(first));
    // B1D4 writes the same pending38C/390/394 fields on every call.
    // Validation precedes the replacement; the active24700 owner is untouched.
    pending_command_image_ = std::move(command);
    if(state_.actor_request_pending)++state_.event_pending_replacements;
    state_.requested_command = first;
    state_.actor_request_pending = state_.selected_extension_pending = true;
    ++state_.event_requests;
    return {}; // B1D4/1DF00 queue request;no fabricated completion signal
  } catch (const std::exception &e) {
    return {GameFsCode::Unavailable, 0, e.what()};
  }
}
int32_t GameActorRuntime::LogicalMotion(uint32_t record) const {
  const auto bytes = AscRecord(actor_asc_, record);
  const auto size = bytes.Describe().size;
  for (uint64_t at = 0; at + 8 <= size && at < 256; at += 4)
    if (bytes.ReadLE32(at) == 0x8010)
      return int32_t(bytes.ReadLE32(at + 4));
  throw std::runtime_error("BA60 logical record has no bounded8010 motion");
}
GameFsStatus GameActorRuntime::BindActorMotion(int32_t motion, uint32_t blend) {
  if (motion < 0 || size_t(motion) >= actor_motion_names_.size())
    return {GameFsCode::Unavailable, 0,
            "Actor motion index outside actual ASC names"};
  // B834 compares the incoming actual motion with BA60(9). BA30 resolves
  // ordinal9+A690(actor+10), then rejoins the remaining binder. Resolve once:
  // ASC10 itself also names motion9, which is a valid source alias, not an
  // invitation to recurse or replace the actor's requested command.
  if(motion==LogicalMotion(9))
    motion=LogicalMotion(9+uint32_t(GameActorMotionSelectorA690(state_.source_field54)));
  // D7DD4[0] and[1] both select B89C;the reached non-turn branch binds both channels. The
  // finite native full-pose channels replace PSP per-node weight lists.
  if (state_.source_field50 != 0 && state_.source_field50 != 1)
    return {GameFsCode::Unavailable, 0,
            "Unimplemented reached actor channel mode"};
  std::array<ActorChannel, 2> pending;
  for (auto &channel : pending) {
    auto status = channel.data.Load(
        *resources_,
        "NEKO\\NEKO\\" + actor_motion_names_[size_t(motion)] + ".DAK",
        *actor_asset_);
    if (!status)
      return status;
    const auto stats = channel.data.Statistics();
    channel.playback.Configure(stats.duration, stats.loop);
    channel.playback.Start();
    channel.motion = motion;
    channel.blend = channels_[0].motion < 0 ? 0 : blend;
    if (channel.blend)
      channel.previous_pose = actor_clone_.Pose();
  }
  channels_ = std::move(pending);
  state_.channel_admissions += 2;
  if (motion == 123)
    ++state_.motion123_admissions;
  if (state_.first_channel_motion == UINT32_MAX)
    state_.first_channel_motion = uint32_t(motion);
  for (unsigned i = 0; i < 2; ++i) {
    state_.channel_motion[i] = motion;
    state_.channels[i] = channels_[i].playback.Snapshot();
  }
  state_.actor_control_ready = false;
  return {};
}
GameFsStatus GameActorRuntime::UpdateActorPose() {
  if (!state_.actor_model_live || channels_[0].motion < 0)
    return {};
  // Named functional frame clock:160 source units per native update.
  // Both controls are live; the reached equal-channel branch samples the
  // same full skeleton. Native blend is a pose crossfade,not VFPU matching.
  for (unsigned i = 0; i < 2; ++i) {
    auto &c = channels_[i];
    auto status = c.playback.Apply(c.data, actor_clone_, 160);
    if (!status)
      return status;
    state_.channels[i] = c.playback.Snapshot();
  }
  auto &channel = channels_[0];
  if (channel.blend && channel.blend_time < channel.blend) {
    const float t = float(channel.blend_time) / float(channel.blend);
    auto &pose = actor_clone_.Pose();
    for (size_t i = 0; i < pose.size(); ++i) {
      const auto &prior = channel.previous_pose.at(i);
      for (unsigned j = 0; j < 3; ++j) {
        pose[i].position[j] =
            prior.position[j] + (pose[i].position[j] - prior.position[j]) * t;
        pose[i].scale[j] =
            prior.scale[j] + (pose[i].scale[j] - prior.scale[j]) * t;
      }
      float dot = 0;
      for (unsigned j = 0; j < 4; ++j)
        dot += prior.rotation[j] * pose[i].rotation[j];
      float norm = 0;
      for (unsigned j = 0; j < 4; ++j) {
        pose[i].rotation[j] = prior.rotation[j] * (1 - t) +
                              (dot < 0 ? -1 : 1) * pose[i].rotation[j] * t;
        norm += pose[i].rotation[j] * pose[i].rotation[j];
      }
      if (norm < 1e-8f)
        return {GameFsCode::Unavailable, 0,
                "Native actor quaternion blend singular"};
      for (auto &v : pose[i].rotation)
        v /= std::sqrt(norm);
    }
    channel.blend_time = (std::min)(channel.blend, channel.blend_time + 160);
  }
  ++state_.motion_samples;
  // 99A8 logical position and wrap(heading+pi),canonical native basis.
  state_.actor_root = {{state_.logical_xy[0], state_.logical_xy[1], 0, 1}};
  const float angle =
      WrapAngle(state_.logical_heading + 3.14159265358979323846f);
  auto root = ModelIdentity();
  root[0] = std::cos(angle);
  root[1] = std::sin(angle);
  root[4] = -std::sin(angle);
  root[5] = std::cos(angle);
  root[12] = state_.actor_root[0];
  root[13] = state_.actor_root[1];
  root[14] = state_.actor_root[2];
  const auto status = actor_clone_.Update(root);
  if (!status)
    return status;
  const auto prior = state_.actor_control_ready;
  state_.actor_control_ready = channels_[0].playback.Complete();
  if (!prior && state_.actor_control_ready && channels_[0].motion == 123)
    ++state_.motion123_completions;
  return {};
}
GameFsStatus GameActorRuntime::AdvanceActorFrame() {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor phase owner expired"};
  //490E0/499F8: request,disable on one owner phase,release on the next.
  //The clone and its borrowed assets stay owned until this second phase.
  ++state_.room_owner_phases;
  for(size_t i=0;i<models_.size();++i){
    auto& slot=models_[i];auto& owner=state_.room_models[i];
    if(!slot.active||!owner.retirement)continue;
    auto record=std::find_if(state_.model_retirement_history.begin(),state_.model_retirement_history.end(),
      [&](const auto& r){return r.request==slot.retirement_request;});
    if(owner.retirement==1){
      owner.retirement=2;
      if(record!=state_.model_retirement_history.end()){
        record->disabled_phase=state_.room_owner_phases;
        record->assets_retained_at_disable=slot.asset&&slot.asset->Live()&&!slot.retirement_watch.expired();
        record->clone_retained_at_disable=slot.clone.Asset()==slot.asset.get();
        record->nodes_at_disable=slot.asset->Statistics().nodes;record->motions_at_disable=uint32_t(slot.motions.size());
      }
      continue;
    }
    const auto statistics=slot.asset->Statistics();
    state_.model_nodes-=statistics.nodes;state_.model_vertices-=statistics.vertices;
    state_.model_triangles-=statistics.triangles;state_.motion_templates-=uint32_t(slot.motions.size());
    slot.clone.Close();slot.motions.clear();slot.asset.reset();slot.active=false;
    owner.active=false;owner.retirement=0;owner.asset_identity=owner.clone_identity=0;--state_.model_slots;++state_.model_retirements;
    if(record!=state_.model_retirement_history.end()){
      record->destroyed_phase=state_.room_owner_phases;record->asset_owner_expired=slot.retirement_watch.expired();
      record->clone_owner_cleared=slot.clone.Asset()==nullptr&&slot.motions.empty();
    }
  }
  const auto scene = UpdateScene();
  if (!scene)
    return scene;
  const auto room=UpdateRoomModels();if(!room)return room;
  const auto camera=UpdateCamera();if(!camera)return camera;
  if(state_.intro_live){std::string error;if(!intro_clone_.Update(error))return {GameFsCode::Unavailable,0,error};
    ++state_.intro_updates;for(uint32_t i=0;i<3;++i)state_.intro_controls[i]=*intro_clone_.Control(i);}
  if (state_.command_blocked&&!state_.actor_request_pending)
    return UpdateActorPose();
  try {
    ++state_.actor_phases;
    const auto sampled = UpdateActorPose();
    if (!sampled)
      throw std::runtime_error(sampled.detail);
    if (state_.requested_command < 0)
      return {};
    if (state_.actor_request_pending) {
      // The checked ASC grammar is shared by actually requested ordinal
      // records. Each reached opcode/parameter still requires an implemented
      // effect;an unknown instruction stops at its real ordinal/cursor.
      // B774 calls24700 before394 clears;1E824 then copies394 toext20.
      command_image_=std::move(pending_command_image_);
      command_loops_.clear();state_.command_loop_depth=0;
      state_.command_repeat_ready258=false;state_.command_wait_motion=false;
      state_.command_flag25A=false; //24730 clears25A only on natural installation
      state_.command_pc=state_.command_effects=state_.command_frontier=0;state_.effect_order={};
      state_.command_error.clear();state_.command_blocked=false;
      state_.active_command = state_.requested_command;
      state_.command_complete = state_.command_ready = false;
      state_.actor_request_pending = state_.selected_extension_pending = false;
    }
    if (state_.pending_logical_motion >= 0) {
      const auto status = BindActorMotion(
          LogicalMotion(uint32_t(state_.pending_logical_motion)), 1280);
      if (!status)
        throw std::runtime_error(status.detail);
      state_.pending_logical_motion = -1;
    }
    if(state_.movement_pending){
      const float dx=state_.movement_target[0]-state_.actor_root[0];
      const float dy=state_.movement_target[1]-state_.actor_root[1];
      const float distance=std::sqrt(dx*dx+dy*dy);
      // Named native locomotion/math-mode adapter. No immediate teleport or
      // frame quota:the already updated live model root must be within the
      // actual A5A4 threshold2 before native mode4 can finish.
      if(distance<2){
        state_.logical_xy=state_.movement_target;state_.movement_pending=false;
        state_.actor_math_mode=0;++state_.movement_completions;
      }else{
        const float step=(std::min)(256.0f,distance);
        state_.logical_xy={{state_.actor_root[0]+dx*step/distance,state_.actor_root[1]+dy*step/distance}};
        ++state_.movement_updates;
      }
      return {};
    }
    if (state_.angular_pending) {
      // Named native angular adapter:rotate the actual logical/root
      // pose by at most10deg/update;finish only at source1deg threshold.
      const float diff =
          WrapAngle(state_.angular_target - state_.logical_heading);
      const float step = (std::clamp)(diff, -.1745329252f, .1745329252f);
      state_.logical_heading = WrapAngle(state_.logical_heading + step);
      ++state_.angular_updates;
      if (std::abs(WrapAngle(state_.angular_target - state_.logical_heading)) >=
          .01745329252f)
        return {};
      state_.angular_pending = false;
      state_.actor_math_mode = 0;
      ++state_.angular_completions;
      state_.pending_logical_motion = 9;
      return {};
    }
    if (state_.command_wait_motion) {
      if (!state_.actor_control_ready)
        return {};
      state_.command_wait_motion = false;
    }
    const auto size = command_image_.Describe().size;
    const uint64_t at = state_.command_pc;
    if (at > size || size - at < 4)
      throw std::runtime_error("ASC instruction outside live ordinal payload");
    const auto op = command_image_.ReadLE32(at);
    if (op == 0x8040) {
      if (size - at < 8)
        throw std::runtime_error("ASC8040 operand truncated");
      state_.command_flag25A = command_image_.ReadLE32(at + 4) != 0;
      state_.command_pc += 8;
      if (state_.command_effects < state_.effect_order.size())
        state_.effect_order[state_.command_effects] = op;
      ++state_.command_effects;
      return {};
    }
    if (op == 0x8020) {
      if(size-at<8||command_loops_.size()>=64)
        throw std::runtime_error("ASC8020 loop operand/depth outside native capacity");
      state_.command_pc+=8;
      command_loops_.push_back({state_.command_pc,command_image_.ReadLE32(at+4)});
      state_.command_repeat_ready258=false; //243A0 byte258=0
      state_.command_loop_depth=uint32_t(command_loops_.size());
    } else if(op==0x8021) {
      const auto step=AdvanceActorLoop8021(command_loops_,state_.command_pc);
      state_.command_pc=step.pc;state_.command_loop_depth=step.depth;
      if(step.set_ready258)state_.command_repeat_ready258=true;
      if(step.repeated)++state_.command_loop_repeats;
    } else if (op == 0x8010) {
      if (size - at < 12)
        throw std::runtime_error("ASC8010 operand truncated");
      const auto blend = command_image_.ReadLE32(at + 8);
      if (blend > 4096)
        throw std::runtime_error("ASC motion blend cap");
      const auto status = BindActorMotion(
          int32_t(command_image_.ReadLE32(at + 4)), blend * 160);
      if (!status)
        throw std::runtime_error(status.detail);
      state_.command_wait_motion = true;
      state_.command_pc += 12;
    } else if (op == 0x8031) {
      state_.command_ready = true;
      if (state_.active_command == 176)
        state_.asc176_ready = true;
      state_.command_pc += 4;
    } else if (op == 0x8000) {
      if(!command_loops_.empty())throw std::runtime_error("ASC8000 abnormal loop unwind outside reached finite contract");
      state_.command_complete = true;
      if (state_.active_command == 176)
        state_.asc176_complete = true;
      state_.last_completed_command=state_.active_command;
      if(state_.command_completions<state_.first_completed_commands.size())
        state_.first_completed_commands[state_.command_completions]=state_.active_command;
      state_.active_command = -1;
      state_.requested_command = -1;
      state_.command_pc += 4;
      ++state_.command_completions;
    } else {
      if (op != 0x8030 || size - at < 12)
        throw std::runtime_error("Unimplemented/truncated reached ASC opcode");
      const auto parameter = command_image_.ReadLE32(at + 4);
      const auto value = int32_t(command_image_.ReadLE32(at + 8));
      if (parameter == 3 || parameter == 4 || parameter == 11) {
        if (value < 0)
          throw std::runtime_error("Negative ASC coordinate index");
        if (!coordinate_loaded_) {
          const auto status = resources_->OpenResource(
              "DATA\\SEQ\\sequence_coord.bin", coordinate_image_);
          if (!status)
            throw std::runtime_error(status.detail);
          coordinate_loaded_ = true;
        }
        const uint64_t coordinate = uint64_t(uint32_t(value)) * 8;
        if (coordinate_image_.Describe().size < coordinate + 8)
          throw std::runtime_error("ASC coordinate pair extent");
        const std::array<float,2> target{{float(int32_t(coordinate_image_.ReadLE32(coordinate))),
                                       float(int32_t(coordinate_image_.ReadLE32(coordinate+4)))}};
        if(parameter==3){
          state_.logical_xy=target;state_.logical_heading=3.14159265358979323846f;
          state_.heading_known=true;state_.actor_math_mode=1;
        }else{
          state_.movement_target=target;state_.movement_pending=true;
          state_.actor_math_mode=parameter==4?3:4;state_.source_field50=0;
        }
        // B720 applies the mode5 point then B744 queues logical9.
        // Do not skip its real motion8/channel handoff to later ASC ops.
        state_.pending_logical_motion = 9;
        state_.command_pc += 12;
      } else if(parameter==8){
        const auto status=ApplySceneLightToggle(value);if(!status)throw std::runtime_error(status.detail);
        state_.command_pc+=12;
      } else if (parameter == 5) {
        state_.source_field58 = value;
        state_.command_pc += 12;
      } else if (parameter == 0) {
        state_.source_field50 = value;
        state_.command_pc += 12;
      } else if (parameter == 2) {
        state_.angular_target =
            WrapAngle((float(value) + 180) * 3.14159265358979323846f / 180);
        state_.angular_pending = true;
        state_.actor_math_mode = 2;
        state_.command_pc += 12;
      } else if (parameter == 10) {
        const auto status = ApplyScenePair(uint32_t(value));
        if (!status) {
          state_.command_frontier = 0x6D6C;
          throw std::runtime_error(status.detail);
        }
        state_.command_pc += 12;
      } else
        throw std::runtime_error("Unimplemented reached ASC8030 parameter");
    }
    if (state_.command_effects < state_.effect_order.size())
      state_.effect_order[state_.command_effects] = op;
    ++state_.command_effects;
    return {};
  } catch (const std::exception &e) {
    state_.command_blocked = true;
    state_.command_error = e.what();
    if (!state_.command_frontier)
      state_.command_frontier = 0x24098;
    return {}; // Live host frame,NOT successful ASC command completion.
  }
}
GameFsStatus GameActorRuntime::CreateModel(int32_t resource, int32_t category,
                                           int32_t motion, float x,
                                           int32_t &out) {
  out = -1;
  if (!Live() || !resources_)
    return {GameFsCode::Unavailable, 0,
            "Render31 needs live native resource owner"};
  if (resource < 0 || uint32_t(resource) >= GameModelResources.size())
    return {GameFsCode::Unavailable, 0,
            "Render31 resource outside finite208 table"};
  if (category != 0 || motion < 0 || motion > 9 || !std::isfinite(x))
    return {
        GameFsCode::Unavailable, 0,
        "Render31 reached category/transform/motion domain not yet recovered"};
  GameMatrix root;int32_t anchor=-1;
  auto controller=RoomModelRoot(motion,x,root,anchor);if(!controller)return controller;
  if (state_.model_slots >= models_.size())
    return {}; // source returns -1 on full pool
  size_t index = 0;
  while (index < models_.size() && models_[index].active)
    ++index;
  if (index == models_.size())
    return {};
  auto asset = std::make_shared<GameModelAsset>();
  const auto &entry = GameModelResources[size_t(resource)];
  const std::string base = std::string("BG_ROOM\\") + entry.name + "\\";
  const auto path = base + "model01.DVS";
  auto status = asset->Load(*resources_, path, base + "gim\\");
  if (!status)
    return status;
  std::vector<GameModelMotion> motions;
  for (uint32_t i = 0; i < entry.motions; ++i) {
    GameModelMotion data;
    status = data.Load(
        *resources_, base + "model0" + std::to_string(i + 1) + ".DAK", *asset);
    if (!status)
      return status;
    motions.push_back(std::move(data));
  }
  GameModelClone clone;
  status = clone.Initialize(asset);
  if (!status)
    return status;
  status = clone.Update(root);
  if (!status)
    return status;
  auto &slot = models_[index];
  slot.resource = resource;
  slot.category = category;
  slot.asset = std::move(asset);
  slot.clone = std::move(clone);
  slot.motions = std::move(motions);
  slot.active = true;slot.retirement_watch.reset();slot.retirement_request=0;
  //Finite native connection of source node ownership and source scalar X.
  state_.room_models[index]={true,0,++state_.model_publications,resource,category,motion,x,anchor,root};
  state_.room_models[index].asset_identity=++state_.model_asset_acquisitions;
  state_.room_models[index].clone_identity=++state_.model_clone_initializations;
  const auto statistics = slot.asset->Statistics();
  ++state_.model_slots;
  state_.model_nodes += statistics.nodes;
  state_.model_vertices += statistics.vertices;
  state_.model_triangles += statistics.triangles;
  state_.last_model_slot = int32_t(index);
  state_.last_model_path = path;
  state_.motion_templates += entry.motions;
  out = int32_t(index);
  return {};
}
GameFsStatus GameActorRuntime::UpdateModel(int32_t id,int32_t selector,float x){
  if(!Live()||id<0||size_t(id)>=models_.size())return {GameFsCode::Unavailable,0,"GAME33 model owner/slot domain"};
  auto& slot=models_[size_t(id)];auto& owner=state_.room_models[size_t(id)];
  if(!slot.active)return {}; //49740 has no allocation or replacement operation.
  if(owner.retirement||owner.category!=0||selector<0||selector>9||!std::isfinite(x))
    return {GameFsCode::Unavailable,0,"GAME33 unsupported live model controller"};
  GameMatrix root;int32_t anchor=-1;auto status=RoomModelRoot(selector,x,root,anchor);if(!status)return status;
  status=slot.clone.Update(root);if(!status)return status;
  owner.selector=selector;owner.source_x=x;owner.native_root=root;owner.anchor_node=anchor;++state_.model_updates;
  return {};
}
GameFsStatus GameActorRuntime::RetireModel(int32_t id){
  if(!Live()||id<0||size_t(id)>=models_.size())return {GameFsCode::Unavailable,0,"GAME32 model owner/slot domain"};
  auto& slot=models_[size_t(id)];auto& owner=state_.room_models[size_t(id)];
  if(slot.active){
    //490C8 repeats restart request7=1;history is passive and bounded.
    if(state_.model_retirement_history.size()==16){state_.model_retirement_history.erase(state_.model_retirement_history.begin());++state_.model_retirement_history_dropped;}
    GameModelRetirementWitness record;record.request=uint64_t(state_.model_retire_requests)+1;
    record.generation=owner.generation;record.requested_phase=state_.room_owner_phases;record.slot=id;record.resource=owner.resource;
    state_.model_retirement_history.push_back(record);slot.retirement_watch=slot.asset;slot.retirement_request=record.request;
    owner.retirement=1;++state_.model_retire_requests;
  }
  return {};
}
GameFsStatus GameActorRuntime::Draw(GameGraphics &graphics,const std::function<GameFsStatus(GameGraphics&)>& overlay) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor render owners expired"};
  //Before the first room/camera publication this owner is only bootstrapped.
  //After any model publication,zero remaining slots must still draw/present.
  if(!state_.model_publications&&!state_.camera_bindings)return {};
  if (!state_.camera_bindings || !state_.camera_follows_actor)
    return {GameFsCode::Unavailable, 0,
            "Room render needs actual following-camera binding;static131D0 "
            "producer unrecovered"};
  // Source493C0 uses an unrecovered world basis. Named functional policy:
  // canonical native identity basis,authored geometry,NO fit/recenter/scale.
  // Cold7650/7E28/8140 eye(0,-4400,980),look+Y,FOV30,near100/far10400.
  // Named functional follow policy uses the actually produced native root
  // XY as eye offset once a real channel exists. It does NOT manufacture a
  // source actor40 write:state.actor_position retains its cold source value.
  // No asset bounds fit,scale or viewer recenter is performed.
  const float follow_x = channels_[0].motion >= 0 ? state_.actor_root[0] : 0;
  const float follow_y = channels_[0].motion >= 0 ? state_.actor_root[1] : 0;
  const float eye_x=state_.camera_transition_mode1C?camera_x_:follow_x;
  const float eye_z=state_.camera_transition_mode1C?camera_z_:980;
  state_.camera_position = {{eye_x, follow_y - 4400, eye_z, 1}};
  state_.camera_look = {{eye_x, follow_y - 4399, eye_z, 1}};
  auto view = ModelIdentity();
  view[5] = 0;
  view[9] = 1;
  view[6] = -1;
  view[10] = 0;
  view[12] = -eye_x;
  view[13] = -eye_z;
  view[14] = follow_y - 4400;
  const auto camera = ModelMultiply(
      ModelPerspective(state_.camera_fov_degrees*3.14159265358979323846f/180, 480.0f / 272.0f, 100, 10400), view);
  if (!graphics.Clear(0xFF000000, 0))
    return {GameFsCode::Unavailable, 0, "Room clear failed"};
  const auto prior_pixels = graphics.Snapshot().mesh_pixels;
  for (size_t i=0;i<models_.size();++i)
    if (const auto& slot=models_[i];slot.active&&state_.room_models[i].retirement<2) {
      const auto pixels=graphics.Snapshot().mesh_pixels;
      // Same named Windows DVS colour convention as the actor: neutral128
      // diffuse vertex colours should not halve the owned texture. This is
      // not recovered PSP combiner or time-of-day lighting semantics.
      const auto status = slot.clone.Draw(graphics, camera,
          GameModelColorPolicy::WindowsDvsTexturedModulation2x);
      if (!status)
        return status;
      ++state_.room_models[i].draw_frames;
      state_.room_models[i].draw_pixel_writes+=graphics.Snapshot().mesh_pixels-pixels;
    }
  if (state_.scene_model_live) {
    const auto status = scene_clone_.Draw(graphics, camera,
        GameModelColorPolicy::WindowsDvsTexturedModulation2x);
    if (!status)
      return status;
    ++state_.scene_frames;
  }
  if (channels_[0].motion >= 0) {
    GameContactFootprint contact;
    auto contact_status=actor_clone_.ContactFootprint(contact);
    if(!contact_status)return contact_status;
    bool supported=false;float floor_height=0;
    if(contact.valid){
      // Native visual receiver policy: only actually published Carpet assets,
      // not arbitrary object tops, decorative shadow meshes or a guessed Z0.
      const auto penetration=(std::max)(5.0f,contact.body_height*.005f);
      for(size_t i=0;i<models_.size();++i){const auto &slot=models_[i];const auto &owner=state_.room_models[i];
        if(!slot.active||owner.retirement>=2||owner.resource<0||size_t(owner.resource)>=GameModelResources.size())continue;
        const std::string name=GameModelResources[size_t(owner.resource)].name;
        if(name.rfind("Carpet",0)!=0)continue;
        bool found=false;float height=0;
        contact_status=slot.clone.HorizontalSurfaceBelow(contact.x,contact.y,contact.foot_z+penetration,
                                                        contact.body_height*.25f+penetration,found,height);
        if(!contact_status)return contact_status;
        if(found&&(!supported||height>floor_height)){supported=true;floor_height=height;}
      }
      if(supported){contact_status=DrawWindowsContactShadow(graphics,camera,contact,floor_height);
        if(!contact_status)return contact_status;}
    }
    const auto before = graphics.Snapshot().mesh_pixels;
    // Bounded Toro color-continuity approximation: source textured RGB is
    // near128, untextured body near255. PSP combiner semantics are unrecovered.
    // World models opt into the same colour policy above; UI remains1x.
    // No source asset is changed.
    auto status = actor_clone_.Draw(graphics, camera,
        GameModelColorPolicy::WindowsDvsTexturedModulation2x,
        GameModelLightingPolicy::WindowsActorSoftDiffuse);
    if (!status)
      return status;
    const auto pixels = graphics.Snapshot().mesh_pixels - before;
    state_.actor_pixel_writes += pixels;
    state_.actor_visible_pixels = uint32_t(pixels);
    ++state_.actor_draw_frames;
  }
  if(state_.intro_live){GameUiDrawStats drawn;std::string error;
    if(!intro_clone_.Draw(graphics,drawn,error))return {GameFsCode::Unavailable,0,error};
    state_.intro_quads=drawn.quads;}
  if (!transition_->Draw(graphics))
    return {GameFsCode::Unavailable, 0, "Room transition draw failed"};
  if(overlay){const auto drawn=overlay(graphics);if(!drawn)return drawn;}
  if (!graphics.SwapBuffers())
    return {GameFsCode::Unavailable, 0, "Room frame presentation failed"};
  state_.model_pixel_writes += graphics.Snapshot().mesh_pixels - prior_pixels;
  GameGraphicsFrame frame;
  if (!graphics.CopyFront(frame))
    return {GameFsCode::Unavailable, 0, "Room front-buffer ownership failed"};
  state_.visible_pixels = 0;
  for (uint32_t y = 0; y < frame.height; ++y)
    for (uint32_t x = 0; x < frame.width; ++x)
      if ((frame.pixels[size_t(y) * frame.stride + x] & 0xFFFFFF) != 0)
        ++state_.visible_pixels;
  ++state_.model_frames;
  return {};
}
void GameActorRuntime::Close() {
  intro_clone_.Close();intro_resource_.reset();state_.intro_live=false;state_.intro_controls={};
  light_on_control_={};season_controls_.clear();
  state_.scene_clock_ready=state_.light_on_control_present=false;
  state_.scene_light_nodes=0;
  state_.season_controls=0;state_.season_present={};state_.season_playback={};state_.light_on_playback={};
  scene_controls_.clear();
  scene_clone_.Close();
  scene_asset_.reset();
  regions_.Close();state_.region_owners_live=false;state_.region_counts={};
  state_.scene_model_live = false;
  state_.scene_controls = 0;
  state_.scene_playback = {};
  for (auto &c : channels_) {
    c = {};
  }
  state_.channel_motion = {{-1, -1}};
  state_.channels = {};
  state_.command_wait_motion = state_.angular_pending = state_.movement_pending = false;
  actor_clone_.Close();
  actor_asset_.reset();
  actor_asc_.Reset();
  command_image_.Reset();
  pending_command_image_.Reset();
  command_loops_.clear();state_.command_loop_depth=0;
  state_.command_repeat_ready258=false;
  coordinate_image_.Reset();
  actor_motion_names_.clear();
  state_.actor_model_live = false;
  state_.actor_request_pending = state_.selected_extension_pending = false;
  coordinate_loaded_ = false;
  state_.active_command = -1;
  state_.pending_logical_motion = -1;
  state_.manager_request8 = false;
  state_.manager_request9 = false;
  state_.manager_camera_request4C=-1;state_.camera_manager_state28=state_.camera_transition_mode1C=0;
  state_.camera_complete2C=false;
  camera_from_fov_=30;camera_from_z_=camera_z_=980;camera_from_x_=camera_x_=0;
  camera_out_target_x_=0;
  state_.command_flag25A = false;
  for (auto &slot : models_) {
    slot.clone.Close();
    slot.asset.reset();
    slot.motions.clear();
    slot.active = false;
  }
  state_.model_slots = 0;
  for(auto& owner:state_.room_models){owner.active=false;owner.retirement=0;owner.asset_identity=owner.clone_identity=0;}
  selection_.Reset();
  transition_ = nullptr;
  resources_ = nullptr;
  state_.live = false;
  state_.camera_follows_actor = false;
}
} // namespace dokodemo::recovered

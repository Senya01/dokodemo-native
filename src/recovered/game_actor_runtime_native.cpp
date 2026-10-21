#include "game_actor_runtime_native.h"
#include "game_model_resources_native.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
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
  return status;
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
          state_.command_ready);
}
GameFsStatus GameActorRuntime::SetField54(int32_t value) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor field54 owner expired"};
  state_.source_field54 = value;
  return {};
}
GameFsStatus GameActorRuntime::RequestManager() {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Game manager0 owner expired"};
  state_.manager_request8 = true;
  return {}; // 1C210 sets8;not manager-update completion
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
    if (first < 0 || second != 0)
      throw std::runtime_error(
          "Actor event outside reached positive-command/zero-followup domain");
    if (state_.actor_request_pending || state_.selected_extension_pending)
      throw std::runtime_error("Actor event already pending");
    auto command = AscRecord(actor_asc_, uint32_t(first));
    command_image_ = std::move(command);
    state_.requested_command = first;
    state_.command_pc = state_.command_effects = state_.command_frontier = 0;
    state_.effect_order={};
    state_.command_error.clear();
    state_.command_blocked = false;
    state_.command_ready = false;
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
  if(state_.source_field54!=0&&motion==LogicalMotion(9))
    return {GameFsCode::Unavailable,0,"Directional neutral-motion aliases not yet admitted"};
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
  const auto scene = UpdateScene();
  if (!scene)
    return scene;
  if (state_.command_blocked)
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
    if (op == 0x8010) {
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
  if (category != 0 || motion != 0 || x != 0)
    return {
        GameFsCode::Unavailable, 0,
        "Render31 reached category/transform/motion domain not yet recovered"};
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
  status = clone.Update(ModelIdentity());
  if (!status)
    return status;
  auto &slot = models_[index];
  slot.resource = resource;
  slot.category = category;
  slot.asset = std::move(asset);
  slot.clone = std::move(clone);
  slot.motions = std::move(motions);
  slot.active = true;
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
GameFsStatus GameActorRuntime::Draw(GameGraphics &graphics) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor render owners expired"};
  if (!state_.model_slots)
    return {};
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
  state_.camera_position = {{follow_x, follow_y - 4400, 980, 1}};
  state_.camera_look = {{follow_x, follow_y - 4399, 980, 1}};
  auto view = ModelIdentity();
  view[5] = 0;
  view[9] = 1;
  view[6] = -1;
  view[10] = 0;
  view[12] = -follow_x;
  view[13] = -980;
  view[14] = follow_y - 4400;
  const auto camera = ModelMultiply(
      ModelPerspective(0.5235987756f, 480.0f / 272.0f, 100, 10400), view);
  if (!graphics.Clear(0xFF000000, 0))
    return {GameFsCode::Unavailable, 0, "Room clear failed"};
  const auto prior_pixels = graphics.Snapshot().mesh_pixels;
  for (const auto &slot : models_)
    if (slot.active) {
      const auto status = slot.clone.Draw(graphics, camera);
      if (!status)
        return status;
    }
  if (state_.scene_model_live) {
    const auto status = scene_clone_.Draw(graphics, camera);
    if (!status)
      return status;
    ++state_.scene_frames;
  }
  if (channels_[0].motion >= 0) {
    const auto before = graphics.Snapshot().mesh_pixels;
    auto status = actor_clone_.Draw(graphics, camera);
    if (!status)
      return status;
    const auto pixels = graphics.Snapshot().mesh_pixels - before;
    state_.actor_pixel_writes += pixels;
    state_.actor_visible_pixels = uint32_t(pixels);
    ++state_.actor_draw_frames;
  }
  if (!transition_->Draw(graphics) || !graphics.SwapBuffers())
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
  light_on_control_={};season_controls_.clear();
  state_.scene_clock_ready=state_.light_on_control_present=false;
  state_.scene_light_nodes=0;
  state_.season_controls=0;state_.season_present={};state_.season_playback={};state_.light_on_playback={};
  scene_controls_.clear();
  scene_clone_.Close();
  scene_asset_.reset();
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
  coordinate_image_.Reset();
  actor_motion_names_.clear();
  state_.actor_model_live = false;
  state_.actor_request_pending = state_.selected_extension_pending = false;
  coordinate_loaded_ = false;
  state_.active_command = -1;
  state_.pending_logical_motion = -1;
  state_.manager_request8 = false;
  state_.command_flag25A = false;
  for (auto &slot : models_) {
    slot.clone.Close();
    slot.asset.reset();
    slot.motions.clear();
    slot.active = false;
  }
  state_.model_slots = 0;
  selection_.Reset();
  transition_ = nullptr;
  resources_ = nullptr;
  state_.live = false;
  state_.camera_follows_actor = false;
}
} // namespace dokodemo::recovered

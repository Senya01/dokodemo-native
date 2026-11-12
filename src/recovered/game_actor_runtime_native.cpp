#include "game_actor_runtime_native.h"
#include "game_actor_script_native.h"
#include "game_inventory_native.h"
#include "game_notebook_native.h"
#include "game_wordmemo_native.h"
#include "game_diary_native.h"
#include "game_system_option_native.h"
#include "../script_vm.h"
#include "game_model_resources_native.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dokodemo::recovered {
struct GameActorRuntime::PrimaryCameraTarget {
  std::weak_ptr<const void> manager;
  PackageView module;
  uint64_t epoch=0;
  std::array<float,4> position{};
  bool Current(const std::shared_ptr<const void>& parent,uint64_t current_epoch)const{
    const auto p=manager.lock();
    if(!p||p!=parent||p.owner_before(parent)||parent.owner_before(p)||epoch!=current_epoch)return false;
    try{return module.Describe().readable;}catch(...){return false;}
  }
};
namespace {
float WindowsCameraCurve79D8(float y){
  // Existing finite native curve adapter: host piecewise interpolation,
  // not PSP VFPU quantization. The input now comes from actual camera90.
  constexpr float ys[]{-1000,0,2000,3000},fs[]{23,18,13,11};
  float result=y<=ys[0]?fs[0]:fs[3];
  for(unsigned i=0;i<3;++i)if(y>=ys[i]&&y<ys[i+1]){
    result=fs[i]+(fs[i+1]-fs[i])*(y-ys[i])/(ys[i+1]-ys[i]);break;
  }
  return result;
}
}
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
bool GameManagerSwitchControl::Initialize(uint64_t epoch,std::shared_ptr<const void> module){
  if(!epoch||epoch<=last_module_epoch_||state_.live)return false;
  state_={};resource_requests_.clear();state_.live=true;state_.epoch=epoch;last_module_epoch_=epoch;
  loaded_models_.clear();
  // Execute177F8, not an adoption-time default or a PSP RAM observation.
  state_.loadman.module_epoch=epoch;state_.loadman.transition50=0;state_.loadman.ui464=0;
  state_.loadman.application20=state_.loadman.next24=-1;state_.loadman.phase28=1;
  state_.loadman.current_variant4A8=state_.loadman.staged_variant4AC=1;
  state_.loadman.cached_period4B4=2;state_.loadman.stored_bgm4BC=0;stored_bgm4BC_=std::make_shared<int32_t>(0);
  state_.loadman.transition50_writer=0x17938;
  state_.loadman.ui464_writer=0x17908;
  state_.loadman.load_writer=0x17860;state_.loadman.change_writer=0x17874;
  if(!module){standalone_module_=std::make_shared<uint64_t>(epoch);module=standalone_module_;}
  module_=module;return true;
}
bool GameManagerSwitchControl::Current()const{return state_.live&&!module_.expired();}
bool GameManagerSwitchControl::CurrentBorrow(const std::shared_ptr<GameManagerSwitchControl>& borrow)const{
  const auto self=weak_from_this().lock();return Current()&&self&&borrow&&self.get()==borrow.get()
    &&!self.owner_before(borrow)&&!borrow.owner_before(self);
}
void GameManagerSwitchControl::BindUi(const GameInventory& inventory,const GameNotebook& notebook,
    const GameWordMemo& wordmemo,const GameDiary& diary,const GameSystemOptionFrontier& options){
  inventory_=&inventory;notebook_=&notebook;wordmemo_=&wordmemo;diary_=&diary;options_=&options;
}
GameManagerSwitchSnapshot GameManagerSwitchControl::Snapshot()const{
  auto out=state_;out.live=Current();out.loadman.current=out.live;out.loadman.ui_current=out.live;
  out.loadman.rendered_current=out.live&&state_.bound&&out.loadman.rendered_selector4DC>=0;
  out.old_bit_known=ResourceRequest(out.old_resource,out.old_bit);
  out.new_bit_known=ResourceRequest(out.new_resource,out.new_bit);
  if(!out.live)return out; // never dereference Content members after parent retirement
  if(stored_bgm4BC_)out.loadman.stored_bgm4BC=*stored_bgm4BC_;
  const bool any=inventory_||notebook_||wordmemo_||diary_||options_;
  if(any&&!(inventory_&&notebook_&&wordmemo_&&diary_&&options_)){out.loadman.ui_current=false;return out;}
  // The actual native owners are registered by Content, never supplied by a
  // completion certificate/provider. Native projection preserves their leases.
  unsigned active=0;
  const auto select=[&](uint32_t selector,uint64_t generation,bool pending){
    if(!pending)return;++active;out.loadman.ui464=selector;out.loadman.ui_generation=generation;
    out.loadman.ui464_writer=0; // typed Windows publication, not a guessed PSP store PC
  };
  if(any){
    const auto i=inventory_->Snapshot();select(0x14,i.generation,i.phase>=1&&i.phase<=6); // source14(hex), legacy inventory loader464 uses native14
    const auto n=notebook_->Snapshot();select(9,n.generation,n.phase>=1&&n.phase<=6);
    const auto w=wordmemo_->Snapshot();select(8,w.generation,w.phase>=1&&w.phase<=6);
    const auto d=diary_->Snapshot();select(12,d.generation,d.phase>=1&&d.phase<=6);
    const auto o=options_->Snapshot();select(10,o.generation,o.phase>=1&&o.phase<=6);
  }
  // Title has its own earlier current-UI domain; no ordinary travel borrow
  // is issued during it. Real Content handoff, not a forced UI reset, exits it.
  if(active>1||state_.loadman.application20==3)out.loadman.ui_current=false;
  return out;
}
bool GameManagerSwitchControl::ResourceRequest(uint32_t index,bool& value)const{
  if(!Current())return false;const auto it=resource_requests_.find(index);
  if(it==resource_requests_.end())return false;value=it->second;return true;
}
bool GameManagerSwitchControl::OrdinaryUiIdle()const{
  const auto s=Snapshot();return s.loadman.current&&s.loadman.ui_current
    &&s.loadman.transition50==0&&s.loadman.ui464==0;
}
bool GameManagerSwitchControl::BindActor(uint64_t epoch){
  if(!Current()||!epoch||state_.bound||state_.pending)return false;state_.epoch=epoch;return true;
}
void GameManagerSwitchControl::ReleaseActor(uint64_t epoch){
  if(Current()&&state_.epoch==epoch)state_.bound=false; // preserve pending/source history
}
bool GameManagerSwitchControl::RequestApplication(int32_t scene,std::optional<uint32_t> flag){
  if(!Current()||scene<1||(scene>4&&scene!=6))return false;
  auto& l=state_.loadman;l.phase28=2;l.application_request_order={{0x19AB0,0,0}};
  // Legacy Windows startup callers without a recovered a2 remain explicitly
  // unbound. Only an executed, admitted source call supplies this byte.
  l.application_flag_known=bool(flag);
  if(flag){l.application_flagEA0=uint8_t(*flag);l.application_request_order[1]=0x19AB4;}
  l.next24=scene;l.application_request_order[2]=0x19ABC;++l.dispatches;return true;
}
bool GameManagerSwitchControl::CommitApplication(){
  if(!Current()||state_.loadman.phase28!=2||state_.loadman.next24<1||(state_.loadman.next24>4&&state_.loadman.next24!=6))return false;
  state_.loadman.application20=state_.loadman.next24;++state_.loadman.dispatches;return true; //17B84
}
bool GameManagerSwitchControl::Phase(uint32_t phase){
  if(!Current()||phase<1||phase>6)return false;
  state_.loadman.phase28=phase;++state_.loadman.dispatches;return true;
}
bool GameManagerSwitchControl::BindScene0(uint64_t epoch){
  if(!Current()||epoch!=state_.epoch||state_.bound||state_.pending)return false;
  state_.current14=state_.requested18=0;state_.bound=true;state_.loadman.rendered_selector4DC=0;return true;
}
bool GameManagerSwitchControl::RequestSame0(uint64_t epoch,int32_t target){
  if(!Current()||!state_.bound||epoch!=state_.epoch||target!=0||state_.current14!=0||state_.pending||!OrdinaryUiIdle())return false;
  state_.pending=true;state_.requested18=target;
  state_.last_request_store_pcs={{0x1C2E4,0x1C2EC}};++state_.requests;return true;
}
bool GameManagerSwitchControl::RequestPending12(uint64_t epoch){
  if(!Current()||!state_.bound||state_.pending||epoch!=state_.epoch||state_.current14!=0||!OrdinaryUiIdle())return false;
  state_.pending=true;state_.requested18=12;
  state_.last_request_store_pcs={{0x1C2E4,0x1C2EC}};++state_.requests;return true;
}
bool GameManagerSwitchControl::ConsumeSame0(uint64_t epoch){
  if(!Current()||!state_.bound||epoch!=state_.epoch||!state_.pending||state_.current14!=0||state_.requested18!=0||!OrdinaryUiIdle())return false;
  state_.pending=false;++state_.consumed;return true; //1B2C8/CC, no resource calls
}
void GameManagerSwitchControl::Retire(){
  state_.live=state_.bound=state_.pending=false;module_.reset();standalone_module_.reset();stored_bgm4BC_.reset();
    state_.asc6_cell.reset();state_.asc6_writes=0;
  inventory_=nullptr;notebook_=nullptr;wordmemo_=nullptr;diary_=nullptr;options_=nullptr;bgm_=nullptr;bgm_lifetime_.reset();
  loaded_models_.clear();
}

namespace {
std::string ContextActorPath(const PackageView& input,bool empty_allowed){
  std::string out;const auto n=input.Describe().size;
  if(!n||n>256)throw std::runtime_error("context actor path extent");
  for(uint64_t i=0;i<n;++i){const auto c=input.ReadU8(i);
    if(!c){if(i+1!=n||(!empty_allowed&&out.empty())||out.find("..")!=out.npos||(!out.empty()&&(out[0]=='/'||out[0]=='\\')))
        throw std::runtime_error("context actor path domain");return out;}
    if(c<32||c>126||c==':')throw std::runtime_error("context actor path character");out+=char(c);}
  throw std::runtime_error("context actor path terminator");
}
float ContextActorFloat(const PackageView& input,unsigned i){
  const auto bits=input.ReadLE32(i*4);float value;std::memcpy(&value,&bits,4);
  if(!std::isfinite(value))throw std::runtime_error("context actor constant nonfinite");return value;
}
// Authored native initializer, not a copied PSP24-byte packet. Leases own
// initialized readonly native data just like caller-supplied packet inputs.
struct ContextActorInput final:PackageStorageLease {
  std::vector<uint8_t> bytes,known;
  bool Span(PackageStorageSpan& out,std::string&)const override{
    out={const_cast<uint8_t*>(bytes.data()),const_cast<uint8_t*>(known.data()),bytes.size(),
      uint64_t(reinterpret_cast<uintptr_t>(bytes.data())),uint64_t(reinterpret_cast<uintptr_t>(this)),1,1,0,true,false};return true;
  }
};
PackageView ActorTextInput(const std::string& text){
  auto p=std::make_shared<ContextActorInput>();p->bytes.assign(text.begin(),text.end());p->bytes.push_back(0);p->known.assign(p->bytes.size(),1);
  return PackageStorage::Attach(p,p->bytes.size());
}
}
struct GameActorRuntime::ContextActorData {
  GameContextActorSnapshot state;
  std::weak_ptr<const void> manager;
  std::shared_ptr<GameModelAsset> asset;
  GameModelClone clone;
  std::array<PackageView,3> inputs;
  std::array<std::array<uint8_t,32>,3> digests{};
  bool Current()const{
    if(manager.expired()||state.presence==GameContextActorPresence::Unknown)return false;
    if(state.presence==GameContextActorPresence::Absent)return true;
    try{for(unsigned i=0;i<3;++i)if(inputs[i].InitializedDigest()!=digests[i])return false;
      return asset&&asset->SourceCurrent()&&clone.Asset()==asset.get()&&clone.Generation()==state.model_generation;
    }catch(...){return false;}
  }
  void Move(float x,float heading){
    const auto& data=inputs[2];const auto divisor=ContextActorFloat(data,4),multiplier=ContextActorFloat(data,5);
    if(divisor==0)throw std::runtime_error("context actor angle divisor");
    const float angle=(heading/divisor)*multiplier;
    state.logical[0]=x;state.logical[1]=state.logical[2]=0;
    if(!std::isfinite(angle))throw std::runtime_error("context actor angle overflow");
    // BD78 writes logical fields only. Actual posed world/animation update is
    // a later consumer; do not move the unrelated primary actor or fake a pose.
    state.logical[3]=angle;++state.moves;
  }
  void Close(){clone.Close();asset.reset();inputs={};state.current=false;}
  ~ContextActorData(){Close();}
};
bool GameActorRuntime::CurrentAscActorField60()const{
  if(!Live()||!state_.actor_model_live||!asc_actor_field60_)return false;
  const auto cell=asc_field60_binding_.lock();const auto owner=asc_actor_field60_->owner.lock();
  const auto module=asc_module_owner_.lock();
  if(!cell||cell!=asc_actor_field60_||cell.owner_before(asc_actor_field60_)||asc_actor_field60_.owner_before(cell)
     ||cell->actor!=this||cell->epoch!=state_.owner_epoch||!owner||owner!=support_manager_
     ||owner.owner_before(support_manager_)||support_manager_.owner_before(owner)
     ||!module||module!=manager_switch_||module.owner_before(manager_switch_)||manager_switch_.owner_before(module)
     ||module->state_.loadman.module_epoch!=asc_module_epoch_)return false;
  // Readback describes this owned cell's lifetime. Source byte/digest checks
  // remain at the actual consuming store, not every observation/frame.
  return true;
}
GameActorRuntimeSnapshot GameActorRuntime::Snapshot()const{
  auto result=state_;result.destination=destination_state_;
  result.source_field60_current=CurrentAscActorField60();
  result.source_field60=result.source_field60_current?asc_actor_field60_->value:std::optional<int32_t>{};
  result.source_field60_writes=result.source_field60_current?asc_actor_field60_->writes:0;
  if(manager_switch_)result.manager_switch=manager_switch_->Snapshot();
  result.camera_target_current=ReadCameraTarget(result.camera_target_position);
  result.camera_target_epoch=result.camera_target_current?state_.owner_epoch:0;
  if(context_actor_){result.context_actor=context_actor_->state;result.context_actor.current=Live()&&context_actor_->Current();}
  result.context_actor_bindings=uint32_t(context_actors_.size());return result;
}
GameFsStatus GameActorRuntime::CreateContextActor5(GameResourceSource& source,PackageView path,PackageView prefix,PackageView constants){
  if(!Live()||(context_actor_&&context_actor_->state.presence==GameContextActorPresence::Present))
    return {GameFsCode::Unavailable,0,"context actor slot needs live manager and explicit prior retirement"};
  try{
    if(constants.Describe().size!=24)throw std::runtime_error("context actor exact six-constant packet required");
    auto pending=std::make_shared<ContextActorData>();pending->manager=support_manager_;
    pending->inputs={{path,prefix,constants}};
    for(unsigned i=0;i<3;++i)pending->digests[i]=pending->inputs[i].InitializedDigest();
    for(unsigned i=0;i<6;++i)(void)ContextActorFloat(constants,i);
    if(ContextActorFloat(constants,4)==0)throw std::runtime_error("context actor zero angle divisor");
    pending->asset=std::make_shared<GameModelAsset>();auto status=pending->asset->Load(source,ContextActorPath(path,false),ContextActorPath(prefix,true));
    if(!status)return status;status=pending->clone.Initialize(pending->asset);if(!status)return status;
    pending->state.presence=GameContextActorPresence::Present;pending->state.epoch=state_.owner_epoch;
    pending->state.serial=++context_actor_serial_;pending->state.constructor_actor=5;
    pending->state.model_generation=pending->clone.Generation();pending->state.parameters_sha256=pending->digests[2];
    pending->Move(ContextActorFloat(constants,0),ContextActorFloat(constants,1));
    pending->state.model_visible=false;++pending->state.hides; // actual1A18C typed model visibility store
    if(!Live()||!pending->Current())throw std::runtime_error("context actor constructor input/owner changed");
    context_actor_=std::move(pending);return {};
  }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::CreateOrdinaryContextActor5(){
  if(!resources_)return {GameFsCode::Unavailable,0,"ordinary actor5 resource provider absent"};
  // Selected26 registration and D7C[5]/D58[5] identify the same KURO mount,
  // folder and body. 8D78/9214 formats are consumed as actual asset paths;
  // no original name table, constructor byte array or default pose is copied.
  const std::string stem="KURO",base=stem+"\\"+stem+"\\";
  auto parameters=std::make_shared<ContextActorInput>();
  // Distinct ctor/request source addresses, equal current literal values.
  for(float value:{-2000.f,270.f,-2000.f,270.f,3.1415927410125732f,180.f}){
    uint32_t bits;std::memcpy(&bits,&value,4);for(unsigned i=0;i<4;++i)parameters->bytes.push_back(uint8_t(bits>>(i*8)));}
  parameters->known.assign(parameters->bytes.size(),1);
  return CreateContextActor5(*resources_,ActorTextInput(base+stem+".DVS"),ActorTextInput(base+"gim\\"),PackageStorage::Attach(parameters,parameters->bytes.size()));
}
GameFsStatus GameActorRuntime::ClearContextActor1A1F4(){
  if(!Live()||!context_actor_||context_actor_->state.presence!=GameContextActorPresence::Present)
    return {GameFsCode::Unavailable,0,"context actor absence needs an actual prior object retirement"};
  try{auto cleared=std::make_shared<ContextActorData>();cleared->manager=support_manager_;
    cleared->state.presence=GameContextActorPresence::Absent;cleared->state.epoch=state_.owner_epoch;
    cleared->state.serial=++context_actor_serial_;context_actor_->Close();context_actor_=std::move(cleared);return {};
  }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::BindScriptContextActor1C0FC(script::Vm& vm){
  if(!Live())return {GameFsCode::Unavailable,0,"context actor binding manager retired"};
  try{const auto context=vm.context_value();const auto w=vm.current_witness();
    if(!w.valid||w.stopped||!w.context_registered||!vm.stable_value_owner(context))throw std::runtime_error("context actor current module/run/thread required");
    for(const auto& old:context_actors_)if(old.context.lock()==context.context_owner.lock()&&old.run==w.run_id)
      throw std::runtime_error("context actor constructor binding cannot replay/rebase");
    context_actors_.erase(std::remove_if(context_actors_.begin(),context_actors_.end(),[&](const ContextActorBinding& b){return b.context.expired()||b.context.lock()==context.context_owner.lock();}),context_actors_.end());
    if(context_actors_.size()==8)throw std::runtime_error("context actor native registry bound");
    const bool nil=context_actor_&&context_actor_->Current()&&context_actor_->state.presence==GameContextActorPresence::Absent;
    context_actors_.push_back({context.context_owner,context_actor_,w.run_id,nil,w.code_sha256,primary_target_});return {};
  }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::RequestPendingScene12(script::Vm& vm){return RequestContextScene(vm,12);}
GameFsStatus GameActorRuntime::RequestSameManager0(script::Vm& vm){return RequestContextScene(vm,0);}
GameFsStatus GameActorRuntime::RequestContextScene(script::Vm& vm,int32_t selector){
  if(!Live())return {GameFsCode::Unavailable,0,"pending scene manager retired"};
  try{const auto w=vm.current_witness();if(!w.valid||w.stopped||!w.context_registered)throw std::runtime_error("pending scene current context required");
    const auto context=vm.context_value();if(!vm.stable_value_owner(context))throw std::runtime_error("pending scene foreign context thread");
    const auto ctx=context.context_owner.lock();std::shared_ptr<ContextActorData> actor;bool nil=false;
    for(const auto& bound:context_actors_)if(bound.context.lock()==ctx&&bound.run==w.run_id&&bound.module==w.code_sha256){actor=bound.actor.lock();nil=bound.captured_nil;break;}
    // Source reads node24 BEFORE pending stores. Unknown/retired is not nil.
    if(!nil&&(!actor||!actor->Current()))throw std::runtime_error("context node24 actor association unknown/retired");
    if(selector==0){if(!state_.scene_clock_ready||state_.manager_scene14!=0||!manager_switch_->RequestSame0(state_.owner_epoch,0))throw std::runtime_error("same scene0 request owner/domain");}
    else if(selector!=12||!manager_switch_->RequestPending12(state_.owner_epoch))throw std::runtime_error("pending scene12 request owner/replay/domain");
    state_.manager_switch=manager_switch_->Snapshot();
    if(!nil){
      actor->Move(ContextActorFloat(actor->inputs[2],2),ContextActorFloat(actor->inputs[2],3));
      if(!actor->Current())throw std::runtime_error("context actor changed after logical stores");
      actor->state.model_visible=false;++actor->state.hides;
    }
    return {}; // request/actor prefix ONLY: no bit publication/adoption/wake
  }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::Initialize(uint32_t actor, PackageView selection,
                                          GameTransition &transition,
                                          GameResourceSource *resources,std::shared_ptr<GameBackgroundRuntime> background,
                                          std::shared_ptr<GameManagerSwitchControl> ordinary_owners) {
  Close();
  if(destination_)return {GameFsCode::Unavailable,0,"prior destination close still pending"};
  state_ = {};
  state_.owner_epoch=next_room_owner_epoch.fetch_add(1,std::memory_order_relaxed)+1;
  try {
    ordinary_manager_borrow_=bool(ordinary_owners);
    if(ordinary_owners){
      if(!ordinary_owners->CurrentBorrow(ordinary_owners)||!ordinary_owners->BindActor(state_.owner_epoch))
        throw std::runtime_error("Current Content LoadMan borrow unavailable/foreign/replayed");
      manager_switch_=std::move(ordinary_owners);
    }else{
      manager_switch_=std::make_shared<GameManagerSwitchControl>();
      if(!manager_switch_->Initialize(state_.owner_epoch))throw std::runtime_error("Manager constructor owner unavailable");
    }
    state_.manager_switch=manager_switch_->Snapshot();
    support_manager_=std::make_shared<uint64_t>(state_.owner_epoch);
    const auto support_init=support_registration_.Initialize(support_manager_,state_.owner_epoch);if(!support_init)throw std::runtime_error(support_init.detail);
    if (actor >= 5 || !selection.Describe().readable ||
        !transition.Snapshot().live)
      throw std::runtime_error(
          "Actor control owners need live selected package and transition");
    selection_ = std::move(selection);
    primary_target_=std::make_shared<PrimaryCameraTarget>();primary_target_->manager=support_manager_;
    primary_target_->module=selection_;primary_target_->epoch=state_.owner_epoch;
    primary_target_->position=state_.actor_position; // actual9480 primary+40 initializer
    transition_ = &transition;
    resources_ = resources;
    background_module_=std::move(background);
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
  if (!state_.live || !transition_ || !transition_->Snapshot().live || !manager_switch_ || !manager_switch_->Current() || manager_switch_->state_.epoch!=state_.owner_epoch)
    return false;
  try {
    return selection_.Describe().readable;
  } catch (const std::exception &) {
    return false;
  }
}
bool GameActorRuntime::BackgroundModuleMatches(const std::shared_ptr<GameBackgroundRuntime>& current)const{
  const auto parent=background_module_.lock();return Live()&&parent&&parent->Live()&&parent==current
      &&!parent.owner_before(current)&&!current.owner_before(parent);
}
GameFsStatus GameActorRuntime::InitializeScene0Clock(const std::array<uint32_t,7>& calendar){
  if(!Live()||state_.scene_clock_ready||state_.scene_model_live||calendar[5]<1||calendar[5]>12||calendar[4]<1||calendar[4]>31||calendar[3]>=24||calendar[2]>=60||calendar[1]>=60)
    return {GameFsCode::Unavailable,0,"fresh5D98 scene clock/current calendar required"};
  scene_calendar_=calendar;state_.scene_period_table52=0;state_.scene_clock_ready=initial_scene_clock_=true;return {};
}
GameFsStatus GameActorRuntime::ReadInitialScenePeriod7398(int32_t& result)const{
  if(!Live()||!initial_scene_clock_||!state_.scene_clock_ready||state_.scene_model_live||state_.scene_period_table52!=0)
    return {GameFsCode::Unavailable,0,"7398 requires actual fresh5D98 clock before5F58, not GAME36 readiness"};
  try{result=ClassifySceneClock7398(SceneClockEndpoints7398(state_.scene_period_table52),scene_calendar_[3],scene_calendar_[2]);return {};}
  catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::InitializeScene0(const std::array<uint32_t,7>& calendar,bool defer_regions) {
  if (!Live() || !resources_ || state_.scene_model_live)
    return {GameFsCode::Unavailable, 0,
            "Scene4DC admission requires live fresh native owner"};
  if(calendar[5]<1||calendar[5]>12||calendar[4]<1||calendar[4]>31||
     calendar[3]>=24||calendar[2]>=60||calendar[1]>=60)
    return {GameFsCode::Unavailable,0,"Scene seasonal initializer requires valid native civil calendar"};
  if(state_.scene_clock_ready&&scene_calendar_!=calendar)
    return {GameFsCode::Unavailable,0,"scene model must consume the SAME initialized clock calendar"};
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
  if(!manager_switch_->BindScene0(state_.owner_epoch))return {GameFsCode::Unavailable,0,"Source scene0 manager binding refused"};
  state_.manager_switch=manager_switch_->Snapshot();
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
  manager_switch_->state_.loadman.source_selector4DC=0;
  manager_switch_->state_.loadman.source_scene_generation=scene_clone_.Generation();
  manager_switch_->loaded_models_.emplace(31,scene_asset_);
  manager_switch_->state_.loadman.render_state20=uint8_t(1);manager_switch_->state_.loadman.render_state21=uint8_t(1);
  world_selector_=0; // actual19E6C/24AA4, distinct from early manager.current14
  old_scene_visible_=true;old_source_retired_=false;
  initial_scene_clock_=false;
  // 68E8 actual selected startup list[10,13,-1]. Stop before start in6D6C.
  status = ApplyScenePair(10);
  if (status)
    status = ApplyScenePair(13);
  if(status&&!defer_regions)status=CompileSceneRegions26B80();
  return status;
}
GameFsStatus GameActorRuntime::CompileSceneRegions26B80(){
  if(!Live()||!state_.scene_model_live||state_.region_owners_live)
    return {GameFsCode::Unavailable,0,"26B80 current fresh scene clone/region owner required"};
  const auto* model=adopting_destination_&&destination_?destination_->clone_.get():&scene_clone_;
  if(!model)return {GameFsCode::Unavailable,0,"26B80 current adopted model absent"};
  const auto status=regions_.Build(*model);if(status){state_.region_owners_live=true;
    for(size_t i=0;i<3;++i)state_.region_counts[i]=uint32_t(regions_.Lists()[i].size());}
  return status;
}
GameFsStatus GameActorRuntime::ApplyScenePlacementBD28(const std::shared_ptr<GameBackgroundRuntime>& runtime,uint32_t scene,PackageView row,PackageView heading){
  if(!BackgroundModuleMatches(runtime)||!state_.scene_model_live||scene!=state_.manager_scene14||!world_selector_||*world_selector_!=scene
      ||(scene!=0&&!(scene==12&&adopting_destination_&&destination_&&destination_state_.prepared)))
    return {GameFsCode::Unavailable,0,"BD28 requires SAME current scene/primary and owning adoption"};
  try{if(row.Describe().size!=12||heading.Describe().size!=4)throw std::runtime_error("BD28 exact held XYZ/heading extents");
    const auto row_hash=row.InitializedDigest(),head_hash=heading.InitializedDigest();std::array<float,4> values{};
    for(unsigned i=0;i<4;++i){const auto word=i==3?heading.ReadLE32(0):row.ReadLE32(i*4);std::memcpy(&values[i],&word,4);if(!std::isfinite(values[i]))throw std::runtime_error("BD28 nonfinite input");}
    if(row.InitializedDigest()!=row_hash||heading.InitializedDigest()!=head_hash)throw std::runtime_error("BD28 held DATA drift");
    const auto prepared=PrepareActor();if(!prepared)return prepared;
    // Source BD58/5C/60/64: source20,30,24,28 stores. This is NOT
    // primary40, which has a distinct constructor/update producer.
    state_.logical_xy[0]=values[0];state_.logical_heading=values[3];state_.logical_xy[1]=values[1];state_.logical_z=values[2];
    state_.source_placement20=values;state_.source_placement_bound=true;state_.source_placement_scene=scene;++state_.source_placement_writes;
    return UpdatePrimaryRoot99A8();
  }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
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
  if(!Live()||!state_.scene_model_live||!state_.scene_clock_ready||!manager_switch_->Current())
    return {GameFsCode::Unavailable,0,"GAME36 requires current adopted background/clock owner"};
  const auto& manager=manager_switch_->state_.loadman;
  if(manager.rendered_selector4DC==12){
    const auto module=background_module_.lock();const auto& d=destination_state_;
    if(!module||!BackgroundModuleMatches(module)||!destination_||!d.adopted||d.failed
        ||!destination_inputs_||!destination_inputs_->Current()||!destination_->Current(module,d.epoch)
        ||manager.source_selector4DC!=12||manager.source_scene_generation!=d.request
        ||destination_->Snapshot().request!=d.request)
      return {GameFsCode::Unavailable,0,"GAME36 adopted request/module/clock binding retired or changed"};
    const auto registration=manager_switch_->loaded_models_.find(d.resource);
    if(registration==manager_switch_->loaded_models_.end()||registration->second!=destination_->asset_
        ||registration->second.owner_before(destination_->asset_)||destination_->asset_.owner_before(registration->second))
      return {GameFsCode::Unavailable,0,"GAME36 adopted clock/model registration foreign or retired"};
    uint32_t table=0;const auto status=destination_->ReadScenePeriod(module,d.epoch,hour,minute,result,table);
    if(status){state_.scene_period_table52=table;scene_calendar_[3]=hour;scene_calendar_[2]=minute;}
    return status; //19AFC flag0: no4B4/4AC/cache publication
  }
  if(manager.rendered_selector4DC!=0||manager.source_selector4DC!=0||!scene_asset_||!scene_asset_->SourceCurrent()
      ||scene_clone_.Asset()!=scene_asset_.get())
    return {GameFsCode::Unavailable,0,"GAME36 selected background clock domain unavailable"};
  try {const auto value=ClassifySceneClock7398(SceneClockEndpoints7398(state_.scene_period_table52),hour,minute);
    scene_calendar_[3]=hour;scene_calendar_[2]=minute;result=value;return {};}
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
  if(!mode)return {GameFsCode::Unavailable,0,"GAME72 static84D0 target is outside current primary40 binding"};
  if(!primary_target_||!primary_target_->Current(support_manager_,state_.owner_epoch))
    return {GameFsCode::Unavailable,0,"GAME72 current primary40 owner unavailable"};
  // 1CF44 copies the current context node18 primary+40 into camera90.
  // The zero branch has a separate static target and remains unbound here.
  state_.camera_mode = mode;
  camera_target_=mode?primary_target_:std::shared_ptr<PrimaryCameraTarget>{};
  state_.camera_follows_actor = mode != 0;
  state_.pose_dirty = mode != 0;
  ++state_.camera_bindings;
  return {};
}
GameFsStatus GameActorRuntime::BindCamera(script::Vm& vm,int32_t mode){
  if(!Live())return {GameFsCode::Unavailable,0,"GAME72 current camera manager retired"};
  try{const auto w=vm.current_witness();const auto ctx=vm.context_value();
    if(!w.valid||w.stopped||!w.context_registered||!vm.stable_value_owner(ctx))throw std::runtime_error("GAME72 current context/module/run required");
    bool found=false;for(const auto& b:context_actors_)if(b.context.lock()==ctx.context_owner.lock()&&b.run==w.run_id&&b.module==w.code_sha256){
      const auto point=b.primary.lock();found=point&&point==primary_target_&&point->Current(support_manager_,state_.owner_epoch);break;}
    if(!found)throw std::runtime_error("GAME72 context node18 primary owner missing/stale/foreign");
    return BindCamera(mode);
  }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameActorRuntime::ReadCameraTarget(std::array<float,4>& out)const{
  const auto point=camera_target_.lock();
  if(!Live()||!point||point!=primary_target_||!point->Current(support_manager_,state_.owner_epoch))return false;
  for(auto x:point->position)if(!std::isfinite(x))return false;
  out=point->position;return true;
}
GameFsStatus GameActorRuntime::RefreshCamera7980(){
  if(!Live())return {GameFsCode::Unavailable,0,"7980 camera owner retired"};
  if(state_.camera_transition_mode1C==1||state_.camera_transition_mode1C==2){
    if(state_.actor!=0)return {GameFsCode::Unavailable,0,"7980 selected actor curve outside admitted actor0"};
    std::array<float,4> point;if(!ReadCameraTarget(point))return {GameFsCode::Unavailable,0,"7980 current camera90 control missing"};
    state_.camera_fov_degrees=WindowsCameraCurve79D8(point[1]);
  }else if(!state_.camera_transition_mode1C)state_.camera_fov_degrees=30;
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
GameFsStatus GameActorRuntime::FinishTransitionGame7() {
  if(!Live()||!transition_||!transition_->FinishGameOpen())
    return {GameFsCode::Unavailable,0,"GAME7 current radial owner unavailable"};
  return {}; // source dispatcher1C284 ignores its argument, not a ready result
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
GameFsStatus GameActorRuntime::RegisterSupportData16(script::Vm& vm,const script::Value& ref){
  if(!Live())return {GameFsCode::Unavailable,0,"GAME16 manager owner retired"};
  const auto status=support_registration_.Register16(vm,ref);state_.support_registration=support_registration_.Snapshot();return status;
}
GameFsStatus GameActorRuntime::RequestSameManager0(int32_t target){
  if(!Live()||!state_.scene_clock_ready||state_.manager_scene14!=0||!manager_switch_->RequestSame0(state_.owner_epoch,target))
    return {GameFsCode::Unavailable,0,"GAME12 outside current same-selector0 owner"};
  state_.manager_switch=manager_switch_->Snapshot();return {};
}
GameFsStatus GameActorRuntime::ConsumeSameManager0(){
  if(!Live()||!state_.scene_clock_ready||state_.manager_scene14!=0||!manager_switch_->ConsumeSame0(state_.owner_epoch))
    return {GameFsCode::Unavailable,0,"1B238 pending equal-selector0 unavailable"};
  state_.manager_switch=manager_switch_->Snapshot();return {};
}
GameFsStatus GameActorRuntime::BindInitialSceneBgmOwner(GameSceneBgm& bgm){
  if(!Live()||manager_switch_->bgm_||manager_switch_->state_.bound)
    return {GameFsCode::Unavailable,0,"initial LoadMan/BGM constructor binding refused"};
  const auto current=bgm.Snapshot();const auto life=bgm.Lifetime().lock();
  if(!life||!current.current||!current.selected||!current.requested||current.actor_epoch!=state_.owner_epoch)
    return {GameFsCode::Unavailable,0,"actual initial19DD4 current BGM selection required"};
  const auto bound=bgm.BindSelectionCell(manager_switch_,state_.owner_epoch);if(!bound)return bound;
  manager_switch_->bgm_=&bgm;manager_switch_->bgm_lifetime_=life;
  manager_switch_->state_.loadman.cached_period4B4=current.period4B4;
  manager_switch_->state_.loadman.stored_bgm4BC=current.stored4BC;
  return {}; // current4A8 remains the executed177F8 value until adoption
}
GameFsStatus GameActorRuntime::PublishPendingScene1B238(GameSceneBgm& bgm){
  if(!Live()||!state_.scene_model_live||!state_.scene_clock_ready)
    return {GameFsCode::Unavailable,0,"1B238 current old rendered scene required"};
  const auto held_owner=manager_switch_;auto& owner=*held_owner;auto& s=owner.state_;
  const auto life=owner.bgm_lifetime_.lock(),given=bgm.Lifetime().lock();
  if(!owner.OrdinaryUiIdle()||!s.bound||s.epoch!=state_.owner_epoch||!s.pending||s.current14!=0||s.requested18!=12
      ||s.publication_phase||owner.bgm_!=&bgm||!life||!given||life!=given||life.owner_before(given)||given.owner_before(life)
      ||s.loadman.rendered_selector4DC!=0||s.loadman.current_variant4A8>2)
    return {GameFsCode::Unavailable,0,"1B238 actual request/LoadMan/BGM/UI lineage or phase unavailable"};
  const auto before=bgm.Snapshot();
  if(!before.current||before.actor_epoch!=state_.owner_epoch||!owner.stored_bgm4BC_||before.stored4BC!=*owner.stored_bgm4BC_)
    return {GameFsCode::Unavailable,0,"1B238 current initial BGM data owner drift"};
  // WindowsTypedResourceRequests owns independently known bits; all other
  // source bits remain UNKNOWN. This is not a zero-filled PSP/profile byte map.
  // First old1B114(flag0) uses actual current4A8, WITHOUT a clock refresh.
  s.old_resource=30+3*uint32_t(s.current14)+s.loadman.current_variant4A8;
  owner.resource_requests_[s.old_resource]=false;s.publication_phase=1;s.publication_order[0]=0x1B284;
  const auto runtime=background_module_.lock();auto status=bgm.RefreshCachedPeriod(runtime,*this);
  if(!status)return status; // retain old-clear and pending source state on failure
  const auto staged=bgm.Snapshot();
  if(!staged.current||staged.actor_epoch!=state_.owner_epoch||!staged.cache_refreshed||staged.variant4AC>2)
    return {GameFsCode::Unavailable,0,"1B114 actual refreshed cache changed"};
  s.loadman.cached_period4B4=staged.period4B4;s.loadman.staged_variant4AC=staged.variant4AC;
  s.publication_phase=2;s.publication_order[1]=0x1B290;
  if(!owner.OrdinaryUiIdle()||s.epoch!=state_.owner_epoch||!s.pending||s.current14!=0||s.requested18!=12
      ||owner.bgm_lifetime_.expired()||!Live())
    return {GameFsCode::Unavailable,0,"1B238 owner/UI drift after current clock; cache effects retained"};
  s.new_resource=30+3*uint32_t(s.requested18)+staged.variant4AC;
  owner.resource_requests_[s.new_resource]=true;s.publication_order[2]=0x1B29C;
  s.current14=s.requested18;s.publication_order[3]=0x1B2A8;
  s.pending=false;s.publication_order[4]=0x1B2AC;
  s.requested18=s.current14;s.publication_order[5]=0x1B2B0;
  s.publication_phase=3;++s.consumed;state_.manager_scene14=uint32_t(s.current14);
  state_.manager_switch=owner.Snapshot();return {};
  // No staged resource is adopted: old4A8/4DC/clones/regions remain owned.
  // No target RNG/BGM/aux sampling, context completion or source event17.
}

GameFsStatus GameActorRuntime::AdvanceDestinationLoad(script::Vm& vm,const GameOutingCommand& command,GameSceneUiBootstrap& ui,GameSceneBgm& bgm){
  GameBackgroundCandidate candidate;auto status=command.CurrentRequest(vm,*this,candidate);if(!status)return status;
  auto& owner=*manager_switch_;auto& l=owner.state_.loadman;const auto runtime=background_module_.lock();
  const auto snapshot=owner.Snapshot();
  if(!runtime||!BackgroundModuleMatches(runtime)||snapshot.publication_phase!=3||snapshot.current14!=12
      ||!l.initial_scene6_handoff||l.application20!=6||!l.application_flag_known||l.application_flagEA0!=0
      ||!l.initial_state188||*l.initial_state188!=1||l.transition50!=0||!snapshot.loadman.ui_current||snapshot.loadman.ui464!=0)
    return {GameFsCode::Unavailable,0,"17D90 current scene6 constructor/request/history/UI domain required"};
  if(!transition_||!transition_->Snapshot().live||&ui.TitleTransitionOwner()!=transition_)
    return {GameFsCode::Unavailable,0,"17D90 current SAME bootstrap radial/moving owners required"};
  if(l.phase28==4&&old_source_retired_)return {}; // actual dispatch reached next constructor
  if(destination_)return {GameFsCode::Unavailable,0,"17D90 cannot restart an existing target job"};
  const auto phase=[&](uint32_t value,uint32_t pc){l.load_phase2C=value;l.load_writer=pc;++l.dispatches;};
  switch(l.load_phase2C){
  case 0:{
    bool requested=false;
    //17C98: the genuinely loaded source31 owner with a cleared desired bit
    // is a sufficient TRUE witness; no unknown source bits are defaulted.
    if(l.source_selector4DC!=0||!state_.scene_model_live||!scene_asset_||!scene_asset_->SourceCurrent()
        ||owner.loaded_models_.find(31)==owner.loaded_models_.end()||owner.loaded_models_.at(31)!=scene_asset_
        ||owner.loaded_models_.at(31).owner_before(scene_asset_)||scene_asset_.owner_before(owner.loaded_models_.at(31))
        ||!owner.ResourceRequest(31,requested)||requested)
      return {GameFsCode::Unavailable,0,"17C98 actual old loaded31/cleared request witness missing"};
    phase(1,0x17E74);l.change40=true;l.change_writer=0x17E78;
    if(transition_->Snapshot().state&&!transition_->BeginLoadClose())return {GameFsCode::Unavailable,0,"17E94 current radial owner retired"};
    return {};
  }
  case 1:
    if(!transition_||!transition_->Snapshot().live)return {GameFsCode::Unavailable,0,"17ED8 radial owner retired"};
    if(transition_->Snapshot().state)return {}; // actual radial poll, no completion certificate
    if(!l.change40)return {GameFsCode::Unavailable,0,"17F04 selected replacement byte40 not published"};
    if(!ui.BeginMoving(true))return {GameFsCode::Unavailable,0,"17F14 actual moving receiver unavailable"};
    l.render_state21=uint8_t(0);l.game_interval2C=20000;l.render_state20=uint8_t(0);
    old_scene_visible_=false; //6D28/29AD0/498A8 visibility, NOT motion activation
    phase(2,0x17DEC);return {};
  case 2:phase(3,0x17F80);return {};
  case 3:
    status=bgm.DepartureControlCurrent(runtime);if(!status)return status;
    phase(4,0x17FD4);return {};
  case 4:
    status=bgm.RetireSceneRecord(runtime);if(!status)return status;
    //19F6C logical old scene/support/regions retire in source order. Explicit
    // WindowsRetainedOldScene keeps their render assets allocated until a
    // replacement is usable, without exposing them as current source4DC.
    l.source_selector4DC=-1;l.source_scene_generation=0;
    regions_.Close();state_.region_owners_live=false;state_.region_counts={};
    old_source_retired_=true;phase(5,0x180F0);return {};
  case 5:
    //18144/253E8 removes the actual old31 registration. Native render assets
    // remain separate retained owners; no new resource bit becomes ready here.
    if(owner.loaded_models_.erase(31)!=1)return {GameFsCode::Unavailable,0,"18150 old31 registration not current"};
    l.phase28=4;phase(0,0x18140);return {};
  default:return {GameFsCode::Unavailable,0,"17D90 unsupported load dispatcher phase"};
  }
}
GameFsStatus GameActorRuntime::PreparePendingDestination(script::Vm& vm,const GameOutingCommand& command,PackageView module,
        const std::shared_ptr<const GameOutingData>& packet,GameSceneBgm& bgm,std::unique_ptr<GameFileSource> source){
  GameBackgroundCandidate candidate;auto status=command.CurrentRequest(vm,*this,candidate);if(!status)return status;
  const auto runtime=background_module_.lock();const auto manager=manager_switch_->Snapshot();bool bit=false;
  if(!packet||!packet->Current()||!bgm.SharesDataBacking(packet->origin)||!runtime||!BackgroundModuleMatches(runtime)||manager.publication_phase!=3
      ||manager.current14!=12||manager.loadman.rendered_selector4DC!=0||!manager_switch_->ResourceRequest(manager.new_resource,bit)||!bit
      ||manager.new_resource!=66+manager.loadman.staged_variant4AC||!old_source_retired_
      ||manager.loadman.phase28!=4||manager.loadman.source_selector4DC!=-1||manager_switch_->loaded_models_.count(manager.new_resource)
      ||candidate.request<=last_destination_request_)
    return {GameFsCode::Unavailable,0,"outing selected command/published resource/current DATA owner required"};
  if(destination_)return {GameFsCode::Unavailable,0,"existing destination attempt requires explicit retirement; no request/RNG replay"};
  last_destination_request_=candidate.request;
  destination_=std::make_shared<GameBackgroundPreparation>();destination_inputs_=packet;
  auto& state=destination_state_;state={};state.attempted=true;state.request=candidate.request;state.epoch=state_.owner_epoch;
  state.candidate=candidate.id;state.selector=candidate.selector;state.period=candidate.period;state.resource=manager.new_resource;
  state.old_source_retired=old_source_retired_;
  const std::shared_ptr<const void> owner=runtime;
  const auto check=[&](){GameBackgroundCandidate current;return packet->Current()&&bool(command.CurrentRequest(vm,*this,current))
      &&current.id==candidate.id&&current.request==candidate.request&&current.period==candidate.period&&runtime->Live();};
  status=destination_->Begin(owner,state.epoch,candidate,packet->registrations,std::move(source),GameModelTransformPolicy::WindowsAreaMarkerCanonicalBasis);
  if(status){
    state.source_scene_allocated=true;manager_switch_->state_.loadman.source_selector4DC=12;
    manager_switch_->state_.loadman.source_scene_generation=candidate.request;
    state.phase=1;status=destination_->RequestBgm(owner,state.epoch,runtime,bgm);
  }
  if(status&&check()){state.phase=2;status=destination_->Load(owner,state.epoch);}
  if(status&&check()){state.phase=3;status=destination_->LoadModelMotions(owner,state.epoch,packet->motion_counts);}
  if(status&&check()){state.phase=4;status=destination_->LoadAuxiliary(owner,state.epoch,packet->triplets,packet->model_format,
      [&](uint32_t token,PackageView& out)->GameFsStatus{const auto found=packet->auxiliary_names.find(token);
          if(found==packet->auxiliary_names.end())return {GameFsCode::Unavailable,0,"current outing auxiliary token absent"};out=found->second;return {};});}
  if(status&&check()){state.phase=5;status=destination_->PrepareEffects(owner,state.epoch,GameBackgroundEffectsPolicy::WindowsReducedBackgroundEffects,packet->light_format);}
  if(status&&check()){state.phase=6;status=destination_->ActivateEffects(owner,state.epoch,runtime);}
  if(status&&check()){state.phase=7;status=destination_->SampleEffects(owner,state.epoch,0);}
  if(status&&check()){
    //19E54 ->6D28: source variant publication precedes support construction;
    // the old Windows rendered owner is still retained independently.
    manager_switch_->state_.loadman.current_variant4A8=manager.loadman.staged_variant4AC;
    state.source_variant_committed=true;
    manager_switch_->loaded_models_.emplace(state.resource,destination_->asset_);
    destination_registration_=destination_->asset_;
    world_selector_=candidate.selector;state.current_selector24AA4=*world_selector_;state.current_selector_writer=0x19E6C;
    state.phase=8;status=destination_->PrepareOwnedSupport(owner,state.epoch,vm,support_registration_,std::move(module),*packet);
  }
  if(status&&!check())status={GameFsCode::Unavailable,0,"outing command/DATA/job owner changed during preparation; effects retained"};
  state.status=status;state.failed=!status;state.prepared=bool(status);if(status)state.phase=9;
  return status; // partial source4DC/4A8 are NOT rendered adoption/event17
}
GameFsStatus GameActorRuntime::CloseDestination(){
  if(destination_){
    const auto registered=destination_registration_.lock();
    auto status=destination_->Close();if(!status)return status;
    if(manager_switch_&&manager_switch_->Current()){
      const auto found=manager_switch_->loaded_models_.find(destination_state_.resource);
      if(found!=manager_switch_->loaded_models_.end()&&registered&&found->second==registered
          &&!found->second.owner_before(registered)&&!registered.owner_before(found->second))manager_switch_->loaded_models_.erase(found);
      if(destination_state_.source_scene_allocated&&manager_switch_->state_.loadman.source_selector4DC==int32_t(destination_state_.selector)
          &&manager_switch_->state_.loadman.source_scene_generation==destination_state_.request){
        manager_switch_->state_.loadman.source_selector4DC=-1;manager_switch_->state_.loadman.source_scene_generation=0;
      }
      if(destination_state_.adopted){manager_switch_->state_.loadman.rendered_selector4DC=-1;state_.scene_model_live=false;regions_.Close();state_.region_owners_live=false;}
    }
    destination_.reset();
  }
  destination_registration_.reset();destination_inputs_.reset();destination_state_={};return {};
}

GameFsStatus GameActorRuntime::AdoptPreparedDestination(script::Vm& vm,const GameOutingCommand& command){
  GameBackgroundCandidate candidate;auto status=command.CurrentRequest(vm,*this,candidate);if(!status)return status;
  const auto runtime=background_module_.lock();const auto& d=destination_state_;
  if(!runtime||!BackgroundModuleMatches(runtime)||!destination_||!destination_inputs_||!destination_inputs_->Current()
      ||!d.prepared||d.failed||d.adopted||!d.old_source_retired||!d.source_scene_allocated||!d.source_variant_committed
      ||d.request!=candidate.request||d.candidate!=candidate.id||d.period!=candidate.period||!destination_->Current(runtime,d.epoch)
      ||!destination_->clone_||!destination_->asset_->SourceCurrent()
      ||manager_switch_->state_.loadman.source_selector4DC!=12||manager_switch_->state_.loadman.source_scene_generation!=d.request)
    return {GameFsCode::Unavailable,0,"19DD4 current prepared job/command/source publication required; no adopt/replay"};
  GameSupportSnapshot support;status=destination_->ReadSupport(runtime,d.epoch,vm,support);if(!status)return status;
  const auto registration=manager_switch_->loaded_models_.find(d.resource);
  if(registration==manager_switch_->loaded_models_.end()||registration->second!=destination_->asset_
      ||registration->second.owner_before(destination_->asset_)||destination_->asset_.owner_before(registration->second))
    return {GameFsCode::Unavailable,0,"19DD4 current target registration replaced/foreign"};
  // No clone duplication: support/glyph children keep the exact prepared
  // main-clone generation and marker aliases. Source placement does not write
  // primary40 or invent a camera90 point/FOV.
  struct Scope {bool& value;explicit Scope(bool& v):value(v){value=true;}~Scope(){value=false;}} scope(adopting_destination_);
  if(!destination_state_.placement_pc){
    status=ApplyScenePlacementBD28(runtime,12,destination_inputs_->position_row,destination_inputs_->placement_heading);
    if(!status)return status;destination_state_.placement_pc=0x19E94;
  }
  if(!destination_state_.regions_pc){
    status=CompileSceneRegions26B80();if(!status)return status;destination_state_.regions_pc=0x19EA0;
  }
  const auto stats=destination_->asset_->Statistics();
  const auto& applied=destination_->applied_;
  int32_t period=-1;uint32_t table=0;
  status=destination_->ReadScenePeriod(runtime,d.epoch,applied.calendar[3],applied.calendar[2],period,table);
  if(!status)return status;
  state_.scene_period_table52=table; // current target constructor/72B8, never old Scene0's row
  scene_calendar_=applied.calendar;state_.scene_clock_ready=true;
  state_.scene_nodes=stats.nodes;state_.scene_vertices=stats.vertices;state_.scene_triangles=stats.triangles;
  //80F14's consumed output is a light-model borrow. WindowsReducedBackgroundEffects
  // retains the actual adopted model's typed light records; unknown839A8
  // source210/20C primary selection is NOT reported as source zero/absent.
  state_.scene_light_nodes=uint32_t(destination_->clone_->Lights().type5_nodes.size());
  state_.scene_light_color14=destination_->clone_->Lights().packed_color14;
  auto& l=manager_switch_->state_.loadman;l.render_state20=uint8_t(1);l.render_state21=uint8_t(1);
  l.rendered_selector4DC=12;l.scene_phase44=1;
  destination_state_.adoption_pc=0x185FC;destination_state_.adopted=true;
  // Actual old render owners now retire. Existing room slots use their real
  // deferred disable/destroy protocol; all target frames exclude those slots.
  scene_clone_.Close();scene_asset_.reset();scene_controls_.clear();season_controls_.clear();light_on_control_={};
  state_.scene_controls=state_.season_controls=0;state_.scene_playback={};state_.season_present={};state_.season_playback={};
  state_.light_on_control_present=false;state_.light_on_playback={};
  for(size_t i=0;i<models_.size();++i)if(models_[i].active&&!state_.room_models[i].retirement){
    status=RetireModel(int32_t(i));if(!status)return status;
  }
  destination_state_.old_render_retired=std::none_of(models_.begin(),models_.end(),[](const auto& m){return m.active;});
  return {}; // display/adoption only; actual18568 context/event17 still required
}

GameFsStatus GameActorRuntime::TickDestinationContinuation(script::Vm& vm,GameOutingCommand& command,GameSceneUiBootstrap& ui,GameSceneBgm& bgm,bool loader469){
  GameBackgroundCandidate candidate;auto status=command.CurrentRequest(vm,*this,candidate);if(!status)return status;
  auto& d=destination_state_;auto& l=manager_switch_->state_.loadman;const auto module=background_module_.lock();
  if(!Live()||!module||!BackgroundModuleMatches(module)||!destination_||!d.adopted||d.failed
    ||d.request!=candidate.request||d.period!=candidate.period||d.candidate!=candidate.id||!destination_->Current(module,d.epoch)
    ||l.source_selector4DC!=12||l.rendered_selector4DC!=12||l.source_scene_generation!=d.request
    ||!transition_||&ui.TitleTransitionOwner()!=transition_||!transition_->Snapshot().live
    ||l.transition50!=0||l.ui_change461)
    return {GameFsCode::Unavailable,0,"18568 current adopted scene/request/radial or admitted history changed"};
  if(d.scene_dispatch_completed)return {};
  switch(l.scene_phase44){
  case 1:{
    //297A8: the Windows synchronous worker polls ITS current held data and
    // owned pixels/attachments. This is not an independent VM wake provider.
    status=destination_->UpdateSupport(module,d.epoch,vm);if(!status)return status;
    GameSupportSnapshot support;status=destination_->ReadSupport(module,d.epoch,vm,support);if(!status)return status;
    if(!support.glyph_pixels_ready)return {};
    l.scene_phase44=3;l.counter42=0;return {};
  }
  case 3:l.scene_phase44=2;[[fallthrough]]; // signedhalf50 current constructor/request history0
  case 2:{
    if(l.counter42<2){++l.counter42;return {};}
    // If preparation completed early, retain this request and WAIT for its
    // actual1C5B/3A/32 owner. Never borrow the latest diagnostic VM witness.
    if(command.WaitingForPause())return {};
    if(!d.camera_ready_writer){d.camera_readyB0=true;d.camera_holdB3=false;d.camera_ready_writer=0x18668;
      state_.camera_byteB0=uint8_t(1);state_.camera_byteB3=uint8_t(0);}
    if(!d.arrival_event_posted){status=RefreshCamera7980();if(!status)return status;}
    if(l.change40&&!d.arrival_event_posted){
      status=command.PostArrival17(vm);if(!status)return status;
      const auto receipt=command.Snapshot(vm);d.arrival_event_posted=true;d.arrival_receipt=receipt.event_receipt;
    }
    //186B0 posts17 BEFORE18698 hides moving. A hide failure retains the
    // real event receipt, so retry cannot post a duplicate callback.
    if(!d.moving_hide_requested){if(!ui.FinishMoving(false))return {GameFsCode::Unavailable,0,"18698 current moving receiver refused hide"};d.moving_hide_requested=true;}
    l.scene_phase44=4;++l.counter42;return {};
  }
  case 4:
    //18754: current radial!=0 OR actual byte461==0 enters5 without DA64.
    // Do not invent an Open here; the live source script may still issue it.
    l.scene_phase44=5;l.counter42=0;return {};
  case 5:
    if(transition_->Snapshot().state!=2)return {};
    {const auto current=manager_switch_->Snapshot();if(!current.loadman.ui_current)return {GameFsCode::Unavailable,0,"18894 current UI464 owner unknown"};
      status=bgm.RequestCleanupControl(module,loader469,current.loadman.ui464);if(!status)return status;} //188BC real local request, no playback credit
    l.scene_phase44=0;l.cleanup4B8=uint8_t(0);l.ui_change461=false;l.change40=false;l.phase28=0;l.cleanup54=uint8_t(0);
    if(!transition_->SetCenter({{0,0}}))return {GameFsCode::Unavailable,0,"1890C actual produced radial center refused"};
    d.scene_dispatch_completed=true;return {};
  default:return {GameFsCode::Unavailable,0,"18568 unsupported source phase"};
  }
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
  std::array<float,4> point;
  if((state_.manager_camera_request4C!=-1||state_.camera_transition_mode1C)&&!ReadCameraTarget(point))
    return {GameFsCode::Unavailable,0,"camera update requires current owned camera90 target"};
  if((state_.manager_camera_request4C==1||state_.camera_transition_mode1C==1)&&state_.actor!=0)
    return {GameFsCode::Unavailable,0,"79D8 selected actor curve outside admitted actor0"};
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
    state_.camera_target_fov=WindowsCameraCurve79D8(point[1]);
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
    // AC60 AE08 binds command-owner30 to24ABC's module cell. Keep the
    // actual native module/control-block and source inputs, not a PSP address.
    if(!manager_switch_||!manager_switch_->CurrentBorrow(manager_switch_))
      throw std::runtime_error("ASC current module cell owner unavailable");
    actor_asc_digest_=asc.InitializedDigest();asc_selection_digest_=selection_.InitializedDigest();
    asc_module_owner_=manager_switch_;asc_module_epoch_=manager_switch_->Snapshot().loadman.module_epoch;
    // AC60 AE1C/AE2C: the command receiver3C names THIS actor+60,
    // not the module cell30, primary9480 vector or current scene4DC.
    // WindowsOwnedAscActorField60 uses the executed native actor lifetime;
    // no PSP address/layout or pre-write source value is manufactured.
    asc_actor_field60_=std::make_shared<AscActorField60>();
    asc_actor_field60_->actor=this;asc_actor_field60_->owner=support_manager_;
    asc_actor_field60_->epoch=state_.owner_epoch;asc_field60_binding_=asc_actor_field60_;
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
    const auto digest=command.InitializedDigest();
    // B1D4 writes the same pending38C/390/394 fields on every call.
    // Validation precedes the replacement; the active24700 owner is untouched.
    pending_command_image_ = std::move(command);
    pending_command_digest_=digest;
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
  const auto rooted=UpdatePrimaryRoot99A8();if(!rooted)return rooted;
  const auto prior = state_.actor_control_ready;
  state_.actor_control_ready = channels_[0].playback.Complete();
  if (!prior && state_.actor_control_ready && channels_[0].motion == 123)
    ++state_.motion123_completions;
  return {};
}
GameFsStatus GameActorRuntime::UpdatePrimaryRoot99A8(){
  if(!state_.actor_model_live)return {GameFsCode::Unavailable,0,"99A8 current primary model required"};
  // 99A8 logical position and wrap(heading+pi),canonical native basis.
  state_.actor_root = {{state_.logical_xy[0], state_.logical_xy[1], state_.logical_z, 1}};
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
  return {};
}
GameFsStatus GameActorRuntime::AdvanceActorFrame(script::Vm* support_vm) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor phase owner expired"};
  if(destination_state_.adopted){
    const auto runtime=background_module_.lock();GameSupportSnapshot support;
    if(!support_vm||!runtime||!destination_||!destination_inputs_||!destination_inputs_->Current())
      return {GameFsCode::Unavailable,0,"adopted update current scoped VM/DATA owner required"};
    const auto checked=destination_->ReadSupport(runtime,destination_state_.epoch,*support_vm,support);if(!checked)return checked;
  }
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
  if(destination_state_.adopted)destination_state_.old_render_retired=std::none_of(models_.begin(),models_.end(),[](const auto& m){return m.active;});
  auto scene=GameFsStatus{};
  if(destination_state_.adopted){
    const auto runtime=background_module_.lock();
    if(!support_vm||!runtime)return {GameFsCode::Unavailable,0,"adopted scene update requires current scoped VM/support owner"};
    scene=destination_->SampleEffects(runtime,destination_state_.epoch,160);
    if(scene)scene=destination_->UpdateSupport(runtime,destination_state_.epoch,*support_vm);
  }else scene=UpdateScene();
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
      command_digest_=pending_command_digest_;
      state_.asc_operand_valid=false;
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
      state_.asc_operand_valid=true;state_.asc_operand_record=state_.active_command;
      state_.asc_operand_at=uint32_t(at);state_.asc_operand_parameter=parameter;state_.asc_operand_value=value;
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
      } else if(parameter==6){
        const auto module=asc_module_owner_.lock();
        if(!module||module!=manager_switch_||module.owner_before(manager_switch_)||manager_switch_.owner_before(module)
           ||!module->CurrentBorrow(module)||module->state_.epoch!=state_.owner_epoch
           ||module->state_.loadman.module_epoch!=asc_module_epoch_
           ||selection_.InitializedDigest()!=asc_selection_digest_||actor_asc_.InitializedDigest()!=actor_asc_digest_
           ||command_image_.InitializedDigest()!=command_digest_)
          throw std::runtime_error("ASC6 current module/record binding retired/foreign/drifted");
        // 245FC/24610: unconditional write through the genuine24ABC cell.
        // Optional current-scene call7094 is exactly JR RA/NOP and reads no
        // receiver/argument: both presence branches have no further effects.
        // No scene absence, readiness, wait, motion or completion is inferred.
        module->state_.asc6_cell=value;++module->state_.asc6_writes;
        state_.command_pc+=12;
      } else if(parameter==9){
        const auto module=asc_module_owner_.lock();
        if(!module||module!=manager_switch_||module.owner_before(manager_switch_)||manager_switch_.owner_before(module)
           ||!module->CurrentBorrow(module)||module->state_.epoch!=state_.owner_epoch
           ||module->state_.loadman.module_epoch!=asc_module_epoch_||!CurrentAscActorField60()
           ||selection_.InitializedDigest()!=asc_selection_digest_||actor_asc_.InitializedDigest()!=actor_asc_digest_
           ||command_image_.InitializedDigest()!=command_digest_)
          throw std::runtime_error("ASC9 current actor/cell/module/record binding retired/foreign/drifted");
        // D9910[9] ->2466C/2467C: full32-bit store through bound3C,
        // then the ordinary240DC loop. No helper/wait/visibility/ready call.
        // The admitted AC60 constructor really binds this non-null cell;
        // unknown/foreign is refused, not recast as the source nil branch.
        asc_actor_field60_->value=value;++asc_actor_field60_->writes;
        state_.command_pc+=12;
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
GameFsStatus GameActorRuntime::Draw(GameGraphics &graphics,const std::function<GameFsStatus(GameGraphics&)>& overlay,script::Vm* support_vm) {
  if (!Live())
    return {GameFsCode::Unavailable, 0, "Actor render owners expired"};
  if(destination_state_.adopted){
    const auto runtime=background_module_.lock();GameSupportSnapshot support;
    if(!support_vm||!runtime||!destination_||!destination_inputs_||!destination_inputs_->Current())
      return {GameFsCode::Unavailable,0,"adopted target render requires current scoped VM/DATA owners"};
    const auto checked=destination_->ReadSupport(runtime,destination_state_.epoch,*support_vm,support);if(!checked)return checked;
  }
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
  const bool primary_visible=state_.actor_model_live&&(channels_[0].motion>=0||state_.source_placement_bound);
  const float follow_x = primary_visible ? state_.actor_root[0] : 0;
  const float follow_y = primary_visible ? state_.actor_root[1] : 0;
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
    if (const auto& slot=models_[i];!destination_state_.adopted&&old_scene_visible_&&slot.active&&state_.room_models[i].retirement<2) {
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
  if(destination_state_.adopted){
    const auto status=destination_->DrawSelected(background_module_.lock(),destination_state_.epoch,*support_vm,graphics,camera);
    if(!status)return status;++state_.scene_frames;
  }else if (state_.scene_model_live&&old_scene_visible_) {
    const auto status = scene_clone_.Draw(graphics, camera,
        GameModelColorPolicy::WindowsDvsTexturedModulation2x);
    if (!status)
      return status;
    ++state_.scene_frames;
  }
  if (primary_visible) {
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
  if(context_actor_&&context_actor_->state.presence==GameContextActorPresence::Present){
    if(!context_actor_->Current())return {GameFsCode::Unavailable,0,"context actor render owner changed"};
    if(context_actor_->state.model_visible){const auto drawn=context_actor_->clone.Draw(graphics,camera);if(!drawn)return drawn;}
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
  if(!CloseDestination())return;
  initial_scene_clock_=false;state_.source_placement_bound=false;
  old_scene_visible_=true;old_source_retired_=false;adopting_destination_=false;
  last_destination_request_=0;
  world_selector_.reset();
  background_module_.reset();
  camera_target_.reset();primary_target_.reset();state_.camera_target_current=false;
  if(context_actor_)context_actor_->Close();context_actor_.reset();context_actors_.clear();
  support_registration_.Close();support_manager_.reset();state_.support_registration=support_registration_.Snapshot();
  if(manager_switch_){
    if(ordinary_manager_borrow_)manager_switch_->ReleaseActor(state_.owner_epoch);else manager_switch_->Retire();
    state_.manager_switch=manager_switch_->Snapshot();manager_switch_.reset();
  }
  ordinary_manager_borrow_=false;
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
  asc_field60_binding_.reset();asc_actor_field60_.reset();
  asc_module_owner_.reset();asc_module_epoch_=0;
  actor_asc_digest_={};asc_selection_digest_={};pending_command_digest_={};command_digest_={};
  state_.asc_operand_valid=false;
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

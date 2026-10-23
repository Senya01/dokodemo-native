#include "game_sound_voice_native.h"
#include <algorithm>
#include <atomic>
#include <tuple>
namespace dokodemo::recovered {
bool GameSourceVoiceExact(const GameSourceVoiceSnapshot& a,const GameSourceVoiceSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.returned,a.auxiliary_null,a.callback_entered,a.callback_returned,a.sequence_result_known,a.owner,a.generation,a.allocation,a.services,a.id,a.bank,a.sequence_slot,a.flags,a.mode,a.group,a.event,a.extra,a.descriptor,a.bank_refs_before,a.bank_refs_after,a.retain_writer,a.initial_left,a.initial_right,a.initial_dirty_mask,a.initial_port_writes,a.initialized_mask,a.port_writes,a.return_pc,a.callback_pc,a.frontier,a.initializer_store_pc,a.words74,a.words80,a.wordsA0,a.initial_lane_controls,a.initial_lane_flags,a.bank_ids,a.working,a.published,a.consumer,a.dirty,a.persistent,a.published_dirty,a.published_persistent,a.consumer_dirty,a.consumer_persistent,a.master_left,a.master_right)
 ==std::tie(b.live,b.active,b.returned,b.auxiliary_null,b.callback_entered,b.callback_returned,b.sequence_result_known,b.owner,b.generation,b.allocation,b.services,b.id,b.bank,b.sequence_slot,b.flags,b.mode,b.group,b.event,b.extra,b.descriptor,b.bank_refs_before,b.bank_refs_after,b.retain_writer,b.initial_left,b.initial_right,b.initial_dirty_mask,b.initial_port_writes,b.initialized_mask,b.port_writes,b.return_pc,b.callback_pc,b.frontier,b.initializer_store_pc,b.words74,b.words80,b.wordsA0,b.initial_lane_controls,b.initial_lane_flags,b.bank_ids,b.working,b.published,b.consumer,b.dirty,b.persistent,b.published_dirty,b.published_persistent,b.consumer_dirty,b.consumer_persistent,b.master_left,b.master_right);
}
bool GameSourceVoicePool::Parameter(uint32_t param,int32_t value){
 if(!state_.live||param<2||param>=12||value<INT16_MIN||value>INT16_MAX)return false;
 const auto bit=uint16_t(1u<<param);if((state_.working[1]&bit)&&int16_t(state_.working[param])==value)return true;
 state_.working[param]=uint16_t(value);state_.working[0]|=bit;state_.working[1]|=bit;++state_.port_writes;return true; //54C38
}
bool GameSourceVoicePool::Gain(int32_t id,int32_t left,int32_t right){
 if(!state_.live||!state_.active||id!=state_.id||!allocated_.at(size_t(id))||(state_.persistent&2))return false;
 left=std::clamp(left,-128,128);right=std::clamp(right,-128,128);
 const auto l=(left*int32_t(state_.master_left))>>7,r=(right*int32_t(state_.master_right))>>7;
 return Parameter(2,std::clamp(l,-128,128)*32)&&Parameter(3,std::clamp(r,-128,128)*32); //56E74/56D34
}
bool GameSourceVoicePool::Rate(int32_t id,float rate){
 if(!state_.live||id!=state_.id||rate<0.01f||rate>2.0f)return false;
 return Parameter(9,int32_t(rate*4096.0f)); //57278 trunc.w.s, actual source operand
}
GameFsStatus GameSourceVoicePool::Allocate(int32_t bank,const std::array<int32_t,4>& ids,uint32_t before,uint32_t after,const std::function<GameFsStatus()>& auxiliary){
 if(state_.live||bank<0||before==UINT32_MAX||after!=before+1)return {GameFsCode::Unavailable,0,"56598 bank retain/voice admission"};
 int32_t id=-1;for(size_t i=0;i<allocated_.size();++i)if(!allocated_[i]){id=int32_t(i);break;}
 if(id<0)return {GameFsCode::Busy,0,"NativeIndexedVoicePool exhausted"};
 static std::atomic<uint64_t> next{0};const auto generation=state_.generation+1;state_={};state_.owner=++next;state_.generation=generation;state_.allocation=1;
 allocated_[size_t(id)]=true;state_.live=true;state_.id=id;state_.bank=bank;state_.bank_refs_before=before;state_.bank_refs_after=after;state_.retain_writer=0x55A2C;
 state_.flags=3;state_.mode=2;state_.group=0;state_.event=6;state_.descriptor=0x7210;
 const auto lookup=auxiliary();if(!lookup)return lookup; //after owned node initial writes, before mappings/clears
 state_.bank_ids=ids;
 //55BA0 selected group0/event6 record+4==0. NULL is auxiliary+7C.
 state_.words74.fill(0);state_.words80.fill(0);state_.wordsA0.fill(0);state_.initial_lane_controls.fill(0);state_.initial_lane_flags.fill(0);
 state_.auxiliary_null=true;
 const auto zero=[](const auto& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});};
 state_.initialized_mask=(zero(state_.initial_lane_controls)&&zero(state_.initial_lane_flags)?1u:0u)|(zero(state_.words74)?2u:0u)
    |(zero(state_.words80)?4u:0u)|(zero(state_.wordsA0)?8u:0u)|(state_.bank_ids==ids?16u:0u)|(state_.auxiliary_null?32u:0u)|(state_.flags==3?64u:0u); //controls/74/80/A0 cleared, four IDs copied
 state_.dirty=state_.persistent=1;state_.active=true; //actual active ownership before56E74
 if(!Gain(id,128,128))return {GameFsCode::Unavailable,0,"56598 mandatory56E74 port readback"};
 state_.initial_left=state_.working[2];state_.initial_right=state_.working[3];state_.initial_dirty_mask=state_.working[0];state_.initial_port_writes=state_.port_writes;
 //56794 rereads actual node ID after port effects; no callback is invoked here.
 if(!allocated_[size_t(state_.id)])return {GameFsCode::Unavailable,0,"56794 owned node ID readback"};
 state_.returned=true;state_.return_pc=0x567BC;return {};
}
GameFsStatus GameSourceVoicePool::Service(){
 if(!state_.live||!state_.returned||!state_.active||state_.services)return {GameFsCode::Unavailable,0,"NativeVoiceService retired/replayed"};
 ++state_.services;
 const auto copy=[](auto& dst,const auto& src){const auto mask=src[0];for(uint32_t i=2;i<12;++i)if(mask&(1u<<i))dst[i]=src[i];dst[0]|=mask;dst[1]|=mask;};
 //Named NativeWorkerVoiceService: the source56254 publication precedes562D4.
 copy(state_.published,state_.working);state_.published_dirty=state_.dirty;state_.published_persistent=state_.persistent;state_.working[0]=0;state_.dirty=0;
 copy(state_.consumer,state_.published);state_.consumer_dirty=state_.published_dirty;state_.consumer_persistent=state_.published_persistent;state_.published[0]=0;state_.published_dirty=0;
 if(!(state_.consumer_dirty&1)||!(state_.consumer_persistent&1)||state_.descriptor!=0x7210)return {GameFsCode::Unavailable,0,"562D4 actual first callback guard"};
 return Callback578CC();
}
GameFsStatus GameSourceVoicePool::Callback578CC(){
 state_.callback_entered=true;state_.callback_pc=0x578CC;
 state_.sequence_slot=-1;state_.words80[0]=UINT32_MAX;state_.initializer_store_pc=0x578D4; //real store BEFORE57CF0, no returned ID
 return AllocateSequence57CF0(state_.bank_ids,state_.wordsA0);
}
GameFsStatus GameSourceVoicePool::AllocateSequence57CF0(const std::array<int32_t,4>& ids,std::array<uint32_t,8>& output){
 if(&ids!=&state_.bank_ids||&output!=&state_.wordsA0||!state_.live||!state_.active||!state_.callback_entered)
 return {GameFsCode::Unavailable,0,"57CF0 actual voice-owned argument storage unavailable"};
 state_.frontier=0x57CF0;
 //No source result is supplied; neither57908 nor58C04 may execute.
 return {GameFsCode::Unavailable,0,"Unsupported selected57CF0(four actual bank IDs, voice+A0 output)"};
}
void GameSourceVoicePool::Close()noexcept{allocated_.fill(false);state_.live=state_.active=false;}
}

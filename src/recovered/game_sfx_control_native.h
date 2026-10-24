#pragma once
#include "game_filesystem_native.h"
#include <array>
#include <atomic>
#include <memory>
#include <new>

namespace dokodemo::recovered {
// WindowsSasControlState32 is a finite platform control-state adapter for the
// reached Init(256,32,stereo,44100), RevVON(1,0), RevEVOL(0,0) contract.
// It owns real state; it does not implement the SAS DSP, render or audio output.
// Neither its layout nor its return status is a claim of PSP SDK execution.
class GameNativeSfxControl16 final {
 public:
 struct Observation {
  bool live=false,platform_live=false;
  uint64_t owner=0,generation=0,sound_owner=0,sound_generation=0;
  uintptr_t base=0,core=0,slot_base=0;
  uint32_t count=0,bytes=0,zero_writes=0,zero_readbacks=0;
  uint32_t grain=0,voices=0,output=0,rate=0,dry=0,wet=0,left=0,right=0;
 };
 struct Record {uint32_t flags=0,word8=0,word10=0,word2C=0,word30=0;uintptr_t data=0;};
 GameNativeSfxControl16()=default;
 GameNativeSfxControl16(const GameNativeSfxControl16&)=delete;
 GameNativeSfxControl16& operator=(const GameNativeSfxControl16&)=delete;
 void Close()noexcept{state_.live=state_.platform_live=false;state_.base=state_.core=state_.slot_base=0;data_={};block_.reset();core_.reset();}
 GameFsStatus Initialize(uint64_t owner,uint64_t generation,uintptr_t slot_base,uint32_t count){
  if(block_||core_||!owner||!generation||!slot_base||!count||count>16)return Refuse("Native control component owner/capacity policy");
  const auto next=state_.generation+1;state_={};state_.generation=next;
  std::unique_ptr<Core> core(new(std::nothrow) Core);
  if(!core)return Refuse("Windows SAS control-state allocation failed");
  core->grain=256;core->voices=32;core->output=0;core->rate=44100;
  core->dry=1;core->wet=0;core->left=0;core->right=0;
  if(reinterpret_cast<uintptr_t>(core.get())%64||core->grain!=256||core->voices!=32||core->rate!=44100||core->output||core->dry!=1||core->wet||core->left||core->right)
   return Refuse("Windows SAS control-state readback failed");
  // NativeDirtyControlCapacity16/positive extent is a Windows policy. The PSP
  // current configured count and complete5D754 thread/bootstrap remain unknown.
  std::unique_ptr<uint32_t[]> block(new(std::nothrow) uint32_t[count*25]);
  if(!block)return Refuse("Native dirty-control allocation failed; no base published");
  for(uint32_t i=0;i<count*25;++i){block[i]=0;++state_.zero_writes;}
  for(uint32_t i=0;i<count*25;++i){if(block[i])return Refuse("Native dirty-control mode1 clear readback");++state_.zero_readbacks;}
  core_=std::move(core);block_=std::move(block);data_={};
  static std::atomic<uint64_t> next_owner{0};state_.owner=++next_owner;
  state_.sound_owner=owner;state_.sound_generation=generation;state_.slot_base=slot_base;
  state_.base=reinterpret_cast<uintptr_t>(block_.get());state_.core=reinterpret_cast<uintptr_t>(core_.get());state_.count=count;state_.bytes=count*100;
  state_.live=state_.platform_live=true;return {};
 }
 Observation Snapshot()const{
  auto out=state_;out.live=Live();out.platform_live=PlatformLive();
  if(core_){out.grain=core_->grain;out.voices=core_->voices;out.output=core_->output;out.rate=core_->rate;out.dry=core_->dry;out.wet=core_->wet;out.left=core_->left;out.right=core_->right;}
  return out;
 }
 bool Read(uint32_t index,Record& out)const{
  if(!Live()||index>=state_.count)return false;const auto* p=block_.get()+25*index;
  out.flags=p[1];out.word8=p[2];out.data=data_[index];out.word10=p[4];out.word2C=p[11];out.word30=p[12];return true;
 }
 GameFsStatus Command0(uint32_t index,uintptr_t data,uint32_t word12,std::array<uintptr_t,12>& values){
  if(!Live()||index>=state_.count||!data)return Refuse("5FD78 current dirty-control record unavailable");
  // G98 is resolved again at each source group. Source+C is a full host-width
  // pointer sidecar, never a truncated uintptr written into a PSP32 word.
  const auto record=[&]()->uint32_t*{return Live()&&index<state_.count?block_.get()+25*index:nullptr;};
  size_t n=0;auto* p=record();if(!p)return Refuse("5FDE0 G98 reload");p[1]|=4;values[n++]=p[1];data_[index]=data;values[n++]=data;
  p=record();if(!p)return Refuse("5FDF4 G98 reload");p[4]=word12;values[n++]=p[4];p[1]|=8;values[n++]=p[1];
  p=record();if(!p)return Refuse("5FE08 G98 reload");p[11]=0;values[n++]=p[11];p[1]|=0x400;values[n++]=p[1];
  p=record();if(!p)return Refuse("5FE20 G98 reload");p[12]=0;values[n++]=p[12];p[1]|=0x800;values[n++]=p[1];
  p=record();if(!p)return Refuse("5FE38 G98 reload");p[2]=0;values[n++]=p[2];p[1]|=2;values[n++]=p[1];
  return {};
 }
 private:
 struct alignas(64) Core {std::array<uint32_t,904> envelope{};uint32_t grain=0,voices=0,output=0,rate=0,dry=0,wet=0,left=0,right=0;};
 static_assert(sizeof(Core)>=3616,"Owned SAS control envelope extent");
 static GameFsStatus Refuse(const char* why){return {GameFsCode::Unavailable,0,why};}
 bool PlatformLive()const{return state_.platform_live&&core_&&state_.core==reinterpret_cast<uintptr_t>(core_.get())&&state_.core%64==0&&core_->grain==256&&core_->voices==32&&core_->output==0&&core_->rate==44100&&core_->dry==1&&core_->wet==0&&core_->left==0&&core_->right==0;}
 bool Live()const{return state_.live&&PlatformLive()&&block_&&state_.owner&&state_.sound_owner&&state_.sound_generation&&state_.base==reinterpret_cast<uintptr_t>(block_.get())&&state_.slot_base&&state_.count>0&&state_.count<=16;}
 std::unique_ptr<Core> core_;
 std::unique_ptr<uint32_t[]> block_;
 std::array<uintptr_t,16> data_{};
 Observation state_;
};
}

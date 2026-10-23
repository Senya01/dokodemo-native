#pragma once
#include "game_filesystem_native.h"
#include <array>
#include <atomic>
#include <memory>
#include <new>

namespace dokodemo::recovered {
// NativeOwnedSfxSlotPool16: bounded Windows capacity/configuration/placement.
// PSP current capacity, heap and configuration remain unknown. The logical
// header keeps its full host pointer separate from PSP32 header words.
class GameNativeSfxSlotPool16 final {
 public:
 struct Slot {bool callback_readback=false;int32_t id=-1;uint32_t flags=0;int32_t half6=0;uint32_t word8=0,wordC=0,kind=0;uint32_t callback_code=0;uintptr_t callback_context=0;};
 struct Selection {bool returned=false;int32_t value=-1;int32_t position=-1;uint32_t free_reads=0,fallback_reads=0;};
 struct Observation {bool live=false;uint64_t owner=0,generation=0,serial=0,sound_owner=0,sound_generation=0;uintptr_t base=0;uint32_t count=0,bytes=0,zero_writes=0,zero_readbacks=0,id_writes=0,header_zero_writes=0,range_writes=0;std::array<uint32_t,26> header{};};
 GameNativeSfxSlotPool16()=default;
 GameNativeSfxSlotPool16(const GameNativeSfxSlotPool16&)=delete;
 GameNativeSfxSlotPool16& operator=(const GameNativeSfxSlotPool16&)=delete;
 void Close()noexcept{state_.live=false;state_.base=0;state_.owner=0;callbacks_={};block_.reset();}
 GameFsStatus Initialize(uint64_t sound_owner,uint64_t sound_generation,uint32_t count=16){
  if(block_||!sound_owner||!sound_generation||!count||count>16)return Refuse("Native SFX cold positive capacity/owner policy");
  //5C7B4 clears the logical H before count/allocation publication.
  const auto next_generation=state_.generation+1;state_={};state_.generation=next_generation;
  for(auto& word:state_.header){word=0;++state_.header_zero_writes;}
  state_.header[1]=count; //5C7EC delay; retained even if allocation fails
  std::unique_ptr<uint32_t[]> candidate(new(std::nothrow) uint32_t[count*8]);
  if(!candidate)return Refuse("Native SFX real allocation failed, H.base null/count retained");
  for(uint32_t i=0;i<count*8;++i){candidate[i]=0;++state_.zero_writes;}
  for(uint32_t i=0;i<count*8;++i){if(candidate[i])return Refuse("Native SFX mode1 clear readback");++state_.zero_readbacks;}
  block_=std::move(candidate);state_.base=reinterpret_cast<uintptr_t>(block_.get());
  for(uint32_t i=0;i<count;++i){block_[8*i]=i;++state_.id_writes;}
  static std::atomic<uint64_t> next{0};state_.owner=++next;state_.serial=state_.generation;
  state_.sound_owner=sound_owner;state_.sound_generation=sound_generation;state_.count=count;state_.bytes=count*32;state_.live=true;callbacks_={};return {};
 }
 GameFsStatus SetRange(uint32_t kind,int32_t start,int32_t count,int32_t& result){
  result=0;
  //Native admission bounds are stronger than5C8A0, which has no extent guard.
  if(!Live()||kind>=8||start<0||count<=0||uint32_t(start)>=state_.count||uint32_t(count)>state_.count-uint32_t(start))return Refuse("Native SFX range/extent admission policy");
  const uint32_t high=uint32_t(start)+uint32_t(count)-1;
  state_.header[(kind*8+12)/4]=high;++state_.range_writes;
  state_.header[(kind*4+0x48)/4]=uint32_t(start);++state_.range_writes;
  state_.header[(kind*8+8)/4]=uint32_t(start);++state_.range_writes;
  result=count;return {};
 }
 GameFsStatus Select(int32_t kind,int32_t argument,Selection& out)const{
  out={};if(!Live()||kind<0||kind>=8)return Refuse("5C8F4 native H/kind owner unavailable");
  const int32_t low=int32_t(state_.header[(kind*8+8)/4]),high=int32_t(state_.header[(kind*8+12)/4]);
  int32_t cursor=int32_t(state_.header[(kind*4+0x48)/4]);
  if(low<0||high<low||uint32_t(high)>=state_.count||cursor<low||cursor>high)return Refuse("5C8F4 native current range/cursor extent policy");
  const auto width=high-low;
  for(int32_t i=0;i<width;++i){const auto s=Read(uint32_t(cursor));++out.free_reads;
   if(!(s.flags&1)){out.returned=true;out.value=s.id;out.position=cursor;return {};}
   ++cursor;if(high<cursor)cursor=low;
  }
  int32_t id=-1,saved_half=-1,saved_word=-1,position=-1;
  for(int32_t i=low;i<=high;++i){const auto s=Read(uint32_t(i));++out.fallback_reads;
   if(s.half6>=argument&&(id<0||(s.half6>=saved_half&&int32_t(s.wordC)>=saved_word))){id=s.id;saved_half=s.half6;saved_word=int32_t(s.wordC);position=i;}
  }
  out.returned=true;out.value=id<0?-1:id;out.position=id<0?-1:position;return {};
 }
 GameFsStatus Allocate(int32_t kind,int32_t argument,int32_t packed,Selection& selected,int32_t& result){
  result=-1;if(packed)return Refuse("Unsupported5CB54 packed branch");
  const auto status=Select(kind,argument,selected);if(!status)return status;
  if(selected.value<0){result=-1;return {};}
  if(uint32_t(selected.value)>=state_.count)return Refuse("5CAAC native returned word0/extent policy");
  const auto s=Read(uint32_t(selected.value));
  if(s.id!=selected.value)return Refuse("5CAAC native current ID/index coherence policy");
  if(s.flags&1)return Refuse("Unsupported selected occupied-slot consumer5CB44 before source effects");
  auto* words=block_.get()+uint32_t(selected.value)*8;
  auto* bytes=reinterpret_cast<uint8_t*>(words);bytes[4]=uint8_t(s.flags|1);
  bytes[6]=uint8_t(argument);bytes[7]=uint8_t(uint32_t(argument)>>8);
  words[2]=uint32_t(packed);words[3]=0;words[4]=0;words[5]=0;callbacks_[uint32_t(selected.value)]={};words[6]=uint32_t(kind);
  const int32_t high=int32_t(state_.header[(kind*8+12)/4]),low=int32_t(state_.header[(kind*8+8)/4]);
  const int32_t next=selected.value+1;
  state_.header[(kind*4+0x48)/4]=uint32_t(next<high?next:low); //allocator differs from selector wrap
  result=int32_t(words[0]);return {};
 }
 GameFsStatus Register(int32_t id,uint32_t source_code,uintptr_t context){
  if(!Live()||id<0||uint32_t(id)>=state_.count||Read(uint32_t(id)).id!=id||!(Read(uint32_t(id)).flags&1)||!context
     ||(source_code!=0x5AA90&&source_code!=0x5AAC4))return Refuse("5CB64 native registration ownership policy");
  auto& callback=callbacks_[uint32_t(id)];
  callback.context=context; //5CB74 full-width host adaptation of slot+14
  callback.code=source_code;block_[uint32_t(id)*8+4]=source_code; //5CB7C JR delay: source code identity, never host callable
  return {};
 }
 Slot Read(uint32_t index)const{
  Slot s;if(!Live()||index>=state_.count)return s;
  const auto* words=block_.get()+index*8;const auto* bytes=reinterpret_cast<const uint8_t*>(words);
  s.id=int32_t(words[0]);s.flags=bytes[4];s.half6=int16_t(uint16_t(bytes[6])|(uint16_t(bytes[7])<<8));s.word8=words[2];s.wordC=words[3];s.kind=words[6];
  s.callback_code=words[4];s.callback_context=callbacks_[index].context;s.callback_readback=words[4]==callbacks_[index].code;return s;
 }
 Observation Snapshot()const{auto out=state_;out.live=Live();out.base=out.live?reinterpret_cast<uintptr_t>(block_.get()):0;return out;}
 private:
 static GameFsStatus Refuse(const char* why){return {GameFsCode::Unavailable,0,why};}
 bool Live()const{return state_.live&&block_&&state_.owner&&state_.base==reinterpret_cast<uintptr_t>(block_.get())&&state_.count>0&&state_.count<=16&&state_.header[1]==state_.count;}
 struct Callback {uintptr_t context=0;uint32_t code=0;};
 std::array<Callback,16> callbacks_{};
 std::unique_ptr<uint32_t[]> block_;
 Observation state_;
};
}

#pragma once
#include "recovered/game_scene_ui_native.h"
#include "recovered/game_sfx_slot_native.h"
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// SOURCE-ONLY: authored against CORE_RELEASE.v4; execution belongs to ROOT.
// Synthetic data, actual public owners. No private issuer or result provider.
namespace game_options_slot_regressions {
using namespace dokodemo::recovered;
using Read=GameSourceFeedbackRead;
using State=GameSourceSoundSnapshot;
using Slot=GameSourceSlotSnapshot;
using Pool=GameNativeSfxSlotPool16;
inline void Need(bool value,const char* id){if(!value)throw std::runtime_error(id);}
inline bool Same(const Pool::Observation& a,const Pool::Observation& b){
 return std::tie(a.live,a.owner,a.generation,a.serial,a.sound_owner,a.sound_generation,a.base,a.count,a.bytes,a.zero_writes,a.zero_readbacks,a.id_writes,a.header_zero_writes,a.range_writes,a.header)
 ==std::tie(b.live,b.owner,b.generation,b.serial,b.sound_owner,b.sound_generation,b.base,b.count,b.bytes,b.zero_writes,b.zero_readbacks,b.id_writes,b.header_zero_writes,b.range_writes,b.header);
}
inline bool Same(const Pool::Slot& a,const Pool::Slot& b){
 return std::tie(a.id,a.flags,a.half6,a.word8,a.wordC,a.kind,a.callback_code,a.callback_context,a.callback_readback)
 ==std::tie(b.id,b.flags,b.half6,b.word8,b.wordC,b.kind,b.callback_code,b.callback_context,b.callback_readback);
}
inline void Unavailable(const GameFsStatus& s,const char* id){Need(!s&&s.code==GameFsCode::Unavailable,id);}

// 1. Real owned allocation/range/selection/registration component. All capacity,
// Init IDs==index and admissible configuration checks are Native NONMATCHING
// policies. Select reads current word0; its equality to position here follows
// from this native initializer, not an original PSP identity guarantee.
inline void Component(){
 Pool p;Pool::Selection choice;int32_t result=17;
 Need(!p.Snapshot().live,"SLOT-C-cold");
 Unavailable(p.Initialize(0,1),"SLOT-C-owner0");
 Unavailable(p.Initialize(1,0),"SLOT-C-gen0");
 Unavailable(p.Initialize(1,1,0),"SLOT-C-count0");
 Unavailable(p.Initialize(1,1,17),"SLOT-C-count17");
 Need(bool(p.Initialize(41,7,4)),"SLOT-C-init");
 const auto cold=p.Snapshot();
 Need(cold.live&&cold.owner&&cold.generation==1&&cold.serial==1&&cold.sound_owner==41&&cold.sound_generation==7
  &&cold.base&&cold.base%4==0&&cold.count==4&&cold.bytes==128&&cold.zero_writes==32&&cold.zero_readbacks==32
  &&cold.id_writes==4&&cold.header_zero_writes==26&&!cold.range_writes&&cold.header[1]==4,"SLOT-C-clear");
 for(size_t i=0;i<cold.header.size();++i)if(i!=1)Need(!cold.header[i],"SLOT-C-header-clear");
 for(uint32_t i=0;i<4;++i){const auto s=p.Read(i);Need(s.id==int32_t(i)&&!s.flags&&!s.half6&&!s.word8&&!s.wordC&&!s.kind&&!s.callback_code&&!s.callback_context,"SLOT-C-slot-clear");}
 Need(p.Read(4).id==-1,"SLOT-C-read-bounds");
 Unavailable(p.Initialize(41,7,4),"SLOT-C-double-init");Need(Same(cold,p.Snapshot()),"SLOT-C-init-refusal-stable");
 for(const auto& bad:std::array<std::array<int32_t,3>,6>{{{{8,0,1}},{{1,-1,1}},{{1,0,0}},{{1,0,-1}},{{1,3,2}},{{1,4,1}}}}){
  result=91;Unavailable(p.SetRange(uint32_t(bad[0]),bad[1],bad[2],result),"SLOT-C-range-bad");
  Need(result==0&&Same(cold,p.Snapshot()),"SLOT-C-range-refusal-stable");
 }
 Need(bool(p.SetRange(1,0,4,result))&&result==4,"SLOT-C-range");
 auto h=p.Snapshot();Need(h.header[4]==0&&h.header[5]==3&&h.header[19]==0&&h.range_writes==3,"SLOT-C-range-writes");
 std::array<Pool::Slot,4> untouched;for(uint32_t i=0;i<4;++i)untouched[i]=p.Read(i);
 Need(bool(p.Select(1,0,choice))&&choice.returned&&choice.position==0&&choice.value==p.Read(uint32_t(choice.position)).id
  &&choice.free_reads==1&&!choice.fallback_reads,"SLOT-C-selector-word0");
 Need(Same(h,p.Snapshot()),"SLOT-C-selector-no-cursor-write");
 for(uint32_t i=0;i<4;++i)Need(Same(untouched[i],p.Read(i)),"SLOT-C-selector-no-slot-write");
 Unavailable(p.Select(-1,0,choice),"SLOT-C-negative-kind");Need(!choice.returned,"SLOT-C-selector-refusal-output");
 for(int32_t i=0;i<3;++i){
  Need(bool(p.Allocate(1,0,0,choice,result))&&result==i&&choice.value==i,"SLOT-C-real-allocation");
  const auto s=p.Read(uint32_t(i));Need(s.id==i&&s.flags==1&&!s.half6&&!s.word8&&!s.wordC&&s.kind==1&&!s.callback_code&&!s.callback_context,"SLOT-C-allocation-effects");
 }
 // After slot2, next3 equals inclusive high3: allocator resets to low0.
 // Selector's own increment wraps only when high < cursor.
 Need(p.Snapshot().header[19]==0,"SLOT-C-allocator-exact-high-boundary");
 const auto before_fallback=p.Snapshot();
 Need(bool(p.Select(1,0,choice))&&choice.value==3&&choice.position==3&&choice.free_reads==3&&choice.fallback_reads==4,"SLOT-C-inclusive-fallback");
 Need(Same(before_fallback,p.Snapshot()),"SLOT-C-fallback-no-write");
 Need(bool(p.Allocate(1,0,0,choice,result))&&result==3,"SLOT-C-fallback-real-allocate");
 std::array<uint8_t,16> contexts{};const auto context=reinterpret_cast<uintptr_t>(contexts.data());
 Need(bool(p.Register(result,0x5AA90,context+2)),"SLOT-C-first-register");
 Need(p.Read(3).callback_code==0x5AA90&&p.Read(3).callback_context==context+2,"SLOT-C-first-readback");
 Need(bool(p.Register(result,0x5AAC4,context)),"SLOT-C-second-register");
 Need(p.Read(3).callback_code==0x5AAC4&&p.Read(3).callback_context==context&&p.Read(3).callback_readback,"SLOT-C-second-readback");
 const auto registered=p.Read(3);h=p.Snapshot();
 Unavailable(p.Register(-1,0x5AA90,context),"SLOT-C-register-negative");
 Unavailable(p.Register(4,0x5AA90,context),"SLOT-C-register-high");
 Unavailable(p.Register(3,0x5AA90,0),"SLOT-C-register-null");
 Unavailable(p.Register(3,0x1234,context),"SLOT-C-register-code");
 Unavailable(p.Allocate(1,0,1,choice,result),"SLOT-C-packed");
 Unavailable(p.Allocate(1,0,0,choice,result),"SLOT-C-occupied-is-unsupported");
 Need(result==-1&&choice.returned&&choice.value==3&&choice.free_reads==3&&choice.fallback_reads==4
  &&Same(h,p.Snapshot())&&Same(registered,p.Read(3)),"SLOT-C-occupied-no-fake-exhaustion");
 p.Close();const auto closed=p.Snapshot();Need(!closed.live&&!closed.base,"SLOT-C-close");
 Unavailable(p.Select(1,0,choice),"SLOT-C-closed-select");
 Unavailable(p.SetRange(1,0,1,result),"SLOT-C-closed-range");
 Unavailable(p.Register(0,0x5AAC4,context),"SLOT-C-closed-register");
 Need(bool(p.Initialize(42,8,1))&&p.Snapshot().generation==cold.generation+1&&p.Snapshot().owner!=cold.owner,"SLOT-C-new-generation");
 Need(bool(p.SetRange(1,0,1,result)),"SLOT-C-one-range");
 Need(bool(p.Select(1,0,choice))&&choice.value==0&&!choice.free_reads&&choice.fallback_reads==1,"SLOT-C-single-inclusive");
 Need(bool(p.Allocate(1,0,0,choice,result))&&result==0&&p.Snapshot().header[19]==0,"SLOT-C-single-allocate");
 p.Close();Need(bool(p.Initialize(43,9,1))&&bool(p.SetRange(1,0,1,result)),"SLOT-C-negative-setup");
 Need(bool(p.Allocate(1,1,0,choice,result))&&choice.returned&&result==-1&&choice.value==-1&&!p.Read(0).flags,"SLOT-C-genuine-negative-selection");
}

// Independent SGXD factory. Offsets are bank-relative, including a real
// WAVE30 record6272 and event6 recordB28/bodyB3C. Prefix7 and tail16 survive.
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes=std::vector<uint8_t>(6766,0xA5),known=std::vector<uint8_t>(6766,1);
 uint64_t generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& out,std::string& error)const override{
  if(!live){error="slot synthetic lease retired";return false;}
  out={};out.bytes=const_cast<uint8_t*>(bytes.data());out.initialized=const_cast<uint8_t*>(known.data());out.size=bytes.size();
  out.host_address=uint64_t(reinterpret_cast<uintptr_t>(bytes.data()));out.owner=out.host_address;
  out.generation=generation;out.serial=serial;out.source_base=0x2000;out.readable=out.writable=true;return true;
 }
};
struct Input {
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 Input(){
  view=PackageStorage::Attach(lease,6766).Subview(7,6752);
  for(uint32_t i=0;i<6752;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,6736);view.WriteLE32(12,0x80000001);
  const auto chunk=[&](uint32_t at,uint32_t tag,uint32_t size){view.WriteLE32(at,tag);view.WriteLE32(at+4,size);};
  chunk(16,0x444E4752,2312);chunk(2336,0x44514553,2232);chunk(4576,0x45564157,2136);chunk(6720,0x454D414E,8);
  view.WriteLE32(28,1);view.WriteLE32(32,41);view.WriteLE32(36,40);
  for(uint32_t i=0;i<41;++i){const uint32_t at=40+56*i;view.WriteLE32(at,9);view.WriteLE32(at+8,56);view.WriteU8(at+12,10);
   view.WriteU8(at+24,uint8_t(32+i));view.WriteU8(at+25,uint8_t(32+i));view.WriteLE32(at+52,30);}
  view.WriteLE32(2348,1);view.WriteLE32(2352,0xA00);view.WriteLE32(0xA04,7);view.WriteLE32(0xA20,0xB28);
  view.WriteLE16(0xB30,0);view.WriteLE16(0xB32,1000);view.WriteLE32(0xB38,23);
  const std::array<uint8_t,9> d0{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(size_t i=0;i<d0.size();++i)view.WriteU8(0xB3C+i,d0[i]);
  view.WriteLE32(4588,38);view.WriteU8(6272+8,3);view.WriteU8(6272+9,1);
  view.WriteLE32(6272+0x20,0x73E04);view.WriteLE32(6272+0x24,UINT32_MAX);
  view.WriteLE32(6272+0x28,0x73E04);view.WriteLE32(6272+0x2C,0x640);view.WriteLE32(6272+0x30,0);
  for(uint32_t i=0;i<16;++i)view.WriteU8(6736+i,uint8_t(0xC0+i));
 }
};
struct Fixture {
 Input input;GameSoundBank bank;
 Fixture(){Need(bool(bank.InitializeSource17F0()),"SLOT-F-init");Need(bool(bank.Register(input.view)),"SLOT-F-register");}
 Read Start(){Read out;const auto current=bank.SourceControl();Need(bool(bank.ReadOptionsVoiceFeedback(current,2,out))&&GameSourceVoiceFeedbackValid(out),"SLOT-F-voice");return out;}
 void Ticks(){bank.ObserveOptionsWorkerTick();bank.ObserveOptionsWorkerTick();}
 Read Reach(){Start();Ticks();Read out;Unavailable(bank.ServiceOptionsSlot(bank.SourceControl(),out),"SLOT-F-6016C-unavailable");Need(GameSourceSlotPrefixValid(out),"SLOT-F-genuine10");return out;}
};
using Predicate=bool(*)(const Read&)noexcept;
inline const std::array<Predicate,11>& Predicates(){
 static const std::array<Predicate,11> p{{GameSourceFeedbackReachedValid,GameSourceVoiceFeedbackValid,GameSourceVoiceServiceValid,GameSourceSequencePrefixValid,GameSourcePortPrefixValid,GameSourceProcessorPrefixValid,GameSourceControlPrefixValid,GameSourceWavePrefixValid,GameSourcePort24PrefixValid,GameSourceChildPrefixValid,GameSourceSlotPrefixValid}};return p;
}
inline void Disjoint(const Read& r,uint32_t domain){for(uint32_t i=0;i<11;++i)Need(Predicates()[i](r)==(i==domain),"SLOT-domain-disjoint");}

// 2. Actual full public API10. Registration records callback identities; it
// does not execute them. HostUnavailable is not a source return from6016C.
inline void Genuine(){
 Fixture f;const auto bytes=f.input.lease->bytes;const auto before=f.bank.SourceControl();const auto r=f.Reach();
 Need(GameSourceSoundSnapshotExact(r.after,f.bank.SourceControl())&&r.outcome==10&&r.frontier==0x6016C,"SLOT-G-current");Disjoint(r,10);
 const auto& q=r.after.voice.sequence;const auto& a=q.child;const auto& s=q.slot;
 Need(r.after.bank_live&&r.after.registration.word30==0&&GameSourceBankRegistrationExact(before.registration,r.after.registration),"SLOT-G-registrar-immutable0");
 Need(r.after.voice.bank_refs_before==0&&r.after.voice.bank_refs_after==1&&q.refs_before==1&&q.refs_after==2
  &&q.track.refs_before==2&&q.track.refs_after==3&&q.release_before==3&&q.release_after==2
  &&q.control.refs_before==2&&q.control.refs_after==3&&s.retain_before==3&&s.retain_after==4&&q.current_refs==4,"SLOT-G-ref-history");
 Need(q.processor.bytes==std::array<uint8_t,9>{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}}
  &&q.record_offset==0xB28&&q.record_bytes==23&&q.track.body_offset==0xB3C&&q.track.cursor_offset==0xB45,"SLOT-G-D0");
 Need(q.control.region_count==41&&q.control.region_first==40&&q.control.walk_count==33&&q.control.stride_reads==32
  &&q.control.region_offset==0x728&&q.control.selected_wave==30&&q.control.selected_byteC==10&&q.control.selected_flags==9,"SLOT-G-RGND");
 Need(q.wave.record_offset==6272&&q.wave.wave_count==38&&q.wave.wave_size==2136&&q.wave.record_read_mask==63&&q.wave.borrow_live,"SLOT-G-WAVE30");
 const std::array<uint32_t,13> packet{{0,10,3,1,0,0,0,0,0x73E04,UINT32_MAX,0x73E04,0,0x640}};
 Need(q.port_input.words==packet&&q.port_input.packet_complete&&s.packet_storage==q.port_input.packet_storage
  &&s.mode_slot==q.port_input.mode_slot&&s.mode_slot==q.wave.control_slot_storage&&s.packet_written&&!s.packet_word6,"SLOT-G-stable-packet");
 Need(a.entered&&a.header_readback&&a.candidate_id==int32_t(a.index)&&a.index<16&&a.flags==0&&a.channel_before==0&&a.channel_after==255
  &&a.source_free_linked&&a.reserved&&a.channel_written&&a.frontier==0x5C8F4&&!a.slot_returned&&!a.child_returned,"SLOT-G-immutable-entry-child");
 Need(s.allocator_result==0&&s.selector_result==0&&s.selected_position==0&&s.stored_channel==0&&s.callback_id==0
  &&s.current_note==64&&s.current_flags==0&&s.current_parent_flags==3&&q.port.flags==3&&s.free_unlinked&&s.parent_linked
  &&s.child_pointer==a.node_address&&s.child_parent==q.port.storage&&s.child_returned&&s.current_readback,"SLOT-G-current-child-lease");
 Need(s.child_links[0]==s.child_links[1]&&s.child_links[0]!=a.candidate_links[0]&&!q.port.sentinel_self[0]&&q.port.sentinel_self[1],"SLOT-G-linked-not-free");
 Need(s.callback_contexts==std::array<uintptr_t,2>{{a.node_address+0x2A,a.node_address}}
  &&s.callback_codes==std::array<uint32_t,2>{{0x5AA90,0x5AAC4}}&&s.first_registered&&s.second_registered,"SLOT-G-registration-order");
 Need(s.constructed&&s.parameter_writes==35&&s.scratch_writes==4&&s.known_halfwords==UINT64_C(0xFFFFFF003)
  &&s.parameters[0]==UINT16_MAX&&!s.parameters[1]&&s.scratch78==std::array<uint32_t,4>{},"SLOT-G-constructor-writes");
 for(uint32_t i=12;i<36;++i){uint16_t wanted=0;if(i>=16&&((i-16)%6)==0)wanted=4096;if(i>=17&&((i-17)%6)==0)wanted=1000;Need(s.parameters[i]==wanted,"SLOT-G-known-parameter");}
 for(uint32_t i=2;i<12;++i)Need(!(s.known_halfwords&(UINT64_C(1)<<i)),"SLOT-G-unknown-not-hostzero");
 Need(s.consumer_entered&&!s.consumer_returned&&s.consumer_pc==0x5B264&&s.consumer_delay_pc==0x5B268&&s.phase==9,"SLOT-G-entry-only");
 Need(q.worker_tick_serial==2&&q.worker_previous_ns&&q.worker_current_ns>=q.worker_previous_ns,"SLOT-G-two-real-ticks");
 Need(f.input.lease->bytes==bytes&&r.after.bank.offset==7&&r.after.registration.tail.size==16,"SLOT-G-input-and-tail-preserved");
 // Actual words/sentinel halves via observed uintptr storage; restored per case.
 // Snapshot rejection isolates readback, without a consumed-service owner claim.
 for(auto address:std::array<uintptr_t,7>{{s.storage+8,s.storage+12,s.storage+24,s.storage+16,s.packet_storage+24,s.child_pointer+0x18,s.child_pointer+0x1A}}){
  auto* word=reinterpret_cast<uint8_t*>(address);const auto saved=*word;*word=saved^1;
  auto drift=r;drift.after=f.bank.SourceControl();const bool rejected=!drift.after.voice.sequence.slot.current_readback&&!GameSourceSlotPrefixValid(drift);
  *word=saved;Need(rejected,"SLOT-G-owned-word-drift");Need(GameSourceSoundSnapshotExact(r.after,f.bank.SourceControl()),"SLOT-G-owned-word-restored");
 }
 Read replay;Unavailable(f.bank.ServiceOptionsSlot(r.owner,replay),"SLOT-G-replay-old");Need(!replay.attempted,"SLOT-G-replay-noattempt");
 Unavailable(f.bank.ServiceOptionsSlot(r.after,replay),"SLOT-G-replay-current");Need(!replay.attempted&&GameSourceSoundSnapshotExact(r.after,f.bank.SourceControl())&&GameSourceSlotPrefixValid(r),"SLOT-G-replay-stable-frozen");
}

inline void NoAttempt(Fixture& f,const State& expected,const char* id){
 const auto before=f.bank.SourceControl();Read out;out.attempted=true;Unavailable(f.bank.ServiceOptionsSlot(expected,out),id);
 Need(!out.attempted&&!out.after.voice.sequence.slot.entered&&GameSourceSoundSnapshotExact(before,f.bank.SourceControl()),id);
}
// 3. Pre-registration malformed inputs and admitted partial failures differ
// from precondition no-attempt refusals. No synthetic timestamp/result supply.
inline void Refusals(){
 for(uint32_t kind=0;kind<5;++kind){Input input;GameSoundBank b;Need(bool(b.InitializeSource17F0()),"SLOT-R-register-init");
  if(kind==0)input.view.WriteLE32(0,0);if(kind==1)input.view.WriteLE32(8,15);if(kind==2)input.view.WriteLE32(20,UINT32_MAX);
  if(kind==3)input.view.WriteLE32(4576,0x444E4752);if(kind==4)input.view.WriteLE32(8,6752);
  Need(!b.Register(input.view)&&!b.SourceControl().voice.live,"SLOT-R-malformed-register");
 }
 for(uint32_t kind=0;kind<6;++kind){Input input;GameSoundBank b;Need(bool(b.InitializeSource17F0()),"SLOT-R-partial-init");
  if(kind==0)input.view.WriteU8(6280,2); //format2 is outside selected body
  if(kind==1){input.view.WriteLE32(6272,1);input.view.WriteU8(6281,2);} //flag1 selects actual two channels
  if(kind==2)input.view.WriteLE32(4588,30); //wave30 outside current count
  if(kind==3)input.view.WriteLE32(36,UINT32_MAX); //RGND first outside chunk
  if(kind==4)input.view.WriteLE32(0xB38,8); //D0 operand crosses body end
  if(kind==5)input.view.WriteU8(6281,2); //flag0 keeps channels1, packet word3 becomes2
  Need(bool(b.Register(input.view)),"SLOT-R-partial-register");Read start,out;
  Need(bool(b.ReadOptionsVoiceFeedback(b.SourceControl(),2,start)),"SLOT-R-partial-voice");b.ObserveOptionsWorkerTick();b.ObserveOptionsWorkerTick();
  Unavailable(b.ServiceOptionsSlot(b.SourceControl(),out),"SLOT-R-partial-service");
  Need(out.attempted&&out.outcome==10&&out.after.voice.services==1&&(out.after.voice.sequence.slot.entered==(kind==5))&&!GameSourceSlotPrefixValid(out),"SLOT-R-partial-not-noattempt");
  if(kind==0)Need(out.after.voice.sequence.child.bank_mode_read&&!out.after.voice.sequence.child.format_dispatched,"SLOT-R-format-cut");
  if(kind==1)Need(out.after.voice.sequence.child.entered&&!out.after.voice.sequence.child.reserved,"SLOT-R-channel-cut");
  if(kind==2)Need(out.after.voice.sequence.wave.returned&&out.after.voice.sequence.wave.result_null&&!out.after.voice.sequence.child.entered,"SLOT-R-wave-bound-cut");
  if(kind==5)Need(out.after.voice.sequence.wave.channels==1&&out.after.voice.sequence.port_input.words[3]==2&&out.frontier==0x6016C,"SLOT-R-flag0-fixedchannel");
 }
 for(uint32_t ticks=0;ticks<2;++ticks){Fixture f;f.Start();if(ticks)f.bank.ObserveOptionsWorkerTick();Read out;
  Unavailable(f.bank.ServiceOptionsSlot(f.bank.SourceControl(),out),"SLOT-R-missing-tick");const auto& q=out.after.voice.sequence;
  Need(out.attempted&&out.outcome==10&&out.frontier==0x5A06C&&q.worker_tick_serial==ticks&&q.current_refs==2&&q.command_released
   &&out.after.voice.services==1&&!q.slot.entered&&!GameSourceSlotPrefixValid(out),"SLOT-R-tick-partial");
 }
 {Fixture f;const auto stale=f.bank.SourceControl();f.Start();NoAttempt(f,stale,"SLOT-R-stale");
  auto changed=f.bank.SourceControl();++changed.owner;NoAttempt(f,changed,"SLOT-R-owner");
  changed=f.bank.SourceControl();++changed.voice.sequence.slot.owner;NoAttempt(f,changed,"SLOT-R-slot-expected-owner");
  Fixture other;other.Start();NoAttempt(f,other.bank.SourceControl(),"SLOT-R-other-owner");
  Read alias;alias.after=f.bank.SourceControl();const auto before=alias.after;
  Unavailable(f.bank.ServiceOptionsSlot(alias.after,alias),"SLOT-R-output-alias");
  Need(!alias.attempted&&GameSourceSoundSnapshotExact(before,f.bank.SourceControl()),"SLOT-R-alias-noattempt");
 }
 for(uint32_t kind=0;kind<5;++kind){Fixture f;f.Start();const auto before=f.bank.SourceControl();
  if(kind==0)f.input.view.WriteLE32(6272+0x30,1);if(kind==1)f.input.lease->live=false;
  if(kind==2)++f.input.lease->generation;if(kind==3)++f.input.lease->serial;
  if(kind==4)f.input.lease->known[7+6272]=0;
  Need(!f.bank.SourceControl().bank_live,"SLOT-R-current-bank-live-gate");NoAttempt(f,before,"SLOT-R-bank-changed");
 }
 {Fixture f;f.Start();const auto before=f.bank.SourceControl();f.bank.Close();Read out;
  Unavailable(f.bank.ServiceOptionsSlot(before,out),"SLOT-R-closed");Need(!out.attempted&&!f.bank.SourceControl().live,"SLOT-R-closed-noattempt");}
}

// Every scalar and every array leaf of the 77+9 SlotSnapshot schema is tested
// for Exact sensitivity. Pure transport has three positive opaque identities
// and ten UNKNOWN parameter halfwords; accepting these is not issuer authority.
template<class T> inline void Flip(T& value){value=static_cast<T>(value^1);}
template<class Edit> inline void Leaf(const Read& original,Edit edit,bool allowed,const char* id){
 auto changed=original;edit(changed.after.voice.sequence.slot);
 const auto& a=original.after.voice.sequence.slot;const auto& b=changed.after.voice.sequence.slot;
 Need(!GameSourceSlotExact(a,b)&&!GameSourceSlotExact(b,a)&&GameSourceSlotExact(a,a),id);
 Need(!GameSourceSequenceExact(original.after.voice.sequence,changed.after.voice.sequence)
  &&!GameSourceVoiceExact(original.after.voice,changed.after.voice)
  &&!GameSourceSoundSnapshotExact(original.after,changed.after)&&!GameSourceFeedbackExact(original,changed),id);
 Need(GameSourceSlotPrefixValid(changed)==allowed,id);
}
// 4. Exact fields/leaf matrix plus composite snapshot guards. These copies are
// passive observations. Historical child entry is retained, never rebased.
inline void Matrix(){
 Fixture f;const auto r=f.Reach();size_t scalars=0,leaves=0;
#define SLOT_BAD(F) do{Leaf(r,[](Slot& s){Flip(s.F);},false,"SLOT-M-" #F);++scalars;}while(false)
#define SLOT_OPAQUE(F) do{Leaf(r,[](Slot& s){s.F+=17;},true,"SLOT-M-opaque-" #F);++scalars;}while(false)
 SLOT_BAD(entered);SLOT_BAD(live);SLOT_BAD(header_read);SLOT_BAD(selector_returned);SLOT_BAD(allocator_returned);SLOT_BAD(dispatcher_returned);
 SLOT_BAD(channel_returned);SLOT_BAD(first_registered);SLOT_BAD(second_registered);SLOT_BAD(constructed);SLOT_BAD(parameters_initialized);SLOT_BAD(scratch_zeroed);
 SLOT_BAD(retain_attempted);SLOT_BAD(retained);SLOT_BAD(free_unlinked);SLOT_BAD(parent_linked);SLOT_BAD(child_returned);SLOT_BAD(packet_written);
 SLOT_BAD(consumer_entered);SLOT_BAD(consumer_returned);SLOT_BAD(current_readback);SLOT_OPAQUE(owner);SLOT_OPAQUE(generation);SLOT_OPAQUE(serial);
 SLOT_BAD(sound_owner);SLOT_BAD(sound_generation);SLOT_BAD(base);SLOT_BAD(storage);SLOT_BAD(child_pointer);SLOT_BAD(parent_storage);SLOT_BAD(packet_storage);
 SLOT_BAD(mode_slot);SLOT_BAD(child_parent);SLOT_BAD(child_sentinel_storage);SLOT_BAD(kind);SLOT_BAD(argument);SLOT_BAD(packed);SLOT_BAD(low);SLOT_BAD(high);
 SLOT_BAD(cursor_before);SLOT_BAD(cursor_after);SLOT_BAD(selector_result);SLOT_BAD(selected_position);SLOT_BAD(allocator_result);SLOT_BAD(stored_channel);
 SLOT_BAD(callback_id);SLOT_BAD(current_note);SLOT_BAD(current_flags);SLOT_BAD(retain_result);SLOT_BAD(count);SLOT_BAD(bytes);SLOT_BAD(zero_writes);
 SLOT_BAD(zero_readbacks);SLOT_BAD(id_writes);SLOT_BAD(header_zero_writes);SLOT_BAD(range_writes);SLOT_BAD(free_reads);SLOT_BAD(fallback_reads);
 SLOT_BAD(allocation_flags);SLOT_BAD(slot_half6);SLOT_BAD(slot_word8);SLOT_BAD(slot_wordC);SLOT_BAD(slot_kind);SLOT_BAD(current_parent_flags);SLOT_BAD(phase);
 SLOT_BAD(retain_before);SLOT_BAD(retain_after);SLOT_BAD(retain_pc);SLOT_BAD(return_pc);SLOT_BAD(return_delay_pc);SLOT_BAD(packet_word6);SLOT_BAD(consumer_pc);
 SLOT_BAD(consumer_delay_pc);SLOT_BAD(frontier);SLOT_BAD(scratch_writes);SLOT_BAD(parameter_writes);SLOT_BAD(known_halfwords);
#undef SLOT_BAD
#undef SLOT_OPAQUE
#define SLOT_ARRAY(F) do{for(size_t i=0;i<r.after.voice.sequence.slot.F.size();++i){Leaf(r,[i](Slot& s){Flip(s.F[i]);},false,"SLOT-M-" #F);++leaves;}}while(false)
 SLOT_ARRAY(child_links);SLOT_ARRAY(callback_contexts);SLOT_ARRAY(child_sentinel_links);SLOT_ARRAY(callback_codes);SLOT_ARRAY(callback_context_pc);SLOT_ARRAY(callback_code_pc);SLOT_ARRAY(child_sentinel_ids);SLOT_ARRAY(scratch78);
#undef SLOT_ARRAY
 for(size_t i=0;i<36;++i){Leaf(r,[i](Slot& s){Flip(s.parameters[i]);},i>=2&&i<12,"SLOT-M-parameters");++leaves;}
 Need(scalars==77&&leaves==54,"SLOT-M-schema77-plus54");
 for(uint32_t kind=0;kind<9;++kind){auto bad=r;auto& q=bad.after.voice.sequence;
  if(kind==0)q.child.channel_after=uint32_t(q.slot.stored_channel);if(kind==1)q.child.candidate_id=q.slot.current_note;
  if(kind==2)q.child.source_free_linked=false;if(kind==3)q.child.slot_returned=true;
  if(kind==4)q.port.flags=1;if(kind==5)q.current_refs=3;if(kind==6)q.slot.base=q.port_input.block_base;
  if(kind==7)q.slot.callback_contexts[0]=q.slot.callback_contexts[1];if(kind==8)q.slot.mode_slot=q.slot.packet_storage;
  Need(!GameSourceSlotPrefixValid(bad),"SLOT-M-composite-refusal");
 }
 // Zero identities are invalid; a coherent positive opaque identity is merely
 // transportable. The pre-service owner gate is covered in Refusals.
 auto bad=r;bad.after.voice.sequence.slot.owner=0;Need(!GameSourceSlotPrefixValid(bad),"SLOT-M-owner0");
 auto passive=r;passive.after.voice.sequence.slot.owner+=19;Need(GameSourceSlotPrefixValid(passive),"SLOT-M-passive-positive-owner");
 Read out;Unavailable(f.bank.ServiceOptionsSlot(passive.after,out),"SLOT-M-consumed-passive-copy");Need(!out.attempted,"SLOT-M-no-restart");
 Need(GameSourceFeedbackExact(r,r)&&GameSourceSoundSnapshotExact(r.after,f.bank.SourceControl()),"SLOT-M-original-preserved");
}

// 5. Real Close/reinitialize/register/voice/slot generation2, frozen receipt,
// and every historic API0..9 through its genuine public service entry.
inline void LifecycleAndHistoric(){
 Fixture f;const auto old=f.Reach();f.bank.Close();
 Need(GameSourceSlotPrefixValid(old)&&!f.bank.SourceControl().voice.sequence.slot.live,"SLOT-L-closed-frozen-observation");
 Read out;Unavailable(f.bank.ServiceOptionsSlot(old.after,out),"SLOT-L-old-after-close");Need(!out.attempted,"SLOT-L-close-noattempt");
 Need(bool(f.bank.InitializeSource17F0())&&bool(f.bank.Register(f.input.view)),"SLOT-L-reinit-register");
 const auto fresh=f.bank.SourceControl();Need(fresh.generation==old.after.generation+1&&fresh.owner!=old.after.owner,"SLOT-L-new-sound-generation");
 f.Start();const auto voice=f.bank.SourceControl();
 Need(voice.voice.generation==old.after.voice.generation+1&&!voice.voice.sequence.live
  &&GameSourceSlotExact(voice.voice.sequence.slot,Slot{}),"SLOT-L-new-voice-detached-slot");
 NoAttempt(f,old.owner,"SLOT-L-stale-original");NoAttempt(f,old.after,"SLOT-L-stale-final");f.Ticks();
 Unavailable(f.bank.ServiceOptionsSlot(f.bank.SourceControl(),out),"SLOT-L-generation2-entry");Need(GameSourceSlotPrefixValid(out),"SLOT-L-generation2-valid");
 Need(out.after.voice.sequence.generation==old.after.voice.sequence.generation+1
  &&out.after.voice.sequence.slot.generation==old.after.voice.sequence.slot.generation+1
  &&out.after.voice.sequence.slot.owner!=old.after.voice.sequence.slot.owner&&GameSourceSlotPrefixValid(old),"SLOT-L-current-versus-frozen");
 const std::array<uint32_t,10> frontier{{0x56598,0x4AD4C,0x57CF0,0x5A5AC,0x59DD4,0x5B9E4,0x557B8,0x5B1EC,0x5ABF0,0x5C8F4}};
 for(uint32_t domain=0;domain<10;++domain){Fixture h;Read r;GameFsStatus status;
  if(domain==0)status=h.bank.ReadOptionsFeedback(h.bank.SourceControl(),2,r);
  else {r=h.Start();if(domain>1){h.Ticks();const auto expected=h.bank.SourceControl();
   switch(domain){case 2:status=h.bank.ServiceOptionsVoice(expected,r);break;case 3:status=h.bank.ServiceOptionsSequence(expected,r);break;
    case 4:status=h.bank.ServiceOptionsPort(expected,r);break;case 5:status=h.bank.ServiceOptionsProcessor(expected,r);break;
    case 6:status=h.bank.ServiceOptionsControl(expected,r);break;case 7:status=h.bank.ServiceOptionsWave(expected,r);break;
    case 8:status=h.bank.ServiceOptionsPort24(expected,r);break;case 9:status=h.bank.ServiceOptionsChild(expected,r);break;}
  }}
  if(domain!=1)Unavailable(status,"SLOT-H-domain-status");
  Need(r.outcome==domain&&r.frontier==frontier[domain]&&!r.after.voice.sequence.slot.entered,"SLOT-H-original-domain");Disjoint(r,domain);
  for(uint32_t wrong=0;wrong<11;++wrong)if(wrong!=domain){auto changed=r;changed.outcome=wrong;Need(!Predicates()[domain](changed),"SLOT-H-wrong-outcome");}
  if(domain==9)Need(r.after.voice.sequence.child.slot_entered&&!r.after.voice.sequence.child.slot_returned
   &&r.after.voice.sequence.child.channel_after==255&&r.after.voice.sequence.current_refs==3,"SLOT-H-old9-before-body");
 }
}
}

inline void GameOptionsSlotRegressionChecks(){
 game_options_slot_regressions::Component();
 game_options_slot_regressions::Genuine();
 game_options_slot_regressions::Refusals();
 game_options_slot_regressions::Matrix();
 game_options_slot_regressions::LifecycleAndHistoric();
}

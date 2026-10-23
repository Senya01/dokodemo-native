#pragma once
// SOURCE ONLY; executed by author=0. ROOT owns compilation/runtime/integration.
// Supplied accepted parent: b77ba45bf537127d26de3a6797eed51be6224bf4 (no Git query).
// Frozen input: CORE_RELEASE.v1 + CORE_DELTA.v1; named v2/v3 body overlays.
// Synthetic live PackageStorage only. Public receipts prove transport shape,
// never private Actor/Content issuance, PSP execution, audio or effect return.
// Own fixture primitives adapted from port header lines 1..66 only.
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace game_options_processor_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Read=GameSourceFeedbackRead;
using Processor=GameSourceProcessorSnapshot;
inline void Check(bool value,const char* id){if(!value)throw std::runtime_error(std::string("options-processor ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-processor ")+id+": "+s.detail);}
inline bool Cut(const GameFsStatus& s,const char* text){return !s&&s.code==GameFsCode::Unavailable&&s.detail.find(text)!=std::string::npos;}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
inline bool Empty(const Read& r){return GameSourceFeedbackExact(r,Read{});}
template<class A> inline bool Zero(const A& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});}

struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& s,std::string& error)const override{
  if(!live){error="processor synthetic lease retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;
  s.generation=generation;s.serial=serial;s.source_base=0x2000;s.readable=s.writable=true;return true;
 }
};
enum class Fault:U {None,Opcode,Tag,Width,Extent,EmptyBody,OneByte,EightBytes,FourBytes,Kind,Divisor,Overflow};
struct Input {
 static constexpr U record=0xB28,body=0xB3C,wave=0xB54,end=0xB6C,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 std::vector<uint8_t> frozen_bytes,frozen_known;
 explicit Input(Fault fault=Fault::None){
  lease->bytes.assign(size+14,0xA5);lease->known.assign(size+14,1);
  view=PackageStorage::Attach(lease,size+14).Subview(7,size);
  for(U i=0;i<size;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,end);view.WriteLE32(12,0x80000001);
  view.WriteLE32(16,0x444E4752);view.WriteLE32(20,4);
  view.WriteLE32(28,0x44514553);view.WriteLE32(32,wave-36);
  view.WriteLE32(40,1);view.WriteLE32(44,64);view.WriteLE32(68,7);view.WriteLE32(96,record);
  view.WriteLE32(record+4,0);view.WriteLE16(record+8,0);view.WriteLE16(record+10,1000);view.WriteLE32(record+16,23);
  for(U i=0;i<23;++i)view.WriteU8(body+i,uint8_t(0x80+i));
  const std::array<uint8_t,9> d0{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(U i=0;i<d0.size();++i)view.WriteU8(body+i,d0[i]);
  view.WriteLE32(wave,0x45564157);view.WriteLE32(wave+4,4);
  view.WriteLE32(wave+12,0x454D414E);view.WriteLE32(wave+16,4);
  // All semantic/body faults precede registration and its initialized digest.
  switch(fault){
   case Fault::Opcode:view.WriteU8(body,0xD1);break;
   case Fault::Tag:view.WriteU8(body+1,0x10);break;
   case Fault::Width:view.WriteU8(body+1,5);break;
   case Fault::Extent:view.WriteU8(body+6,4);break;
   case Fault::EmptyBody:view.WriteLE32(record+16,0);break;
   case Fault::OneByte:view.WriteLE32(record+16,1);break;
   case Fault::EightBytes:view.WriteLE32(record+16,8);break;
   case Fault::FourBytes:view.WriteLE32(record+16,4);break;
   case Fault::Kind:view.WriteLE16(record+8,1);break;
   case Fault::Divisor:view.WriteLE16(record+10,0);break;
   case Fault::Overflow:view.WriteLE32(record+16,UINT32_MAX);break;
   default:break;
  }
  frozen_bytes=lease->bytes;frozen_known=lease->known;
 }
 void Unchanged()const{
  Check(lease->bytes==frozen_bytes&&lease->known==frozen_known,"I001 whole synthetic allocation unchanged");
  for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"I002 guards unchanged");
 }
};
struct Fixture {
 Input input;GameSoundBank bank;
 explicit Fixture(Fault fault=Fault::None):input(fault){
  Ok(bank.InitializeSource17F0(),"I003 initialize");Ok(bank.Register(input.view),"I004 actual public registration");
 }
};
inline Read Start(Fixture& f){
 const auto before=f.bank.SourceControl();
 Check(before.live&&before.bank_live&&before.registration.live&&before.registration.attached&&before.registration.coherent,"I005 registered live bank");
 Check(before.registration.writer==0x4AB58&&before.registration.word30==0&&before.se5.bank_handle==before.registration.id,"I006 genuine bank field writer");
 Read r;Ok(f.bank.ReadOptionsVoiceFeedback(before,2,r),"I007 actual returned voice");
 Check(GameSourceVoiceFeedbackValid(r)&&Same(r.owner,before)&&Same(r.after,f.bank.SourceControl()),"I008 voice receipt/current readback");
 Check(r.after.voice.services==0&&r.after.registration.word30==0,"I009 historical registration stays zero");
 return r;
}
inline void Ticks(Fixture& f,U count=2){for(U i=0;i<count;++i)f.bank.ObserveOptionsWorkerTick();}
inline Q KnownMask(){Q mask=3;for(U i=12;i<36;++i)mask|=Q(1)<<i;return mask;}
inline void PortFacts(const GameSourcePortSnapshot& p){
 Check(p.live&&p.active&&p.returned&&p.id==0&&p.flags==1&&p.free_unlinked&&p.active_linked,"I010 actual returned signed port zero");
 Check(p.known_halfwords==KnownMask(),"I011 unknown halfwords remain unknown");
 for(U i=2;i<12;++i)Check(!(p.known_halfwords&(Q(1)<<i)),"I012 no invented known mask");
 for(U i=0;i<36;++i)if(p.known_halfwords&(Q(1)<<i)){
  const U expected=(i>=16&&(i-16)%6==0)?4096:(i>=17&&(i-17)%6==0)?1000:0;
  Check(p.parameters[i]==expected,"I013 allocator known parameters unchanged");
 }
 Check(Zero(p.zero70)&&p.sentinel_ids==std::array<int16_t,2>{{-1,-1}}&&p.sentinel_flags==std::array<uint16_t,2>{{0,0}}
  &&p.sentinel_self==std::array<bool,2>{{true,true}},"I014 allocator sentinels/clear");
}
inline Read Advance(Fixture& f){
 const auto before=f.bank.SourceControl();Read r;
 Check(Cut(f.bank.ServiceOptionsProcessor(before,r),"5B9E4"),"I015 genuine host unavailable at control body");
 Check(Same(r.owner,before)&&Same(r.after,f.bank.SourceControl()),"I016 actual processor service owner");return r;
}
inline void NoAttempt(Fixture& f,const Sound& expected,const char* id){
 const auto before=f.bank.SourceControl();Read r;r.outcome=0xBAD;
 const auto status=f.bank.ServiceOptionsProcessor(expected,r);
 Check(!status&&status.code==GameFsCode::Unavailable&&Empty(r)&&Same(before,f.bank.SourceControl()),id);
}
inline void PositiveFacts(const Read& r){
 const auto& v=r.after.voice;const auto& q=v.sequence;const auto& p=q.processor;
 Check(r.outcome==5&&r.frontier==0x5B9E4&&v.frontier==r.frontier&&q.frontier==r.frontier,"A001 outcome5 exact next frontier");
 Check(GameSourceProcessorPrefixValid(r),"A002 processor transport predicate");
 Check(!GameSourcePortPrefixValid(r)&&!GameSourceSequencePrefixValid(r)&&!GameSourceVoiceServiceValid(r)
  &&!GameSourceVoiceFeedbackValid(r)&&!GameSourceFeedbackReachedValid(r),"A003 receipt domains disjoint");
 Check(v.services==1&&v.callback_entered&&v.callback_returned&&v.sequence_result_known,"A004 actual callback returned before update");
 Check(q.live&&q.active&&q.flags==3&&q.track.child_linked&&!q.sentinel_self&&!q.child_head_same&&q.sentinel_id==-1,"A005 attached child/sentinel topology");
 Check(q.handler_returned&&q.admission_returned&&q.command_released&&q.track.returned&&q.track_activated,"A006 genuine pre-update returns");
 Check(q.refs_before==1&&q.refs_after==2&&q.track.refs_before==2&&q.track.refs_after==3&&q.current_refs==2
  &&q.release_before==3&&q.release_after==2&&q.release_pc==0x55A7C&&r.after.registration.word30==0,"A007 live refs2 historical registration0");
 Check(q.update_entered&&q.processor_attempted&&q.update_pc==0x54FE8&&q.processor_pc==0x5A120,"A008 actual worker update and context entry");
 Check(q.worker_tick_serial==2&&q.worker_previous_ns&&q.worker_current_ns>=q.worker_previous_ns,"A009 two actual consecutive worker ticks");
 const Q elapsed=q.worker_current_ns-q.worker_previous_ns;
 const float delta=float(double(elapsed)/1000000000.0),scaled=delta*1000000.0f;
 Check(std::isfinite(scaled)&&scaled>=0&&double(scaled)<=double(INT32_MAX)&&q.native_elapsed_ns==elapsed
  &&q.native_delta_seconds==delta&&q.update_quantum==int32_t(scaled)&&q.processor_quantum==q.update_quantum,"A010 native elapsed truncation provenance");
 Check(q.bank_ids==v.bank_ids&&q.output_unchanged&&Zero(q.output_before)&&q.output_after==v.wordsA0,"A011 actual IDs/output storage");
 Check(q.track.record_offset==0xB28&&q.track.body_offset==0xB3C&&q.track.end_offset==0xB53&&q.track.cursor_offset==0xB45,"A012 actual body cursor advance");
 Processor e;e.entered=e.sentinel_terminated=e.controls_read=e.kind_dispatched=e.opcode_read=e.operands_returned=e.control_attempted=true;
 e.context_storage=q.track.context_storage;e.track_storage=q.track.storage;e.command_storage=p.command_storage;
 e.sentinel_pc=0x5A05C;e.dispatch_pc=0x59F50;e.kind_target=0x59168;e.opcode_pc=0x5923C;e.opcode=0xD0;
 e.cursor_before=0xB3C;e.cursor_after=0xB45;e.body_end=0xB53;e.operand_count=4;e.control_pc=0x5903C;e.frontier=0x5B9E4;
 e.quantum=q.update_quantum;e.port_id=0;e.operands={64,0,64,4096};
 e.operand_begin={0xB3D,0xB3F,0xB40,0xB42};e.operand_end={0xB3F,0xB40,0xB42,0xB45};
 e.operand_call={0x5935C,0x5936C,0x5937C,0x5938C};e.bytes={0xD0,1,0x64,0,1,0x64,2,0x40,0x96};
 e.command={0,64,uint32_t(q.bank_ids[0]),0,64,4096};
 Check(GameSourceProcessorExact(p,e),"A013 all processor facts exact");
 Check(p.context_storage==q.processor_context&&p.track_storage==q.track.storage&&p.command_storage
  &&p.command_storage!=q.ids_storage&&p.command_storage!=q.output_storage&&p.command_storage!=q.track.storage
  &&p.command_storage!=q.track.context_storage&&p.command_storage!=q.port.storage,"A014 actual distinct command ABI storage");
 Check(!p.control_returned&&!p.kind_returned&&!p.context_returned&&p.control_attempted,"A015 host refusal supplies no source return");
 PortFacts(q.port);
}

// Family 1: actual public registration, voice, two ticks and live bank D0 read.
inline void ActualProcessor(){
 Fixture f;Start(f);Ticks(f);const auto r=Advance(f);PositiveFacts(r);f.input.Unchanged();
}

// Family 2: two missing timestamp cases and eleven pre-registration faults.
// These are partial *actual* services, not unchanged-owner gate refusals.
inline void PartialAndBounds(){
 for(U n=0;n<2;++n){
  Fixture f;Start(f);Ticks(f,n);Read r;const auto before=f.bank.SourceControl();
  Check(Cut(f.bank.ServiceOptionsProcessor(before,r),"consecutive worker timestamps"),"B001 missing actual timestamps");
  const auto& q=r.after.voice.sequence;
  Check(r.outcome==5&&r.frontier==0x5A06C&&r.after.voice.services==1&&r.after.voice.callback_returned
   &&q.track.returned&&q.command_released&&q.current_refs==2&&!q.update_entered&&!q.processor_attempted
   &&!q.processor.entered&&q.track.cursor_offset==0xB3C&&q.worker_tick_serial==n,"B002 partial callback/track before missing time refusal");
  Check(!GameSourceProcessorPrefixValid(r),"B003 missing time is no complete processor receipt");f.input.Unchanged();
 }
 struct Case {Fault fault;U cursor,count;const char* detail;};
 const std::array<Case,8> cases{{
  {Fault::Opcode,0xB3D,0,"opcode"},{Fault::Tag,0xB3D,0,"encoding"},
  {Fault::Width,0xB3D,0,"extent"},{Fault::Extent,0xB42,3,"extent"},
  {Fault::EmptyBody,0xB3C,0,"owning body bounds"},{Fault::OneByte,0xB3D,0,"owning body bounds"},
  {Fault::EightBytes,0xB43,3,"owning body bounds"},{Fault::FourBytes,0xB40,2,"owning body bounds"}
 }};
 for(const auto& c:cases){
  Fixture f(c.fault);Start(f);Ticks(f);Read r;
  Check(Cut(f.bank.ServiceOptionsProcessor(f.bank.SourceControl(),r),c.detail),"B004 selected decoder refusal");
  const auto& q=r.after.voice.sequence;const auto& p=q.processor;
  Check(r.outcome==5&&r.frontier==0x59DD4&&r.after.voice.services==1&&r.after.voice.callback_returned
   &&q.handler_returned&&q.command_released&&q.current_refs==2&&q.update_entered&&q.processor_attempted,"B005 decoder failure follows genuine prior returns");
  Check(p.entered&&p.sentinel_terminated&&p.controls_read&&p.kind_dispatched&&p.cursor_before==0xB3C
   &&p.cursor_after==c.cursor&&q.track.cursor_offset==c.cursor&&p.operand_count==c.count,"B006 partial cursor and completed operands");
  Check(!p.operands_returned&&!p.control_attempted&&!p.control_returned&&!p.kind_returned&&!p.context_returned
   &&!p.command_storage&&Zero(p.command)&&!GameSourceProcessorPrefixValid(r),"B007 no control after partial selected read");
  if(c.fault==Fault::EmptyBody)Check(!p.opcode_read,"B008 empty body never reads opcode");
  if(c.fault==Fault::EightBytes)Check(p.bytes[7]==0x40&&p.bytes[8]==0&&p.operand_begin[3]==0xB42&&p.operand_end[3]==0,
   "B009 v3 failed local payload cursor not published after tag B43");
  PortFacts(q.port);f.input.Unchanged();
 }
 for(const auto fault:{Fault::Kind,Fault::Divisor,Fault::Overflow}){
  Fixture f(fault);Start(f);Ticks(f);Read r;const auto status=f.bank.ServiceOptionsProcessor(f.bank.SourceControl(),r);
  const auto& q=r.after.voice.sequence;
  Check(!status&&status.code==GameFsCode::Unavailable&&r.outcome==5&&r.after.voice.services==1
   &&!r.after.voice.callback_returned&&!q.handler_returned&&!q.command_released&&!q.update_entered&&!q.processor_attempted
   &&!q.processor.entered&&!GameSourceProcessorPrefixValid(r),"B010 malformed record refuses before processor");f.input.Unchanged();
 }
}

// Family 3: eight owner/lease/replay gate scenarios; zero additional attempts.
inline void OwnerGates(){
 {Fixture f;Start(f);const auto stale=f.bank.SourceControl();GameSourceSoundGainReceipt gain;
  Ok(f.bank.QuerySourceGain(stale,77,gain),"C001 real query makes supplied owner stale");Ticks(f);
  NoAttempt(f,stale,"C002 stale whole expected snapshot");f.input.Unchanged();}
 {Fixture f;Start(f);Ticks(f);auto alias=f.bank.SourceControl();++alias.bank.offset;
  NoAttempt(f,alias,"C003 same allocation different view offset is not owner");f.input.Unchanged();}
 {Fixture f;Start(f);Ticks(f);const auto expected=f.bank.SourceControl();f.input.lease->known[7+Input::body+8]=0;
  NoAttempt(f,expected,"C004 unknown body byte fails digest/current owner before service");f.input.lease->known[7+Input::body+8]=1;f.input.Unchanged();}
 {Fixture f;Start(f);Ticks(f);const auto expected=f.bank.SourceControl();f.input.lease->bytes[7+Input::body]^=1;
  NoAttempt(f,expected,"C005 changed known byte fails current bank digest");f.input.lease->bytes[7+Input::body]^=1;f.input.Unchanged();}
 {Fixture f;Start(f);Ticks(f);const auto expected=f.bank.SourceControl();f.input.lease->live=false;
  NoAttempt(f,expected,"C006 closed lease makes bank unavailable");f.input.lease->live=true;f.input.Unchanged();}
 {Fixture f;Start(f);Ticks(f);const auto expected=f.bank.SourceControl();f.bank.Close();
  NoAttempt(f,expected,"C007 closed bank cannot service old owner");f.input.Unchanged();}
 {Fixture f;Start(f);Ticks(f);const auto r=Advance(f);NoAttempt(f,r.owner,"C008 replay of pre-service owner");
  NoAttempt(f,r.after,"C009 replay even with current post-service owner");f.input.Unchanged();}
 {Fixture f,other;Start(f);Start(other);Ticks(f);NoAttempt(f,other.bank.SourceControl(),"C010 another real bank owner rejected");f.input.Unchanged();other.input.Unchanged();}
}

template<class Edit> inline void ProcessorMutation(const Read& base,const Edit& edit,bool reject=true){
 auto changed=base;edit(changed.after.voice.sequence.processor);
 Check(!GameSourceProcessorExact(base.after.voice.sequence.processor,changed.after.voice.sequence.processor)
  &&!GameSourceFeedbackExact(base,changed),"D001 exact comparators detect each processor field/element");
 Check(GameSourceProcessorPrefixValid(changed)!=reject,"D002 processor transport mutation expectation");
}
template<class Edit> inline void ReceiptMutation(const Read& base,const Edit& edit){
 auto changed=base;edit(changed);Check(!GameSourceFeedbackExact(base,changed)&&!GameSourceProcessorPrefixValid(changed),"D003 receipt envelope mutation rejected");
}
// Family 4: ALL 32 processor scalar fields + ALL 31 array elements covered.
// 62 reject; command_storage is deliberately opaque, not issuance authority.
// Also six zero/alias command identities, all 26 receipt scalars, two bindings.
inline void TransportMutations(){
 Fixture f;Start(f);Ticks(f);const auto base=Advance(f);Check(GameSourceProcessorPrefixValid(base),"D004 mutation baseline transport");
#define DNP_BOOL(field) ProcessorMutation(base,[](Processor& p){p.field=!p.field;})
 DNP_BOOL(entered);DNP_BOOL(sentinel_terminated);DNP_BOOL(controls_read);DNP_BOOL(kind_dispatched);DNP_BOOL(opcode_read);
 DNP_BOOL(operands_returned);DNP_BOOL(control_attempted);DNP_BOOL(control_returned);DNP_BOOL(kind_returned);DNP_BOOL(context_returned);
#undef DNP_BOOL
#define DNP_SCALAR(field) ProcessorMutation(base,[](Processor& p){p.field^=1;})
 DNP_SCALAR(context_storage);DNP_SCALAR(track_storage);DNP_SCALAR(sentinel_pc);DNP_SCALAR(dispatch_pc);DNP_SCALAR(kind_target);
 DNP_SCALAR(opcode_pc);DNP_SCALAR(opcode);DNP_SCALAR(cursor_before);DNP_SCALAR(cursor_after);DNP_SCALAR(body_end);
 DNP_SCALAR(effective_flags);DNP_SCALAR(context_word18);DNP_SCALAR(track_e4);DNP_SCALAR(track_e6);DNP_SCALAR(byte29);
 DNP_SCALAR(countdown);DNP_SCALAR(operand_count);DNP_SCALAR(control_pc);DNP_SCALAR(frontier);DNP_SCALAR(quantum);DNP_SCALAR(port_id);
#undef DNP_SCALAR
 const auto& q=base.after.voice.sequence;
 const std::array<uintptr_t,6> forbidden{{0,q.ids_storage,q.output_storage,q.track.storage,q.port.storage,q.track.context_storage}};
 for(const auto identity:forbidden)ProcessorMutation(base,[&](Processor& p){p.command_storage=identity;});
 uintptr_t opaque=1;while(opaque==q.processor.command_storage||std::find(forbidden.begin(),forbidden.end(),opaque)!=forbidden.end())++opaque;
 ProcessorMutation(base,[&](Processor& p){p.command_storage=opaque;},false);
 // The preceding accepted synthetic transport mutation never enters a bank,
 // Actor or Content consumer; ROOT's separate private proof owns issuance.
 for(U i=0;i<4;++i){
  ProcessorMutation(base,[&](Processor& p){p.operands[i]^=1;});
  ProcessorMutation(base,[&](Processor& p){p.operand_begin[i]^=1;});
  ProcessorMutation(base,[&](Processor& p){p.operand_end[i]^=1;});
  ProcessorMutation(base,[&](Processor& p){p.operand_call[i]^=1;});
 }
 for(U i=0;i<9;++i)ProcessorMutation(base,[&](Processor& p){p.bytes[i]^=1;});
 for(U i=0;i<6;++i)ProcessorMutation(base,[&](Processor& p){p.command[i]^=1;});
#define DNP_READ_NUM(field) ReceiptMutation(base,[](Read& r){r.field^=1;})
 DNP_READ_NUM(context_index);DNP_READ_NUM(read_pc);DNP_READ_NUM(frontier);DNP_READ_NUM(selector);DNP_READ_NUM(params_written);
 DNP_READ_NUM(virtual_target);DNP_READ_NUM(wrapper);DNP_READ_NUM(issuer);DNP_READ_NUM(bank_handle);DNP_READ_NUM(gain);
 DNP_READ_NUM(group);DNP_READ_NUM(event);DNP_READ_NUM(extra);DNP_READ_NUM(mode);DNP_READ_NUM(context_owner);DNP_READ_NUM(context_generation);DNP_READ_NUM(outcome);
#undef DNP_READ_NUM
#define DNP_READ_BOOL(field) ReceiptMutation(base,[](Read& r){r.field=!r.field;})
 DNP_READ_BOOL(attempted);DNP_READ_BOOL(bank_attached);DNP_READ_BOOL(bank_handle_known);DNP_READ_BOOL(read_returned);
 DNP_READ_BOOL(getter_returned);DNP_READ_BOOL(start_returned);DNP_READ_BOOL(handle_stored);DNP_READ_BOOL(feedback_returned);
#undef DNP_READ_BOOL
 ReceiptMutation(base,[](Read& r){r.rate=2.0f;});
 ReceiptMutation(base,[](Read& r){r.after.voice.sequence.track.cursor_offset=0xB3C;});
 ReceiptMutation(base,[](Read& r){r.after.bank_live=false;});
 f.input.Unchanged();Check(Same(base.after,f.bank.SourceControl()),"D005 pure validator mutations never change actual owner");
}

// Family 5: same-object actual Close/reinitialize and all old domains separate.
inline void LifecycleAndHistorical(){
 Fixture f;Start(f);Ticks(f);const auto first=Advance(f);PositiveFacts(first);
 f.bank.Close();const auto retired=f.bank.SourceControl();
 Check(!retired.live&&!retired.bank_live&&!retired.voice.live&&!retired.voice.sequence.live
  &&!retired.voice.sequence.track.live&&!retired.voice.sequence.port.live,"E001 actual close retires owners");
 NoAttempt(f,first.after,"E002 retired processor owner cannot service");
 Ok(f.bank.InitializeSource17F0(),"E003 actual same-object reinitialize");Ok(f.bank.Register(f.input.view),"E004 actual same-bank re-registration");
 Start(f);Ticks(f);const auto second=Advance(f);PositiveFacts(second);
 Check(second.after.generation==2&&second.after.voice.generation==2&&second.after.voice.sequence.generation==2
  &&second.after.voice.sequence.port.generation==2&&second.after.registration.generation==2,"E005 actual generation2 lifecycle");
 Check(second.after.owner!=first.after.owner&&second.after.voice.owner!=first.after.voice.owner
  &&second.after.voice.sequence.owner!=first.after.voice.sequence.owner,"E006 new owners after retirement");
 NoAttempt(f,first.owner,"E007 old generation cannot service reinitialized bank");f.input.Unchanged();
 {Input input;GameSourceSoundControl raw;Ok(raw.Initialize17F0(),"E008 old raw owner initialize");Ok(raw.BindSe5(input.view),"E009 old raw SE5 borrow");
  Read r;Check(Cut(raw.Feedback4AC38(raw.Snapshot(),r),"4AC84")&&r.read_pc==0x4AC84&&r.attempted&&!r.read_returned
   &&!r.getter_returned&&!r.start_returned&&!GameSourceProcessorPrefixValid(r),"E010 historical unknown bank read remains separate");input.Unchanged();}
 {Fixture old;Read r;Check(Cut(old.bank.ReadOptionsFeedback(old.bank.SourceControl(),2,r),"56598")
   &&r.outcome==0&&r.frontier==0x56598&&GameSourceFeedbackReachedValid(r)&&!r.after.voice.live&&!GameSourceProcessorPrefixValid(r),"E011 old issuer domain preserved");old.input.Unchanged();}
 {Fixture old;Start(old);Read r;Check(Cut(old.bank.ServiceOptionsVoice(old.bank.SourceControl(),r),"57CF0")
   &&r.outcome==2&&r.frontier==0x57CF0&&GameSourceVoiceServiceValid(r)&&!GameSourceProcessorPrefixValid(r),"E012 old next-service domain preserved");old.input.Unchanged();}
 {Fixture old;Start(old);Read r;Check(Cut(old.bank.ServiceOptionsSequence(old.bank.SourceControl(),r),"5A5AC")
   &&r.outcome==3&&r.frontier==0x5A5AC&&GameSourceSequencePrefixValid(r)&&!GameSourceProcessorPrefixValid(r),"E013 old selected port-allocation domain preserved");old.input.Unchanged();}
 {Fixture old;Start(old);Ticks(old);Read r;Check(Cut(old.bank.ServiceOptionsPort(old.bank.SourceControl(),r),"59DD4")
   &&r.outcome==4&&r.frontier==0x59DD4&&GameSourcePortPrefixValid(r)&&!GameSourceProcessorPrefixValid(r),"E014 old entry-only processor domain preserved");
  Check(GameSourceProcessorExact(r.after.voice.sequence.processor,Processor{})&&r.after.voice.sequence.track.cursor_offset==0xB3C,
   "E015 old ServicePort consumes no D0 body");
  PortFacts(r.after.voice.sequence.port);
  Check(r.after.voice.sequence.port.known_halfwords==second.after.voice.sequence.port.known_halfwords
   &&r.after.voice.sequence.port.parameters==second.after.voice.sequence.port.parameters,"E016 D0 refusal changes no allocator parameters/known mask");old.input.Unchanged();}
}
} // namespace game_options_processor_regressions

// Only entry point; no old header/entry/family invocation and no hidden runner.
inline void GameOptionsProcessorRegressionChecks(){
 using namespace game_options_processor_regressions;
 ActualProcessor();PartialAndBounds();OwnerGates();TransportMutations();LifecycleAndHistorical();
}

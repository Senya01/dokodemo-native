#pragma once
// SOURCE ONLY, executed=0. CORE_RELEASE.v3; accepted parent supplied by ROOT:
// e9ebc8fdf185bcce68bf14ced2d9b2431c36a855. No old entry/family is included or called.
// Synthetic initialized SGXD; real public registration/voice/sequence producers.
// Native pool sizes/IDs/pointers and host reservation are NON-MATCHING policies.
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace game_options_sequence_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Voice=GameSourceVoiceSnapshot;
using Sequence=GameSourceSequenceSnapshot;using Read=GameSourceFeedbackRead;
inline void Check(bool b,const char* id){if(!b)throw std::runtime_error(std::string("options-sequence ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-sequence ")+id+": "+s.detail);}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
template<class A> inline bool Zero(const A& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});}
inline bool StatusSame(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& s,std::string& e)const override{
  if(!live){e="sequence synthetic lease retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;
  s.generation=generation;s.serial=serial;s.source_base=0x2000;s.readable=s.writable=true;return true;
 }
};
struct Input {
 static constexpr U record=0xB28,body=record+20,wave=0xB54,end=0xB6C,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 // Faults are authored BEFORE registration/digest, never forged live receipts.
 explicit Input(U fault=0){
  lease->bytes.assign(size+14,0xA5);lease->known.assign(size+14,1);
  view=PackageStorage::Attach(lease,size+14).Subview(7,size);
  for(U i=0;i<size;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,end);view.WriteLE32(12,0x80000001);
  view.WriteLE32(16,0x444E4752);view.WriteLE32(20,4);
  view.WriteLE32(28,0x44514553);view.WriteLE32(32,wave-36);
  view.WriteLE32(40,1);view.WriteLE32(44,64);view.WriteLE32(68,7);view.WriteLE32(96,record);
  view.WriteLE32(record+4,0);view.WriteLE16(record+8,0);view.WriteLE16(record+10,1000);view.WriteLE32(record+16,23);
  for(U i=0;i<23;++i)view.WriteU8(body+i,uint8_t(0x80+i));
  view.WriteLE32(wave,0x45564157);view.WriteLE32(wave+4,4);
  view.WriteLE32(wave+12,0x454D414E);view.WriteLE32(wave+16,4);
  switch(fault){case 1:view.WriteLE16(record+8,1);break;case 2:view.WriteLE16(record+10,0);break;
   case 3:view.WriteLE32(96,size-4);break;case 4:view.WriteLE32(44,size-4);break;
   case 5:view.WriteLE32(68,6);break;case 6:view.WriteLE32(record+4,1);break;}
 }
 void Guards()const{for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"S001");}
};
struct Fixture {
 Input input;GameSoundBank bank;
 explicit Fixture(U fault=0):input(fault){Ok(bank.InitializeSource17F0(),"S002");Ok(bank.Register(input.view),"S003");}
};
inline Read Start(Fixture& f){
 const auto before=f.bank.SourceControl();Read r;Ok(f.bank.ReadOptionsVoiceFeedback(before,2,r),"S004");
 const auto after=f.bank.SourceControl();
 Check(GameSourceVoiceFeedbackValid(r)&&r.outcome==1&&r.frontier==0x4AD4C&&r.start_returned&&r.handle_stored&&r.feedback_returned,"S005");
 Check(Same(before,r.owner)&&Same(after,r.after)&&after.bank_live&&after.voice.returned&&after.voice.bank_refs_before==0
  &&after.voice.bank_refs_after==1&&after.voice.retain_writer==0x55A2C&&after.se5.handle==after.voice.id,"S006");
 Check(GameSourceBankRegistrationExact(before.registration,after.registration)&&after.registration.word30==0
  &&GameSourceSequenceExact(after.voice.sequence,Sequence{})&&!after.voice.services,"S007");
 return r;
}
inline Read Cut(Fixture& f){
 Read r;const auto before=f.bank.SourceControl();const auto status=f.bank.ServiceOptionsSequence(before,r);
 Check(status.code==GameFsCode::Unavailable&&status.detail.find("5A5AC")!=std::string::npos,"S008");
 Check(Same(r.owner,before)&&Same(r.after,f.bank.SourceControl())&&GameSourceSequencePrefixValid(r),"S009");
 return r;
}
inline void Context(const Read& r){
 const auto& b=r.owner.voice;const auto& v=r.after.voice;const auto& s=v.sequence;
 Check(r.outcome==3&&r.frontier==0x5A5AC&&!GameSourceVoiceServiceValid(r)&&!GameSourceVoiceFeedbackValid(r)
  &&!GameSourceFeedbackReachedValid(r),"S010");
 Check(v.live&&v.active&&v.returned&&v.services==1&&v.callback_entered&&!v.callback_returned&&v.callback_pc==0x578CC
  &&v.initializer_store_pc==0x578D4&&v.sequence_result_known&&v.frontier==0x5A5AC,"S011");
 Check(s.live&&s.active&&s.allocation_returned&&s.owner&&s.generation&&s.allocation==1
  &&s.voice_owner==v.owner&&s.voice_generation==v.generation&&s.id==0&&s.id>=0&&s.id<16,"S012");
 Check(v.sequence_slot==s.id&&v.words80[0]==U(s.id)&&s.allocation_pc==0x57CF0&&s.return_pc==0x57D80
  &&s.result_store_pc==0x57908,"S013");
 Check(s.flags==1&&s.sentinel_id==-1&&!s.sentinel_flags&&s.sentinel_self&&s.child_head_same,"S014");
 Check(s.ids_storage&&s.output_storage&&s.ids_storage!=s.output_storage
  &&s.ids_storage!=reinterpret_cast<uintptr_t>(&v.bank_ids)&&s.output_storage!=reinterpret_cast<uintptr_t>(&v.wordsA0)
  &&s.bank_ids==b.bank_ids&&s.bank_ids==v.bank_ids,"S015");
 Check(s.output_unchanged&&s.output_before==b.wordsA0&&s.output_after==b.wordsA0&&v.wordsA0==b.wordsA0
  &&Zero(v.wordsA0)&&Zero(v.words74)&&!v.words80[1]&&!v.words80[2]&&!v.words80[3],"A0-no-command32-output");
 Check(v.owner==b.owner&&v.generation==b.generation&&v.id==b.id&&v.bank==b.bank&&v.bank_refs_after==1,"S016");
}
inline void Command(const Read& r){
 const auto& b=r.owner;const auto& a=r.after;const auto& v=a.voice;const auto& s=v.sequence;
 Check(s.lookup_returned&&s.lookup_pc==0x57A90&&s.clear_returned&&s.clear_pc==0x58540
  &&s.command_entered&&s.command_pc==0x58768&&!s.command&&!s.subindex&&!s.group&&s.event==6,"58C04-command0-clear-empty");
 Check(s.bank==b.registration.id&&s.bank==v.bank&&s.bank_retained&&s.retain_writer==0x55A2C
  &&s.refs_before==1&&s.refs_after==2&&a.bank_live,"S017");
 Check(GameSourceBankRegistrationExact(b.registration,a.registration)&&!a.registration.word30,"S018");
 Check(s.record_lookup_returned&&s.record_offset==Input::record&&!s.record_kind&&s.record_divisor==1000
  &&s.record_bytes==23&&s.initial_factor==1000.0f&&s.selected_factor==1000.0f,"S019");
 Check(s.track_reserved&&s.track_id==0&&!s.track_pool_stride&&!s.track_source_initialized&&!s.track_activated
  &&s.port_attempted&&s.port_call_pc==0x582D8&&s.frontier==0x5A5AC&&!s.port_result_known&&s.port_id==-1
  &&!s.handler_returned&&!v.callback_returned,"S020");
 std::array<uint16_t,12> stored{};stored[1]=0x20C;stored[2]=stored[3]=1440;stored[9]=4096;
 auto consumed=stored;consumed[0]=0x20C;
 Check(v.working==stored&&v.published==stored&&v.consumer==consumed&&!v.dirty&&v.persistent==1
  &&!v.published_dirty&&v.published_persistent==1&&v.consumer_dirty==1&&v.consumer_persistent==1,"S021");
 Check(v.port_writes==5&&v.initial_port_writes==2&&Zero(v.initial_lane_controls)&&Zero(v.initial_lane_flags),"S022");
 auto expected=b;expected.voice=v;
 Check(Same(expected,a)&&a.current_id==-1&&a.se5.handle==v.id&&a.feedback_frontier==0x4AD4C
  &&!a.feedback_failed&&!a.failed,"S023");
}
// Family 1: actual context result, pointers, sentinel,57908, unchanged output.
inline void ReturnedContext(){
 Fixture f;const auto registered=f.bank.SourceControl();
 Check(registered.registration.live&&registered.registration.attached&&registered.registration.coherent
  &&registered.registration.writer==0x4AB58&&registered.registration.id==63&&!registered.registration.word30,"S024");
 Check(f.input.view.ReadLE32(96)==Input::record&&f.input.view.ReadLE32(Input::record+4)==0
  &&f.input.view.ReadLE16(Input::record+8)==0&&f.input.view.ReadLE16(Input::record+10)==1000
  &&f.input.view.ReadLE32(Input::record+16)==23,"S025");
 for(U i=0;i<23;++i)Check(f.input.view.ReadU8(Input::body+i)==uint8_t(0x80+i),"S026");
 Start(f);const auto issued=Cut(f);Context(issued);const auto observed=f.bank.SourceControl();
 Check(GameSourceSequenceExact(observed.voice.sequence,issued.after.voice.sequence)
  &&GameSourceVoiceExact(observed.voice,issued.after.voice),"S027");
 const auto metadata=f.bank.Snapshot();Check(metadata.metadata_ready&&!metadata.mixer_ready&&!metadata.playback_requests
  &&metadata.last_playback_result==-1,"S028");f.input.Guards();
}
// Family 2: real command cut and admitted partial failures in retained storage.
inline void CommandAndPartial(){
 Fixture f;Start(f);GameSourceSoundGainReceipt gain;
 Ok(f.bank.QuerySourceGain(f.bank.SourceControl(),105,gain),"S029");
 Check(GameSourceSoundGainValid(gain)&&gain.id==-1&&gain.negative_guard&&!gain.dispatched,"S030");
 const auto issued=Cut(f);Context(issued);Command(issued);
 for(U fault=1;fault<=2;++fault){
  Fixture malformed(fault);Start(malformed);const auto before=malformed.bank.SourceControl();Read partial;
  const auto status=malformed.bank.ServiceOptionsSequence(before,partial);const auto after=malformed.bank.SourceControl();const auto& s=after.voice.sequence;
  Check(status.code==GameFsCode::Unavailable&&status.detail.find("580D4")!=std::string::npos
   &&Same(partial.owner,before)&&Same(partial.after,after)&&after.bank_live,"S031");
  Check(after.voice.sequence_result_known&&s.allocation_returned&&s.lookup_returned&&s.clear_returned&&s.command_entered
   &&s.bank_retained&&s.refs_before==1&&s.refs_after==2&&s.record_lookup_returned,"S032");
  Check(!s.track_reserved&&!s.port_attempted&&!s.port_result_known&&!s.track_source_initialized&&!s.track_activated
   &&!s.handler_returned&&!after.voice.callback_returned&&!GameSourceSequencePrefixValid(partial),"S033");
  Read retry;Check(!malformed.bank.ServiceOptionsSequence(after,retry)&&!retry.attempted&&Same(after,malformed.bank.SourceControl()),"S034");
  malformed.input.Guards();
 }
 for(U fault=3;fault<=6;++fault){
  Fixture malformed(fault);const auto before=malformed.bank.SourceControl();Read partial;
  const auto status=malformed.bank.ReadOptionsVoiceFeedback(before,2,partial);const auto after=malformed.bank.SourceControl();
  Check(status.code==GameFsCode::Unavailable&&after.bank_live&&after.feedback_failed&&after.voice.live&&!after.voice.active
   &&!after.voice.returned&&after.voice.allocation==1&&after.voice.bank_refs_before==0&&after.voice.bank_refs_after==1,"S035");
  Check(!partial.start_returned&&!partial.handle_stored&&!partial.feedback_returned&&after.se5.handle==-1
   &&!after.voice.services&&!after.voice.port_writes&&GameSourceSequenceExact(after.voice.sequence,Sequence{})
   &&!GameSourceVoiceFeedbackValid(partial)&&!GameSourceSequencePrefixValid(partial),"S036");
  Read retry;Check(!malformed.bank.ServiceOptionsSequence(after,retry)&&!retry.attempted&&Same(after,malformed.bank.SourceControl()),"S037");
  Read replay;Check(StatusSame(status,malformed.bank.ReadOptionsVoiceFeedback(before,2,replay))
   &&GameSourceFeedbackExact(partial,replay)&&Same(after,malformed.bank.SourceControl()),"S038");
  malformed.input.Guards();
 }
}
inline void Denied(Fixture& f,const Sound& expected,const char* id){
 const auto before=f.bank.SourceControl();Read out;out.attempted=true;out.outcome=99;
 Check(!f.bank.ServiceOptionsSequence(expected,out)&&GameSourceFeedbackExact(out,Read{})&&Same(before,f.bank.SourceControl()),id);
}
// Family 3: wrong owner, stale queries, aliases, close/reopen and no replay.
inline void AuthorityAndLifetime(){
 Fixture f;const auto voice=Start(f);const auto ready=f.bank.SourceControl();
 for(U i=0;i<10;++i){auto wrong=ready;
  switch(i){case 0:++wrong.owner;break;case 1:++wrong.generation;break;case 2:++wrong.registration.serial;break;
   case 3:++wrong.registration.word30;break;case 4:++wrong.voice.owner;break;case 5:++wrong.voice.generation;break;
   case 6:++wrong.voice.bank_refs_after;break;case 7:wrong.voice.sequence_result_known=true;break;
   case 8:wrong.voice.sequence.owner=1;break;case 9:wrong.voice.working[2]^=1;break;}
  Denied(f,wrong,"S039");
 }
 Fixture other;Start(other);Denied(other,ready,"S040");
 GameSourceSoundGainReceipt gain;Ok(f.bank.QuerySourceGain(ready,105,gain),"S041");
 Denied(f,ready,"S042");const auto first=Cut(f);Context(first);Command(first);
 Denied(f,first.owner,"S043");Denied(f,first.after,"S044");
 auto forged=first.after;forged.voice.sequence.refs_after=1;Denied(f,forged,"S045");
 std::weak_ptr<Lease> weak=f.input.lease;f.input.view.Reset();f.input.lease.reset();
 Check(!weak.expired()&&Same(first.after,f.bank.SourceControl()),"S046");
 f.bank.Close();const auto closed=f.bank.SourceControl();
 Check(weak.expired()&&!closed.live&&!closed.bank_live&&!closed.voice.live&&!closed.voice.active
  &&!closed.voice.sequence.live&&!closed.voice.sequence.active,"S047");
 Denied(f,first.after,"S048");
 // Separate reusable owner keeps its own retained input for the public reopen.
 Fixture reuse;Start(reuse);const auto prior=Cut(reuse);reuse.bank.Close();
 Ok(reuse.bank.InitializeSource17F0(),"S049");Ok(reuse.bank.Register(reuse.input.view),"S050");
 const auto newbank=reuse.bank.SourceControl();Denied(reuse,prior.after,"S051");
 Check(newbank.owner!=prior.after.owner&&newbank.registration.id==prior.after.registration.id
  &&newbank.registration.generation!=prior.after.registration.generation&&newbank.registration.serial>prior.after.registration.serial,"S052");
 const auto fresh=Start(reuse);Check(GameSourceSequenceExact(fresh.after.voice.sequence,Sequence{})
  &&fresh.after.voice.owner!=prior.after.voice.owner,"S053");
 const auto next=Cut(reuse);Context(next);Command(next);
 Check(next.after.voice.sequence.id==prior.after.voice.sequence.id&&next.after.voice.sequence.owner!=prior.after.voice.sequence.owner
  &&next.after.voice.sequence.generation==prior.after.voice.sequence.generation+1&&next.after.voice.sequence.generation==2,"S054");
 Check(GameSourceSequencePrefixValid(prior)&&GameSourceVoiceFeedbackValid(voice),"S055");
 Denied(reuse,prior.after,"S056");Denied(reuse,next.after,"S057");
 for(U i=0;i<9;++i){Fixture alias;Start(alias);const auto before=alias.bank.SourceControl();
  switch(i){case 0:alias.input.view.WriteU8(0,0);break;case 1:alias.input.view.WriteU8(Input::record+10,0);break;
   case 2:alias.input.view.WriteU8(Input::body,0);break;case 3:alias.input.view.WriteU8(Input::size-1,1);break;
   case 4:alias.input.lease->known[7+Input::record]=0;break;case 5:alias.input.lease->known[7+Input::body+22]=0;break;
   case 6:++alias.input.lease->generation;break;case 7:++alias.input.lease->serial;break;case 8:alias.input.lease->live=false;break;}
  const auto invalid=alias.bank.SourceControl();Check(!invalid.bank_live&&invalid.voice.bank_refs_after==1
   &&GameSourceSequenceExact(invalid.voice.sequence,Sequence{}),"S058");
  Denied(alias,before,"S059");Denied(alias,invalid,"S060");
 }
}
struct Change {const char* name;void(*apply)(Read&);};
inline void Bad(const Read& issued,const Change& c){
 auto bad=issued;c.apply(bad);Check(!GameSourceSequencePrefixValid(bad)&&!GameSourceFeedbackExact(issued,bad),c.name);
}
// Family 4: copies are inputs only to pure validators, never a success provider.
inline void ValidationMutations(){
 Fixture f;Start(f);const auto issued=Cut(f);const auto untouched=f.bank.SourceControl();
#define SQ_BAD(F,V) {#F,[](Read& r){r.F=V;}}
 const Change envelope[]={
  SQ_BAD(context_index,4),SQ_BAD(read_pc,0),SQ_BAD(attempted,false),SQ_BAD(bank_attached,false),SQ_BAD(bank_handle_known,false),
  SQ_BAD(read_returned,false),SQ_BAD(frontier,0x57CF0),SQ_BAD(selector,1),SQ_BAD(params_written,5),SQ_BAD(virtual_target,0),
  SQ_BAD(wrapper,0),SQ_BAD(issuer,0),SQ_BAD(bank_handle,-1),SQ_BAD(gain,105),SQ_BAD(group,1),SQ_BAD(event,5),SQ_BAD(extra,1),SQ_BAD(mode,1),
  SQ_BAD(rate,0.5f),SQ_BAD(context_owner,0),SQ_BAD(context_generation,0),SQ_BAD(outcome,2),SQ_BAD(getter_returned,false),
  SQ_BAD(start_returned,false),SQ_BAD(handle_stored,false),SQ_BAD(feedback_returned,false),
  SQ_BAD(owner.owner,0),SQ_BAD(owner.generation,0),SQ_BAD(after.bank_live,false),SQ_BAD(after.current_id,63),SQ_BAD(after.se5.handle,63),
  SQ_BAD(after.registration.word30,2),SQ_BAD(after.registration.serial,0),SQ_BAD(after.queries,UINT64_MAX),SQ_BAD(after.feedback_failed,true)
 };
#define SQ_V(F,V) SQ_BAD(after.voice.F,V)
 const Change voice[]={
  SQ_V(live,false),SQ_V(active,false),SQ_V(returned,false),SQ_V(auxiliary_null,false),SQ_V(callback_entered,false),SQ_V(callback_returned,true),
  SQ_V(sequence_result_known,false),SQ_V(owner,0),SQ_V(generation,0),SQ_V(allocation,2),SQ_V(services,2),SQ_V(id,-1),SQ_V(bank,-1),
  SQ_V(sequence_slot,-1),SQ_V(flags,1),SQ_V(mode,1),SQ_V(group,1),SQ_V(event,5),SQ_V(extra,1),SQ_V(descriptor,0),SQ_V(bank_refs_before,1),
  SQ_V(bank_refs_after,2),SQ_V(retain_writer,0),SQ_V(initial_left,1440),SQ_V(initial_right,1440),SQ_V(initial_dirty_mask,0),
  SQ_V(initial_port_writes,0),SQ_V(initialized_mask,0),SQ_V(port_writes,6),SQ_V(return_pc,0),SQ_V(callback_pc,0),SQ_V(frontier,0),
  SQ_V(initializer_store_pc,0x57908),SQ_V(dirty,1),SQ_V(persistent,0),SQ_V(published_dirty,1),SQ_V(published_persistent,0),
  SQ_V(consumer_dirty,0),SQ_V(consumer_persistent,0),SQ_V(master_left,127),SQ_V(master_right,127)
 };
#define SQ_S(F,V) SQ_BAD(after.voice.sequence.F,V)
 // Every sequence scalar field, including zero/unknown result domains.
 const Change sequence[]={
  SQ_S(live,false),SQ_S(active,false),SQ_S(allocation_returned,false),SQ_S(lookup_returned,false),SQ_S(clear_returned,false),
  SQ_S(command_entered,false),SQ_S(bank_retained,false),SQ_S(record_lookup_returned,false),SQ_S(track_reserved,false),
  SQ_S(track_source_initialized,true),SQ_S(track_activated,true),SQ_S(port_attempted,false),SQ_S(port_result_known,true),SQ_S(handler_returned,true),
  SQ_S(owner,0),SQ_S(generation,0),SQ_S(allocation,2),SQ_S(voice_owner,0),SQ_S(voice_generation,0),SQ_S(ids_storage,0),SQ_S(output_storage,0),
  SQ_S(id,-1),SQ_S(bank,-1),SQ_S(sentinel_id,0),SQ_S(track_id,1),SQ_S(port_id,0),SQ_S(flags,3),SQ_S(sentinel_flags,1),
  SQ_S(allocation_pc,0),SQ_S(return_pc,0),SQ_S(result_store_pc,0),SQ_S(lookup_pc,0),SQ_S(clear_pc,0),SQ_S(command,1),SQ_S(subindex,1),
  SQ_S(group,1),SQ_S(event,5),SQ_S(command_pc,0),SQ_S(retain_writer,0),SQ_S(refs_before,0),SQ_S(refs_after,1),SQ_S(record_offset,112),
  SQ_S(record_kind,1),SQ_S(record_divisor,0),SQ_S(record_bytes,22),SQ_S(track_pool_stride,0xE8),SQ_S(port_call_pc,0),SQ_S(frontier,0),
  SQ_S(initial_factor,1.0f),SQ_S(selected_factor,1.0f),SQ_S(sentinel_self,false),SQ_S(child_head_same,false),SQ_S(output_unchanged,false)
 };
#undef SQ_S
#undef SQ_V
#undef SQ_BAD
 for(const auto& c:envelope)Bad(issued,c);for(const auto& c:voice)Bad(issued,c);for(const auto& c:sequence)Bad(issued,c);
 for(U lane=0;lane<3;++lane)for(U i=0;i<12;++i){auto bad=issued;auto& v=bad.after.voice;
  (lane==0?v.working:lane==1?v.published:v.consumer)[i]^=1;
  Check(!GameSourceSequencePrefixValid(bad)&&!GameSourceVoiceExact(issued.after.voice,v),"S061");}
 for(U i=0;i<4;++i){auto bad=issued;bad.after.voice.bank_ids[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S062");
  bad=issued;bad.after.voice.sequence.bank_ids[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S063");
  bad=issued;bad.after.voice.words80[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S064");}
 for(U i=0;i<2;++i){auto bad=issued;bad.after.voice.words74[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S065");}
 for(U i=0;i<8;++i){auto bad=issued;bad.after.voice.wordsA0[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S066");
  bad=issued;bad.after.voice.sequence.output_before[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S067");
  bad=issued;bad.after.voice.sequence.output_after[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S068");}
 for(U i=0;i<6;++i){auto bad=issued;bad.after.voice.initial_lane_controls[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S069");
  bad=issued;bad.after.voice.initial_lane_flags[i]^=1;Check(!GameSourceSequencePrefixValid(bad),"S070");}
 for(int32_t id:{-1,16,64,32767,INT32_MIN}){auto bad=issued;bad.after.voice.sequence.id=id;
  bad.after.voice.sequence_slot=id;bad.after.voice.words80[0]=U(id);Check(!GameSourceSequencePrefixValid(bad),"S071");}
 for(U outcome:{0u,1u,2u,4u,UINT32_MAX}){auto bad=issued;bad.outcome=outcome;Check(!GameSourceSequencePrefixValid(bad),"S072");}
 {auto bad=issued;bad.after.voice.sequence.ids_storage=bad.after.voice.sequence.output_storage;
  Check(!GameSourceSequencePrefixValid(bad),"S073");}
 for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
  auto bad=issued;bad.after.voice.sequence.selected_factor=value;Check(!GameSourceSequencePrefixValid(bad),"S074");
 }
 Check(Same(untouched,f.bank.SourceControl())&&GameSourceSequencePrefixValid(issued),"S075");
}
// Family 5: disjoint historical57CF0,56598 and unregistered4AC84 refusals.
inline void HistoricalDomains(){
 Fixture historic;Start(historic);const auto before=historic.bank.SourceControl();Read old;
 const auto failure=historic.bank.ServiceOptionsVoice(before,old);const auto after=historic.bank.SourceControl();
 Check(failure.code==GameFsCode::Unavailable&&failure.detail.find("57CF0")!=std::string::npos
  &&GameSourceVoiceServiceValid(old)&&old.outcome==2&&old.frontier==0x57CF0&&!GameSourceSequencePrefixValid(old),"S076");
 Check(Same(old.owner,before)&&Same(old.after,after)&&after.bank_live&&after.voice.bank_refs_after==1
  &&GameSourceBankRegistrationExact(before.registration,after.registration)&&!after.registration.word30,"S077");
 Check(after.voice.sequence_slot==-1&&after.voice.words80[0]==UINT32_MAX&&!after.voice.sequence_result_known
  &&!after.voice.callback_returned&&after.voice.initializer_store_pc==0x578D4&&Zero(after.voice.wordsA0)
  &&GameSourceSequenceExact(after.voice.sequence,Sequence{}),"S078");
 Denied(historic,after,"S079");
 Fixture issuer;const auto ibefore=issuer.bank.SourceControl();Read refused;
 const auto istatus=issuer.bank.ReadOptionsFeedback(ibefore,2,refused);const auto iafter=issuer.bank.SourceControl();
 Check(istatus.code==GameFsCode::Unavailable&&istatus.detail.find("56598")!=std::string::npos
  &&GameSourceFeedbackReachedValid(refused)&&!GameSourceVoiceFeedbackValid(refused)&&!GameSourceVoiceServiceValid(refused)
  &&!GameSourceSequencePrefixValid(refused)&&refused.outcome==0&&!refused.start_returned&&!refused.handle_stored&&!refused.feedback_returned
  &&!iafter.voice.live&&iafter.se5.handle==-1&&iafter.current_id==-1&&!iafter.registration.word30,"S080");
 Denied(issuer,iafter,"S081");
 Input input;GameSourceSoundControl legacy;Ok(legacy.Initialize17F0(),"S082");Ok(legacy.BindSe5(input.view),"S083");
 const auto lbefore=legacy.Snapshot();Read read;const auto lstatus=legacy.Feedback4AC38(lbefore,read);
 Check(lstatus.code==GameFsCode::Unavailable&&lstatus.detail.find("4AC84")!=std::string::npos&&read.attempted&&read.bank_attached
  &&!read.bank_handle_known&&!read.read_returned&&!read.getter_returned&&!read.start_returned&&!read.handle_stored&&!read.feedback_returned
  &&!GameSourceSequencePrefixValid(read)&&!GameSourceVoiceServiceValid(read)&&!GameSourceFeedbackReachedValid(read)
  &&Same(lbefore,legacy.Snapshot()),"S084");
 Read empty;Check(!legacy.ServiceSequence(lbefore,empty)&&GameSourceFeedbackExact(empty,Read{})&&Same(lbefore,legacy.Snapshot()),"S085");
 Check(GameSourceVoiceServiceValid(old)&&GameSourceFeedbackReachedValid(refused),"S086");
}
}
inline void GameOptionsSequenceRegressionChecks(){
 namespace sequence=game_options_sequence_regressions;
 sequence::ReturnedContext();sequence::CommandAndPartial();sequence::AuthorityAndLifetime();
 sequence::ValidationMutations();sequence::HistoricalDomains();
}

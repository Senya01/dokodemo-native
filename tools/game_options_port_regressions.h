#pragma once
// SOURCE ONLY: authored for CORE_RELEASE.v4 + declared v2/v3/v4 deltas; executed=0.
// Accepted parent supplied by ROOT: 1d9db11578697f90594e05d27b29d59db4fac7dc.
// Only fixture primitives adapted from sequence header lines1..66. No old family.
// Windows ownership/timing are NON-MATCHING. No backend/audio/event6 interpreter.
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace game_options_port_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Sequence=GameSourceSequenceSnapshot;
using Read=GameSourceFeedbackRead;using Track=GameSourceTrackSnapshot;
inline void Check(bool b,const char* id){if(!b)throw std::runtime_error(std::string("options-port ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-port ")+id+": "+s.detail);}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
template<class A> inline bool Zero(const A& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});}
inline bool StatusSame(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline bool Empty(const Read& r){return GameSourceFeedbackExact(r,Read{});}
inline bool Cut(const GameFsStatus& s,const char* text){return !s&&s.code==GameFsCode::Unavailable&&s.detail.find(text)!=std::string::npos;}
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& s,std::string& e)const override{
  if(!live){e="port synthetic lease retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;
  s.generation=generation;s.serial=serial;s.source_base=0x2000;s.readable=s.writable=true;return true;
 }
};
struct Input {
 static constexpr U record=0xB28,body=record+20,wave=0xB54,end=0xB6C,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 // Every fault precedes registration/digest; no live receipt or provider forged.
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
   case 5:view.WriteLE32(68,6);break;case 6:view.WriteLE32(record+4,1);break;
   case 7:view.WriteLE32(record+16,UINT32_MAX);break;case 8:lease->known[7+record+8]=0;break;}
 }
 void Guards()const{for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"P001 guards");}
 void Body()const{for(U i=0;i<23;++i)Check(view.ReadU8(body+i)==uint8_t(0x80+i),"P002 body bytes unchanged");Guards();}
};
struct Fixture {
 Input input;GameSoundBank bank;
 explicit Fixture(U fault=0):input(fault){Ok(bank.InitializeSource17F0(),"P003 initialize");Ok(bank.Register(input.view),"P004 register");}
};
inline Read Start(Fixture& f){
 const auto before=f.bank.SourceControl();Read r;Ok(f.bank.ReadOptionsVoiceFeedback(before,2,r),"P005 real voice");
 const auto after=f.bank.SourceControl();
 Check(GameSourceVoiceFeedbackValid(r)&&r.outcome==1&&r.frontier==0x4AD4C&&r.start_returned&&r.handle_stored&&r.feedback_returned,"P006 returned feedback");
 Check(Same(before,r.owner)&&Same(after,r.after)&&after.bank_live&&after.voice.returned&&after.voice.bank_refs_before==0
  &&after.voice.bank_refs_after==1&&after.voice.retain_writer==0x55A2C&&after.se5.handle==after.voice.id,"P007 voice retain/readback");
 Check(GameSourceBankRegistrationExact(before.registration,after.registration)&&after.registration.word30==0
  &&GameSourceSequenceExact(after.voice.sequence,Sequence{})&&!after.voice.services,"P008 immutable registration");
 f.input.Guards();return r;
}
inline Read PortAfterVoice(Fixture& f){
 const auto before=f.bank.SourceControl();f.bank.ObserveOptionsWorkerTick();f.bank.ObserveOptionsWorkerTick();
 Read r;const auto s=f.bank.ServiceOptionsPort(before,r);const auto after=f.bank.SourceControl();
 Check(Cut(s,"59DD4")&&s.detail.find("before event6 grammar")!=std::string::npos,"P009 actual update refusal");
 Check(GameSourcePortPrefixValid(r)&&r.outcome==4&&r.frontier==0x59DD4&&Same(r.owner,before)&&Same(r.after,after),"P010 real port receipt");
 Check(!GameSourceSequencePrefixValid(r)&&!GameSourceVoiceServiceValid(r)&&!GameSourceVoiceFeedbackValid(r)&&!GameSourceFeedbackReachedValid(r),"P011 disjoint prefix domains");
 return r;
}
inline void Returned(const Read& r){
 const auto& a=r.after;const auto& v=a.voice;const auto& q=v.sequence;const auto& p=q.port;const auto& t=q.track;
 Check(v.services==1&&v.callback_entered&&v.callback_returned&&v.callback_pc==0x578CC&&v.sequence_result_known
  &&v.sequence_slot==q.id&&v.words80[0]==U(q.id)&&v.frontier==0x59DD4,"P012 callback really returned");
 Check(q.live&&q.active&&q.flags==3&&q.allocation_returned&&q.lookup_returned&&q.clear_returned
  &&q.port_result_known&&q.handler_returned&&q.admission_returned&&q.track_source_initialized&&q.track_activated,"P013 returned context topology");
 Check(!q.sentinel_self&&!q.child_head_same&&q.sentinel_id==-1&&q.sentinel_flags==0&&q.ids_storage&&q.output_storage
  &&q.ids_storage!=q.output_storage&&q.output_unchanged&&Zero(q.output_before)&&Zero(q.output_after)&&Zero(v.wordsA0),"P014 topology and untouched output");
 Check(p.live&&p.active&&p.returned&&p.free_unlinked&&p.active_linked&&p.owner&&p.generation==q.generation&&p.allocation==1
  &&p.id>=0&&p.id<16&&q.port_id==p.id&&p.flags==1&&p.initialize_pc==0x5A490&&p.return_pc==0x5A660,"P015 actual free-head port readback");
 Check(p.sentinel_self==std::array<bool,2>{true,true}&&p.sentinel_ids==std::array<int16_t,2>{-1,-1}&&Zero(p.sentinel_flags)&&Zero(p.zero70),"P016 embedded port sentinels");
 Q mask=3;for(U i=12;i<36;++i)mask|=Q(1)<<i;
 Check(p.known_halfwords==mask,"P017 exact source known halfwords");
 for(U i=0;i<36;++i){U expected=0;for(U k=0;k<4;++k){if(i==16+6*k)expected=4096;if(i==17+6*k)expected=1000;}
  // Halfwords2..11 are native backing zero, NOT additional source knowledge.
  Check(p.parameters[i]==expected,"P018 port zero and four parameter pairs");}
 Check(t.live&&t.initialized&&t.free_unlinked&&t.child_linked&&t.lane_initialized&&t.returned&&t.allocation_index==0
  &&t.subindex==int32_t(q.subindex)&&t.bank==r.bank_handle&&t.record_offset==Input::record&&t.body_offset==Input::body
  &&t.end_offset==Input::body+23&&t.factor==1000&&t.allocated_ports==1&&t.lanes==1,"P019 actual track fields");
 Check(!t.word20&&t.wordCC==UINT32_MAX&&!t.wordE4&&!t.wordE6&&!t.byte28&&!t.byte29
  &&t.lane_controls==std::array<uint8_t,7>{127,64,0,0,0,127,64},"P020 independent track stores and lane controls");
 for(U i=0;i<16;++i)Check(t.ports[i]==(i?int16_t(-1):int16_t(p.id)),"P021 track port slots");
 Check(p.storage&&t.storage&&t.context_storage&&p.storage!=t.storage&&p.storage!=t.context_storage&&t.storage!=t.context_storage,"P022 actual storage topology");
 Check(q.refs_before==1&&q.refs_after==2&&t.retain_pc==0x55A2C&&t.refs_before==2&&t.refs_after==3&&t.return_pc==0x58290
  &&q.command_released&&q.release_pc==0x55A7C&&q.release_before==3&&q.release_after==2&&q.current_refs==2,"P023 third retain and command release");
 Check(a.bank_live&&a.registration.word30==0&&GameSourceBankRegistrationExact(r.owner.registration,a.registration),"P024 current refs accounted separately");
 Check(q.worker_tick_serial==2&&q.worker_previous_ns&&q.worker_current_ns>=q.worker_previous_ns
  &&q.native_elapsed_ns==q.worker_current_ns-q.worker_previous_ns,"P025 actual consecutive timestamp provenance");
 const float delta=float(double(q.native_elapsed_ns)/1000000000.0),scaled=delta*1000000.0f;
 Check(std::isfinite(scaled)&&scaled>=0&&double(scaled)<=double(INT32_MAX)&&q.native_delta_seconds==delta&&q.update_quantum==int32_t(scaled)
  &&q.update_entered&&q.update_pc==0x54FE8&&q.processor_attempted&&q.processor_pc==0x5A120&&q.frontier==0x59DD4,"P026 host quantum before unsupported body");
 Check(q.processor_context==t.context_storage&&q.processor_context&&q.processor_quantum==q.update_quantum,"P029 typed consumer argument readback v4");
}
inline void Positive(){
 Fixture a,b;Start(a);Start(b);const auto x=PortAfterVoice(a),y=PortAfterVoice(b);Returned(x);Returned(y);
 Check(x.after.owner!=y.after.owner&&x.after.voice.owner!=y.after.voice.owner
  &&x.after.voice.sequence.owner!=y.after.voice.sequence.owner&&x.after.voice.sequence.port.owner!=y.after.voice.sequence.port.owner
  &&x.after.registration.registry!=y.after.registration.registry,"P027 independent owners within each domain");
 Check(x.after.voice.sequence.port.storage!=y.after.voice.sequence.port.storage&&x.after.voice.sequence.track.storage!=y.after.voice.sequence.track.storage,"P028 disjoint live fixture storage");
 a.input.Body();b.input.Body();
}
template<class F> inline void Reject(const Read& good,F mutate,const char* id){
 auto bad=good;mutate(bad);Check(!GameSourceFeedbackExact(bad,good),id);Check(!GameSourcePortPrefixValid(bad),id);
}
inline void ReceiptNegatives(){
 Fixture f;Start(f);const auto r=PortAfterVoice(f);
 // Fresh copy per mutation. Opaque owners/storage are rejected at zero/alias;
 // these pure transport predicates do not establish private issuance/currentness.
#define OP_BAD(expr) Reject(r,[](Read& b){expr;},"N: " #expr)
#define OP_PORT(field,value) OP_BAD(b.after.voice.sequence.port.field=value)
 OP_PORT(live,false);OP_PORT(active,false);OP_PORT(returned,false);OP_PORT(free_unlinked,false);OP_PORT(active_linked,false);
 OP_PORT(owner,0);OP_PORT(generation,0);OP_PORT(allocation,2);OP_PORT(known_halfwords,0);OP_PORT(storage,0);OP_PORT(id,-1);
 OP_PORT(flags,3);OP_PORT(initialize_pc,0);OP_PORT(return_pc,0);
 for(U i=0;i<2;++i){Reject(r,[i](Read& b){b.after.voice.sequence.port.sentinel_ids[i]=0;},"N port sentinel ID");
  Reject(r,[i](Read& b){b.after.voice.sequence.port.sentinel_flags[i]=1;},"N port sentinel flag");
  Reject(r,[i](Read& b){b.after.voice.sequence.port.sentinel_self[i]=false;},"N port sentinel self");}
 for(U i=0;i<36;++i)Reject(r,[i](Read& b){b.after.voice.sequence.port.parameters[i]^=1;},"N port parameter element");
 for(U i=0;i<4;++i)Reject(r,[i](Read& b){b.after.voice.sequence.port.zero70[i]=1;},"N port zero70 word");
#define OP_TRACK(field,value) OP_BAD(b.after.voice.sequence.track.field=value)
 OP_TRACK(live,false);OP_TRACK(initialized,false);OP_TRACK(free_unlinked,false);OP_TRACK(child_linked,false);OP_TRACK(lane_initialized,false);OP_TRACK(returned,false);
 OP_TRACK(storage,0);OP_TRACK(context_storage,0);OP_TRACK(allocation_index,1);OP_TRACK(subindex,1);OP_TRACK(bank,-1);
 OP_TRACK(record_offset,0);OP_TRACK(body_offset,0);OP_TRACK(end_offset,0);OP_TRACK(word20,1);OP_TRACK(wordCC,0);OP_TRACK(wordE4,1);OP_TRACK(wordE6,1);
 OP_TRACK(byte28,1);OP_TRACK(byte29,1);OP_TRACK(allocated_ports,2);OP_TRACK(lanes,2);OP_TRACK(retain_pc,0);OP_TRACK(refs_before,1);OP_TRACK(refs_after,2);OP_TRACK(return_pc,0);OP_TRACK(factor,999);
 for(U i=0;i<16;++i)Reject(r,[i](Read& b){b.after.voice.sequence.track.ports[i]=-2;},"N track port element");
 for(U i=0;i<7;++i)Reject(r,[i](Read& b){b.after.voice.sequence.track.lane_controls[i]^=1;},"N lane control element");
#define OP_SEQ(field,value) OP_BAD(b.after.voice.sequence.field=value)
 OP_SEQ(admission_returned,false);OP_SEQ(command_released,false);OP_SEQ(update_entered,false);OP_SEQ(processor_attempted,false);
 OP_SEQ(current_refs,3);OP_SEQ(release_pc,0);OP_SEQ(release_before,2);OP_SEQ(release_after,3);OP_SEQ(update_pc,0);OP_SEQ(processor_pc,0);
 OP_BAD(++b.after.voice.sequence.native_elapsed_ns);OP_SEQ(worker_tick_serial,1);OP_SEQ(worker_previous_ns,0);
 OP_BAD(b.after.voice.sequence.worker_current_ns=b.after.voice.sequence.worker_previous_ns-1);
 OP_BAD(b.after.voice.sequence.native_delta_seconds+=1);OP_BAD(b.after.voice.sequence.update_quantum^=1);
 OP_SEQ(processor_context,0);OP_BAD(b.after.voice.sequence.processor_quantum^=1);
 OP_BAD(b.after.voice.sequence.processor_context=b.after.voice.sequence.port.storage);
 OP_BAD(b.after.voice.sequence.processor_context=b.after.voice.sequence.track.storage);
 OP_SEQ(track_source_initialized,false);OP_SEQ(track_activated,false);OP_SEQ(port_result_known,false);OP_SEQ(handler_returned,false);
 OP_SEQ(port_id,-1);OP_SEQ(flags,1);OP_SEQ(sentinel_self,true);OP_SEQ(child_head_same,true);OP_SEQ(frontier,0);
 OP_SEQ(ids_storage,0);OP_SEQ(output_storage,0);
 OP_BAD(b.after.voice.sequence.ids_storage=b.after.voice.sequence.output_storage);
 OP_BAD(b.after.voice.sequence.port.storage=b.after.voice.sequence.track.storage);
 OP_BAD(b.after.voice.sequence.port.storage=b.after.voice.sequence.track.context_storage);
 OP_BAD(b.after.voice.sequence.track.storage=b.after.voice.sequence.track.context_storage);
 OP_SEQ(native_delta_seconds,std::numeric_limits<float>::infinity());OP_SEQ(native_delta_seconds,std::numeric_limits<float>::quiet_NaN());
 OP_BAD(b.after.voice.sequence.worker_current_ns=b.after.voice.sequence.worker_previous_ns+Q(INT32_MAX)*1000+1000000000);
 // All26 scalar envelope members, plus both nested envelope snapshots.
 OP_BAD(b.context_index=4);OP_BAD(b.read_pc=0);OP_BAD(b.attempted=false);OP_BAD(b.bank_attached=false);OP_BAD(b.bank_handle_known=false);OP_BAD(b.read_returned=false);
 OP_BAD(b.frontier=0);OP_BAD(b.selector=1);OP_BAD(b.params_written=5);OP_BAD(b.virtual_target=0);OP_BAD(b.wrapper=0);OP_BAD(b.issuer=0);
 OP_BAD(b.bank_handle=-1);OP_BAD(b.gain=46);OP_BAD(b.group=1);OP_BAD(b.event=5);OP_BAD(b.extra=1);OP_BAD(b.mode=1);OP_BAD(b.rate=2);
 OP_BAD(b.context_owner=0);OP_BAD(b.context_generation=0);OP_BAD(b.outcome=3);OP_BAD(b.getter_returned=false);OP_BAD(b.start_returned=false);OP_BAD(b.handle_stored=false);OP_BAD(b.feedback_returned=false);
 OP_BAD(b.owner=Sound{});OP_BAD(b.after=Sound{});OP_BAD(b.after.voice.callback_returned=false);OP_BAD(b.after.voice.frontier=0);
 OP_BAD(b.after.bank_live=false);OP_BAD(b.after.registration.word30=2);
 OP_BAD(b.frontier=0x59E00;b.after.voice.frontier=0x59E00;b.after.voice.sequence.frontier=0x59E00);
 OP_BAD(b.outcome=5); //fabricated later continuation is not a port prefix
#undef OP_SEQ
#undef OP_TRACK
#undef OP_PORT
#undef OP_BAD
 f.input.Body();
}
inline void NoReissue(GameSoundBank& bank,const Sound& expected,const char* id){
 const auto before=bank.SourceControl();Read out;const auto s=bank.ServiceOptionsPort(expected,out);
 Check(Cut(s,"current service owner unavailable")&&Empty(out)&&Same(before,bank.SourceControl()),id);
}
inline void RefusalsAndDomains(){
 {Fixture a,b;Start(a);Start(b);const auto live=a.bank.SourceControl();
  NoReissue(a.bank,b.bank.SourceControl(),"R001 wrong owner");
  Read alias;alias.owner=live;const auto s=a.bank.ServiceOptionsPort(alias.owner,alias);
  Check(!s&&Empty(alias)&&Same(live,a.bank.SourceControl()),"R002 expected/output alias refused before service");
  GameSourceSoundGainReceipt gain;Ok(a.bank.QuerySourceGain(live,37,gain),"R003 real query makes snapshot stale");
  Check(GameSourceSoundGainValid(gain)&&gain.after.queries==live.queries+1,"R004 real query receipt");
  NoReissue(a.bank,live,"R005 stale query snapshot");const auto r=PortAfterVoice(a);Returned(r);
  NoReissue(a.bank,r.owner,"R006 replay original owner");NoReissue(a.bank,r.after,"R007 repeated current service");}
 // Missing ticks are AFTER real callback/track return, BEFORE quantum entry.
 for(U ticks=0;ticks<2;++ticks){Fixture f;Start(f);const auto before=f.bank.SourceControl();
  if(ticks)f.bank.ObserveOptionsWorkerTick();Read r;const auto s=f.bank.ServiceOptionsPort(before,r);
  const auto& q=r.after.voice.sequence;
  Check(Cut(s,"5A06C missing actual consecutive worker timestamps")&&r.outcome==4&&r.frontier==0x5A06C
   &&r.after.voice.callback_returned&&r.after.voice.services==1&&q.handler_returned&&q.admission_returned,"R008 precise missing tick boundary");
  Check(q.port.returned&&q.track.returned&&q.track.refs_before==2&&q.track.refs_after==3&&q.command_released&&q.current_refs==2
   &&q.worker_tick_serial==ticks&&!q.worker_previous_ns&&(ticks?bool(q.worker_current_ns):!q.worker_current_ns),"R009 missing tick still accounts real retains");
  Check(!q.update_entered&&!q.update_pc&&!q.processor_attempted&&!q.processor_pc&&!q.processor_context&&!q.processor_quantum&&!q.native_elapsed_ns&&!q.native_delta_seconds&&!q.update_quantum
   &&q.frontier==0x5A06C&&r.after.bank_live&&r.after.registration.word30==0&&!GameSourcePortPrefixValid(r),"R010 no fabricated quantum");
  NoReissue(f.bank,r.after,"R011 no replay after missing tick");f.input.Body();}
 {Input input;GameSourceSoundControl control;Ok(control.Initialize17F0(),"R012 cold control");Ok(control.BindSe5(input.view),"R013 actual borrowed bank");
  const auto before=control.Snapshot();Read r;const auto s=control.Feedback4AC38(before,r);
  Check(Cut(s,"read4AC84")&&r.attempted&&r.bank_attached&&!r.bank_handle_known&&!r.read_returned&&!before.registration.live
   &&Same(before,control.Snapshot())&&!GameSourcePortPrefixValid(r),"R014 actual unregistered 4AC84 domain");
  Read port;Check(!control.ServicePort(before,port)&&Empty(port)&&Same(before,control.Snapshot()),"R015 no voice after 4AC84");}
 {Fixture f;const auto before=f.bank.SourceControl();Read r;const auto s=f.bank.ReadOptionsFeedback(before,2,r);
  Check(Cut(s,"issuer56598")&&GameSourceFeedbackReachedValid(r)&&r.outcome==0&&r.frontier==0x56598&&!r.start_returned
   &&!r.after.voice.live&&!GameSourcePortPrefixValid(r),"R016 actual historic 56598 domain");
  Read replay;const auto again=f.bank.ReadOptionsFeedback(r.after,2,replay);
  Check(StatusSame(s,again)&&GameSourceFeedbackExact(r,replay),"R017 cached refusal no reissue");NoReissue(f.bank,r.after,"R018 no port after56598");}
 {Fixture f;Start(f);Read r;const auto s=f.bank.ServiceOptionsVoice(f.bank.SourceControl(),r);
  Check(Cut(s,"57CF0")&&GameSourceVoiceServiceValid(r)&&r.outcome==2&&r.frontier==0x57CF0&&!r.after.voice.callback_returned
   &&GameSourceSequenceExact(r.after.voice.sequence,Sequence{})&&!GameSourcePortPrefixValid(r),"R019 actual historic57CF0 domain");
  NoReissue(f.bank,r.after,"R020 no new port from retired voice service");}
 {Fixture f;Start(f);Read r;const auto s=f.bank.ServiceOptionsSequence(f.bank.SourceControl(),r);const auto& q=r.after.voice.sequence;
  Check(Cut(s,"5A5AC")&&GameSourceSequencePrefixValid(r)&&r.outcome==3&&r.frontier==0x5A5AC&&q.track_reserved&&!q.port_result_known
   &&!q.track_source_initialized&&!q.track_activated&&!q.handler_returned&&!r.after.voice.callback_returned&&!GameSourcePortPrefixValid(r),"R021 actual historic5A5AC domain");
  Check(q.refs_after==2&&r.after.bank_live&&r.after.registration.word30==0&&!q.command_released,"R022 reserved track retains accounted");
  NoReissue(f.bank,r.after,"R023 no implicit upgrade of old service domain");}
}
inline void Lifecycle(){
 Fixture f;const auto old_start=Start(f);const auto old=PortAfterVoice(f);const auto frozen=old;const auto retired=old.after;
 f.bank.Close();const auto closed=f.bank.SourceControl();
 Check(!closed.live&&!closed.bank_live&&!closed.voice.live&&!closed.voice.active&&!closed.voice.sequence.live&&!closed.voice.sequence.active
  &&!closed.voice.sequence.port.live&&!closed.voice.sequence.port.active&&!closed.voice.sequence.track.live,"L001 nested owners retired v2");
 Check(closed.voice.sequence.generation==retired.voice.sequence.generation&&closed.voice.sequence.port.owner==retired.voice.sequence.port.owner
  &&closed.voice.sequence.port.generation==retired.voice.sequence.port.generation,"L002 retained observation is not live authority");
 NoReissue(f.bank,retired,"L003 closed old snapshot");NoReissue(f.bank,closed,"L004 closed current snapshot");
 Check(GameSourcePortPrefixValid(old)&&GameSourceFeedbackExact(old,frozen)&&GameSourceVoiceFeedbackValid(old_start),"L005 frozen receipts remain pure transport");
 Ok(f.bank.InitializeSource17F0(),"L006 real reinitialize");Ok(f.bank.Register(f.input.view),"L007 real reregister");
 const auto fresh_start=Start(f);const auto fresh=PortAfterVoice(f);Returned(fresh);const auto& a=fresh.after;
 Check(a.generation==2&&a.voice.generation==2&&a.voice.sequence.generation==2&&a.voice.sequence.port.generation==2
  &&a.owner!=retired.owner&&a.voice.owner!=retired.voice.owner&&a.voice.sequence.owner!=retired.voice.sequence.owner
  &&a.voice.sequence.port.owner!=retired.voice.sequence.port.owner,"L008 generation2 actual new owners");
 Check(a.registration.generation>retired.registration.generation&&a.registration.word30==0&&a.bank_live
  &&fresh_start.after.voice.bank_refs_before==0&&fresh_start.after.voice.bank_refs_after==1,"L009 new registration begins without old retains");
 // Backing addresses/IDs may be reused by this same native pool. Never dereference
 // a snapshot pointer, require new identity/generation and actual fresh readback.
 NoReissue(f.bank,old.owner,"L010 old callback owner not live");NoReissue(f.bank,retired,"L011 old returned snapshot not live");
 Check(GameSourcePortPrefixValid(old)&&GameSourceFeedbackExact(old,frozen)&&GameSourcePortPrefixValid(fresh),"L012 transport does not revalidate liveness");
 f.input.Body();
}
inline void FaultPartials(){
 for(U fault=1;fault<=7;++fault){Fixture f(fault);const auto registered=f.bank.SourceControl();Read r;
  if(fault==1||fault==2||fault==7){Start(f);const auto voice=f.bank.SourceControl();
   f.bank.ObserveOptionsWorkerTick();f.bank.ObserveOptionsWorkerTick();const auto s=f.bank.ServiceOptionsPort(voice,r);const auto& q=r.after.voice.sequence;
   Check(!s&&s.code==GameFsCode::Unavailable&&q.allocation_returned&&q.lookup_returned&&q.command_entered&&q.bank_retained
    &&q.refs_before==1&&q.refs_after==2&&q.record_lookup_returned&&!q.handler_returned&&!q.admission_returned&&!q.command_released
    &&!r.after.voice.callback_returned&&!q.update_entered&&!q.processor_attempted&&!q.current_refs,"F001 command retain preserved without source return");
   if(fault==7){Check(Cut(s,"580D4 actual port/record domain unavailable")&&q.track_reserved&&q.port_attempted&&q.port_result_known
     &&q.port.returned&&q.port.live&&!q.track_source_initialized&&!q.track_activated&&GameSourceTrackExact(q.track,Track{}),"F002 span overflow after actual port return");}
   else Check(Cut(s,"580D4 selected live record unavailable")&&!q.track_reserved&&!q.port_attempted&&!q.port_result_known
     &&!q.track_source_initialized&&!q.track_activated&&!q.port.live,"F003 invalid kind/divisor before reservation");
  }else{const auto s=f.bank.ReadOptionsVoiceFeedback(registered,2,r);const auto& v=r.after.voice;
   Check(!s&&s.code==GameFsCode::Unavailable&&v.live&&!v.returned&&!v.active&&v.bank_refs_before==0&&v.bank_refs_after==1
    &&v.retain_writer==0x55A2C&&!v.services&&!v.callback_entered&&GameSourceSequenceExact(v.sequence,Sequence{})
    &&r.frontier==0x56598&&!r.start_returned&&!r.handle_stored&&!r.feedback_returned&&r.after.se5.handle==-1,"F004 auxiliary bounds partial retains real voice");
   Read replay;const auto again=f.bank.ReadOptionsVoiceFeedback(r.after,2,replay);
   Check(StatusSame(s,again)&&GameSourceFeedbackExact(r,replay),"F005 cached partial failure");
  }
  Check(r.after.bank_live&&r.after.registration.word30==0&&GameSourceBankRegistrationExact(registered.registration,r.after.registration)
   &&!GameSourcePortPrefixValid(r)&&!GameSourceVoiceFeedbackValid(r),"F006 partial retains accounted by current bank readback");
  NoReissue(f.bank,r.after,"F007 no service replay of partial");f.input.Body();
 }
 {Input input(8);GameSoundBank bank;Ok(bank.InitializeSource17F0(),"F008 initialize unknown byte fixture");const auto s=bank.Register(input.view);
  const auto before=bank.SourceControl();Check(!s&&!before.registration.live&&!before.voice.live,"F009 unknown payload cannot issue registration");
  Read r;Check(!bank.ReadOptionsVoiceFeedback(before,2,r)&&Empty(r)&&Same(before,bank.SourceControl()),"F010 no fabricated return from unregistered unknown byte");input.Guards();}
}
} //namespace game_options_port_regressions

inline void GameOptionsPortRegressionChecks(){
 using namespace game_options_port_regressions;
 Positive();ReceiptNegatives();RefusalsAndDomains();Lifecycle();FaultPartials();
}

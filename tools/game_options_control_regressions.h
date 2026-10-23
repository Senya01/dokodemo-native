#pragma once
// SOURCE ONLY: author executions/builds/runtime=0. ROOT owns integration.
// Supplied parent 4f66bf061fafc7276c8519c89d9dfb5bd2185f7e; no Git query.
// Frozen CORE_RELEASE.v1 + named v2/v3/v4 overlays; no private source files.
// Synthetic public receipts test transport, never Actor/Content issuance.
// Fixture primitives adapted only from processor header lines 1..86.
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace game_options_control_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Read=GameSourceFeedbackRead;
using Control=GameSourceControlSnapshot;
inline void Check(bool yes,const char* id){if(!yes)throw std::runtime_error(std::string("options-control ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-control ")+id+": "+s.detail);}
inline bool Cut(const GameFsStatus& s,const char* text){return !s&&s.code==GameFsCode::Unavailable&&s.detail.find(text)!=std::string::npos;}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
inline bool Empty(const Read& r){return GameSourceFeedbackExact(r,Read{});}
template<class A> inline bool Zero(const A& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});}

struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& s,std::string& error)const override{
  if(!live){error="control synthetic lease retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;
  s.generation=generation;s.serial=serial;s.source_base=0x2000;s.readable=s.writable=true;return true;
 }
};
enum class Fault:U {None,NoGroups,NoRegions,NegativeCount,NoFirst,BelowFirst,OutsideFirst,
 ShortGroup,ShortDirectory,ZeroStride,OverflowStride,OutsideStride,ShortHeader,NoMatch,
 FiniteWalk,Packed,Override,Wave31,ByteCNegative,ByteDNegative,Flags8,EarlyKey,
 Key63,Note32,ProgramBounds,AlternateProgram};
struct Input {
 static constexpr U seq=0x920,record=0xB28,body=0xB3C,wave=0xB54,end=0xB6C,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 std::vector<uint8_t> frozen_bytes,frozen_known;
 explicit Input(Fault f=Fault::None){
  lease->bytes.assign(size+14,0xA5);lease->known.assign(size+14,1);
  view=PackageStorage::Attach(lease,size+14).Subview(7,size);
  for(U i=0;i<size;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,end);view.WriteLE32(12,0x80000001);
  view.WriteLE32(16,0x444E4752);view.WriteLE32(20,seq-24);
  view.WriteLE32(28,1);view.WriteLE32(32,41);view.WriteLE32(36,40);
  for(U i=0;i<41;++i){const auto at=40+56*i;
   view.WriteLE32(at,9);view.WriteLE32(at+8,56);view.WriteU8(at+12,10);
   view.WriteU8(at+24,uint8_t(32+i));view.WriteU8(at+25,uint8_t(32+i));view.WriteLE32(at+52,30);
  }
  view.WriteLE32(seq,0x44514553);view.WriteLE32(seq+4,wave-seq-8);
  view.WriteLE32(seq+12,1);view.WriteLE32(seq+16,0x960);
  view.WriteLE32(0x964,7);view.WriteLE32(0x980,record);
  view.WriteLE32(record+4,0);view.WriteLE16(record+8,0);view.WriteLE16(record+10,1000);view.WriteLE32(record+16,23);
  for(U i=0;i<23;++i)view.WriteU8(body+i,uint8_t(0x80+i));
  const std::array<uint8_t,9> d0{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(U i=0;i<d0.size();++i)view.WriteU8(body+i,d0[i]);
  view.WriteLE32(wave,0x45564157);view.WriteLE32(wave+4,4);
  view.WriteLE32(wave+12,0x454D414E);view.WriteLE32(wave+16,4);
  // Every fault is installed BEFORE registration/digest issuance.
  switch(f){
   case Fault::NoGroups:view.WriteLE32(28,0);break;
   case Fault::NoRegions:view.WriteLE32(32,0);break;
   case Fault::NegativeCount:view.WriteLE32(32,0x80000000);break;
   case Fault::NoFirst:view.WriteLE32(36,0);break;
   case Fault::BelowFirst:view.WriteLE32(36,16);break;
   case Fault::OutsideFirst:view.WriteLE32(36,seq);break;
   case Fault::ShortGroup:case Fault::ShortDirectory:{
    const U n=f==Fault::ShortGroup?4:8,next=24+n;
    view.WriteLE32(20,n);view.WriteLE32(next,0x4B4E554A);view.WriteLE32(next+4,seq-next-8);break;
   }
   case Fault::ZeroStride:view.WriteLE32(48,0);break;
   case Fault::OverflowStride:view.WriteLE32(48,UINT32_MAX);break;
   case Fault::OutsideStride:view.WriteLE32(48,seq-40);break;
   case Fault::ShortHeader:view.WriteLE32(36,seq-16);break;
   case Fault::NoMatch:view.WriteLE32(32,1);break;
   case Fault::FiniteWalk:for(U i=0;i<41;++i){view.WriteU8(40+56*i+24,32);view.WriteU8(40+56*i+25,32);}break;
   case Fault::Packed:view.WriteU8(0x728+13,1);break;
   case Fault::Override:view.WriteLE32(0x728,13);break;
   case Fault::Wave31:view.WriteLE32(0x728+52,31);break;
   case Fault::ByteCNegative:view.WriteU8(0x728+12,0xFF);break;
   case Fault::ByteDNegative:view.WriteU8(0x728+13,0xFF);break;
   case Fault::Flags8:view.WriteLE32(0x728,8);break;
   case Fault::EarlyKey:view.WriteU8(64,64);view.WriteU8(65,64);break;
   case Fault::Key63:view.WriteU8(body+5,0x63);break;
   case Fault::Note32:view.WriteU8(body+2,0x32);break;
   case Fault::ProgramBounds:case Fault::AlternateProgram:{
    // Keep nine bytes: program=1, range-key=0, note=64, final operand4096.
    view.WriteU8(body+3,1);view.WriteU8(body+4,1);view.WriteU8(body+5,0);
    if(f==Fault::AlternateProgram){view.WriteLE32(28,2);view.WriteLE32(40,1);view.WriteLE32(44,0x8E8);
     view.WriteU8(0x8E8+24,0);view.WriteU8(0x8E8+25,0);view.WriteLE32(0x8E8+52,11);}
    break;
   }
   default:break;
  }
  frozen_bytes=lease->bytes;frozen_known=lease->known;
 }
 void Unchanged()const{
  Check(lease->bytes==frozen_bytes&&lease->known==frozen_known,"I01 whole synthetic allocation unchanged");
  for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"I02 guards unchanged");
 }
};
struct Fixture {
 Input input;GameSoundBank bank;
 explicit Fixture(Fault f=Fault::None):input(f){Ok(bank.InitializeSource17F0(),"I03 initialize");Ok(bank.Register(input.view),"I04 actual registration");}
};
inline Read Start(Fixture& f){
 const auto before=f.bank.SourceControl();Read r;
 Ok(f.bank.ReadOptionsVoiceFeedback(before,2,r),"I05 real voice feedback");
 Check(GameSourceVoiceFeedbackValid(r)&&r.outcome==1&&Same(r.after,f.bank.SourceControl()),"I06 current voice receipt");return r;
}
inline void Ticks(Fixture& f){f.bank.ObserveOptionsWorkerTick();f.bank.ObserveOptionsWorkerTick();}
inline Read Service(Fixture& f,const char* detail="557B8"){
 const auto expected=f.bank.SourceControl();Read r;
 Check(Cut(f.bank.ServiceOptionsControl(expected,r),detail),"I07 typed unavailable frontier");
 Check(r.outcome==6&&Same(r.owner,expected)&&Same(r.after,f.bank.SourceControl()),"I08 actual owner/after readbacks");return r;
}
inline void Pending(const Read& r){
 const auto& q=r.after.voice.sequence;const auto& c=q.control;
 Check(r.after.bank_live&&r.after.registration.word30==0&&q.current_refs==3,"I09 current3/immutable registrar0");
 Check(q.refs_before==1&&q.refs_after==2&&q.track.refs_before==2&&q.track.refs_after==3,"I10 historical retains");
 Check(q.command_released&&q.release_before==3&&q.release_after==2&&q.release_pc==0x55A7C,"I11 prior command release retained");
 Check(c.prepare_entered&&c.prepare_returned&&c.prepare_pc==0x5B33C&&c.prepare_return_pc==0x5B09C,"I12 actual prepare return");
 Check(c.bank_retained&&c.retain_pc==0x5B344&&c.retain_writer==0x55A2C&&c.refs_before==2&&c.refs_after==3,"I13 D0 pending retain");
 Check(!c.wave_output_known&&!c.wave_result_known&&!c.wave_returned&&!c.child_returned&&!c.handler_returned&&!c.control_returned,"I14 no result or return invented");
 Check(!q.processor.control_returned&&!q.processor.kind_returned&&!q.processor.context_returned,"I15 no post-D0/context completion");
}

// FAMILY 1: one genuine public owner/control-current-read scenario.
inline void GenuineOwnerControlReadFacts(){
 Fixture f;const auto cold=f.bank.SourceControl();
 Check(cold.registration.id==63&&cold.registration.chunks[0]==24&&cold.registration.sizes[0]==Input::seq-24,"G01 real RGND registration");
 Check(cold.registration.word30==0&&cold.se5.bank_handle==63,"G02 registrar/typed bank");
 const auto voice=Start(f);Ticks(f);const auto r=Service(f);const auto& q=r.after.voice.sequence;const auto& c=q.control;
 Check(GameSourceControlPrefixValid(r),"G03 genuine new transport prefix");Pending(r);
 Check(r.after.voice.id==0&&q.id==0&&q.port.id==0&&q.bank==63&&c.bank==63,"G04 distinct bank/voice/context/port domains");
 Check(q.processor.command==std::array<U,6>{{0,64,63,0,64,4096}}&&q.track.cursor_offset==0xB45,"G05 owning command/cursor");
 Check(c.entered&&c.lookup_returned&&c.command_dispatched&&c.lookup_pc==0x5A1B4&&c.source_stride==0x80&&c.pool_count==16&&c.lookup_flags==1,"G06 actual lookup/dispatch");
 Check(c.dispatch_pc==0x5BA44&&c.handler_target==0x5B2F4&&c.port_storage==q.port.storage&&c.command_storage==q.processor.command_storage,"G07 owned identities");
 Check(c.port_owner==q.port.owner&&c.port_generation==q.port.generation&&q.port.sentinel_ids[0]==-1&&q.port.sentinel_self[0],"G08 fresh actual sentinel survives prepare");
 Check(c.rgnd_base==24&&c.group_count==1&&c.region_count==41&&c.region_first==40&&c.region_returned,"G09 live group0 directory");
 Check(c.walk_count==33&&c.stride_reads==32&&c.region_selected&&c.region_offset==0x728,"G10 actual 33-header walk");
 for(U i=0;i<33;++i){Check(c.offsets[i]==40+56*i&&c.lows[i]==32+int32_t(i)&&c.highs[i]==32+int32_t(i),"G11 each offset/key");Check(c.strides[i]==(i<32?56u:0u),"G12 only prior strides read");}
 Check(c.selected_flags==9&&c.selected_byteC==10&&!c.selected_byteD&&c.selected_wave==30,"G13 selected fields");
 Check(c.child_entered&&c.child_call_pc==0x5B494&&c.child_target==0x5B130&&c.child_args==std::array<int32_t,7>{{0,64,63,30,10,0,-1}},"G14 typed child ABI");
 Check(c.wave_attempted&&c.wave_call_pc==0x5B178&&c.frontier==0x557B8&&r.frontier==0x557B8&&c.wave_output_storage,"G15 opaque owned output storage");
 const uintptr_t ids[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage};
 for(auto p:ids)Check(c.wave_output_storage!=p,"G16 separate output identity, no dereference");
 const auto elapsed=q.worker_current_ns-q.worker_previous_ns;const float delta=float(double(elapsed)/1000000000.0);
 Check(q.worker_tick_serial==2&&q.worker_previous_ns&&q.native_elapsed_ns==elapsed&&q.native_delta_seconds==delta&&q.update_quantum==int32_t(delta*1000000.0f),"G17 actual consecutive worker samples/float32 trunc");
 Q known=3;for(U i=12;i<36;++i)known|=Q(1)<<i;
 Check(q.port.known_halfwords==known,"G18 source-known26/36 halfwords, parameters2..11 UNKNOWN");
 // Do not inspect unknown parameters2..11, node gap24..27, output pointee,
 // unselected region payloads or the 14 bytes after the selected D0 prefix.
 Check(Zero(q.output_after)&&q.output_unchanged&&q.track.word20==0,"G19 no sequence output/countdown completion");
 Check(!GameSourceProcessorPrefixValid(r)&&!GameSourcePortPrefixValid(r)&&!GameSourceSequencePrefixValid(r)&&!GameSourceVoiceServiceValid(r)&&!GameSourceFeedbackReachedValid(r),"G20 strict old outcome domains");
 Check(GameSourceVoiceFeedbackValid(voice)&&!GameSourceControlPrefixValid(voice),"G21 returned voice boundary disjoint");f.input.Unchanged();
}

// FAMILY 2: 25 pre-registration semantic/bounds/key/program variants.
inline void PreRegistrationRgndVariants(){
 const Fault faults[]={Fault::NoGroups,Fault::NoRegions,Fault::NegativeCount,Fault::NoFirst,Fault::BelowFirst,Fault::OutsideFirst,Fault::ShortGroup,Fault::ShortDirectory,
  Fault::ZeroStride,Fault::OverflowStride,Fault::OutsideStride,Fault::ShortHeader,Fault::NoMatch,Fault::FiniteWalk,Fault::Packed,Fault::Override,Fault::Wave31,
  Fault::ByteCNegative,Fault::ByteDNegative,Fault::Flags8,Fault::EarlyKey,Fault::Key63,Fault::Note32,Fault::ProgramBounds,Fault::AlternateProgram};
 for(auto fault:faults){Fixture f(fault);Start(f);Ticks(f);const auto r=Service(f,"");const auto& c=r.after.voice.sequence.control;Pending(r);
  Check(!GameSourceControlPrefixValid(r),"R01 variants cannot impersonate frozen selected receipt");
  if(fault==Fault::Wave31||fault==Fault::ByteCNegative||fault==Fault::ByteDNegative||fault==Fault::Flags8||fault==Fault::EarlyKey||fault==Fault::Key63||fault==Fault::Note32||fault==Fault::AlternateProgram){
   Check(c.region_returned&&c.region_selected&&c.child_entered&&c.wave_attempted&&c.frontier==0x557B8,"R02 real alternate selection reaches opaque entry");
   Check(c.child_args[1]==c.note&&c.child_args[2]==63&&c.child_args[3]==int32_t(c.selected_wave),"R03 ABI follows actual selected fields");
   if(fault==Fault::Wave31)Check(c.selected_wave==31&&c.child_args[3]==31,"R04 actual changed wave");
   if(fault==Fault::ByteCNegative)Check(c.selected_byteC==255&&c.child_args[4]==-1,"R05 signed byteC");
   if(fault==Fault::ByteDNegative)Check(c.selected_byteD==255&&c.child_args[5]==0,"R06 negative byteD skips positive packed path");
   if(fault==Fault::Flags8)Check(c.selected_flags==8&&c.child_args[6]==-1,"R07 actual nonoverride flags");
   if(fault==Fault::EarlyKey)Check(c.region_offset==40&&c.walk_count==1&&!c.stride_reads,"R08 first matching key stops walk");
   if(fault==Fault::Key63)Check(c.note==64&&c.region_offset==40+56*31&&c.walk_count==32&&c.child_args[1]==64,"R09 range key63 independent of note64");
   if(fault==Fault::Note32)Check(c.note==32&&c.region_offset==0x728&&c.walk_count==33&&c.child_args[1]==32,"R10 range key64 independent of note32");
   if(fault==Fault::AlternateProgram)Check(c.program==1&&c.group_count==2&&c.region_first==0x8E8&&c.walk_count==1&&c.selected_wave==11,"R11 actual alternate program directory");
  }else{
   Check(!c.wave_attempted&&!c.wave_output_storage&&!c.child_entered,"R12 partial refusal before child, no fabricated result/release");
   if(fault==Fault::NoGroups||fault==Fault::ProgramBounds)Check(!c.region_returned&&!c.walk_count,"R13 actual program bounds");
   if(fault==Fault::NoRegions||fault==Fault::NegativeCount||fault==Fault::NoFirst||fault==Fault::BelowFirst||fault==Fault::OutsideFirst||fault==Fault::ShortGroup||fault==Fault::ShortDirectory)Check(!c.region_returned,"R14 directory/first-pointer refusal");
   if(fault==Fault::ZeroStride||fault==Fault::OverflowStride||fault==Fault::OutsideStride)Check(c.walk_count==1&&c.stride_reads==1,"R15 failing stride captured once");
   if(fault==Fault::ShortHeader)Check(c.region_returned&&!c.walk_count,"R16 short header without fabricated key");
   if(fault==Fault::NoMatch)Check(c.walk_count==1&&!c.region_selected,"R17 exhausted unmatched one-region walk");
   if(fault==Fault::FiniteWalk)Check(c.walk_count==33&&c.stride_reads==33&&!c.region_selected,"R18 finite captured walk guard");
   if(fault==Fault::Packed||fault==Fault::Override)Check(c.region_selected&&c.region_offset==0x728,"R19 selected unsupported packed/override branch");
  }f.input.Unchanged();
 }
}

inline void NoAttempt(Fixture& f,const Sound& expected){
 const auto before=f.bank.SourceControl();Read r;r.outcome=99;r.attempted=true;
 const auto status=f.bank.ServiceOptionsControl(expected,r);
 Check(!status&&status.code==GameFsCode::Unavailable&&Empty(r)&&Same(before,f.bank.SourceControl()),"N01 reject/clear output/no owner mutation");
}
// FAMILY 3: 27 gates (cold + 12 expected mutations + stale + alias +
// 6 live lease invalidations + 3 replay/closed + 2 missing ticks + empty).
inline void CurrentOwnerNoAttemptGates(){
 {Fixture f;NoAttempt(f,f.bank.SourceControl());f.input.Unchanged();}
 {Fixture f;const auto stale=f.bank.SourceControl();Start(f);Ticks(f);const auto current=f.bank.SourceControl();
#define CONTROL_BAD_OWNER(field) {auto bad=current;bad.field^=1;NoAttempt(f,bad);}
  CONTROL_BAD_OWNER(owner) CONTROL_BAD_OWNER(generation) CONTROL_BAD_OWNER(queries)
  CONTROL_BAD_OWNER(bank.owner) CONTROL_BAD_OWNER(bank.generation) CONTROL_BAD_OWNER(bank.serial)
  CONTROL_BAD_OWNER(registration.registry) CONTROL_BAD_OWNER(registration.word30)
  CONTROL_BAD_OWNER(registration.digest[0]) CONTROL_BAD_OWNER(voice.owner)
  CONTROL_BAD_OWNER(voice.returned) CONTROL_BAD_OWNER(se5.bank_handle_known)
#undef CONTROL_BAD_OWNER
  NoAttempt(f,stale);NoAttempt(f,Sound{});f.input.Unchanged();
 }
 {Fixture a,b;Start(a);Start(b);Ticks(a);NoAttempt(a,b.bank.SourceControl());a.input.Unchanged();b.input.Unchanged();}
 for(U which=0;which<6;++which){Fixture f;Start(f);Ticks(f);const auto old=f.bank.SourceControl();
  switch(which){case 0:++f.input.lease->generation;break;case 1:++f.input.lease->serial;break;
   case 2:f.input.lease->live=false;break;case 3:f.input.lease->bytes[7+0x728]^=1;break;
   case 4:f.input.lease->known[7+0x728]=0;break;case 5:f.input.lease->bytes[7+Input::body+9]^=1;break;}
  // Deliberately invalid lease/whole-bank digest: no rebase or re-registration.
  NoAttempt(f,old);Check(!f.bank.SourceControl().bank_live,"N02 live lease/digest unavailable");
 }
 {Fixture f;Start(f);Ticks(f);const auto r=Service(f);NoAttempt(f,r.owner);NoAttempt(f,r.after);f.bank.Close();NoAttempt(f,r.after);f.input.Unchanged();}
 for(U ticks=0;ticks<2;++ticks){Fixture f;Start(f);if(ticks)f.bank.ObserveOptionsWorkerTick();const auto r=Service(f,"5A06C");const auto& q=r.after.voice.sequence;
  Check(!q.control.entered&&!q.control.bank_retained&&q.current_refs==2&&!q.processor.entered&&!GameSourceControlPrefixValid(r),"N03 missing ticks stop before processor/control");f.input.Unchanged();}
}

inline void RejectMutation(const Read& original,const Read& changed){
 Check(!GameSourceControlExact(original.after.voice.sequence.control,changed.after.voice.sequence.control),"M01 every control member participates in exact comparison");
 Check(!GameSourceSequenceExact(original.after.voice.sequence,changed.after.voice.sequence)&&!GameSourceVoiceExact(original.after.voice,changed.after.voice)
  &&!Same(original.after,changed.after)&&!GameSourceFeedbackExact(original,changed),"M02 exact comparison propagates through public owner/receipt");
 Check(!GameSourceControlPrefixValid(changed),"M03 selected transport mutation rejected");
}
// FAMILY 4: all 51 scalar fields (wave identity has its own tests), all139
// array elements, 7 identity refusals, 8 opaque identity acceptances and
// 20 envelope/history mutations. Every acceptance is transport-only.
inline void ExactTransportMutations(){
 Fixture f;Start(f);Ticks(f);const auto original=Service(f);Check(GameSourceControlPrefixValid(original),"M04 genuine mutation baseline");
#define CONTROL_MUTATE(field) {auto m=original;m.after.voice.sequence.control.field^=1;RejectMutation(original,m);}
 CONTROL_MUTATE(entered) CONTROL_MUTATE(lookup_returned) CONTROL_MUTATE(command_dispatched)
 CONTROL_MUTATE(prepare_entered) CONTROL_MUTATE(prepare_returned) CONTROL_MUTATE(bank_retained)
 CONTROL_MUTATE(region_returned) CONTROL_MUTATE(region_selected) CONTROL_MUTATE(child_entered) CONTROL_MUTATE(wave_attempted)
 CONTROL_MUTATE(wave_output_known) CONTROL_MUTATE(wave_result_known) CONTROL_MUTATE(wave_returned)
 CONTROL_MUTATE(child_returned) CONTROL_MUTATE(handler_returned) CONTROL_MUTATE(control_returned)
 CONTROL_MUTATE(port_storage) CONTROL_MUTATE(command_storage) CONTROL_MUTATE(port_owner) CONTROL_MUTATE(port_generation)
 CONTROL_MUTATE(port_id) CONTROL_MUTATE(note) CONTROL_MUTATE(bank) CONTROL_MUTATE(program)
 CONTROL_MUTATE(lookup_pc) CONTROL_MUTATE(source_stride) CONTROL_MUTATE(pool_count) CONTROL_MUTATE(lookup_flags)
 CONTROL_MUTATE(dispatch_pc) CONTROL_MUTATE(handler_target) CONTROL_MUTATE(prepare_pc) CONTROL_MUTATE(prepare_return_pc)
 CONTROL_MUTATE(retain_pc) CONTROL_MUTATE(retain_writer) CONTROL_MUTATE(refs_before) CONTROL_MUTATE(refs_after)
 CONTROL_MUTATE(rgnd_base) CONTROL_MUTATE(group_count) CONTROL_MUTATE(region_count) CONTROL_MUTATE(region_first) CONTROL_MUTATE(region_offset)
 CONTROL_MUTATE(walk_count) CONTROL_MUTATE(stride_reads) CONTROL_MUTATE(selected_flags) CONTROL_MUTATE(selected_byteC)
 CONTROL_MUTATE(selected_byteD) CONTROL_MUTATE(selected_wave) CONTROL_MUTATE(child_call_pc) CONTROL_MUTATE(child_target) CONTROL_MUTATE(wave_call_pc) CONTROL_MUTATE(frontier)
#undef CONTROL_MUTATE
 for(U i=0;i<33;++i){
  {auto m=original;m.after.voice.sequence.control.offsets[i]^=1;RejectMutation(original,m);}
  {auto m=original;m.after.voice.sequence.control.strides[i]^=1;RejectMutation(original,m);}
  {auto m=original;m.after.voice.sequence.control.lows[i]^=1;RejectMutation(original,m);}
  {auto m=original;m.after.voice.sequence.control.highs[i]^=1;RejectMutation(original,m);}
 }
 for(U i=0;i<7;++i){auto m=original;m.after.voice.sequence.control.child_args[i]^=1;RejectMutation(original,m);}
 const auto& q=original.after.voice.sequence;
 const uintptr_t aliases[]={0,q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage};
 for(auto p:aliases){auto m=original;m.after.voice.sequence.control.wave_output_storage=p;RejectMutation(original,m);}
 // All 52 scalar fields are now exercised; nonzero wave identity is opaque.
 std::array<uintptr_t,7> tokens{};
 for(U i=0;i<7;++i){auto m=original;auto& s=m.after.voice.sequence;const auto p=reinterpret_cast<uintptr_t>(&tokens[i]);
  switch(i){case 0:s.ids_storage=p;break;case 1:s.output_storage=p;break;
   case 2:s.track.storage=s.processor.track_storage=p;break;
   case 3:s.track.context_storage=s.processor_context=s.processor.context_storage=p;break;
   case 4:s.port.storage=s.control.port_storage=p;break;
   case 5:s.processor.command_storage=s.control.command_storage=p;break;
   case 6:s.control.wave_output_storage=p;break;}
  Check(!GameSourceFeedbackExact(original,m)&&GameSourceControlPrefixValid(m),"M05 correlated opaque identity accepted as transport only");
  NoAttempt(f,m.after); // Actual owner gate rejects structural rebase.
 }
 {auto m=original;m.after.voice.sequence.control.wave_output_storage^=1;
  Check(!GameSourceControlExact(q.control,m.after.voice.sequence.control)&&GameSourceControlPrefixValid(m),"M06 changed opaque wave identity need not be rejected structurally");NoAttempt(f,m.after);}
#define CONTROL_ENVELOPE(field) {auto m=original;m.field^=1;Check(!GameSourceFeedbackExact(original,m)&&!GameSourceControlPrefixValid(m),"M07 envelope/history mutation rejected");}
 CONTROL_ENVELOPE(outcome) CONTROL_ENVELOPE(frontier) CONTROL_ENVELOPE(context_owner) CONTROL_ENVELOPE(context_generation)
 CONTROL_ENVELOPE(bank_handle) CONTROL_ENVELOPE(params_written) CONTROL_ENVELOPE(start_returned) CONTROL_ENVELOPE(feedback_returned)
 CONTROL_ENVELOPE(after.bank_live) CONTROL_ENVELOPE(after.registration.word30) CONTROL_ENVELOPE(after.voice.frontier)
 CONTROL_ENVELOPE(after.voice.sequence.current_refs) CONTROL_ENVELOPE(after.voice.sequence.frontier)
 CONTROL_ENVELOPE(after.voice.sequence.refs_before) CONTROL_ENVELOPE(after.voice.sequence.refs_after)
 CONTROL_ENVELOPE(after.voice.sequence.track.refs_before) CONTROL_ENVELOPE(after.voice.sequence.track.refs_after)
 CONTROL_ENVELOPE(after.voice.sequence.release_before) CONTROL_ENVELOPE(after.voice.sequence.release_after)
 CONTROL_ENVELOPE(after.voice.sequence.command_released)
#undef CONTROL_ENVELOPE
 f.input.Unchanged();
}

// FAMILY 5: Close/reinitialize + 7 historical API boundaries + outcome6
// contamination of a genuine historical56598 receipt (9 scenarios).
inline void CloseReinitializeAndHistoricBoundaries(){
 {Fixture f;Start(f);Ticks(f);const auto old=Service(f);const auto oldSound=f.bank.SourceControl();f.bank.Close();
  const auto retired=f.bank.SourceControl();Check(!retired.live&&!retired.bank_live&&!retired.voice.live&&!retired.voice.sequence.live&&!retired.voice.sequence.port.live,"H01 actual Close retires owners");
  NoAttempt(f,old.after);Ok(f.bank.InitializeSource17F0(),"H02 actual reinitialize");Ok(f.bank.Register(f.input.view),"H03 re-register unchanged owned package");
  const auto fresh=f.bank.SourceControl();Check(fresh.generation==2&&fresh.owner!=oldSound.owner&&fresh.registration.generation==2&&fresh.registration.word30==0&&!fresh.voice.live,"H04 sound/registry second generation");
  NoAttempt(f,old.owner);NoAttempt(f,old.after);Start(f);Ticks(f);const auto now=Service(f);const auto& q=now.after.voice.sequence;
  Check(GameSourceControlPrefixValid(now)&&now.after.voice.generation==2&&q.generation==2&&q.port.generation==2&&q.control.port_generation==2,"H05 actual voice/context/port generation2");
  Check(q.owner!=old.after.voice.sequence.owner&&q.port.owner!=old.after.voice.sequence.port.owner&&!GameSourceFeedbackExact(old,now),"H06 retired owner identities distinct");Pending(now);f.input.Unchanged();
 }
 {Fixture f;const auto before=f.bank.SourceControl();Read r;Check(Cut(f.bank.ReadOptionsFeedback(before,r),"4ACA0")&&r.attempted&&!r.read_returned&&!r.frontier&&!GameSourceFeedbackReachedValid(r)&&Same(before,f.bank.SourceControl()),"H07 actual old4AC84 read-only boundary");f.input.Unchanged();}
 {Fixture f;Read r;Check(Cut(f.bank.ReadOptionsFeedback(f.bank.SourceControl(),2,r),"56598")&&r.outcome==0&&GameSourceFeedbackReachedValid(r)&&!GameSourceControlPrefixValid(r),"H08 actual old56598 issuer cut");
  auto changed=r;changed.outcome=6;Check(!GameSourceFeedbackReachedValid(changed)&&!GameSourceControlPrefixValid(changed),"H09 historical56598 outcome6 forbidden");f.input.Unchanged();}
 for(U outcome=2;outcome<=5;++outcome){Fixture f;Start(f);Ticks(f);Read r;const auto expected=f.bank.SourceControl();GameFsStatus status;
  if(outcome==2)status=f.bank.ServiceOptionsVoice(expected,r);else if(outcome==3)status=f.bank.ServiceOptionsSequence(expected,r);
  else if(outcome==4)status=f.bank.ServiceOptionsPort(expected,r);else status=f.bank.ServiceOptionsProcessor(expected,r);
  Check(!status&&status.code==GameFsCode::Unavailable&&r.outcome==outcome&&Same(r.after,f.bank.SourceControl()),"H10 genuine historical service API");
  const bool valid=outcome==2?GameSourceVoiceServiceValid(r):outcome==3?GameSourceSequencePrefixValid(r):outcome==4?GameSourcePortPrefixValid(r):GameSourceProcessorPrefixValid(r);
  Check(valid&&!GameSourceControlPrefixValid(r)&&GameSourceControlExact(r.after.voice.sequence.control,Control{}),"H11 unchanged historical predicate/control absent");f.input.Unchanged();
 }
 {Input input;GameSourceSoundControl control;Ok(control.Initialize17F0(),"H12 public unknown-handle owner");Ok(control.BindSe5(input.view),"H13 public bind without registrar");Read r;
  Check(Cut(control.Feedback4AC38(control.Snapshot(),r),"4AC84")&&r.attempted&&!r.bank_handle_known&&!r.read_returned&&!GameSourceControlPrefixValid(r),"H14 unknown handle stays unread");input.Unchanged();}
}
} // namespace game_options_control_regressions

inline void GameOptionsControlRegressionChecks(){
 using namespace game_options_control_regressions;
 GenuineOwnerControlReadFacts();
 PreRegistrationRgndVariants();
 CurrentOwnerNoAttemptGates();
 ExactTransportMutations();
 CloseReinitializeAndHistoricBoundaries();
}

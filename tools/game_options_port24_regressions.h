#pragma once
// SOURCE ONLY, CORE_RELEASE.v4. Authored families=5; compiler/execution=0.
// Supplied parent0d9bd0459601e49b5a7a0c7f201d84c92ccd48df; no Git queries.
// Synthetic SGXD is independently owned. Receipt copies test transport only.
// NativeOwnedZeroedPortPool16 is Windows NONMATCHING, PSP actual count UNKNOWN.
#include "recovered/game_scene_ui_native.h"
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace game_options_port24_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Read=GameSourceFeedbackRead;
using InputState=GameSourcePortInputSnapshot;
inline void Check(bool ok,const char* id){if(!ok)throw std::runtime_error(std::string("options-port24 ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-port24 ")+id+": "+s.detail);}
inline bool Cut(const GameFsStatus& s,const char* text){return !s&&s.code==GameFsCode::Unavailable&&s.detail.find(text)!=std::string::npos;}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
inline bool Empty(const Read& r){return GameSourceFeedbackExact(r,Read{});}
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& s,std::string& error)const override{
  if(!live){error="port24 synthetic storage retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;s.generation=generation;s.serial=serial;
  s.source_base=0x2000;s.readable=s.writable=true;return true;
 }
};
using Edit=std::function<void(PackageView)>;
struct Input {
 static constexpr U seq=2336,record=0xB28,body=0xB3C,wave=4576,payload=4584,selected=6272,region=0x728,end=6752,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 std::vector<uint8_t> initial_bytes,initial_known;
 explicit Input(const Edit& edit={}){
  lease->bytes.assign(size+14,0xA5);lease->known.assign(size+14,1);
  view=PackageStorage::Attach(lease,size+14).Subview(7,size);
  for(U i=0;i<size;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,end);view.WriteLE32(12,0x80000001);
  view.WriteLE32(16,0x444E4752);view.WriteLE32(20,2312);view.WriteLE32(28,1);view.WriteLE32(32,41);view.WriteLE32(36,40);
  for(U i=0;i<41;++i){const U at=40+56*i;view.WriteLE32(at,9);view.WriteLE32(at+8,56);view.WriteU8(at+12,10);
   view.WriteU8(at+24,uint8_t(32+i));view.WriteU8(at+25,uint8_t(32+i));view.WriteLE32(at+52,30);}
  view.WriteLE32(seq,0x44514553);view.WriteLE32(seq+4,2232);view.WriteLE32(seq+12,1);view.WriteLE32(seq+16,0x960);
  view.WriteLE32(0x964,7);view.WriteLE32(0x980,record);view.WriteLE16(record+8,0);view.WriteLE16(record+10,1000);view.WriteLE32(record+16,23);
  const std::array<uint8_t,9> command{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(U i=0;i<23;++i)view.WriteU8(body+i,uint8_t(0x80+i));
  for(U i=0;i<command.size();++i)view.WriteU8(body+i,command[i]);
  view.WriteLE32(wave,0x45564157);view.WriteLE32(wave+4,2136);view.WriteLE32(payload+4,38);
  view.WriteU8(selected+8,3);view.WriteU8(selected+9,1);
  // Static selected suffix values; no waveform or original asset is acquired.
  view.WriteLE32(selected+0x20,0x73E04);view.WriteLE32(selected+0x24,UINT32_MAX);
  view.WriteLE32(selected+0x28,0x73E04);view.WriteLE32(selected+0x2C,0x640);
  view.WriteLE32(6720,0x454D414E);view.WriteLE32(6724,24);
  if(edit)edit(view); // Mutations always precede registration/digest issuance.
  initial_bytes=lease->bytes;initial_known=lease->known;
 }
 void Unchanged()const{
  Check(lease->bytes==initial_bytes&&lease->known==initial_known,"whole synthetic allocation unchanged");
  for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"offset7 guards unchanged");
 }
};
struct Fixture {
 Input input;GameSoundBank bank;
 explicit Fixture(const Edit& edit={}):input(edit){Ok(bank.InitializeSource17F0(),"initialize");Ok(bank.Register(input.view),"actual Register");}
};
inline Read Start(Fixture& f){Read r;const auto owner=f.bank.SourceControl();Ok(f.bank.ReadOptionsVoiceFeedback(owner,2,r),"actual voice feedback");
 Check(r.outcome==1&&GameSourceVoiceFeedbackValid(r)&&Same(r.after,f.bank.SourceControl()),"current returned voice");return r;}
inline void Ticks(Fixture& f){f.bank.ObserveOptionsWorkerTick();f.bank.ObserveOptionsWorkerTick();}
inline Read Entry(Fixture& f){Read r;const auto owner=f.bank.SourceControl();
 Check(Cut(f.bank.ServiceOptionsPort24(owner,r),"selected5ABF0"),"actual unavailable5ABF0 entry");
 Check(r.outcome==8&&Same(r.owner,owner)&&Same(r.after,f.bank.SourceControl()),"current entry receipt");return r;}
inline bool Valid(const Read& r,U domain){switch(domain){
 case 0:return GameSourceFeedbackReachedValid(r);case 1:return GameSourceVoiceFeedbackValid(r);
 case 2:return GameSourceVoiceServiceValid(r);case 3:return GameSourceSequencePrefixValid(r);
 case 4:return GameSourcePortPrefixValid(r);case 5:return GameSourceProcessorPrefixValid(r);
 case 6:return GameSourceControlPrefixValid(r);case 7:return GameSourceWavePrefixValid(r);
 case 8:return GameSourcePort24PrefixValid(r);default:return false;}}
inline void Domain(const Read& r,U expected){for(U i=0;i<9;++i)Check(Valid(r,i)==(i==expected),"strict disjoint domains0..8");}
inline void Pending(const Read& r){const auto& v=r.after.voice;const auto& q=v.sequence;const auto& c=q.control;const auto& p=q.processor;
 Check(r.after.live&&r.after.bank_live&&!r.after.failed&&v.bank==63&&q.bank==63&&q.port.id==0,"current native owner IDs");
 Check(r.owner.registration.word30==0&&r.after.registration.word30==0&&GameSourceBankRegistrationExact(r.owner.registration,r.after.registration),"immutable registrar0");
 Check(v.bank_refs_before==0&&v.bank_refs_after==1&&q.refs_before==1&&q.refs_after==2,"retain history0to1to2");
 Check(q.track.refs_before==2&&q.track.refs_after==3&&q.release_before==3&&q.release_after==2&&q.release_pc==0x55A7C,"admission history2to3to2");
 Check(q.command_released&&q.admission_returned&&q.handler_returned&&v.callback_returned&&c.refs_before==2&&c.refs_after==3&&q.current_refs==3,"pending D0 retain2to3");
 Check(!c.child_returned&&!c.handler_returned&&!c.control_returned&&!p.control_returned&&!p.kind_returned&&!p.context_returned,"no child/control/processor return");
 Q mask=3;for(U i=12;i<36;++i)mask|=Q(1)<<i;
 Check(q.port.known_halfwords==mask&&(q.port.known_halfwords&0xFFCu)==0,"parameters2..11 UNKNOWN");
 Check(q.port.sentinel_ids==std::array<int16_t,2>{{-1,-1}}&&q.port.sentinel_self==std::array<bool,2>{{true,true}},"no child allocation result");
}
inline void Canonical(Fixture& f,const Read& r){Domain(r,8);Pending(r);
 const auto& q=r.after.voice.sequence;const auto& a=q.port_input;const auto& w=q.wave;const auto d=f.input.view.Describe();
 Check(d.offset==7&&w.image_offset==7&&w.image_host_address==d.host_address,"actual owner offset7");
 Check(w.result_pointer==reinterpret_cast<uintptr_t>(f.input.lease->bytes.data()+7+Input::selected)&&w.result_pointer==d.host_address+7+Input::selected,"actual WAVE30 pointer");
 Check(w.borrow_live&&w.image_owner==d.owner&&w.image_generation==d.generation&&w.image_serial==d.serial,"live immutable WAVE borrow");
 Check(w.returned&&!w.result_null&&w.output_readback&&w.output_value==w.control_slot_storage&&w.output_storage==q.control.wave_output_storage,"genuine WAVE return and mode address");
 Check(a.entered&&a.allocation_live&&a.zeroed&&a.bound&&a.read_known&&a.read_returned&&a.value==0,"actual produced PORT24 read0");
 Check(a.block_owner&&a.block_generation==1&&a.block_serial==1&&a.block_base&&a.block_base%4==0,"real first block identity");
 Check(a.bytes==2048&&a.count==16&&a.stride==128&&a.zero_writes==512&&a.zero_readbacks==512,"explicit whole-block zero observations");
 Check(a.index==0&&a.node_address==a.block_base&&a.read_offset==0x24&&a.read_pc==0x5B1EC&&a.flags==1,"source-layout node binding");
 Check(a.sound_owner==r.after.owner&&a.sound_generation==r.after.generation&&a.port_owner==q.port.owner&&a.port_generation==q.port.generation&&a.port_storage==q.port.storage,"sound/port causal identities");
 Check(a.node_address!=q.port.storage&&a.node_address+0x24!=q.port.storage+0x24,"source layout separate from host Port");
 Check(a.packet_complete&&a.child_entered&&!a.child_returned&&a.packet_storage&&a.stack_known==0x7FFF&&a.stack_writes==8,"complete15logical packet, entry only");
 Check(a.words==std::array<U,13>{{0,10,3,1,0,0,0,0,0x73E04,UINT32_MAX,0x73E04,0,0x640}},"all13 scalar words");
 Check(a.mode_slot==w.output_value&&a.mode_slot==w.control_slot_storage&&a.mode_slot!=1&&a.mode_slot!=w.output_storage&&a.mode_slot!=w.result_pointer,"full uintptr mode-slot ADDRESS");
 Check(a.mode_slot!=reinterpret_cast<uintptr_t>(&r.after.registration.mode)&&a.mode_slot!=reinterpret_cast<uintptr_t>(&r.owner.registration.mode),"mode slot is not receipt-copy address");
 Check(a.store_index==std::array<U,8>{{7,8,9,11,10,14,13,12}}&&a.store_pc==std::array<U,8>{{0x5B1F0,0x5B1F8,0x5B200,0x5B208,0x5B20C,0x5B218,0x5B21C,0x5B224}},"eight suffix stores including delay");
 Check(w.store_index==std::array<U,7>{{1,0,2,3,4,6,5}}&&w.store_pc==std::array<U,7>{{0x5B1A4,0x5B1B0,0x5B1B8,0x5B1C0,0x5B1C4,0x5B1D4,0x5B1E0}},"seven prefix stores preserved");
 Check(a.note==64&&a.bank==63&&a.channels==1&&a.child_pc==0x5B220&&a.child_delay_pc==0x5B224&&a.frontier==0x5ABF0,"actual entry arguments/call/delay");
 Check(w.port_word24_known&&w.port_word24_returned&&q.frontier==0x5ABF0,"new read returned, child did not");
 const uintptr_t aliases[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,w.result_pointer,w.control_slot_storage};
 for(auto p:aliases)Check(a.block_base!=p&&a.node_address!=p&&a.packet_storage!=p,"block/node/packet distinct ABI storage");
 Check(a.packet_storage<a.block_base||a.packet_storage>=a.block_base+2048,"packet outside source block");
 Check(q.worker_tick_serial==2&&q.worker_previous_ns&&q.worker_current_ns>=q.worker_previous_ns&&q.native_elapsed_ns==q.worker_current_ns-q.worker_previous_ns,"two genuine observed ticks");
 f.input.Unchanged();
}

inline void Family1_ActualOwnedZeroReadAndCompletePacket(){
 Fixture f;const auto registrar=f.bank.SourceControl().registration;Check(registrar.id==63&&registrar.mode==1&&registrar.word30==0,"registered mode1");
 Start(f);Ticks(f);const auto r=Entry(f);Canonical(f,r);
 Check(GameSourceBankRegistrationExact(registrar,r.after.registration),"registrar unchanged across entry");
}

inline void Family2_PreRegistrationMalformedAndActualBranches(){
 // All bytes edited in the factory, before the real registrar sees them.
 const std::array<Edit,3> malformed{{
  [](PackageView v){v.WriteLE32(0,0);},
  [](PackageView v){v.WriteLE32(8,Input::size+1);},
  [](PackageView v){v.WriteLE32(Input::wave+4,UINT32_MAX);}
 }};
 for(const auto& edit:malformed){Input input(edit);GameSoundBank bank;Ok(bank.InitializeSource17F0(),"malformed cold initialize");
  Check(!bank.Register(input.view),"actual malformed Register refusal");Read out;
  Check(!bank.ServiceOptionsPort24(bank.SourceControl(),out)&&Empty(out),"malformed bank cannot issue service");input.Unchanged();}
 struct Branch {Edit edit;const char* detail;U frontier;};
 const std::array<Branch,10> branches{{
  {[](PackageView v){v.WriteLE16(Input::record+10,0);},"selected live record",0},
  {[](PackageView v){v.WriteLE32(Input::record+16,0);},"owning body bounds",0x59DD4},
  {[](PackageView v){v.WriteU8(Input::body,0xD1);},"opcode before D0",0x59DD4},
  {[](PackageView v){v.WriteU8(Input::body+1,0xF1);},"operand encoding",0x59DD4},
  {[](PackageView v){v.WriteLE32(28,0);},"program bounds",0x5B9E4},
  {[](PackageView v){v.WriteLE32(32,0);},"positive region",0x5B9E4},
  {[](PackageView v){v.WriteLE32(48,0);},"stride overflow",0x5B9E4},
  {[](PackageView v){v.WriteU8(Input::region+13,1);},"packed/override",0x5B9E4},
  {[](PackageView v){v.WriteLE32(Input::payload+4,30);},"null-result return",0x5B184},
  {[](PackageView v){v.WriteLE32(Input::payload+4,100);v.WriteLE32(Input::region+52,38);},"WAVE record extent",0x557B8}
 }};
 for(const auto& b:branches){Fixture f(b.edit);Start(f);Ticks(f);const auto owner=f.bank.SourceControl();Read r;
  Check(Cut(f.bank.ServiceOptionsPort24(owner,r),b.detail),"actual selected malformed branch");
  Check(r.outcome==8&&r.frontier==b.frontier&&Same(r.owner,owner)&&Same(r.after,f.bank.SourceControl()),"partial branch receipt");
  Check(!r.after.voice.sequence.port_input.entered&&!r.after.voice.sequence.port_input.packet_complete&&!GameSourcePort24PrefixValid(r),"partial branch receives no PORT24/entry credit");
  const auto after=f.bank.SourceControl();Read replay;
  Check(!f.bank.ServiceOptionsPort24(after,replay)&&Empty(replay)&&Same(after,f.bank.SourceControl()),"failed partial service cannot replay");f.input.Unchanged();
 }
}

inline void Family3_CurrentnessAliasesMissingTicksAndHistory(){
 Fixture f;const auto stale=f.bank.SourceControl();Start(f);Ticks(f);const auto before=f.bank.SourceControl();Read out;
 Check(!f.bank.ServiceOptionsPort24(stale,out)&&Empty(out)&&Same(before,f.bank.SourceControl()),"stale owner refused before service");
 Fixture foreign;Check(!f.bank.ServiceOptionsPort24(foreign.bank.SourceControl(),out)&&Empty(out)&&Same(before,f.bank.SourceControl()),"foreign owner refused");
 auto forged=before;forged.voice.sequence.port_input.block_owner=1;
 Check(!f.bank.ServiceOptionsPort24(forged,out)&&Empty(out)&&Same(before,f.bank.SourceControl()),"snapshot cannot seed private block");
 Read alias;alias.owner=before;Check(!f.bank.ServiceOptionsPort24(alias.owner,alias)&&Empty(alias)&&Same(before,f.bank.SourceControl()),"out.owner aliases expected: cleared and rejected");
 alias.after=before;Check(!f.bank.ServiceOptionsPort24(alias.after,alias)&&Empty(alias)&&Same(before,f.bank.SourceControl()),"out.after aliases expected: cleared and rejected");
 const auto r=Entry(f);Canonical(f,r);const auto after=f.bank.SourceControl();
 Check(!f.bank.ServiceOptionsPort24(before,out)&&Empty(out)&&Same(after,f.bank.SourceControl()),"old pre-entry owner no replay");
 Check(!f.bank.ServiceOptionsPort24(after,out)&&Empty(out)&&Same(after,f.bank.SourceControl()),"current post-entry owner no replay");
 Check(!f.bank.ReadOptionsVoiceFeedback(after,2,out)&&Empty(out)&&Same(after,f.bank.SourceControl()),"no duplicate voice issuance");
 for(U ticks=0;ticks<2;++ticks){Fixture missing;Start(missing);if(ticks)missing.bank.ObserveOptionsWorkerTick();const auto owner=missing.bank.SourceControl();Read partial;
  Check(Cut(missing.bank.ServiceOptionsPort24(owner,partial),"missing actual consecutive"),"missing ticks actual cut");
  const auto& q=partial.after.voice.sequence;
  Check(partial.outcome==8&&partial.frontier==0x5A06C&&q.worker_tick_serial==ticks&&q.admission_returned&&q.command_released&&q.current_refs==2&&!q.update_entered&&!q.port_input.entered,"partial admission before missing tick cut");
  for(U i=0;i<9;++i)Check(!Valid(partial,i),"missing ticks outside completed domains");
  const auto current=missing.bank.SourceControl();Read denied;Check(!missing.bank.ServiceOptionsPort24(current,denied)&&Empty(denied)&&Same(current,missing.bank.SourceControl()),"missing tick partial no replay");missing.input.Unchanged();
 }
 // Fresh actual calls create each historical body; no older test entry is used.
 for(U domain=0;domain<8;++domain){Fixture h;Read historic;const auto cold=h.bank.SourceControl();
  if(domain==0)Check(Cut(h.bank.ReadOptionsFeedback(cold,2,historic),"issuer56598"),"actual historical issuer cut");
  else {historic=Start(h);if(domain>=2){Ticks(h);const auto current=h.bank.SourceControl();GameFsStatus s;
   switch(domain){case 2:s=h.bank.ServiceOptionsVoice(current,historic);break;case 3:s=h.bank.ServiceOptionsSequence(current,historic);break;
    case 4:s=h.bank.ServiceOptionsPort(current,historic);break;case 5:s=h.bank.ServiceOptionsProcessor(current,historic);break;
    case 6:s=h.bank.ServiceOptionsControl(current,historic);break;default:s=h.bank.ServiceOptionsWave(current,historic);break;}
   Check(!s&&s.code==GameFsCode::Unavailable,"actual historical service cut");}}
  Domain(historic,domain);Check(GameSourcePortInputExact(historic.after.voice.sequence.port_input,InputState{}),"old domains receive zero new suffix credit");
  if(domain==7){const auto& q=historic.after.voice.sequence;Pending(historic);Check(!q.wave.port_word24_known&&!q.wave.port_word24_returned&&q.frontier==0x5B1EC,"historical7 before actual PORT24 read");}
  h.input.Unchanged();
 }
 f.bank.Close();const auto closed=f.bank.SourceControl();Check(!closed.live&&!closed.voice.sequence.port_input.allocation_live&&!closed.voice.sequence.port_input.bound&&!closed.voice.sequence.wave.borrow_live,"actual Close retires block and borrow");
 Check(!f.bank.ServiceOptionsPort24(after,out)&&Empty(out)&&Same(closed,f.bank.SourceControl()),"closed service refused");f.input.Unchanged();
}

template<class T>inline T Changed(T value){if constexpr(std::is_same_v<T,bool>)return !value;else return T(value+T(1));}
template<class T>inline void ScalarMutation(const Read& base,T InputState::* field,bool accepts,const char* name){
 auto r=base;auto& a=r.after.voice.sequence.port_input;a.*field=Changed(a.*field);
 Check(!GameSourcePortInputExact(base.after.voice.sequence.port_input,a),name);
 Check(!GameSourceFeedbackExact(base,r),"nested Exact covers new scalar");
 Check(GameSourcePort24PrefixValid(r)==accepts,name);
}
inline void Family4_All43FieldsExactVersusPureTransport(){
 Fixture f;Start(f);Ticks(f);const auto base=Entry(f);Canonical(f,base);
 Check(GameSourceFeedbackExact(base,base)&&GameSourcePortInputExact(base.after.voice.sequence.port_input,base.after.voice.sequence.port_input),"authentic reflexive Exact");
 // Four opaque identities are intentionally transport-only acceptances.
#define P24_FIELD(name,accepts) ScalarMutation(base,&InputState::name,accepts,#name)
 P24_FIELD(entered,false);P24_FIELD(allocation_live,false);P24_FIELD(zeroed,false);P24_FIELD(bound,false);
 P24_FIELD(read_known,false);P24_FIELD(read_returned,false);P24_FIELD(packet_complete,false);P24_FIELD(child_entered,false);P24_FIELD(child_returned,false);
 P24_FIELD(block_owner,true);P24_FIELD(block_generation,true);P24_FIELD(block_serial,true);
 P24_FIELD(sound_owner,false);P24_FIELD(sound_generation,false);P24_FIELD(port_owner,false);P24_FIELD(port_generation,false);
 P24_FIELD(block_base,false);P24_FIELD(node_address,false);P24_FIELD(port_storage,false);
 // +1 remains a nonzero opaque packet address outside the source block.
 P24_FIELD(packet_storage,true);P24_FIELD(mode_slot,false);
 P24_FIELD(bytes,false);P24_FIELD(count,false);P24_FIELD(stride,false);P24_FIELD(zero_writes,false);P24_FIELD(zero_readbacks,false);
 P24_FIELD(index,false);P24_FIELD(flags,false);P24_FIELD(read_pc,false);P24_FIELD(read_offset,false);P24_FIELD(value,false);
 P24_FIELD(stack_known,false);P24_FIELD(stack_writes,false);P24_FIELD(child_pc,false);P24_FIELD(child_delay_pc,false);P24_FIELD(frontier,false);
 P24_FIELD(port_id,false);P24_FIELD(note,false);P24_FIELD(bank,false);P24_FIELD(channels,false);
#undef P24_FIELD
 const auto mutate=[&](const std::function<void(InputState&)>& edit){auto r=base;edit(r.after.voice.sequence.port_input);
  Check(!GameSourcePortInputExact(base.after.voice.sequence.port_input,r.after.voice.sequence.port_input)&&!GameSourceFeedbackExact(base,r),"all array elements affect Exact");
  Check(!GameSourcePort24PrefixValid(r),"array mutation rejected by transport");};
 for(U i=0;i<13;++i)mutate([i](InputState& a){a.words[i]^=1u;});
 for(U i=0;i<8;++i){mutate([i](InputState& a){a.store_index[i]^=1u;});mutate([i](InputState& a){a.store_pc[i]^=1u;});}
 // Coherent relocation can pass a pure predicate; no private call uses it.
 auto relocated=base;auto& a=relocated.after.voice.sequence.port_input;
 const auto& q=base.after.voice.sequence;
 const uintptr_t occupied[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,q.wave.control_slot_storage,q.wave.result_pointer};
 bool chosen=false;
 for(uintptr_t i=0;i<12&&!chosen;++i){const uintptr_t candidate=4+4096*i;bool free=true;
  for(auto address:occupied)if(candidate==address)free=false;
  if(a.packet_storage>=candidate&&a.packet_storage<candidate+2048)free=false;
  if(free){a.block_base=candidate;a.node_address=candidate;chosen=true;}}
 Check(chosen,"bounded nonaliasing opaque relocation selected");
 Check(!GameSourceFeedbackExact(base,relocated)&&GameSourcePort24PrefixValid(relocated),"coherent opaque block relocation accepted, not issued");
 const uintptr_t aliases[]={base.after.voice.sequence.port_input.block_base,base.after.voice.sequence.port_input.block_base+4,q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,q.wave.control_slot_storage,q.wave.result_pointer};
 for(auto address:aliases){auto bad=base;bad.after.voice.sequence.port_input.packet_storage=address;
  Check(!GameSourcePort24PrefixValid(bad),"packet alias into block/owned ABI rejected");}
 for(U outcome=0;outcome<8;++outcome){auto bad=base;bad.outcome=outcome;Check(!GameSourcePort24PrefixValid(bad),"outcome relabel cannot grant new domain");}
 const auto current=f.bank.SourceControl();Read out;
 Check(!f.bank.ServiceOptionsPort24(relocated.after,out)&&Empty(out)&&Same(current,f.bank.SourceControl()),"transport acceptance never issues private service");f.input.Unchanged();
}

inline void Family5_CloseReinitializeGenerationAndOwner(){
 Fixture f;Start(f);Ticks(f);const auto first=Entry(f);Canonical(f,first);const auto old=first.after;
 f.bank.Close();const auto closed=f.bank.SourceControl();Read out;
 Check(!closed.live&&!closed.bank_live&&!closed.voice.live&&!closed.voice.sequence.port_input.allocation_live&&!closed.voice.sequence.port_input.bound&&!closed.voice.sequence.wave.borrow_live,"Close retires actual current owners");
 Check(!f.bank.ServiceOptionsPort24(old,out)&&Empty(out)&&Same(closed,f.bank.SourceControl()),"saved observation cannot revive closed allocation");
 // Retained receipt is passive: a pure predicate can still describe its past.
 Check(GameSourcePort24PrefixValid(first),"historical receipt is not live authority");
 Ok(f.bank.InitializeSource17F0(),"reinitialize same object");Ok(f.bank.Register(f.input.view),"actual new registration after Close");
 const auto fresh=f.bank.SourceControl();Check(fresh.owner!=old.owner&&fresh.generation==2&&!fresh.voice.live,"new sound owner generation2");
 Start(f);Ticks(f);const auto before=f.bank.SourceControl();
 Check(!f.bank.ServiceOptionsPort24(old,out)&&Empty(out)&&Same(before,f.bank.SourceControl()),"old owner rejected in new generation");
 const auto second=Entry(f);Domain(second,8);Pending(second);
 const auto& a=first.after.voice.sequence.port_input;const auto& b=second.after.voice.sequence.port_input;
 Check(b.block_owner!=a.block_owner&&b.block_generation==2&&b.block_serial==2&&b.sound_generation==2&&b.sound_owner==second.after.owner,"fresh block identity even if allocator reuses address");
 Check(b.port_owner!=a.port_owner&&b.port_generation==2&&second.after.voice.generation==2&&second.after.voice.sequence.generation==2,"fresh voice/context/port generations");
 Check(b.allocation_live&&b.read_known&&b.read_returned&&b.value==0&&b.zero_writes==512&&b.zero_readbacks==512&&b.packet_complete&&!b.child_returned,"second actual zero production and entry");
 Check(b.words==a.words&&b.mode_slot==second.after.voice.sequence.wave.control_slot_storage&&b.port_storage==second.after.voice.sequence.port.storage,"new issued packet current owner");
 Check(!GameSourceFeedbackExact(first,second),"address reuse cannot equal old receipt");
 const auto current=f.bank.SourceControl();Check(!f.bank.ServiceOptionsPort24(first.after,out)&&Empty(out)&&Same(current,f.bank.SourceControl()),"retired receipt no service after gen2");
 f.bank.Close();Check(!f.bank.SourceControl().voice.sequence.port_input.allocation_live,"second block retired");f.input.Unchanged();
}
}

inline void GameOptionsPort24RegressionChecks(){
 using namespace game_options_port24_regressions;
 Family1_ActualOwnedZeroReadAndCompletePacket();
 Family2_PreRegistrationMalformedAndActualBranches();
 Family3_CurrentnessAliasesMissingTicksAndHistory();
 Family4_All43FieldsExactVersusPureTransport();
 Family5_CloseReinitializeGenerationAndOwner();
}

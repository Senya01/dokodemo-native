#pragma once
// SOURCE authored against CORE_RELEASE.v2 four Sound pins (unchanged in v3).
// Five independent public Sound API families; compiler/execution=0 by author.
// Native pool/config are Windows NONMATCHING; original PSP config UNKNOWN.
#include "recovered/game_scene_ui_native.h"
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace game_options_child_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Read=GameSourceFeedbackRead;using Child=GameSourceChildSnapshot;
inline void Check(bool b,const char* id){if(!b)throw std::runtime_error(std::string("options-child ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-child ")+id+": "+s.detail);}
inline bool Cut(const GameFsStatus& s,const char* id){return !s&&s.code==GameFsCode::Unavailable&&s.detail.find(id)!=std::string::npos;}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
inline bool Empty(const Read& r){return GameSourceFeedbackExact(r,Read{});}
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& out,std::string& error)const override{
  if(!live){error="child fixture lease retired";return false;}
  out={};out.bytes=const_cast<uint8_t*>(bytes.data());out.initialized=const_cast<uint8_t*>(known.data());out.size=bytes.size();
  out.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));out.owner=out.host_address;out.generation=generation;out.serial=serial;
  out.source_base=0x3000;out.readable=out.writable=true;return true;
 }
};
using Edit=std::function<void(PackageView)>;
struct Input {
 static constexpr U record=0xB28,body=0xB3C,selected=6272,region=0x728,end=6752,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 std::vector<uint8_t> saved,saved_known;
 explicit Input(const Edit& edit={}){
  lease->bytes.assign(size+14,0xA5);lease->known.assign(size+14,1);view=PackageStorage::Attach(lease,size+14).Subview(7,size);
  for(U i=0;i<size;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,end);view.WriteLE32(12,0x80000001);
  view.WriteLE32(16,0x444E4752);view.WriteLE32(20,2312);view.WriteLE32(28,1);view.WriteLE32(32,41);view.WriteLE32(36,40);
  for(U i=0;i<41;++i){const U at=40+56*i;view.WriteLE32(at,9);view.WriteLE32(at+8,56);view.WriteU8(at+12,10);
   view.WriteU8(at+24,uint8_t(32+i));view.WriteU8(at+25,uint8_t(32+i));view.WriteLE32(at+52,30);}
  view.WriteLE32(2336,0x44514553);view.WriteLE32(2340,2232);view.WriteLE32(2348,1);view.WriteLE32(2352,0x960);
  view.WriteLE32(0x964,7);view.WriteLE32(0x980,record);view.WriteLE16(record+8,0);view.WriteLE16(record+10,1000);view.WriteLE32(record+16,23);
  for(U i=0;i<23;++i)view.WriteU8(body+i,uint8_t(0x80+i));
  const std::array<uint8_t,9> d0{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(U i=0;i<d0.size();++i)view.WriteU8(body+i,d0[i]);
  view.WriteLE32(4576,0x45564157);view.WriteLE32(4580,2136);view.WriteLE32(4588,38);
  view.WriteU8(selected+8,3);view.WriteU8(selected+9,1);
  view.WriteLE32(selected+0x20,0x73E04);view.WriteLE32(selected+0x24,UINT32_MAX);
  view.WriteLE32(selected+0x28,0x73E04);view.WriteLE32(selected+0x2C,0x640);
  view.WriteLE32(6720,0x454D414E);view.WriteLE32(6724,24);
  if(edit)edit(view); // All byte mutations precede real registrar/digest issuance.
  saved=lease->bytes;saved_known=lease->known;
 }
 void Unchanged()const{Check(lease->bytes==saved&&lease->known==saved_known,"entire offset7 input immutable");
  for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"both offset7 guards");}
};
struct Fixture {Input input;GameSoundBank bank;
 explicit Fixture(const Edit& edit={}):input(edit){Ok(bank.InitializeSource17F0(),"cold init");Ok(bank.Register(input.view),"real Register");}
};
inline Read Start(GameSoundBank& bank){Read r;const auto before=bank.SourceControl();Ok(bank.ReadOptionsVoiceFeedback(before,2,r),"real voice feedback");
 Check(GameSourceVoiceFeedbackValid(r)&&Same(r.after,bank.SourceControl()),"returned current voice");return r;}
inline void Ticks(GameSoundBank& b,U n=2){for(U i=0;i<n;++i)b.ObserveOptionsWorkerTick();}
inline bool Valid(const Read& r,U domain){switch(domain){
 case 0:return GameSourceFeedbackReachedValid(r);case 1:return GameSourceVoiceFeedbackValid(r);case 2:return GameSourceVoiceServiceValid(r);
 case 3:return GameSourceSequencePrefixValid(r);case 4:return GameSourcePortPrefixValid(r);case 5:return GameSourceProcessorPrefixValid(r);
 case 6:return GameSourceControlPrefixValid(r);case 7:return GameSourceWavePrefixValid(r);case 8:return GameSourcePort24PrefixValid(r);
 case 9:return GameSourceChildPrefixValid(r);default:return false;}}
inline void Only(const Read& r,U domain){for(U i=0;i<10;++i)Check(Valid(r,i)==(i==domain),"strict independent outcomes0..9");}
inline void None(const Read& r){for(U i=0;i<10;++i)Check(!Valid(r,i),"partial is not any completed prefix");}
inline Read Reach(GameSoundBank& b){const auto before=b.SourceControl();Read r;
 Check(Cut(b.ServiceOptionsChild(before,r),"selected5C8F4"),"actual typed helper entry unavailable before body/result");
 Check(r.outcome==9&&Same(r.owner,before)&&Same(r.after,b.SourceControl()),"current component9 receipt");return r;}
inline void Refuse(GameSoundBank& b,const Sound& expected){const auto before=b.SourceControl();Read out;out.outcome=99;
 Check(!b.ServiceOptionsChild(expected,out)&&Empty(out)&&Same(before,b.SourceControl()),"refused currentness/replay before new effects");}
// Only dereference an actual live receipt from this Fixture, never a mutation.
template<class T> inline T Load(uintptr_t p){T value{};std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof value);return value;}
inline void Pending(const Read& r){const auto& v=r.after.voice;const auto& q=v.sequence;const auto& c=q.control;const auto& p=q.processor;
 Check(r.after.live&&r.after.bank_live&&!r.after.failed&&q.current_refs==3,"live pending D0 refs3");
 Check(r.owner.registration.word30==0&&r.after.registration.word30==0&&GameSourceBankRegistrationExact(r.owner.registration,r.after.registration),"immutable initial registrar0");
 Check(v.bank_refs_before==0&&v.bank_refs_after==1&&q.refs_before==1&&q.refs_after==2&&q.track.refs_before==2&&q.track.refs_after==3,"history0to1to2to3");
 Check(q.release_before==3&&q.release_after==2&&q.release_pc==0x55A7C&&q.command_released&&q.admission_returned&&q.handler_returned&&v.callback_returned,"prior admission release3to2");
 Check(c.refs_before==2&&c.refs_after==3&&!c.child_returned&&!c.handler_returned&&!c.control_returned&&!p.control_returned&&!p.kind_returned&&!p.context_returned,"D0 retain without child/control return");
 Q mask=3;for(U i=12;i<36;++i)mask|=Q(1)<<i;
 Check(q.port.known_halfwords==mask&&(q.port.known_halfwords&0xFFCu)==0,"parameters2..11 remain unknown");
 Check(q.port.sentinel_ids==std::array<int16_t,2>{{-1,-1}}&&q.port.sentinel_self==std::array<bool,2>{{true,true}},"no source child activation/attachment");
 Check(p.cursor_after==Input::body+9&&p.body_end==Input::body+23&&p.bytes==std::array<uint8_t,9>{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}},"D0 9of23 parsed;14 unread");
}
inline void Canonical(Fixture& f,const Read& r){Only(r,9);Pending(r);const auto& q=r.after.voice.sequence;const auto& a=q.child;const auto& w=q.wave;const auto& p=q.port_input;
 const auto d=f.input.view.Describe();
 Check(d.offset==7&&w.image_offset==7&&w.image_owner==d.owner&&w.image_generation==d.generation&&w.image_serial==d.serial,"offset7 owning WAVE provenance");
 Check(w.result_pointer==d.host_address+d.offset+Input::selected&&w.borrow_live&&w.returned&&w.output_readback&&w.output_value==w.control_slot_storage,"real WAVE30 borrow and full-width mode cell");
 Check(w.registry==r.after.registration.registry&&w.registry_generation==r.after.registration.generation&&w.registry_serial==r.after.registration.serial,"actual registry identity");
 Check(p.words==std::array<U,13>{{0,10,3,1,0,0,0,0,0x73E04,UINT32_MAX,0x73E04,0,0x640}}&&p.value==0&&p.packet_complete&&p.stack_known==0x7FFF,"all15 logical packet values");
 Check(p.mode_slot==w.control_slot_storage&&p.mode_slot!=1&&p.packet_storage==a.packet_storage&&a.bank_mode_slot==p.mode_slot,"same host-width packet/mode address");
 Check(w.store_index==std::array<U,7>{{1,0,2,3,4,6,5}}&&w.store_pc==std::array<U,7>{{0x5B1A4,0x5B1B0,0x5B1B8,0x5B1C0,0x5B1C4,0x5B1D4,0x5B1E0}},"prefix stores in source order");
 Check(p.store_index==std::array<U,8>{{7,8,9,11,10,14,13,12}}&&p.store_pc==std::array<U,8>{{0x5B1F0,0x5B1F8,0x5B200,0x5B208,0x5B20C,0x5B218,0x5B21C,0x5B224}},"suffix stores including call delay");
 Check(p.allocation_live&&p.bound&&p.zero_writes==512&&p.zero_readbacks==512&&p.bytes==2048&&p.stride==128,"separate produced PORT24 allocation");
 Check(a.entered&&a.live&&a.reserved&&a.source_free_linked&&a.header_readback&&a.flags==0&&a.channel_written,"real reserved child still SOURCE free");
 Check(a.pool_owner&&a.pool_generation&&a.pool_serial&&a.config_owner&&a.config_generation&&a.sound_owner==r.after.owner&&a.sound_generation==r.after.generation,"current private pool/config identity");
 Check(a.bytes==2176&&a.count==16&&a.stride==136&&a.zero_writes==544&&a.zero_readbacks==544&&a.header_writes==2*a.count,"C++ semantic clear/readback counts");
 Check(a.index<a.count&&a.candidate_id>=0&&U(a.candidate_id)==a.index&&a.index+1==a.count,"actual selected signed ID/index and fresh ascending policy");
 Check(a.node_address==a.pool_base+a.index*a.stride&&a.channel_offset==a.index*a.stride+0x2A,"actual candidate raw address");
 // Host link0/link4 are the first two native pointers, not PSP32 raw slots.
 Check(Load<uintptr_t>(a.head_storage+sizeof(uintptr_t))==a.node_storage,"actual physical head link4 selected semantic node");
 Check(Load<uintptr_t>(a.node_storage)==a.head_storage&&Load<uintptr_t>(a.node_storage+sizeof(uintptr_t))==a.candidate_links[1]&&a.candidate_links[0]==a.head_storage,"actual host physical links unchanged");
 Check(Load<uintptr_t>(a.candidate_links[1])==a.node_storage,"actual predecessor link0 coherence");
 const auto id=int16_t(U(Load<uint8_t>(a.node_address+8))|(U(Load<uint8_t>(a.node_address+9))<<8));
 Check(id==a.candidate_id&&Load<uint16_t>(a.node_address+10)==0&&Load<uint8_t>(a.pool_base+a.channel_offset)==255,"actual raw signed-ID/flags/channel readback");
 Check(a.channel_before==0&&a.channel_after==255&&a.channel_writes==1&&a.channel_pc==0x5AC70&&a.caller_pc==0x5AC80,"one channel delay-store");
 Check(a.port_storage==q.port.storage&&a.parent_port_id==q.port.id&&a.note==64&&a.bank==q.bank&&a.channels==1,"parent note64 separate from allocator opaque10");
 const std::array<uint8_t,16> key{{'s','g','x','-','p','s','p','-','s','s','e','-','u','s','e',0}};
 Check(a.config_key==key&&a.config_null&&a.config_copy_returned&&a.config_written&&a.config_readback,"owned Windows NULL-config policy");
 Check(Load<uintptr_t>(a.config_storage)==0&&Load<uint8_t>(a.output_storage)==0&&Load<U>(a.g_storage)==0,"actual NULL input/out/G reads");
 for(U i=0;i<key.size();++i)Check(Load<uint8_t>(a.key_storage+i)==key[i],"immutable named config key");
 for(U i=0;i<4;++i)Check(Load<uint8_t>(a.default_storage+i)==0,"actual empty saved default");
 Check(a.config_default==0&&a.config_out_byte==0&&a.config_copy_writes==1&&a.g_writes==2&&a.g_writer_pc==0x54B74&&a.config_call_pc==0x5CCCC,"empty copy then default writer/readback before live");
 Check(a.bank_mode_read&&a.bank_mode==1&&a.mode_read_pc==0x5F57C&&a.format==3&&a.format_dispatched&&a.format_table_word==0xDF520&&a.format_target==0x5F5F4,"fresh mode then selected format");
 Check(Load<U>(a.bank_mode_slot)==1&&a.g_read&&a.sse_use==0&&a.g_read_pc==0x5F5F8&&a.g_reference_segment==1&&a.g_source_addend==0xE47F4,"current PH1-symbol writer/read identity");
 Check(a.allocator_kind==1&&a.allocator_argument==10&&a.packed==0&&a.allocator_pc==0x5F5B8,"actual typed5CA60 arguments1 opaque10 packed0");
 Check(a.slot_entered&&!a.slot_returned&&!a.child_returned&&!p.child_returned&&a.slot_pc==0x5CA8C&&a.slot_delay_pc==0x5CA90&&a.frontier==0x5C8F4,"entry only before helper body/result");
 Check(q.worker_tick_serial==2&&q.worker_previous_ns&&q.worker_current_ns>=q.worker_previous_ns&&q.native_elapsed_ns==q.worker_current_ns-q.worker_previous_ns,"two genuine worker observations");
 f.input.Unchanged();
}

inline void Family1_ActualOwnedPrefix(){Fixture f;Start(f.bank);Ticks(f.bank);const auto r=Reach(f.bank);Canonical(f,r);
 Check(!GameSourcePort24PrefixValid(r)&&!GameSourceWavePrefixValid(r),"new9 does not masquerade as old8or7");}

inline void Family2_ActualMalformedAndSelectedPartials(){
 const std::array<Edit,3> malformed{{[](PackageView v){v.WriteLE32(0,0);},[](PackageView v){v.WriteLE32(20,UINT32_MAX);},[](PackageView v){v.WriteLE32(8,Input::size+1);}}};
 for(const auto& edit:malformed){Input input(edit);GameSoundBank bank;Ok(bank.InitializeSource17F0(),"malformed cold init");
  Check(!bank.Register(input.view),"actual preregistration malformed refusal");
  Check(GameSourceChildExact(bank.SourceControl().voice.sequence.child,Child{}),"malformed bank cannot reserve child");input.Unchanged();}
 for(const uint8_t format:{uint8_t(1),uint8_t(4),uint8_t(255)}){Fixture f([=](PackageView v){v.WriteU8(Input::selected+8,format);});Start(f.bank);Ticks(f.bank);Read r;
  const auto before=f.bank.SourceControl();Check(Cut(f.bank.ServiceOptionsChild(before,r),"mode/format"),"actual selected format branch refusal");
  const auto& a=r.after.voice.sequence.child;Pending(r);None(r);
  Check(a.entered&&a.live&&a.reserved&&a.source_free_linked&&a.header_readback&&a.channel_written&&a.channel_after==255&&a.channel_writes==1,"format refusal keeps completed reserved prefix");
  Check(a.config_readback&&a.config_copy_writes==1&&a.g_writes==2&&a.bank_mode_read&&a.bank_mode==1&&a.format==int8_t(format)&&a.frontier==0x5F57C&&!a.format_dispatched&&!a.g_read&&!a.slot_entered,"partial stops before G read/helper");
  Check(Same(r.after,f.bank.SourceControl()),"partial state current");Refuse(f.bank,r.after);f.input.Unchanged();}
 {Fixture f([](PackageView v){v.WriteLE32(Input::selected,1);v.WriteU8(Input::selected+9,2);});Start(f.bank);Ticks(f.bank);Read r;
  Check(Cut(f.bank.ServiceOptionsChild(f.bank.SourceControl(),r),"component unavailable"),"actual channels2 selected safety refusal");None(r);Pending(r);
  const auto& q=r.after.voice.sequence;Check(q.port_input.packet_complete&&q.port_input.channels==2&&q.child.entered&&!q.child.reserved&&!q.child.channel_written&&q.child.frontier==0x5ABF0,"real packet/entry precedes channels refusal");f.input.Unchanged();}
 {Fixture f([](PackageView v){v.WriteLE32(4588,30);});Start(f.bank);Ticks(f.bank);Read r;
  Check(Cut(f.bank.ServiceOptionsChild(f.bank.SourceControl(),r),"null-result"),"actual WAVE count null branch");None(r);const auto& q=r.after.voice.sequence;
  Check(q.wave.returned&&q.wave.result_null&&!q.wave.borrow_live&&q.frontier==0x5B184&&!q.port_input.entered&&!q.child.entered&&q.current_refs==3,"genuine null return is a partial not fabricated callback");f.input.Unchanged();}
 {Fixture f([](PackageView v){v.WriteU8(Input::region+13,1);});Start(f.bank);Ticks(f.bank);Read r;
  Check(Cut(f.bank.ServiceOptionsChild(f.bank.SourceControl(),r),"packed/override"),"actual region branch refusal");None(r);const auto& q=r.after.voice.sequence;
  Check(q.control.region_selected&&q.control.bank_retained&&q.current_refs==3&&!q.control.child_entered&&!q.wave.entered&&!q.child.entered,"region partial has real prior retain");f.input.Unchanged();}
 {Fixture f([](PackageView v){v.WriteU8(Input::body,0xD1);});Start(f.bank);Ticks(f.bank);Read r;
  Check(Cut(f.bank.ServiceOptionsChild(f.bank.SourceControl(),r),"opcode"),"actual opcode branch refusal");None(r);const auto& q=r.after.voice.sequence;
  Check(q.processor.opcode_read&&q.processor.opcode==0xD1&&!q.processor.control_attempted&&!q.child.entered&&q.current_refs==2,"real parser partial before D0 retain");f.input.Unchanged();}
}

inline GameFsStatus OldCall(GameSoundBank& b,U n,const Sound& s,Read& r){switch(n){
 case 0:return b.ReadOptionsFeedback(s,2,r);case 1:return b.ReadOptionsVoiceFeedback(s,2,r);case 2:return b.ServiceOptionsVoice(s,r);
 case 3:return b.ServiceOptionsSequence(s,r);case 4:return b.ServiceOptionsPort(s,r);case 5:return b.ServiceOptionsProcessor(s,r);
 case 6:return b.ServiceOptionsControl(s,r);case 7:return b.ServiceOptionsWave(s,r);default:return b.ServiceOptionsPort24(s,r);}}
inline void Family3_CurrentnessTicksAndHistoricalBoundaries(){
 {Fixture f;Refuse(f.bank,f.bank.SourceControl());const auto cold=f.bank.SourceControl();Start(f.bank);Refuse(f.bank,cold);
  const auto now=f.bank.SourceControl();auto altered=now;altered.voice.sequence.child.pool_base=now.voice.owner;Refuse(f.bank,altered);
  Fixture foreign;Start(foreign.bank);Refuse(f.bank,foreign.bank.SourceControl());
  Read alias;alias.after=now;Check(!f.bank.ServiceOptionsChild(alias.after,alias)&&Empty(alias)&&Same(now,f.bank.SourceControl()),"expected/out alias refuses without new effects");
  Ticks(f.bank);const auto r=Reach(f.bank);Canonical(f,r);Refuse(f.bank,r.owner);Refuse(f.bank,r.after);
  f.input.lease->live=false;const auto dead=f.bank.SourceControl();Check(!dead.bank_live&&!dead.voice.sequence.wave.borrow_live,"retired input observed before new call");Refuse(f.bank,r.after);Refuse(f.bank,dead);}
 for(U ticks=0;ticks<2;++ticks){Fixture f;Start(f.bank);Ticks(f.bank,ticks);Read r;
  Check(Cut(f.bank.ServiceOptionsChild(f.bank.SourceControl(),r),"timestamps"),"actual missing consecutive ticks refusal");None(r);const auto& q=r.after.voice.sequence;
  Check(r.outcome==9&&q.worker_tick_serial==ticks&&q.frontier==0x5A06C&&q.port.returned&&q.track.returned&&q.admission_returned&&q.command_released&&q.current_refs==2&&r.after.voice.callback_returned&&!q.processor.entered&&!q.child.entered,"missing ticks follows real returned admission, not NoAttempt");
  Refuse(f.bank,r.after);f.input.Unchanged();}
 const U expected_frontier[]={0x56598,0x4AD4C,0x57CF0,0x5A5AC,0x59DD4,0x5B9E4,0x557B8,0x5B1EC,0x5ABF0};
 for(U n=0;n<9;++n){Fixture f;if(n>=2)Start(f.bank);if(n>=4)Ticks(f.bank);const auto before=f.bank.SourceControl();Read r;const auto status=OldCall(f.bank,n,before,r);
  Check((n==1?bool(status):!status)&&r.outcome==n&&r.frontier==expected_frontier[n]&&Same(r.owner,before)&&Same(r.after,f.bank.SourceControl()),"real old API boundary");Only(r,n);
  Check(GameSourceChildExact(r.after.voice.sequence.child,Child{}),"old0..8 leave child audit default");f.input.Unchanged();}
}

template<class T> inline void Flip(T& v){if constexpr(std::is_same_v<T,bool>)v=!v;else ++v;}
inline void Family4_ExactAndPureTransportMatrix(){Fixture f;Start(f.bank);Ticks(f.bank);const auto r=Reach(f.bank);Canonical(f,r);
 U leaves=0,accepted=0;
 const auto test=[&](const char* id,const std::function<void(Child&)>& mutate,bool pure_accept){auto m=r;mutate(m.after.voice.sequence.child);++leaves;
  Check(!GameSourceChildExact(r.after.voice.sequence.child,m.after.voice.sequence.child),id);
  Check(!GameSourceSequenceExact(r.after.voice.sequence,m.after.voice.sequence)&&!GameSourceVoiceExact(r.after.voice,m.after.voice)&&!Same(r.after,m.after)&&!GameSourceFeedbackExact(r,m),"complete Exact composition sees every leaf");
  Check(GameSourceChildPrefixValid(m)==pure_accept,id);if(pure_accept)++accepted;
  Refuse(f.bank,m.after); // opaque predicate acceptance is not current issuer authority.
 };
#define CHILD_LEAF(field,accept) test(#field,[](Child& a){Flip(a.field);},accept)
 // 76 scalar fields. Pure accepted opaque identities are deliberately visible.
 CHILD_LEAF(entered,false);CHILD_LEAF(live,false);CHILD_LEAF(reserved,false);CHILD_LEAF(source_free_linked,false);CHILD_LEAF(header_readback,false);CHILD_LEAF(channel_written,false);
 CHILD_LEAF(bank_mode_read,false);CHILD_LEAF(format_dispatched,false);CHILD_LEAF(g_read,false);CHILD_LEAF(slot_entered,false);CHILD_LEAF(slot_returned,false);CHILD_LEAF(child_returned,false);
 CHILD_LEAF(config_null,false);CHILD_LEAF(config_copy_returned,false);CHILD_LEAF(config_written,false);CHILD_LEAF(config_readback,false);
 CHILD_LEAF(pool_owner,true);CHILD_LEAF(pool_generation,true);CHILD_LEAF(pool_serial,true);CHILD_LEAF(sound_owner,false);CHILD_LEAF(sound_generation,false);CHILD_LEAF(config_owner,true);CHILD_LEAF(config_generation,true);
 CHILD_LEAF(pool_base,false);CHILD_LEAF(node_address,false);CHILD_LEAF(node_storage,true);CHILD_LEAF(head_storage,false);CHILD_LEAF(port_storage,false);CHILD_LEAF(packet_storage,false);CHILD_LEAF(bank_mode_slot,false);
 CHILD_LEAF(config_storage,true);CHILD_LEAF(key_storage,true);CHILD_LEAF(default_storage,true);CHILD_LEAF(output_storage,true);CHILD_LEAF(g_storage,true);
 CHILD_LEAF(candidate_id,false);CHILD_LEAF(parent_port_id,false);CHILD_LEAF(note,false);CHILD_LEAF(channels,false);CHILD_LEAF(bank,false);CHILD_LEAF(bank_mode,false);CHILD_LEAF(format,false);CHILD_LEAF(sse_use,false);CHILD_LEAF(allocator_kind,false);CHILD_LEAF(allocator_argument,false);CHILD_LEAF(packed,false);
 CHILD_LEAF(bytes,false);CHILD_LEAF(count,false);CHILD_LEAF(stride,false);CHILD_LEAF(index,false);CHILD_LEAF(flags,false);CHILD_LEAF(zero_writes,false);CHILD_LEAF(zero_readbacks,false);CHILD_LEAF(header_writes,false);
 CHILD_LEAF(channel_offset,false);CHILD_LEAF(channel_before,false);CHILD_LEAF(channel_after,false);CHILD_LEAF(channel_writes,false);CHILD_LEAF(channel_pc,false);CHILD_LEAF(caller_pc,false);CHILD_LEAF(mode_read_pc,false);
 CHILD_LEAF(format_table_word,false);CHILD_LEAF(format_target,false);CHILD_LEAF(g_read_pc,false);CHILD_LEAF(g_reference_segment,false);CHILD_LEAF(g_source_addend,false);
 CHILD_LEAF(config_default,false);CHILD_LEAF(config_out_byte,false);CHILD_LEAF(config_copy_writes,false);CHILD_LEAF(g_writes,false);CHILD_LEAF(g_writer_pc,false);CHILD_LEAF(config_call_pc,false);
 CHILD_LEAF(allocator_pc,false);CHILD_LEAF(slot_pc,false);CHILD_LEAF(slot_delay_pc,false);CHILD_LEAF(frontier,false);
#undef CHILD_LEAF
 for(U i=0;i<2;++i)test("candidate_links leaf",[=](Child& a){Flip(a.candidate_links[i]);},i==1);
 for(U i=0;i<16;++i)test("config_key leaf",[=](Child& a){Flip(a.config_key[i]);},false);
 Check(leaves==94&&accepted==12,"78 fields ->94 Exact leaves;12 pure opaque acceptances");
 // Transport-only coherent alternatives, never dereferenced or adopted.
 const auto coherent=[&](const std::function<void(Read&)>& change,bool expected){auto m=r;change(m);
  Check(!GameSourceFeedbackExact(r,m)&&GameSourceChildPrefixValid(m)==expected,"coherent transport predicate expectation");Refuse(f.bank,m.after);};
 coherent([](Read& m){auto& a=m.after.voice.sequence.child;a.pool_base+=0x10000000;a.node_address=a.pool_base+a.stride*a.index;},true);
 coherent([](Read& m){auto& a=m.after.voice.sequence.child;++a.head_storage;a.candidate_links[0]=a.head_storage;},true);
 coherent([](Read& m){auto& a=m.after.voice.sequence.child;a.index=(a.index+1)%a.count;a.candidate_id=int32_t(a.index);a.node_address=a.pool_base+a.stride*a.index;a.channel_offset=a.stride*a.index+0x2A;},true);
 coherent([](Read& m){auto& q=m.after.voice.sequence;++q.port_input.packet_storage;q.child.packet_storage=q.port_input.packet_storage;},true);
 coherent([](Read& m){auto& q=m.after.voice.sequence;++q.wave.control_slot_storage;q.wave.output_value=q.wave.control_slot_storage;q.port_input.mode_slot=q.wave.control_slot_storage;q.child.bank_mode_slot=q.wave.control_slot_storage;},true);
 coherent([](Read& m){m.after.voice.sequence.child.pool_base=0;},false);
 coherent([](Read& m){auto& a=m.after.voice.sequence.child;a.pool_base=UINTPTR_MAX-3;a.node_address=a.pool_base;},false);
 coherent([](Read& m){auto& q=m.after.voice.sequence;q.child.node_storage=q.port.storage;},false);
 coherent([](Read& m){auto& a=m.after.voice.sequence.child;a.g_storage=a.output_storage;},false);
 coherent([](Read& m){auto& a=m.after.voice.sequence.child;a.candidate_links[1]=a.node_storage;},false);
 coherent([](Read& m){m.after.voice.sequence.child.candidate_links[1]=0;},false);
 f.input.Unchanged();
}

inline void Retired(GameSoundBank& bank){const auto s=bank.SourceControl();const auto& q=s.voice.sequence;
 Check(!s.live&&!s.bank_live&&!q.child.live&&!q.child.reserved&&!q.child.source_free_linked&&!q.child.config_readback&&!q.wave.borrow_live&&!q.port_input.allocation_live,"Close retires free reservation/config/borrow before bank teardown");}
inline void Family5_CloseAndGeneration2(){Fixture f;Start(f.bank);Ticks(f.bank);const auto first=Reach(f.bank);Canonical(f,first);
 const auto a=first.after.voice.sequence.child;f.bank.Close();Retired(f.bank);Refuse(f.bank,first.owner);Refuse(f.bank,first.after);
 Check(GameSourceChildPrefixValid(first),"past value receipt remains pure transport, not live authority");
 Ok(f.bank.InitializeSource17F0(),"real reinitialize");Input fresh;Ok(f.bank.Register(fresh.view),"new owned bank registration");Start(f.bank);Ticks(f.bank);const auto second=Reach(f.bank);
 Only(second,9);Pending(second);const auto& q=second.after.voice.sequence;const auto& b=q.child;
 Check(second.after.owner!=first.after.owner&&second.after.generation==first.after.generation+1&&second.after.voice.generation==first.after.voice.generation+1&&q.generation==first.after.voice.sequence.generation+1,"fresh sound/voice/context generation2");
 Check(b.pool_owner!=a.pool_owner&&b.pool_generation==a.pool_generation+1&&b.pool_serial==a.pool_serial+1&&b.config_owner!=a.config_owner&&b.config_generation==a.config_generation+1,"new allocation/config authority even if addresses reuse");
 Check(b.pool_generation==2&&b.config_generation==2&&q.worker_tick_serial==2&&q.wave.borrow_live&&b.live&&b.reserved&&b.source_free_linked&&b.config_readback,"actual second generation prefix");
 Check(second.after.registration.generation==first.after.registration.generation+1&&second.after.registration.serial>first.after.registration.serial&&q.wave.image_owner==fresh.view.Describe().owner&&q.wave.image_owner!=first.after.voice.sequence.wave.image_owner,"new registry entry generation/serial and separately owned bank borrow");
 Check(b.node_address==b.pool_base+b.index*b.stride&&b.candidate_id==int32_t(b.index)&&b.index+1==b.count&&Load<uint8_t>(b.node_address+0x2A)==255,"second generation actual candidate/channel readback");
 Refuse(f.bank,first.owner);Refuse(f.bank,first.after);Refuse(f.bank,second.after);fresh.Unchanged();f.input.Unchanged();f.bank.Close();Retired(f.bank);
 {Fixture partial([](PackageView v){v.WriteU8(Input::selected+8,4);});Start(partial.bank);Ticks(partial.bank);Read r;
  Check(Cut(partial.bank.ServiceOptionsChild(partial.bank.SourceControl(),r),"mode/format")&&r.after.voice.sequence.child.reserved,"real partial before Close");
  partial.bank.Close();Retired(partial.bank);Refuse(partial.bank,r.after);partial.input.Unchanged();}
}
static_assert(!std::is_default_constructible_v<GameOptionsChildCallScope> && !std::is_destructible_v<GameOptionsChildCallScope>);
static_assert(!std::is_copy_constructible_v<GameOptionsChildCallScope> && !std::is_copy_assignable_v<GameOptionsChildCallScope>);
static_assert(!std::is_move_constructible_v<GameOptionsChildCallScope> && !std::is_move_assignable_v<GameOptionsChildCallScope>);
} // namespace game_options_child_regressions

inline void GameOptionsChildRegressionChecks(){using namespace game_options_child_regressions;
 Family1_ActualOwnedPrefix();Family2_ActualMalformedAndSelectedPartials();Family3_CurrentnessTicksAndHistoricalBoundaries();
 Family4_ExactAndPureTransportMatrix();Family5_CloseAndGeneration2();
}

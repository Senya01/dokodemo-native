#pragma once
// SOURCE ONLY: authored against CORE_RELEASE.v2; builds/product executions=0.
// Supplied accepted parent299d1abb88ac69ff5c33dd7af61e4812261958bb; no Git.
// Independent synthetic fixture; primitives adapted from control lines1..124 ONLY.
// Public receipt mutations exercise transport validation, never private issuance.
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace game_options_wave_regressions {
using namespace dokodemo::recovered;
using U=uint32_t;using Q=uint64_t;
using Sound=GameSourceSoundSnapshot;using Read=GameSourceFeedbackRead;using Wave=GameSourceWaveSnapshot;
inline void Check(bool v,const char* id){if(!v)throw std::runtime_error(std::string("options-wave ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-wave ")+id+": "+s.detail);}
inline bool Cut(const GameFsStatus& s,const char* text){return !s&&s.code==GameFsCode::Unavailable&&s.detail.find(text)!=std::string::npos;}
inline bool Same(const Sound& a,const Sound& b){return GameSourceSoundSnapshotExact(a,b);}
inline bool Empty(const Read& r){return GameSourceFeedbackExact(r,Read{});}
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;Q generation=1,serial=1;bool live=true;
 bool Span(PackageStorageSpan& s,std::string& error)const override{
  if(!live){error="wave synthetic lease retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;s.generation=generation;s.serial=serial;
  s.source_base=0x2000;s.readable=s.writable=true;return true;
 }
};
using Edit=std::function<void(PackageView)>;
struct Input {
 static constexpr U seq=2336,record=0xB28,body=0xB3C,wave=4576,payload=4584,selected=6272,end=6752,size=end+5;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>();PackageView view;
 std::vector<uint8_t> frozen_bytes,frozen_known;
 explicit Input(const Edit& edit={}){
  lease->bytes.assign(size+14,0xA5);lease->known.assign(size+14,1);
  view=PackageStorage::Attach(lease,size+14).Subview(7,size);
  for(U i=0;i<size;++i)view.WriteU8(i,0);
  view.WriteLE32(0,0x44584753);view.WriteLE32(8,end);view.WriteLE32(12,0x80000001);
  view.WriteLE32(16,0x444E4752);view.WriteLE32(20,2312);view.WriteLE32(28,1);view.WriteLE32(32,41);view.WriteLE32(36,40);
  for(U i=0;i<41;++i){const U at=40+56*i;view.WriteLE32(at,9);view.WriteLE32(at+8,56);view.WriteU8(at+12,10);
   view.WriteU8(at+24,uint8_t(32+i));view.WriteU8(at+25,uint8_t(32+i));view.WriteLE32(at+52,30);}
  view.WriteLE32(seq,0x44514553);view.WriteLE32(seq+4,2232);view.WriteLE32(seq+12,1);view.WriteLE32(seq+16,0x960);
  view.WriteLE32(0x964,7);view.WriteLE32(0x980,record);view.WriteLE32(record+4,0);view.WriteLE16(record+8,0);
  view.WriteLE16(record+10,1000);view.WriteLE32(record+16,23);
  for(U i=0;i<23;++i)view.WriteU8(body+i,uint8_t(0x80+i));
  const std::array<uint8_t,9> d0{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(U i=0;i<d0.size();++i)view.WriteU8(body+i,d0[i]);
  view.WriteLE32(wave,0x45564157);view.WriteLE32(wave+4,2136);view.WriteLE32(payload+4,38);
  // All remaining record fields are SYNTHETIC KNOWN ZERO, not original facts.
  view.WriteLE32(selected,0);view.WriteU8(selected+8,3);view.WriteU8(selected+9,1);
  view.WriteLE32(selected+0x10,0);view.WriteLE32(selected+0x14,0);view.WriteLE32(selected+0x1C,0);
  view.WriteLE32(6720,0x454D414E);view.WriteLE32(6724,24);
  if(edit)edit(view); // Always BEFORE actual registration and digest issuance.
  frozen_bytes=lease->bytes;frozen_known=lease->known;
 }
 void Unchanged()const{
  Check(lease->bytes==frozen_bytes&&lease->known==frozen_known,"I01 whole allocation unchanged");
  for(U i=0;i<7;++i)Check(lease->bytes[i]==0xA5&&lease->bytes[size+7+i]==0xA5,"I02 seven guards each side");
 }
};
struct Fixture {
 Input input;GameSoundBank bank;
 explicit Fixture(const Edit& edit={}):input(edit){Ok(bank.InitializeSource17F0(),"I03 initialize");Ok(bank.Register(input.view),"I04 real registration");}
};
inline Read Start(Fixture& f){
 const auto before=f.bank.SourceControl();Read r;Ok(f.bank.ReadOptionsVoiceFeedback(before,2,r),"I05 real voice");
 Check(r.outcome==1&&GameSourceVoiceFeedbackValid(r)&&Same(r.after,f.bank.SourceControl()),"I06 current voice receipt");return r;
}
inline void Ticks(Fixture& f){f.bank.ObserveOptionsWorkerTick();f.bank.ObserveOptionsWorkerTick();}
inline Read Service(Fixture& f,const char* detail="5B1EC"){
 const auto expected=f.bank.SourceControl();Read r;Check(Cut(f.bank.ServiceOptionsWave(expected,r),detail),"I07 real unavailable cut");
 Check(r.outcome==7&&Same(r.owner,expected)&&Same(r.after,f.bank.SourceControl()),"I08 actual owner/after");return r;
}
inline bool Valid(const Read& r,U n){switch(n){
 case 0:return GameSourceFeedbackReachedValid(r);case 1:return GameSourceVoiceFeedbackValid(r);
 case 2:return GameSourceVoiceServiceValid(r);case 3:return GameSourceSequencePrefixValid(r);
 case 4:return GameSourcePortPrefixValid(r);case 5:return GameSourceProcessorPrefixValid(r);
 case 6:return GameSourceControlPrefixValid(r);case 7:return GameSourceWavePrefixValid(r);default:return false;}}
inline void Domain(const Read& r,U n){for(U i=0;i<8;++i)Check(Valid(r,i)==(i==n),"I09 disjoint strict outcome predicates");}
inline void Pending(const Read& r){
 const auto& v=r.after.voice;const auto& q=v.sequence;const auto& c=q.control;const auto& p=q.processor;const auto& w=q.wave;
 Check(r.bank_handle==63&&v.bank==63&&v.id==0&&q.bank==63&&q.id==0&&q.port.id==0,"I10 bank/voice/context/port typed IDs");
 Check(r.owner.registration.word30==0&&r.after.registration.word30==0&&GameSourceBankRegistrationExact(r.owner.registration,r.after.registration),"I11 immutable registrar initial0");
 Check(v.bank_refs_before==0&&v.bank_refs_after==1&&q.refs_before==1&&q.refs_after==2,"I12 actual retain0to1to2");
 Check(q.track.refs_before==2&&q.track.refs_after==3&&q.release_before==3&&q.release_after==2&&q.release_pc==0x55A7C,"I13 admission3to2");
 Check(q.command_released&&q.admission_returned&&q.handler_returned&&v.callback_returned&&c.refs_before==2&&c.refs_after==3&&q.current_refs==3,"I14 pending D0 retain2to3");
 Check(c.wave_returned&&c.wave_result_known&&c.wave_output_known&&!c.child_returned&&!c.handler_returned&&!c.control_returned,"I15 WAVE return only");
 Check(!p.control_returned&&!p.kind_returned&&!p.context_returned,"I16 no D0 post/59DD4/5A128 return");
 Check(w.port_word24_attempted&&!w.port_word24_known&&!w.port_word24_returned&&w.port_read_pc==0x5B1EC,"I17 unknown PORT24 never returned");
 Q mask=3;for(U i=12;i<36;++i)mask|=Q(1)<<i;
 Check(q.port.known_halfwords==mask&&(q.port.known_halfwords&0xFFCu)==0,"I18 params2..11 remain unknown");
 Check(q.port.sentinel_self==std::array<bool,2>{{true,true}}&&q.port.sentinel_ids==std::array<int16_t,2>{{-1,-1}},"I19 no child allocation5ABF0");
}
inline void Canonical(Fixture& f,const Read& r){
 Domain(r,7);Pending(r);const auto& q=r.after.voice.sequence;const auto& w=q.wave;const auto d=f.input.view.Describe();
 const auto actual=reinterpret_cast<uintptr_t>(f.input.lease->bytes.data()+7+Input::selected);
 Check(d.offset==7&&d.host_address==Q(reinterpret_cast<uintptr_t>(f.input.lease->bytes.data())),"I20 allocation base distinct from subview");
 Check(w.result_pointer==actual&&w.result_pointer==d.host_address+d.offset+6272&&w.result_pointer!=d.host_address+6272,"I21 exact actual record pointer");
 Check(w.borrow_live&&w.image_offset==7&&w.image_host_address==d.host_address&&w.image_owner==d.owner&&w.image_generation==d.generation&&w.image_serial==d.serial,"I22 real owning borrow");
 Check(w.output_storage==q.control.wave_output_storage&&w.output_value==w.control_slot_storage&&w.output_value>1&&w.output_value!=w.result_pointer,"I23 output stores mode field ADDRESS");
 Check(w.output_value!=reinterpret_cast<uintptr_t>(&r.after.registration.mode)&&w.output_value!=reinterpret_cast<uintptr_t>(&r.owner.registration.mode),"I24 not copy snapshot mode address");
 const uintptr_t ids[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,w.result_pointer};
 for(auto id:ids)Check(w.control_slot_storage!=id,"I25 mode slot distinct from owned ABI storage");
 Check(w.mode_read&&w.control_mode==1&&w.output_written&&w.output_readback&&w.output_store_pc==0x55830,"I26 genuine mode1/output readback");
 Check(w.returned&&w.result_known&&!w.result_null&&w.return_pc==0x55848&&w.return_delay_pc==0x5584C,"I27 genuine JR/delay return");
 Check(w.wave_payload==4584&&w.wave_size==2136&&w.wave_count==38&&w.record_offset==6272&&w.stride==56,"I28 full WAVE layout");
 Check(w.record_read_mask==63&&w.stack_known==127&&w.stack_writes==7&&w.channels==1,"I29 actual reads and stack mask");
 Check(w.parameters==std::array<int32_t,7>{{0,10,3,1,0,0,0}},"I30 seven postWave source words; second extra branch reads0");
 Check(w.store_index==std::array<U,7>{{1,0,2,3,4,6,5}}&&w.store_pc==std::array<U,7>{{0x5B1A4,0x5B1B0,0x5B1B8,0x5B1C0,0x5B1C4,0x5B1D4,0x5B1E0}},"I31 source stack write order; no sp1C store");
 Check(q.worker_tick_serial==2&&q.worker_previous_ns&&q.worker_current_ns>=q.worker_previous_ns&&q.native_elapsed_ns==q.worker_current_ns-q.worker_previous_ns,"I32 actual two tick provenance");
 f.input.Unchanged();
}
inline void Family1_GenuineOwnershipAndReturn(){
 Fixture f;const auto registrar=f.bank.SourceControl().registration;
 Check(registrar.id==63&&registrar.mode==1&&registrar.attached&&registrar.word30==0,"F1 actual mode1 initial registrar");
 Start(f);Ticks(f);const auto r=Service(f);Canonical(f,r);
 Check(GameSourceBankRegistrationExact(registrar,r.after.registration),"F1 registrar unchanged through all retains");
}
inline void Partial(Fixture& f,const Read& r){
 Check(!GameSourceWavePrefixValid(r)&&r.outcome==7&&r.after.bank_live&&r.after.voice.sequence.current_refs==3,"F2 real partial not canonical proof");
 for(U i=0;i<7;++i)Check(!Valid(r,i),"F2 partial never legacy outcome");f.input.Unchanged();
}
inline void Family2_PreRegistrationDataBranches(){
 // Signed count-minus-one is tested as source semantics, not unsigned range lore.
 const std::array<U,5> nullCounts{{0,30,0x80000001u,0xFFFFFFFFu,1}};
 for(U count:nullCounts){Fixture f([=](PackageView v){v.WriteLE32(Input::payload+4,count);});Start(f);Ticks(f);const auto r=Service(f,"null-result");Partial(f,r);
  const auto& w=r.after.voice.sequence.wave;Check(w.count_read&&w.wave_count==count&&w.result_known&&w.result_null&&w.returned&&w.child_continued&&!w.mode_read&&!w.output_written&&!w.borrow_live&&!w.port_word24_attempted&&w.frontier==0x5B184,"F2 genuine lookup null return before child return");}
 for(U count:std::array<U,2>{{31,0x80000000u}}){Fixture f([=](PackageView v){v.WriteLE32(Input::payload+4,count);});Start(f);Ticks(f);const auto r=Service(f);Partial(f,r);
  const auto& w=r.after.voice.sequence.wave;Check(w.returned&&!w.result_null&&w.borrow_live&&w.record_offset==6272&&w.wave_count==count&&w.stack_writes==7,"F2 signed count permits actual record30");}
 for(U index:std::array<U,4>{{38,0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu}}){Fixture f([=](PackageView v){v.WriteLE32(0x728+52,index);});Start(f);Ticks(f);const auto r=Service(f,"null-result");Partial(f,r);
  const auto& w=r.after.voice.sequence.wave;Check(w.wave==int32_t(index)&&w.returned&&w.result_null&&w.child_continued&&!w.output_written&&!w.port_word24_attempted,"F2 signed index/bounds null result");
  Check(w.count_read==(int32_t(index)>=0),"F2 negative index returns before count load");}
 for(U index:std::array<U,2>{{31,37}}){Fixture f([=](PackageView v){v.WriteLE32(0x728+52,index);});Start(f);Ticks(f);const auto r=Service(f);Partial(f,r);
  const auto& w=r.after.voice.sequence.wave;Check(w.returned&&!w.result_null&&w.borrow_live&&w.result_pointer==reinterpret_cast<uintptr_t>(f.input.lease->bytes.data()+7+Input::payload+8+56*index),"F2 genuine alternate record pointer");}
 for(U extent:std::array<U,2>{{4,8}}){Fixture f([=](PackageView v){v.WriteLE32(Input::wave+4,extent);const U next=Input::payload+extent;v.WriteLE32(next,0x4B4E554A);v.WriteLE32(next+4,6720-next-8);});Start(f);Ticks(f);
  const auto r=Service(f,extent==4?"count extent":"record extent");Partial(f,r);const auto& w=r.after.voice.sequence.wave;
  Check(w.entered&&w.bank_lookup_returned&&w.wave_chunk_known&&!w.returned&&!w.result_known&&!w.child_continued&&!w.output_written&&w.count_read==(extent==8),"F2 real extent refusal never helper return");}
 // Variants exercise actual postWave reads, not injected lookup/helper failures.
 for(U variant=0;variant<7;++variant){Fixture f([=](PackageView v){switch(variant){
   case 0:v.WriteLE32(Input::selected,1);v.WriteU8(Input::selected+9,0xFE);break;
   case 1:v.WriteU8(Input::selected+9,0xFE);break;case 2:v.WriteU8(Input::selected+8,0x80);break;
   case 3:v.WriteLE32(Input::selected+0x10,0x11223344);break;case 4:v.WriteLE32(Input::selected+0x14,0x89ABCDEF);break;
   case 5:v.WriteLE32(Input::selected+0x1C,0x12345678);break;case 6:v.WriteU8(0x728+12,0xFF);break;}});
  Start(f);Ticks(f);const auto r=Service(f);Partial(f,r);const auto& w=r.after.voice.sequence.wave;
  Check(w.returned&&w.borrow_live&&w.record_read_mask==63&&w.stack_writes==7&&!r.after.voice.sequence.control.child_returned,"F2 record variant genuine WAVE only return");
  if(variant==0)Check(w.channels==-2&&w.parameters[3]==1,"F2 flag1 selects byte9 channels");
  if(variant==1)Check(w.channels==1&&w.parameters[3]==-2,"F2 signed byte9 alternate operand");
  if(variant==2)Check(w.parameters[2]==-128,"F2 signed byte8 format");
  if(variant==3)Check(w.parameters[4]==0x11223344,"F2 extra-minus1 second branch replaces t2");
  if(variant==4)Check(w.parameters[5]==int32_t(0x89ABCDEFu),"F2 word14 stack18");
  if(variant==5)Check(w.word1C==0x12345678&&w.stack_known==127&&w.parameters==std::array<int32_t,7>{{0,10,3,1,0,0,0}},"F2 word1C local only no eighth store");
  if(variant==6)Check(w.parameters[1]==-1,"F2 signed region byteC");}
}
inline void NoAttempt(Fixture& f,const Sound& expected){
 const auto before=f.bank.SourceControl();Read r;r.outcome=99;
 Check(Cut(f.bank.ServiceOptionsWave(expected,r),"current service owner")&&Empty(r)&&Same(before,f.bank.SourceControl()),"F3 gate leaves empty receipt/current state");
}
inline void Family3_LiveGatesAndMissingTicks(){
 {Fixture f;const auto stale=f.bank.SourceControl();Start(f);Ticks(f);NoAttempt(f,stale);Canonical(f,Service(f));}
 {Fixture f;Start(f);Ticks(f);const auto stale=f.bank.SourceControl();GameSourceSoundGainReceipt gain;Ok(f.bank.QuerySourceGain(stale,19,gain),"F3 real changed query");NoAttempt(f,stale);Canonical(f,Service(f));}
 {Fixture f,foreign;Start(f);Ticks(f);Start(foreign);NoAttempt(f,foreign.bank.SourceControl());Canonical(f,Service(f));foreign.input.Unchanged();}
 // Aliases change bytes/known/lease identity AFTER digest issuance; never rebase.
 for(U kind=0;kind<5;++kind){Fixture f;Start(f);Ticks(f);const auto old=f.bank.SourceControl();
  if(kind==0)f.input.lease->bytes[7+Input::selected+40]^=1;
  if(kind==1)f.input.lease->known[7+Input::selected+40]=0;
  if(kind==2)++f.input.lease->generation;if(kind==3)++f.input.lease->serial;if(kind==4)f.input.lease->live=false;
  const auto bytes=f.input.lease->bytes,known=f.input.lease->known;
  Check(!f.bank.SourceControl().bank_live,"F3 whole bank livegate rejects changed alias even unread record byte");
  NoAttempt(f,old);NoAttempt(f,f.bank.SourceControl());
  Check(f.input.lease->bytes==bytes&&f.input.lease->known==known,"F3 gate never repairs/rebases altered allocation");}
 {Fixture f;Start(f);Ticks(f);const auto r=Service(f);NoAttempt(f,r.after);Canonical(f,r);}
 {Fixture f;Start(f);Ticks(f);const auto old=f.bank.SourceControl();f.bank.Close();NoAttempt(f,old);NoAttempt(f,f.bank.SourceControl());f.input.Unchanged();}
 for(U count=0;count<2;++count){Fixture f;Start(f);if(count)f.bank.ObserveOptionsWorkerTick();const auto r=Service(f,"missing actual consecutive");
  const auto& q=r.after.voice.sequence;Check(!GameSourceWavePrefixValid(r)&&q.frontier==0x5A06C&&q.current_refs==2&&q.command_released&&q.admission_returned&&r.after.voice.callback_returned&&!q.update_entered&&!q.processor_attempted&&q.worker_tick_serial==count,"F3 real partial5A06C refs2");
  Check(GameSourceWaveExact(q.wave,Wave{})&&GameSourceControlExact(q.control,GameSourceControlSnapshot{}),"F3 no WAVE attempt before timestamps");f.input.Unchanged();}
}

// 56 declared scalar fields. Arrays are covered at every element separately.
#define GAME_OPTIONS_WAVE_SCALARS(X) \
 X(entered) X(bank_lookup_returned) X(wave_chunk_known) X(count_read) X(mode_read) X(output_written) X(output_readback) \
 X(result_known) X(result_null) X(returned) X(borrow_live) X(child_continued) X(port_word24_attempted) X(port_word24_known) X(port_word24_returned) \
 X(output_storage) X(control_slot_storage) X(output_value) X(result_pointer) X(port_storage) \
 X(registry) X(registry_generation) X(registry_serial) X(image_owner) X(image_generation) X(image_serial) X(image_offset) X(image_host_address) \
 X(bank) X(wave) X(port_id) X(byte8) X(byte9) X(channels) \
 X(lookup_pc) X(wave_load_pc) X(count_load_pc) X(mode_load_pc) X(output_store_pc) X(return_pc) X(return_delay_pc) \
 X(wave_payload) X(wave_size) X(wave_count) X(stride) X(record_offset) X(control_mode) X(record_flags) X(record_read_mask) \
 X(word10) X(word14) X(word1C) X(stack_known) X(stack_writes) X(port_read_pc) X(frontier)
inline bool FullWaveExact(const Wave& a,const Wave& b){
 bool same=true;
#define WAVE_FIELD_EQUAL(field) same=same&&(a.field==b.field);
 GAME_OPTIONS_WAVE_SCALARS(WAVE_FIELD_EQUAL)
#undef WAVE_FIELD_EQUAL
 return same&&a.parameters==b.parameters&&a.store_pc==b.store_pc&&a.store_index==b.store_index;
}
template<class T>inline void Change(T& value){if constexpr(std::is_same_v<T,bool>)value=!value;else ++value;}
inline void Mutation(const Read& original,const Read& altered){
 const auto& a=original.after.voice.sequence.wave;const auto& b=altered.after.voice.sequence.wave;
 Check(!FullWaveExact(a,b)&&!GameSourceWaveExact(a,b),"F4 exact comparator sees every wave field");
 Check(!GameSourceSequenceExact(original.after.voice.sequence,altered.after.voice.sequence)&&!GameSourceVoiceExact(original.after.voice,altered.after.voice),"F4 nested sequence and voice comparators");
 Check(!Same(original.after,altered.after)&&!GameSourceSoundOwnerStable(original.after,altered.after)&&!GameSourceFeedbackExact(original,altered),"F4 nested owner/sound/feedback comparators");
 Check(!GameSourceWavePrefixValid(altered),"F4 single field mutation rejected structurally");
}
inline void Family4_CompleteWaveAndTransportMutations(){
 Fixture f;Start(f);Ticks(f);const auto r=Service(f);Canonical(f,r);const auto& w=r.after.voice.sequence.wave;
 Check(FullWaveExact(w,w)&&GameSourceWaveExact(w,w)&&GameSourceFeedbackExact(r,r),"F4 exact reflexive positive");
#define WAVE_MUTATE(field) {auto x=r;Change(x.after.voice.sequence.wave.field);Mutation(r,x);}
 GAME_OPTIONS_WAVE_SCALARS(WAVE_MUTATE)
#undef WAVE_MUTATE
 for(U i=0;i<7;++i){auto x=r;++x.after.voice.sequence.wave.parameters[i];Mutation(r,x);
  x=r;++x.after.voice.sequence.wave.store_pc[i];Mutation(r,x);x=r;++x.after.voice.sequence.wave.store_index[i];Mutation(r,x);}
 // Positive transport coherence is not an issuance/authenticity assertion.
 {auto x=r;uintptr_t identity=0;const auto opaque=reinterpret_cast<uintptr_t>(&identity);
  x.after.voice.sequence.wave.control_slot_storage=x.after.voice.sequence.wave.output_value=opaque;
  Check(!GameSourceFeedbackExact(r,x)&&GameSourceWavePrefixValid(x),"F4 coherent opaque mode identity may validate; never dereferenced");}
 {auto x=r;x.after.voice.sequence.wave.output_value=1;Check(!GameSourceWavePrefixValid(x),"F4 output is address not mode literal");}
 {auto x=r;x.after.voice.sequence.wave.output_value=w.result_pointer;Check(!GameSourceWavePrefixValid(x),"F4 output not returned wave pointer");}
 {auto x=r;x.after.voice.sequence.wave.result_pointer=0;Check(!GameSourceWavePrefixValid(x),"F4 v2 null positive pointer guard");}
 {auto x=r;x.after.bank.host_address=UINTPTR_MAX;Check(!GameSourceWavePrefixValid(x),"F4 v2 host plus offset overflow");}
 {auto x=r;x.after.bank.host_address=UINTPTR_MAX-7;x.after.bank.offset=7;Check(!GameSourceWavePrefixValid(x),"F4 v2 record offset overflow");}
 {auto x=r;x.outcome=6;Check(!GameSourceWavePrefixValid(x)&&!GameSourceControlPrefixValid(x),"F4 relabeling is not issuance/legacy proof");}
 {auto x=r;auto& q=x.after.voice.sequence;q.control.child_returned=true;Check(!GameSourceWavePrefixValid(x),"F4 fabricated child return rejected");}
 {auto x=r;x.after.voice.sequence.port.known_halfwords|=Q(1)<<2;Check(!GameSourceWavePrefixValid(x),"F4 fabricated unknown port value knowledge rejected");}
 {auto x=r;x.after.voice.sequence.processor.control_returned=true;Check(!GameSourceWavePrefixValid(x),"F4 fabricated D0 control return rejected");}
 {auto x=r;x.owner.voice.sequence.wave.entered=true;Check(!GameSourceFeedbackExact(r,x)&&!GameSourceWavePrefixValid(x),"F4 owner nested default wave participates");}
 f.input.Unchanged();
}
#undef GAME_OPTIONS_WAVE_SCALARS
inline void Family5_CloseReinitializeAndLegacyDomains(){
 {Fixture f;const auto v1=Start(f);Ticks(f);const auto r=Service(f);Canonical(f,r);const auto saved=r.after.voice.sequence.wave;
  f.bank.Close();const auto closed=f.bank.SourceControl();
  Check(!closed.live&&!closed.bank_live&&!closed.voice.live&&!closed.voice.sequence.live&&!closed.voice.sequence.port.live&&!closed.voice.sequence.wave.borrow_live,"F5 actual close retires owners and borrow");
  Check(closed.voice.sequence.wave.result_pointer==saved.result_pointer&&closed.voice.sequence.wave.image_offset==7,"F5 retired observation preserves pointer/offset without dereferencing");
  Check(saved.borrow_live&&saved.result_pointer==reinterpret_cast<uintptr_t>(f.input.lease->bytes.data()+7+6272),"F5 saved receipt passive pointer identity only");
  NoAttempt(f,r.after);Ok(f.bank.InitializeSource17F0(),"F5 reinit");Ok(f.bank.Register(f.input.view),"F5 real reregister");
  const auto cold=f.bank.SourceControl();Check(cold.generation==2&&cold.owner!=r.after.owner,"F5 new source generation2");
  const auto v2=Start(f);Check(v2.after.voice.generation==2&&v2.after.voice.owner!=v1.after.voice.owner&&GameSourceWaveExact(v2.after.voice.sequence.wave,Wave{}),"F5 new voice detaches retired WAVE observation");
  Ticks(f);const auto again=Service(f);Canonical(f,again);Check(again.after.voice.sequence.generation==2&&again.after.voice.sequence.wave.borrow_live,"F5 generation2 actual new WAVE borrow");}
 for(U outcome=0;outcome<=6;++outcome){Fixture f;Read r;
  if(outcome==0){const auto before=f.bank.SourceControl();Check(Cut(f.bank.ReadOptionsFeedback(before,2,r),"56598"),"F5 old issuer unavailable");}
  else {r=Start(f);if(outcome>=2){Ticks(f);const auto before=f.bank.SourceControl();GameFsStatus status;
    switch(outcome){case 2:status=f.bank.ServiceOptionsVoice(before,r);break;case 3:status=f.bank.ServiceOptionsSequence(before,r);break;
     case 4:status=f.bank.ServiceOptionsPort(before,r);break;case 5:status=f.bank.ServiceOptionsProcessor(before,r);break;default:status=f.bank.ServiceOptionsControl(before,r);break;}
    Check(!status&&status.code==GameFsCode::Unavailable,"F5 genuine old API cut");}}
  Check(r.outcome==outcome&&Same(r.after,f.bank.SourceControl()),"F5 actual old result");Domain(r,outcome);
  Check(GameSourceWaveExact(r.after.voice.sequence.wave,Wave{}),"F5 old domains never invoke new WAVE helper");
  if(outcome==6){const auto& c=r.after.voice.sequence.control;Check(r.frontier==0x557B8&&c.wave_attempted&&!c.wave_output_known&&!c.wave_result_known&&!c.wave_returned&&!c.child_returned,"F5 old6 retains stub557B8");}
  f.input.Unchanged();}
}
} // namespace game_options_wave_regressions
inline void GameOptionsWaveRegressionChecks(){
 using namespace game_options_wave_regressions;
 Family1_GenuineOwnershipAndReturn();Family2_PreRegistrationDataBranches();Family3_LiveGatesAndMissingTicks();
 Family4_CompleteWaveAndTransportMutations();Family5_CloseReinitializeAndLegacyDomains();
}

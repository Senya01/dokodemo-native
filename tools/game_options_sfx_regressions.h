#pragma once
#include "recovered/game_scene_ui_native.h"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// SOURCE-only authorship, executed=0. Synthetic owning input and public Sound
// components only. A valid public transport is never an Options GUI issuer.
namespace game_options_sfx_regressions {
using namespace dokodemo::recovered;
using State=GameSourceSoundSnapshot;
using Read=GameSourceFeedbackRead;
inline void Check(bool ok,const char* id){if(!ok)throw std::runtime_error(id);}
struct Lease final:PackageStorageLease {
 std::vector<uint8_t> bytes,known;
 uint64_t generation=1,serial=1;
 bool live=true;
 explicit Lease(size_t size):bytes(size,0xA5),known(size,1){}
 bool Span(PackageStorageSpan& out,std::string& error)const override{
  if(!live){error="SFX test lease retired";return false;}
  out={};out.bytes=const_cast<uint8_t*>(bytes.data());out.initialized=const_cast<uint8_t*>(known.data());
  out.size=bytes.size();out.host_address=uint64_t(reinterpret_cast<uintptr_t>(bytes.data()));out.owner=out.host_address;
  out.generation=generation;out.serial=serial;out.source_base=0x4000;out.readable=out.writable=true;return true;
 }
};
struct Input {
 static constexpr uint32_t directory_end=6732,tail_size=191233;
 std::shared_ptr<Lease> lease=std::make_shared<Lease>(size_t(directory_end)+tail_size+14);
 PackageView image;
 explicit Input(uint32_t waveC=44100,uint32_t wave30=0){
  image=PackageStorage::Attach(lease,lease->bytes.size()).Subview(7,directory_end+tail_size);
  for(uint32_t i=0;i<directory_end+tail_size;++i)image.WriteU8(i,0);
  image.WriteLE32(0,0x44584753);image.WriteLE32(8,directory_end);image.WriteLE32(12,0x80000001);
  // Four genuine chunk directories. RGND41 starts40/stride56; SEQD owns B28.
  image.WriteLE32(16,0x444E4752);image.WriteLE32(20,2328);
  image.WriteLE32(28,1);image.WriteLE32(32,41);image.WriteLE32(36,40);
  for(uint32_t i=0;i<41;++i){const auto at=40+56*i;
   image.WriteLE32(at,9);image.WriteLE32(at+8,56);image.WriteU8(at+12,10);
   image.WriteU8(at+24,uint8_t(32+i));image.WriteU8(at+25,uint8_t(32+i));image.WriteLE32(at+52,30);
  }
  image.WriteLE32(2352,0x44514553);image.WriteLE32(2356,2216);
  image.WriteLE32(2364,1);image.WriteLE32(2368,2400);
  image.WriteLE32(2404,7);image.WriteLE32(2432,0xB28);
  image.WriteLE16(0xB28+8,0);image.WriteLE16(0xB28+10,1000);image.WriteLE32(0xB28+16,23);
  const std::array<uint8_t,9> body{{0xD0,1,0x64,0,1,0x64,2,0x40,0x96}};
  for(size_t i=0;i<body.size();++i)image.WriteU8(0xB3C+i,body[i]);
  image.WriteLE32(4576,0x45564157);image.WriteLE32(4580,2136);image.WriteLE32(4588,38);
  image.WriteLE32(6272,0);image.WriteU8(6280,3);image.WriteU8(6281,1);
  image.WriteLE32(6272+0x0C,waveC);image.WriteLE32(6272+0x20,0x73E04);
  image.WriteLE32(6272+0x24,UINT32_MAX);image.WriteLE32(6272+0x28,0x73E04);
  image.WriteLE32(6272+0x2C,0x640);image.WriteLE32(6272+0x30,wave30);
  image.WriteLE32(6720,0x454D414E);image.WriteLE32(6724,4);
 }
};
struct Fixture {
 Input input;
 GameSoundBank bank;
 Read started;
 explicit Fixture(uint32_t waveC=44100,uint32_t wave30=0):input(waveC,wave30){
  Check(bool(bank.InitializeSource17F0()),"SFX-setup-init");
  Check(bool(bank.Register(input.image)),"SFX-setup-register");
 }
 void Start(){const auto cold=bank.SourceControl();Check(bool(bank.ReadOptionsVoiceFeedback(cold,2,started)),"SFX-setup-voice");
  Check(GameSourceVoiceFeedbackValid(started),"SFX-setup-voice-contract");}
 void Ticks(){bank.ObserveOptionsWorkerTick();bank.ObserveOptionsWorkerTick();}
 Read Service(){Start();Ticks();Read out;const auto status=bank.ServiceOptionsSfx(bank.SourceControl(),out);
  Check(!status&&out.outcome==11,"SFX-service-finite-return");return out;}
};
inline void NoAttempt(GameSoundBank& bank,const State& expected,const char* id){
 const auto before=bank.SourceControl();Read out;out.attempted=true;out.outcome=77;out.after.owner=99;
 const auto status=bank.ServiceOptionsSfx(expected,out);
 Check(!status&&!out.attempted&&!out.outcome&&!out.after.owner&&GameSourceSoundSnapshotExact(before,bank.SourceControl()),id);
}
inline void Components(){
 GameNativeSfxSlotPool16 slots;GameNativeSfxControl16 control;
 Check(!slots.Snapshot().live&&!control.Snapshot().live,"SFX-component-detached");
 Check(!slots.Initialize(0,1)&&!slots.Initialize(1,0)&&!slots.Initialize(1,1,0)&&!slots.Initialize(1,1,17),"SFX-slot-admission");
 Check(bool(slots.Initialize(71,9)),"SFX-slot-init");const auto h=slots.Snapshot();
 Check(h.live&&h.owner&&h.generation==1&&h.sound_owner==71&&h.sound_generation==9&&h.count==16&&h.bytes==512
  &&h.zero_writes==128&&h.zero_readbacks==128&&h.id_writes==16&&h.header_zero_writes==26,"SFX-slot-clear");
 for(uint32_t i=0;i<16;++i){const auto s=slots.Read(i);Check(s.id==int32_t(i)&&!s.flags&&!s.word8&&!s.wordC&&!s.word1C&&!s.byte5,"SFX-slot-initial-record");}
 Check(!control.Initialize(0,9,h.base,16)&&!control.Initialize(71,0,h.base,16)&&!control.Initialize(71,9,0,16)
  &&!control.Initialize(71,9,h.base,0)&&!control.Initialize(71,9,h.base,17),"SFX-control-admission");
 Check(bool(control.Initialize(71,9,h.base,16)),"SFX-control-init");const auto d=control.Snapshot();
 Check(d.live&&d.platform_live&&d.owner&&d.generation==1&&d.base&&d.core&&d.core%64==0&&d.base!=d.core&&d.slot_base==h.base
  &&d.sound_owner==71&&d.sound_generation==9&&d.count==16&&d.bytes==1600&&d.zero_writes==400&&d.zero_readbacks==400
  &&d.grain==256&&d.voices==32&&!d.output&&d.rate==44100&&d.dry==1&&!d.wet&&!d.left&&!d.right,"SFX-control-platform");
 // Observe only the public core address; no assumed Core layout/size probe.
 for(uint32_t i=0;i<16;++i){GameNativeSfxControl16::Record r;Check(control.Read(i,r)&&!r.flags&&!r.word8&&!r.data&&!r.word10&&!r.word2C&&!r.word30,"SFX-control-record-clear");}
 GameNativeSfxControl16::Record invalid;std::array<uintptr_t,12> writes{};
 Check(!control.Read(16,invalid)&&!control.Command0(16,d.base,0x640,writes)&&!control.Command0(0,0,0x640,writes)
  &&!control.Initialize(71,9,h.base,16),"SFX-control-boundaries");
 int32_t range=0;Check(bool(slots.SetRange(1,0,16,range))&&range==16&&bool(slots.PublishRuntimeKind1()),"SFX-slot-config");
 for(uint32_t i=0;i<16;++i)Check(slots.Read(i).word1C==i,"SFX-slot-runtime-before-live-use");
 GameNativeSfxSlotPool16::Selection selection;int32_t id=-1;
 Check(bool(slots.Allocate(1,10,0,selection,id))&&id==0&&selection.free_reads==1&&slots.Read(0).flags==1,"SFX-slot-allocate");
 Check(bool(slots.Register(id,0x5AA90,h.base))&&bool(slots.Register(id,0x5AAC4,d.base))&&slots.Read(0).callback_readback,"SFX-slot-registrations");
 const uintptr_t full_data=reinterpret_cast<uintptr_t>(&writes);
 Check(bool(control.Command0(0,full_data,0x640,writes)),"SFX-component-command0");
 const std::array<uintptr_t,12> values{{4,full_data,0x640,12,0,0x40C,0,0xC0C,0,0xC0E,0,0}};
 Check(writes==values,"SFX-component-ordered-values");GameNativeSfxControl16::Record r;
 Check(control.Read(0,r)&&r.flags==0xC0E&&r.data==full_data&&r.word10==0x640&&!r.word8&&!r.word2C&&!r.word30,"SFX-component-command-readback");
 int32_t result=0;Check(bool(slots.FinishCommand0(0,result))&&result==3&&slots.Read(0).flags==3&&slots.Read(0).byte5==1,"SFX-component-source-return");
 const auto owner=d.owner;control.Close();slots.Close();
 Check(!control.Snapshot().live&&!control.Snapshot().platform_live&&!control.Snapshot().base&&!control.Snapshot().core
  &&!control.Snapshot().slot_base&&!control.Read(0,r)&&!control.Command0(0,full_data,0,writes)&&!slots.Snapshot().live,"SFX-component-close");
 Check(bool(slots.Initialize(71,10))&&slots.Snapshot().generation==2&&bool(control.Initialize(71,10,slots.Snapshot().base,16)),"SFX-component-regenerate");
 const auto regenerated=control.Snapshot();Check(regenerated.owner!=owner&&regenerated.generation==2&&regenerated.zero_writes==400&&regenerated.zero_readbacks==400,"SFX-component-generation");
 Check(control.Read(0,r)&&!r.data&&!r.flags&&!r.word10,"SFX-component-regenerated-clear");
}
inline void SuccessfulService(){
 struct Pitch {uint32_t wave;int32_t product,result,half;};
 // Independently calculated low32 products and signed HI/shift/correction
 // results. These do not assume native division by a sample rate.
 const Pitch cases[]={{0,0,0,0},{44100,180633600,4096,4096},{22050,90316800,2048,2048},{UINT32_MAX,-4096,0,0},{474628,1944076288,44083,-21453}};
 for(const auto& p:cases)for(const uint32_t offset:std::array<uint32_t,2>{{0,191232}}){
  Fixture f(p.wave,offset);const auto registered=f.bank.SourceControl();
  Check(registered.bank.offset==7&&registered.registration.tail.offset==7+Input::directory_end
   &&registered.registration.tail.size==Input::tail_size&&!registered.registration.word30,"SFX-service-owning-input");
  const auto r=f.Service();const auto& q=r.after.voice.sequence;const auto& a=q.sfx;
  Check(GameSourceSfxPrefixValid(r)&&GameSourceSoundSnapshotExact(r.after,f.bank.SourceControl()),"SFX-service-valid-current");
  Check(a.entered&&a.live&&a.platform_live&&a.alias_read&&a.index_read&&a.tail_read&&a.dirty_written&&a.current_readback
   &&a.handler_returned&&a.consumer_returned&&a.parent_returned&&a.port28_read&&a.port2C_attempted&&!a.port2C_known,"SFX-service-reached");
  Check(r.frontier==0x5B4C8&&a.frontier==0x5B4C8&&a.port_read_pc==0x5B4C8&&a.handler_result==3&&a.parent_result==q.slot.child_pointer
   &&a.parent_result!=uintptr_t(a.handler_result)&&a.parent_result!=uintptr_t(q.slot.allocator_result),"SFX-service-distinct-pointer-return");
  Check(a.count==16&&a.bytes==1600&&a.zero_writes==400&&a.zero_readbacks==400&&a.core%64==0&&a.grain==256&&a.voices==32
   &&a.rate==44100&&!a.output&&a.dry==1&&!a.wet&&!a.left&&!a.right,"SFX-service-owned-control");
  Check(a.slot_base==q.slot.base&&a.slot_storage==q.slot.storage&&a.runtime_index==0&&a.record_storage==a.base
   &&a.slot_flags_before==1&&a.slot_flags_after==3&&a.slot_byte5==1&&q.slot.allocation_flags==3,"SFX-service-runtime-alias");
  Check(a.tail_owner==registered.registration.tail.owner&&a.tail_generation==registered.registration.tail.generation
   &&a.tail_serial==registered.registration.tail.serial&&a.tail_offset==registered.registration.tail.offset&&a.tail_bytes==Input::tail_size
   &&a.tail_base==registered.registration.tail.host_address+registered.registration.tail.offset&&a.data_pointer==a.tail_base+offset,"SFX-service-same-bank-tail");
  Check(a.mode_slot==q.wave.control_slot_storage&&a.mode_slot==q.wave.output_value&&a.mode_slot==q.port_input.mode_slot
   &&a.mode_slot!=reinterpret_cast<uintptr_t>(&registered.registration.mode)&&a.tail_base!=a.mode_slot+8
   &&a.packet_storage==q.port_input.packet_storage&&a.packet_storage!=a.mode_slot&&a.child_wave==q.wave.result_pointer
   &&a.child_wave==registered.bank.host_address+registered.bank.offset+6272,"SFX-service-private-output-identities");
  Check(q.wave.record_offset==6272&&q.wave.wave==30&&q.control.region_count==41&&q.control.region_first==40
   &&q.control.region_offset==0x728&&q.control.walk_count==33&&q.processor.opcode==0xD0&&q.processor.operands==std::array<int32_t,4>{{64,0,64,4096}},"SFX-service-authentic-route");
  Check(q.port_input.words==std::array<uint32_t,13>{{0,10,3,1,0,0,0,0,0x73E04,UINT32_MAX,0x73E04,0,0x640}}
   &&q.slot.wave_word30==offset&&q.slot.packet_word6==offset,"SFX-service-packet-suffix");
  Check(a.dirty_flags==0xC0E&&!a.dirty_word8&&a.dirty_word10==0x640&&!a.dirty_word2C&&!a.dirty_word30,"SFX-service-dirty");
  Check(a.write_pc==std::array<uint32_t,12>{{0x5FDE8,0x5FDEC,0x5FE00,0x5FE08,0x5FE18,0x5FE20,0x5FE30,0x5FE38,0x5FE48,0x5FE50,0x5FE5C,0x5FE6C}}
   &&a.write_value==std::array<uintptr_t,12>{{4,a.data_pointer,0x640,12,0,0x40C,0,0xC0C,0,0xC0E,1,3}}&&a.writes==12,"SFX-service-ordered-writes");
  Check(a.pitch_before==4096&&a.wave_wordC==p.wave&&a.pitch_product==p.product&&a.pitch_result==p.result&&a.pitch_after==p.half,"SFX-service-signed-magic");
  Check(!r.owner.registration.word30&&!r.after.registration.word30&&q.current_refs==4
   &&r.after.voice.bank_refs_before==0&&r.after.voice.bank_refs_after==1&&q.refs_before==1&&q.refs_after==2
   &&q.track.refs_before==2&&q.track.refs_after==3&&q.release_before==3&&q.release_after==2
   &&q.control.refs_before==2&&q.control.refs_after==3&&q.slot.retain_before==3&&q.slot.retain_after==4,"SFX-service-reference-history");
  Check(q.slot.known_halfwords==UINT64_C(0xFFFFFF003)&&q.port.known_halfwords==UINT64_C(0xFFFFFF003),"SFX-service-known-mask");
  for(uint32_t i=2;i<12;++i)Check(!(q.slot.known_halfwords&(uint64_t(1)<<i))&&!(q.port.known_halfwords&(uint64_t(1)<<i)),"SFX-service-unknown-parameters");
  Check(!q.control.handler_returned&&!q.control.control_returned&&!q.processor.control_returned&&a.handler_pc==0x5FDA8
   &&a.handler_return_pc==0x5FE68&&a.handler_return_delay_pc==0x5FE6C&&a.consumer_return_pc==0x601AC&&a.parent_return_pc==0x5B2D4,"SFX-service-next-read-before-command8");
 }
}
inline void Drift(Fixture& f,const Read& issued,uintptr_t address,size_t width,const char* id){
 std::array<uint8_t,4> saved{};Check(address&&width<=saved.size(),"SFX-drift-input");
 auto* p=reinterpret_cast<uint8_t*>(address);std::memcpy(saved.data(),p,width);p[0]^=1;
 bool readback=false,valid=true;
 try{const auto now=f.bank.SourceControl();readback=now.voice.sequence.sfx.current_readback;auto changed=issued;changed.after=now;valid=GameSourceSfxPrefixValid(changed);}
 catch(...){std::memcpy(p,saved.data(),width);throw;}
 std::memcpy(p,saved.data(),width); // Restore owned backing BEFORE assertions.
 Check(!readback&&!valid,id);Check(GameSourceSoundSnapshotExact(issued.after,f.bank.SourceControl()),"SFX-drift-restored");
}
// Factories with pre-registration mutations preserve a genuine registered
// digest and distinguish an actual partial service from a rejected owner.
inline void MutatedInputCases(){
 for(uint32_t kind=0;kind<4;++kind){Input in;
  if(kind==0)in.image.WriteU8(0xB3C,0);
  if(kind==1)in.image.WriteLE32(6272+0x30,Input::tail_size);
  if(kind==2)in.image.WriteU8(6272+8,2);
  if(kind==3)in.image.WriteLE32(0x728+52,38);
  GameSoundBank bank;Check(bool(bank.InitializeSource17F0())&&bool(bank.Register(in.image)),"SFX-malformed-preregister");Read start;
  Check(bool(bank.ReadOptionsVoiceFeedback(bank.SourceControl(),2,start)),"SFX-malformed-real-voice");
  bank.ObserveOptionsWorkerTick();bank.ObserveOptionsWorkerTick();Read r;Check(!bank.ServiceOptionsSfx(bank.SourceControl(),r)&&r.outcome==11&&!GameSourceSfxPrefixValid(r),"SFX-malformed-partial-not-noattempt");
  Check(r.after.voice.services==1&&!r.after.voice.sequence.sfx.dirty_written,"SFX-malformed-no-command0-effects");
 }
}
inline void CurrentAndReplay(){
 for(const uint32_t offset:std::array<uint32_t,2>{{Input::tail_size,UINT32_MAX}}){Fixture f(44100,offset);const auto r=f.Service();const auto& a=r.after.voice.sequence.sfx;
  Check(a.entered&&a.alias_read&&a.index_read&&!a.tail_read&&!a.dirty_written&&!a.handler_returned&&!a.consumer_returned&&!a.parent_returned
   &&!GameSourceSfxPrefixValid(r)&&r.after.voice.sequence.current_refs==4,"SFX-malformed-tail-partial");
 }
 MutatedInputCases();
 Fixture ready;ready.Start();ready.Ticks();const auto expected=ready.bank.SourceControl();
 const auto bad=[&](auto edit){auto copy=expected;edit(copy);NoAttempt(ready.bank,copy,"SFX-preservice-expected-gate");};
 bad([](State& s){++s.owner;});bad([](State& s){++s.generation;});bad([](State& s){++s.queries;});
 bad([](State& s){++s.bank.offset;});bad([](State& s){++s.registration.tail.serial;});bad([](State& s){++s.registration.word30;});
 bad([](State& s){++s.se5.handle;});bad([](State& s){++s.voice.owner;});bad([](State& s){++s.voice.generation;});
 Read r;Check(!ready.bank.ServiceOptionsSfx(expected,r)&&GameSourceSfxPrefixValid(r),"SFX-current-genuine-service");
 const auto& a=r.after.voice.sequence.sfx;
 for(const uintptr_t off:std::array<uintptr_t,5>{{4,8,0x10,0x2C,0x30}})Drift(ready,r,a.record_storage+off,4,"SFX-drift-dirty-record");
 Drift(ready,r,a.slot_storage+4,1,"SFX-drift-slot-flags");Drift(ready,r,a.slot_storage+5,1,"SFX-drift-slot-byte5");
 Drift(ready,r,a.slot_storage+0x1C,4,"SFX-drift-slot-runtime-index");
 Drift(ready,r,r.after.voice.sequence.slot.child_pointer+0x2E,2,"SFX-drift-child-pitch");
 Drift(ready,r,a.packet_storage+6*sizeof(uint32_t),4,"SFX-drift-actual-packet-word6");
 // Replay has BOTH whole-owner and consumed-service guards. It is not an
 // isolated proof of owner authority, nor an Options GUI publication test.
 NoAttempt(ready.bank,expected,"SFX-replay-old-owner");NoAttempt(ready.bank,ready.bank.SourceControl(),"SFX-replay-current-owner");
 {Fixture f;f.Start();f.Ticks();const auto before=f.bank.SourceControl();f.input.image.WriteU8(Input::directory_end,1);
  Check(!f.bank.SourceControl().bank_live,"SFX-current-bank-byte-drift");NoAttempt(f.bank,before,"SFX-current-bank-gate");}
 {Fixture f;f.Start();f.Ticks();f.input.lease->known[7+Input::directory_end]=0;
  Check(!f.bank.SourceControl().bank_live,"SFX-current-bank-unknown-byte");NoAttempt(f.bank,f.bank.SourceControl(),"SFX-current-bank-unknown-gate");}
}
inline void TransportMatrix(){
 Fixture f;const auto issued=f.Service();const auto& original=issued.after.voice.sequence.sfx;
 Check(GameSourceSfxExact(original,original)&&GameSourceSfxPrefixValid(issued),"SFX-matrix-baseline");
 // These identities are opaque public transport. No private issuer is claimed.
 const auto mutate=[&](auto edit,bool accepted){auto changed=issued;edit(changed.after.voice.sequence.sfx);
  Check(!GameSourceSfxExact(original,changed.after.voice.sequence.sfx)&&!GameSourceFeedbackExact(issued,changed),"SFX-exact-every-scalar");
  Check(GameSourceSfxPrefixValid(changed)==accepted,"SFX-prefix-scalar-mutation");};
#define SFX_BOOL(F) mutate([](GameSourceSfxSnapshot& a){a.F=!a.F;},false)
#define SFX_SCALAR(F) mutate([](GameSourceSfxSnapshot& a){++a.F;},false)
 SFX_BOOL(entered);SFX_BOOL(live);SFX_BOOL(platform_live);SFX_BOOL(alias_read);SFX_BOOL(index_read);SFX_BOOL(tail_read);SFX_BOOL(dirty_written);
 SFX_BOOL(handler_returned);SFX_BOOL(consumer_returned);SFX_BOOL(parent_returned);SFX_BOOL(port28_read);SFX_BOOL(port2C_attempted);SFX_BOOL(port2C_known);SFX_BOOL(current_readback);
 mutate([](GameSourceSfxSnapshot& a){++a.owner;},true);mutate([](GameSourceSfxSnapshot& a){++a.generation;},true);
 SFX_SCALAR(sound_owner);SFX_SCALAR(sound_generation);SFX_SCALAR(tail_owner);SFX_SCALAR(tail_generation);SFX_SCALAR(tail_serial);SFX_SCALAR(tail_offset);SFX_SCALAR(tail_bytes);
 SFX_SCALAR(base);SFX_SCALAR(core);SFX_SCALAR(slot_base);SFX_SCALAR(slot_storage);SFX_SCALAR(record_storage);SFX_SCALAR(packet_storage);SFX_SCALAR(mode_slot);
 SFX_SCALAR(tail_base);SFX_SCALAR(data_pointer);SFX_SCALAR(child_wave);SFX_SCALAR(parent_result);
 SFX_SCALAR(count);SFX_SCALAR(bytes);SFX_SCALAR(zero_writes);SFX_SCALAR(zero_readbacks);SFX_SCALAR(grain);SFX_SCALAR(voices);SFX_SCALAR(output);SFX_SCALAR(rate);
 SFX_SCALAR(dry);SFX_SCALAR(wet);SFX_SCALAR(left);SFX_SCALAR(right);SFX_SCALAR(runtime_index);SFX_SCALAR(dirty_flags);SFX_SCALAR(dirty_word8);SFX_SCALAR(dirty_word10);
 SFX_SCALAR(dirty_word2C);SFX_SCALAR(dirty_word30);SFX_SCALAR(slot_flags_before);SFX_SCALAR(slot_flags_after);SFX_SCALAR(slot_byte5);SFX_SCALAR(wave_wordC);SFX_SCALAR(port_word28);
 SFX_SCALAR(writes);SFX_SCALAR(handler_pc);SFX_SCALAR(handler_return_pc);SFX_SCALAR(handler_return_delay_pc);SFX_SCALAR(consumer_return_pc);SFX_SCALAR(parent_return_pc);SFX_SCALAR(port_read_pc);SFX_SCALAR(frontier);
 SFX_SCALAR(slot_id);SFX_SCALAR(handler_result);SFX_SCALAR(pitch_before);SFX_SCALAR(pitch_product);SFX_SCALAR(pitch_result);SFX_SCALAR(pitch_after);
#undef SFX_BOOL
#undef SFX_SCALAR
 for(size_t i=0;i<12;++i){mutate([i](GameSourceSfxSnapshot& a){a.write_pc[i]^=1;},false);mutate([i](GameSourceSfxSnapshot& a){a.write_value[i]^=1;},false);}
 auto opaque=issued;auto& a=opaque.after.voice.sequence.sfx;
 a.owner+=100;a.generation+=100;a.core=a.core>UINTPTR_MAX-0x100000?a.core-0x100000:a.core+0x100000;
 a.base=a.base>UINTPTR_MAX-0x200000?a.base-0x200000:a.base+0x200000;a.record_storage=a.base;
 Check(GameSourceSfxPrefixValid(opaque)&&!GameSourceSfxExact(original,a),"SFX-opaque-coherent-owner-core-transport");
 for(size_t i=2;i<12;++i){auto unknown=issued;unknown.after.voice.sequence.slot.parameters[i]^=0x5A5A;
  Check(GameSourceSfxPrefixValid(unknown)&&!GameSourceFeedbackExact(issued,unknown),"SFX-unknown-halfword-transport");}
 const auto envelope=[&](auto edit){auto changed=issued;edit(changed);Check(!GameSourceSfxPrefixValid(changed),"SFX-prefix-envelope-mutation");};
 envelope([](Read& x){x.outcome=10;});envelope([](Read& x){x.frontier=0x6016C;});
 envelope([](Read& x){x.after.voice.sequence.current_refs=3;});envelope([](Read& x){x.after.voice.sequence.control.child_returned=false;});
 envelope([](Read& x){x.after.voice.sequence.slot.known_halfwords|=4;});envelope([](Read& x){++x.after.registration.tail.offset;});
 envelope([](Read& x){x.after.voice.sequence.sfx.core=x.after.voice.sequence.sfx.base;});
}
inline void LifecycleAndDomains(){
 Fixture f;Check(GameSourceSfxExact(f.bank.SourceControl().voice.sequence.sfx,GameSourceSfxSnapshot{}),"SFX-lifecycle-cold-default");
 const auto r=f.Service();const auto old=f.bank.SourceControl();const auto old_voice=old.voice;
 Check(!GameSourceFeedbackReachedValid(r)&&!GameSourceVoiceFeedbackValid(r)&&!GameSourceVoiceServiceValid(r)
  &&!GameSourceSequencePrefixValid(r)&&!GameSourcePortPrefixValid(r)&&!GameSourceProcessorPrefixValid(r)&&!GameSourceControlPrefixValid(r)
  &&!GameSourceWavePrefixValid(r)&&!GameSourcePort24PrefixValid(r)&&!GameSourceChildPrefixValid(r)&&!GameSourceSlotPrefixValid(r),"SFX-outcome11-disjoint-domains");
 // Real independently initialized historical runs, not edited projections.
 for(uint32_t domain=0;domain<=10;++domain){Fixture h;Read historical;
  if(domain==0){Check(!h.bank.ReadOptionsFeedback(h.bank.SourceControl(),2,historical)&&GameSourceFeedbackReachedValid(historical),"SFX-historical0");
   // Start from a genuine outcome0. Equal nondefault SFX copies in owner and
   // after must still fail its default-audit contract. This is public exact
   // comparison/transport validation, never evidence of private issuance.
   for(uint32_t kind=0;kind<2;++kind)for(uint32_t sides=1;sides<=3;++sides){auto injected=historical;
    auto payload=r.after.voice.sequence.sfx;if(kind==1){payload={};payload.writes=1;}
    if(sides&1)injected.owner.voice.sequence.sfx=payload;
    if(sides&2)injected.after.voice.sequence.sfx=payload;
    Check(!GameSourceFeedbackReachedValid(injected)&&!GameSourceFeedbackExact(historical,injected),"SFX-historical0-nondefault-injection");
    if(sides==3)Check(GameSourceSfxExact(injected.owner.voice.sequence.sfx,injected.after.voice.sequence.sfx),"SFX-historical0-equal-injected-copies");
   }
  }
  else {h.Start();historical=h.started;if(domain>1){h.Ticks();const auto s=h.bank.SourceControl();GameFsStatus status;
   switch(domain){case 2:status=h.bank.ServiceOptionsVoice(s,historical);break;case 3:status=h.bank.ServiceOptionsSequence(s,historical);break;
    case 4:status=h.bank.ServiceOptionsPort(s,historical);break;case 5:status=h.bank.ServiceOptionsProcessor(s,historical);break;
    case 6:status=h.bank.ServiceOptionsControl(s,historical);break;case 7:status=h.bank.ServiceOptionsWave(s,historical);break;
    case 8:status=h.bank.ServiceOptionsPort24(s,historical);break;case 9:status=h.bank.ServiceOptionsChild(s,historical);break;
    default:status=h.bank.ServiceOptionsSlot(s,historical);break;}
   Check(!status,"SFX-historical-finite-status");}}
  const bool genuine[]={GameSourceFeedbackReachedValid(historical),GameSourceVoiceFeedbackValid(historical),GameSourceVoiceServiceValid(historical),GameSourceSequencePrefixValid(historical),GameSourcePortPrefixValid(historical),GameSourceProcessorPrefixValid(historical),GameSourceControlPrefixValid(historical),GameSourceWavePrefixValid(historical),GameSourcePort24PrefixValid(historical),GameSourceChildPrefixValid(historical),GameSourceSlotPrefixValid(historical)};
  Check(historical.outcome==domain&&genuine[domain]&&!GameSourceSfxPrefixValid(historical)&&!historical.after.voice.sequence.sfx.entered,"SFX-real-historical-domain-refusal");
 }
 f.bank.Close();const auto detached=f.bank.SourceControl();
 const auto& retired=detached.voice.sequence.sfx;
 Check(!detached.live&&!detached.bank_live&&!detached.voice.live&&!retired.live&&!retired.platform_live&&!retired.current_readback
  &&retired.entered&&retired.owner==r.after.voice.sequence.sfx.owner&&retired.parent_result==r.after.voice.sequence.sfx.parent_result,"SFX-lifecycle-retired-history");
 NoAttempt(f.bank,old,"SFX-lifecycle-old-after-close");
 Check(bool(f.bank.InitializeSource17F0())&&bool(f.bank.Register(f.input.image)),"SFX-lifecycle-reinit");
 const auto fresh=f.bank.SourceControl();Check(fresh.owner!=old.owner&&fresh.generation==2&&!fresh.voice.live,"SFX-lifecycle-fresh-owner");
 NoAttempt(f.bank,old,"SFX-lifecycle-old-generation");f.Start();
 Check(GameSourceSfxExact(f.bank.SourceControl().voice.sequence.sfx,GameSourceSfxSnapshot{}),"SFX-lifecycle-new-voice-detached-default");
 f.Ticks();Read next;
 Check(!f.bank.ServiceOptionsSfx(f.bank.SourceControl(),next)&&GameSourceSfxPrefixValid(next),"SFX-lifecycle-second-service");
 Check(next.after.voice.owner!=old_voice.owner&&next.after.voice.generation==2&&next.after.voice.sequence.generation==2
  &&next.after.voice.sequence.sfx.generation==2&&next.after.voice.sequence.sfx.owner!=r.after.voice.sequence.sfx.owner,"SFX-lifecycle-second-generation");
 std::weak_ptr<Lease> weak=f.input.lease;f.input.image.Reset();f.input.lease.reset();Check(!weak.expired(),"SFX-lifecycle-owning-borrow");
 f.bank.Close();Check(weak.expired(),"SFX-lifecycle-borrow-release");
}
} // namespace game_options_sfx_regressions
inline void GameOptionsSfxRegressionChecks(){
 using namespace game_options_sfx_regressions;
 Components();SuccessfulService();CurrentAndReplay();TransportMatrix();LifecycleAndDomains();
}

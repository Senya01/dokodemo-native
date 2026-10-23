#pragma once
#include "recovered/game_scene_ui_native.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// SOURCE-only, executed=0. Synthetic input; no success provider/private authority.
namespace game_options_se5_regressions {
using namespace dokodemo::recovered;
using SoundState=GameSourceSoundSnapshot;
using FeedbackRead=GameSourceFeedbackRead;
inline void Require(bool ok,const char* id){if(!ok)throw std::runtime_error(id);}
inline bool SameView(const PackageViewDescription& a,const PackageViewDescription& b){
  return std::tie(a.owner,a.generation,a.serial,a.allocation_bytes,a.host_address,a.offset,a.size,a.source_address,a.allocation_source_base,a.readable,a.writable)
  ==std::tie(b.owner,b.generation,b.serial,b.allocation_bytes,b.host_address,b.offset,b.size,b.source_address,b.allocation_source_base,b.readable,b.writable);
}
inline bool SameRecord(const GameSourceSoundRecord& a,const GameSourceSoundRecord& b){
  return std::tie(a.name,a.context54,a.context58,a.resource,a.resource_state,a.vtable,a.bank_handle,a.bank_index,a.handle,a.gain,a.state18,a.state1C,a.state20,a.step,a.position,a.request,a.flags,a.rate,a.bank_attached,a.bank_handle_known,a.name_known,a.context_owner,a.context_generation)
  ==std::tie(b.name,b.context54,b.context58,b.resource,b.resource_state,b.vtable,b.bank_handle,b.bank_index,b.handle,b.gain,b.state18,b.state1C,b.state20,b.step,b.position,b.request,b.flags,b.rate,b.bank_attached,b.bank_handle_known,b.name_known,b.context_owner,b.context_generation);
}
inline bool SameRegistration(const GameSourceBankRegistration& a,const GameSourceBankRegistration& b){
  return std::tie(a.live,a.attached,a.coherent,a.registry,a.generation,a.serial,a.id,a.active,a.mode,a.state,a.writer,a.word0C,a.word30,a.initial_zero24_mask,a.ids,a.chunks,a.sizes,a.digest)
  ==std::tie(b.live,b.attached,b.coherent,b.registry,b.generation,b.serial,b.id,b.active,b.mode,b.state,b.writer,b.word0C,b.word30,b.initial_zero24_mask,b.ids,b.chunks,b.sizes,b.digest)
  &&SameView(a.image,b.image)&&SameView(a.tail,b.tail);
}
inline bool Same(const SoundState& a,const SoundState& b){
  return std::tie(a.live,a.cold_initialized,a.bank_live,a.failed,a.owner,a.generation,a.cold_receipt,a.id_writes,a.bank_bindings,a.queries,a.bgm_attempts,a.constructor,a.last_id_writer,a.se_clean_mask,a.bgm_clean_mask,a.automatic_phase,a.current_id,a.previous_id,a.pending_id,a.last_query_gain,a.automatic,a.feedback_attempts,a.feedback_writes,a.feedback_frontier,a.feedback_gain,a.feedback_failed)
  ==std::tie(b.live,b.cold_initialized,b.bank_live,b.failed,b.owner,b.generation,b.cold_receipt,b.id_writes,b.bank_bindings,b.queries,b.bgm_attempts,b.constructor,b.last_id_writer,b.se_clean_mask,b.bgm_clean_mask,b.automatic_phase,b.current_id,b.previous_id,b.pending_id,b.last_query_gain,b.automatic,b.feedback_attempts,b.feedback_writes,b.feedback_frontier,b.feedback_gain,b.feedback_failed)
  &&SameView(a.bank,b.bank)&&SameRegistration(a.registration,b.registration)&&SameRecord(a.se5,b.se5);
}
inline bool SameStatus(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
struct Lease final:PackageStorageLease {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(83,0xA5),known=std::vector<uint8_t>(83,1);
  uint64_t generation=1,serial=1;
  bool live=true;
  bool Span(PackageStorageSpan& s,std::string& error)const override{
  if(!live){error="SE5 synthetic lease retired";return false;}
  s={};s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
  s.host_address=uint64_t(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;
  s.generation=generation;s.serial=serial;s.source_base=0x2000;s.readable=s.writable=true;return true;
  }
};
struct Input {
  std::shared_ptr<Lease> lease=std::make_shared<Lease>();
  PackageView view;
  Input(){
  view=PackageStorage::Attach(lease,83).Subview(7,69);
  for(uint32_t i=0;i<69;++i)view.WriteU8(i,uint8_t(i+1));
  view.WriteLE32(0,0x44584753);view.WriteLE32(4,0);view.WriteLE32(8,64);view.WriteLE32(12,0x80000001);
  const uint32_t tags[]={0x444E4752,0x44514553,0x45564157,0x454D414E};
  for(uint32_t i=0;i<4;++i){view.WriteLE32(16+12*i,tags[i]);view.WriteLE32(20+12*i,4);}
  }
};
struct Fixture {
  Input input;
  GameSoundBank bank;
  SoundState cold;
  Fixture(){Require(bool(bank.InitializeSource17F0()),"SE5-SETUP-init");cold=bank.SourceControl();Require(bool(bank.Register(input.view)),"SE5-SETUP-register");}
};
inline SoundState ExpectedFeedback(SoundState s){
  s.feedback_attempts=1;s.feedback_writes=6;s.feedback_frontier=0x56598;s.feedback_gain=45;s.feedback_failed=true;
  s.se5.context_owner=s.owner;s.se5.context_generation=s.generation;s.se5.gain=45;s.se5.rate=1.0f;
  s.se5.request=6;s.se5.position=0;s.se5.resource_state=0;return s;
}
inline void Registration(){
  Fixture f;const auto s=f.bank.SourceControl();const auto& r=s.registration;
  Require(r.live&&r.attached&&r.coherent&&r.registry&&r.generation&&r.serial&&r.id>0&&r.id<64
  &&r.active==1&&r.mode==1&&r.state==0&&r.writer==0x4AB58
  &&!r.word0C&&!r.word30&&r.initial_zero24_mask==7&&r.ids==std::array<int32_t,4>{r.id,r.id,r.id,r.id},"SE5-REG-01");
  Require(r.chunks==std::array<uint32_t,4>{24,48,36,60}&&r.sizes==std::array<uint32_t,4>{4,4,4,4}
  &&SameView(r.image,f.input.view.Describe())&&SameView(r.tail,f.input.view.Subview(64,5).Describe())
  &&r.digest==f.input.view.InitializedDigest(),"SE5-REG-01");
  auto expected=f.cold;expected.bank_live=true;expected.bank_bindings=1;expected.bank=f.input.view.Describe();expected.registration=r;
  expected.se_clean_mask=0x1F;expected.se5.bank_index=5;expected.se5.bank_attached=true;expected.se5.bank_handle_known=true;
  expected.se5.name_known=false;expected.se5.bank_handle=r.id;
  Require(Same(s,expected)&&s.se5.handle==-1&&s.current_id==-1&&s.last_id_writer==0x18E4,"SE5-REG-01");
  const auto m=f.bank.Snapshot();
  Require(m.metadata_ready&&!m.mixer_ready&&m.image_bytes==69&&m.chunks==4&&m.pool_capacity==64
  &&m.regions&&m.waves&&m.sequences&&m.names&&m.native_metadata_slot==r.id&&!m.playback_requests
  &&m.last_playback_result==-1,"SE5-REG-01");
  Input other;const auto duplicate=f.bank.Register(other.view);
  Require(duplicate.code==GameFsCode::Busy&&Same(s,f.bank.SourceControl()),"SE5-REG-02");
  std::weak_ptr<Lease> weak=f.input.lease;f.input.view.Reset();f.input.lease.reset();
  Require(!weak.expired()&&Same(s,f.bank.SourceControl()),"SE5-REG-03");
  f.bank.Close();Require(weak.expired(),"SE5-REG-03");
  GameSoundBank metadata;Input only;
  Require(bool(metadata.Register(only.view))&&metadata.Snapshot().metadata_ready,"SE5-REG-04");
  Require(bool(metadata.InitializeSource17F0()),"SE5-REG-04");const auto empty=metadata.SourceControl();FeedbackRead denied;
  Require(!metadata.ReadOptionsFeedback(empty,2,denied)&&!empty.registration.live&&!empty.bank_live
  &&!denied.attempted&&Same(empty,metadata.SourceControl()),"SE5-REG-04");
}
inline void Corruptions(const FeedbackRead& issued){
  // Synthetic receipt copies, no private authority.
  struct Case{const char* id;void(*edit)(FeedbackRead&);};
#define SE5_BAD(ID,EDIT) {"SE5-RC-" #ID,[](FeedbackRead& r){EDIT;}}
  const Case cases[]={
  SE5_BAD(01,r.attempted=false),SE5_BAD(02,r.bank_attached=false),
  SE5_BAD(03,r.bank_handle_known=false),SE5_BAD(04,r.read_returned=false),
  SE5_BAD(05,r.getter_returned=false),SE5_BAD(06,r.start_returned=true),
  SE5_BAD(07,r.handle_stored=true),SE5_BAD(08,r.feedback_returned=true),
  SE5_BAD(09,r.context_index=4),SE5_BAD(10,r.read_pc=0x4ACA0),
  SE5_BAD(11,r.frontier=0x56930),SE5_BAD(12,r.selector=1),
  SE5_BAD(13,r.gain=105),SE5_BAD(14,r.gain=128),
  SE5_BAD(15,r.params_written=5),SE5_BAD(16,r.virtual_target=0),
  SE5_BAD(17,r.wrapper=0),SE5_BAD(18,r.issuer=0),
  SE5_BAD(19,r.bank_handle=-1),SE5_BAD(20,r.group=1),
  SE5_BAD(21,r.event=5),SE5_BAD(22,r.extra=1),
  SE5_BAD(23,r.mode=0),SE5_BAD(24,r.rate=0.5f),
  SE5_BAD(25,++r.context_owner),SE5_BAD(26,++r.context_generation),
  SE5_BAD(27,r.owner.registration.live=false),SE5_BAD(28,r.owner.registration.coherent=false),
  SE5_BAD(29,r.owner.registration.attached=false),SE5_BAD(30,r.owner.registration.writer=0),
  SE5_BAD(31,++r.owner.registration.id),SE5_BAD(32,r.owner.feedback_attempts=1),
  SE5_BAD(33,r.owner.feedback_writes=1),SE5_BAD(34,r.owner.feedback_failed=true),
  SE5_BAD(35,r.after.se5.handle=r.bank_handle),SE5_BAD(36,r.after.current_id=r.bank_handle),
  SE5_BAD(37,++r.after.queries),SE5_BAD(38,++r.after.last_query_gain),
  SE5_BAD(39,++r.after.id_writes),SE5_BAD(40,++r.after.bgm_attempts),
  SE5_BAD(41,r.after.bgm_clean_mask=0),SE5_BAD(42,r.after.se5.context_owner=0),
  SE5_BAD(43,r.after.se5.context_generation=0),SE5_BAD(44,r.after.se5.request=0),
  SE5_BAD(45,r.after.se5.position=1),SE5_BAD(46,r.after.se5.resource_state=1),
  SE5_BAD(47,r.after.se5.flags=1),SE5_BAD(48,r.after.feedback_attempts=2),
  SE5_BAD(49,r.after.feedback_writes=5),SE5_BAD(50,r.after.feedback_failed=false),
  SE5_BAD(51,++r.after.registration.serial),SE5_BAD(52,++r.after.registration.generation),
  SE5_BAD(53,++r.after.registration.image.offset),SE5_BAD(54,--r.after.registration.tail.size),
  SE5_BAD(55,++r.after.registration.chunks[2]),SE5_BAD(56,++r.after.registration.sizes[1]),
  SE5_BAD(57,r.after.registration.digest[0]^=1),SE5_BAD(58,r.after.registration.mode=0),
  SE5_BAD(59,r.after.registration.active=0),SE5_BAD(60,r.after.registration.state=1),
  SE5_BAD(61,r.after.registration.word0C=1),SE5_BAD(62,r.after.registration.word30=1),
  SE5_BAD(63,r.after.registration.initial_zero24_mask=0),SE5_BAD(64,++r.after.registration.ids[3])
  };
#undef SE5_BAD
  Require(GameSourceFeedbackReachedValid(issued),"SE5-FEED-02");
  for(const auto& c:cases){auto r=issued;c.edit(r);Require(!GameSourceFeedbackReachedValid(r),c.id);}
}
inline void Feedback(){
  Fixture f;const auto before=f.bank.SourceControl();GameSourceSoundGainReceipt gain;
  Require(bool(f.bank.QuerySourceGain(before,105,gain)),"SE5-BGM-01");
  auto queried=before;++queried.queries;queried.last_query_gain=105;
  Require(Same(gain.before,before)&&Same(gain.after,queried)&&Same(f.bank.SourceControl(),queried)
  &&gain.read_pc==0x4AFC8&&gain.return_pc==0x4AFE0&&gain.id==-1&&gain.value==105
  &&gain.returned&&gain.negative_guard&&!gain.dispatched&&GameSourceSoundGainValid(gain),"SE5-BGM-01");
  FeedbackRead receipt;const auto failure=f.bank.ReadOptionsFeedback(queried,2,receipt);
  Require(failure.code==GameFsCode::Unavailable&&failure.detail.find("56598")!=std::string::npos,"SE5-FEED-01");
  Require(Same(receipt.owner,queried)&&Same(receipt.after,ExpectedFeedback(queried))
  &&Same(f.bank.SourceControl(),ExpectedFeedback(queried))&&receipt.context_index==5&&receipt.read_pc==0x4AC84
  &&receipt.attempted&&receipt.bank_attached&&receipt.bank_handle_known&&receipt.read_returned&&receipt.getter_returned
  &&receipt.frontier==0x56598&&receipt.selector==0&&receipt.gain==45&&receipt.params_written==6
  &&receipt.virtual_target==0x4AF54&&receipt.wrapper==0x56930&&receipt.issuer==0x56598
  &&receipt.bank_handle==queried.registration.id&&receipt.group==0&&receipt.event==6&&receipt.extra==0
  &&receipt.mode==2&&receipt.rate==1.0f&&receipt.context_owner==queried.owner&&receipt.context_generation==queried.generation
  &&!receipt.start_returned&&!receipt.handle_stored&&!receipt.feedback_returned,"SE5-FEED-01");
  Corruptions(receipt);
  const auto after=f.bank.SourceControl();FeedbackRead again;
  const auto replay=f.bank.ReadOptionsFeedback(queried,3,again);
  Require(SameStatus(failure,replay)&&GameSourceFeedbackExact(receipt,again)&&Same(after,f.bank.SourceControl()),"SE5-TERM-01");
  const auto meta=f.bank.Snapshot();Require(!meta.mixer_ready&&!meta.playback_requests&&meta.last_playback_result==-1,"SE5-TERM-01");
  Require(!f.bank.ReadOptionsFeedback(before,2,again)&&!again.attempted&&Same(after,f.bank.SourceControl()),"SE5-TERM-03");
  Require(SameStatus(failure,f.bank.ReadOptionsFeedback(after,2,again))&&GameSourceFeedbackExact(receipt,again)
  &&Same(after,f.bank.SourceControl()),"SE5-TERM-01");
  f.bank.Close();const auto closed=f.bank.SourceControl();
  Require(!f.bank.ReadOptionsFeedback(after,2,again)&&!again.attempted&&!again.read_returned&&!closed.live&&!closed.bank_live
  &&!closed.registration.live&&!closed.se5.bank_handle_known&&Same(closed,f.bank.SourceControl()),"SE5-TERM-02");
  for(unsigned i=0;i<3;++i){Fixture stale;FeedbackRead r;const auto live=stale.bank.SourceControl();
  Require(!stale.bank.ReadOptionsFeedback(live,2,r)&&r.frontier==0x56598,"SE5-TERM-04");
  auto expected=stale.bank.SourceControl();expected.bank_live=false;
  if(i==0)++stale.input.lease->generation;else if(i==1)stale.input.view.WriteU8(64,0);else stale.input.lease->known[75]=0;
  Require(!stale.bank.ReadOptionsFeedback(live,2,r)&&!r.attempted&&Same(expected,stale.bank.SourceControl()),"SE5-TERM-04");
  }
}
inline void Gates(){
  Fixture f;const auto s=f.bank.SourceControl();
  for(uint32_t root:{0u,3u,UINT32_MAX}){
  FeedbackRead out;Require(!f.bank.ReadOptionsFeedback(s,root,out)&&!out.attempted&&Same(s,f.bank.SourceControl()),"SE5-GATE-01");
  }
  for(unsigned i=0;i<6;++i){auto wrong=s;
  switch(i){case 0:++wrong.owner;break;case 1:++wrong.generation;break;case 2:++wrong.registration.serial;break;
      case 3:wrong.se5.bank_handle=-1;break;case 4:wrong.current_id=s.registration.id;break;case 5:wrong.registration.digest[0]^=1;break;}
  FeedbackRead out;Require(!f.bank.ReadOptionsFeedback(wrong,2,out)&&!out.attempted&&Same(s,f.bank.SourceControl()),"SE5-GATE-02");
  }
  Fixture other;const auto foreign=other.bank.SourceControl();FeedbackRead out;
  Require(!other.bank.ReadOptionsFeedback(s,2,out)&&!out.attempted&&Same(foreign,other.bank.SourceControl()),"SE5-GATE-03");
  f.bank.Close();auto closed=f.bank.SourceControl();
  Require(!f.bank.ReadOptionsFeedback(s,2,out)&&!out.attempted&&!closed.live&&!closed.bank_live&&Same(closed,f.bank.SourceControl()),"SE5-LIVE-01");
  Require(bool(f.bank.InitializeSource17F0())&&bool(f.bank.Register(f.input.view)),"SE5-LIVE-02");const auto fresh=f.bank.SourceControl();
  Require(fresh.registration.id==s.registration.id&&fresh.registration.registry==s.registration.registry
  &&fresh.registration.generation!=s.registration.generation&&fresh.registration.serial>s.registration.serial
  &&fresh.owner!=s.owner&&fresh.generation!=s.generation,"SE5-LIVE-02");
  Require(!f.bank.ReadOptionsFeedback(s,2,out)&&!out.attempted&&Same(fresh,f.bank.SourceControl()),"SE5-LIVE-02");
  Require(!f.bank.ReadOptionsFeedback(fresh,2,out)&&out.frontier==0x56598&&GameSourceFeedbackReachedValid(out),"SE5-LIVE-02");
}
inline void Invalidation(){
  for(unsigned i=0;i<3;++i){Fixture f;const auto before=f.bank.SourceControl();
  switch(i){case 0:f.input.lease->live=false;break;case 1:++f.input.lease->generation;break;
      case 2:++f.input.lease->serial;break;}
  const auto stale=f.bank.SourceControl();auto expected=before;expected.bank_live=false;FeedbackRead out;
  Require(Same(stale,expected)&&!f.bank.ReadOptionsFeedback(before,2,out)&&!out.attempted&&Same(stale,f.bank.SourceControl()),"SE5-LIVE-03");
  GameSourceSoundGainReceipt gain;Require(!f.bank.QuerySourceGain(before,105,gain)&&!gain.returned&&Same(stale,f.bank.SourceControl()),"SE5-LIVE-03");
  }
  // F1: aliased payload/knowledge mutations.
  for(unsigned i=0;i<6;++i){Fixture f;const auto before=f.bank.SourceControl();
  const uint32_t offsets[]={0,16,20,24,64};
  if(i<5){const auto at=offsets[i];f.input.view.WriteU8(at,uint8_t(f.input.view.ReadU8(at)^0x40));}
  else f.input.lease->known[7+68]=0;
  auto expected=before;expected.bank_live=false;const auto invalid=f.bank.SourceControl();FeedbackRead out;
  Require(Same(invalid,expected)&&!f.bank.ReadOptionsFeedback(before,2,out)&&!out.attempted&&Same(invalid,f.bank.SourceControl()),"SE5-LIVE-06");
  }
  Fixture bounded;const auto before=bounded.bank.SourceControl();bounded.input.lease->bytes[0]^=1;bounded.input.lease->known[82]=0;
  Require(Same(before,bounded.bank.SourceControl()),"SE5-LIVE-07");
}
inline void Malformed(){
  for(unsigned i=0;i<10;++i){Input input;GameSoundBank bank;Require(bool(bank.InitializeSource17F0()),"SE5-BAD-01");const auto before=bank.SourceControl();
  switch(i){case 0:input.view.WriteLE32(0,0);break;case 1:input.view.WriteLE32(8,15);break;
  case 2:input.view.WriteLE32(8,70);break;case 3:input.view.WriteLE32(8,59);break;
  case 4:input.view.WriteLE32(20,UINT32_MAX);break;case 5:input.view.WriteLE32(16,0);break;
  case 6:input.view.WriteLE32(28,0x444E4752);break;case 7:input.view=input.view.Subview(0,64);break;
  case 8:input.lease->known[7+24]=0;break;case 9:input.lease->known[7+68]=0;break;}
  const auto rejected=bank.Register(input.view);const auto after=bank.SourceControl();FeedbackRead out;
  Require(!rejected&&!bank.Snapshot().metadata_ready&&Same(before,after)&&!after.registration.live
      &&!after.bank_bindings&&!bank.ReadOptionsFeedback(after,2,out)&&!out.attempted,"SE5-BAD-01");
  }
  Input stale;++stale.lease->generation;GameSoundBank bank;Require(bool(bank.InitializeSource17F0()),"SE5-BAD-02");const auto before=bank.SourceControl();
  Require(!bank.Register(stale.view)&&Same(before,bank.SourceControl())&&!bank.Snapshot().metadata_ready,"SE5-BAD-02");
}
inline void Legacy(){
  Input input;GameSourceSoundControl legacy;Require(bool(legacy.Initialize17F0())&&bool(legacy.BindSe5(input.view)),"SE5-LEGACY-01");
  const auto s=legacy.Snapshot();FeedbackRead out;
  const auto stopped=legacy.Feedback4AC38(s,out);
  Require(!stopped&&stopped.detail.find("4AC84")!=std::string::npos&&out.attempted&&out.bank_attached
  &&!out.bank_handle_known&&!out.read_returned&&!s.se5.bank_handle_known&&!s.registration.live&&Same(s,legacy.Snapshot()),"SE5-LEGACY-01");
  out={};Require(!legacy.Feedback4AC38(s,2,out)&&!out.attempted&&Same(s,legacy.Snapshot()),"SE5-LEGACY-02");
  Require(!legacy.BindSe5(input.view)&&Same(s,legacy.Snapshot()),"SE5-LEGACY-02");
}
}

inline void GameOptionsSe5RegressionChecks(){
  namespace s5=game_options_se5_regressions;
  s5::Registration();s5::Feedback();s5::Gates();s5::Invalidation();s5::Malformed();s5::Legacy();
}

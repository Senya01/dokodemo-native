#pragma once
#include "../src/game73_selected.h"
#include "game_sound_component_regressions.h"
#include <tuple>

// Synthetic fixtures ONLY: no original game audio, firmware, device or audible
// equivalence credit. ADSR literals: independently derived numerical contract in
// external functional-first/game73-adsr-spec-v3; no reference C++ copied/ported.
namespace dokodemo::game_sound_selected_checks_detail {
using audio::PcmClip;
using Code=audio::SelectedSoundCode;
using Trace=audio::SelectedSoundTrace;
using Phase=audio::SelectedEnvelopePhase;
using Envelope=audio::SimpleEnvelope80FF5FC0;
using game_sound_codec_checks_detail::Check;
using game_sound_codec_checks_detail::Lease;
using game_sound_codec_checks_detail::Same;
using recovered::PackageStorage;
using recovered::PackageView;
inline auto TraceKey(const Trace& t) {
  return std::tie(t.group,t.entry,t.volume,t.profile_gain,t.effective_parameter,t.master_left,t.master_right,
    t.sequence_offset,t.command_bytes,t.events,t.region_index,t.wave_index,
    t.wave_offset,t.wave_bytes,t.pitch4096,t.combined_volume,t.base_gain,
    t.dry_left,t.dry_right,t.envelope1,t.envelope2,t.first_end_block,
    t.storage_frames,t.input_frames,t.output_frames,t.height_before_natural_end,
    t.phase_before_natural_end,t.sequence_ended,t.key_off_at_ff,t.reference_flag_policy);
}
inline Trace Sentinel() {
  Trace t;
  t.group=t.entry=t.volume=t.profile_gain=t.effective_parameter=-7;
  t.master_left=-5;t.master_right=-6;
  t.sequence_offset=t.command_bytes=t.events=t.region_index=t.wave_index=71;
  t.wave_offset=t.wave_bytes=t.pitch4096=t.combined_volume=t.base_gain=72;
  t.dry_left=t.dry_right=t.envelope1=t.envelope2=t.first_end_block=73;
  t.storage_frames=t.input_frames=t.output_frames=99;
  t.height_before_natural_end=77;t.phase_before_natural_end=Phase::Release;
  t.sequence_ended=t.key_off_at_ff=t.reference_flag_policy=true;
  return t;
}
inline bool SameEnvelope(const audio::SelectedEnvelopeSnapshot& a,
            const audio::SelectedEnvelopeSnapshot& b) {
  return std::tie(a.phase,a.height,a.ticks,a.on)==std::tie(b.phase,b.height,b.ticks,b.on);
}
inline void EnvelopeGoldens() {
  using P=Phase;
  Envelope e;
  const auto off=e.Snapshot();
  Check(off.phase==P::Off&&off.height==0&&!off.on,"envelope: fresh Off");
  Check(!e.KeyOff()&&SameEnvelope(off,e.Snapshot()),"envelope: idle keyoff rejected unchanged");
  Check(e.KeyOn(),"envelope: fresh keyon accepted");
  for (unsigned n=0;n<=40;++n) {
    const auto s=e.Snapshot();
    P phase=P::Sustain;std::int64_t h=1073725440;
    if(n==0){phase=P::PendingKeyOn;h=0;}
    else if(n<32){phase=P::StartupCounter;h=n-1;}
    else if(n<36){phase=P::Attack;const std::int64_t v[]{0,469762048,939524096,1056964608};h=v[n-32];}
    else if(n==36){phase=P::Decay;h=1073741824;}
    Check(s.phase==phase&&s.height==h&&s.on&&s.ticks==n,
       "envelope: REF-RAW-KEYON32 and active landmarks");
    Check(!e.KeyOn()&&SameEnvelope(s,e.Snapshot()),"envelope: active keyon rejects without reset");
    if(n<32)Check(Envelope::Scale(32767,h)==0,
          "envelope: startup counters quantize silent");
    e.Advance();
  }
  Check(e.KeyOff(),"envelope: sustain keyoff accepted");
  const std::int64_t release[]{1073725440,805289984,536854528,268419072,0};
  for(unsigned r=0;r<5;++r){
    const auto s=e.Snapshot();
    Check(!s.on&&s.height==release[r]&&s.phase==(r==4?P::Off:P::Release),
       "envelope: release values and overshoot clamp");
    Check(!e.KeyOff()&&SameEnvelope(s,e.Snapshot()),"envelope: repeated keyoff rejected unchanged");
    e.Advance();
  }
  Check(e.Snapshot().phase==P::Off&&e.Snapshot().height==0,"envelope: Off remains silent");
  const unsigned starts[]{0,1,2,31,32,33,34,35,36,37};
  const unsigned lengths[]{1,1,1,1,1,2,4,4,4,4};
  for(unsigned i=0;i<10;++i){
    Envelope x;Check(x.KeyOn(),"envelope: branch keyon");
    for(unsigned n=0;n<starts[i];++n)x.Advance();
    const auto h=x.Snapshot().height;
    Check(x.KeyOff()&&!x.Snapshot().on&&x.Snapshot().height==h&&x.Snapshot().phase==P::Release,
       "envelope: keyoff preserves startup/active height");
    for(unsigned r=1;r<=lengths[i];++r){x.Advance();
      Check((x.Snapshot().phase==P::Off)==(r==lengths[i]),"envelope: exact finite release length");}
    Check(x.Snapshot().height==0,"envelope: release ends at zero");
  }
  Envelope retrigger;Check(retrigger.KeyOn(),"envelope: retrigger seed");
  for(unsigned n=0;n<37;++n)retrigger.Advance();
  Check(retrigger.KeyOff(),"envelope: retrigger enters release");retrigger.Advance();
  Check(retrigger.KeyOn()&&retrigger.Snapshot().height==805289984&&
     retrigger.Snapshot().phase==P::PendingKeyOn&&retrigger.Snapshot().on,
     "envelope: release retrigger retains height");
  retrigger.Advance();Check(retrigger.Snapshot().height==0&&retrigger.Snapshot().phase==P::StartupCounter,
              "envelope: retrigger first advance resets height");
  retrigger.NaturalEnd();Check(retrigger.Snapshot().phase==P::Off&&!retrigger.Snapshot().on&&
                retrigger.Snapshot().height==0,"envelope: natural end clears active voice");
}
inline void ScaleGoldens() {
  const std::int64_t h[]{0,469762048,939524096,1056964608,1073741824,1073725440,805289984,536854528,268419072};
  const int positive[]{0,14336,28671,32255,32767,32767,24575,16384,8192};
  const int negative[]{0,-14336,-28672,-32256,-32768,-32768,-24576,-16384,-8192};
  for(unsigned i=0;i<9;++i){
    Check(Envelope::Scale(32767,h[i])==positive[i]&&
       Envelope::Scale(-32768,h[i])==negative[i],
       "scale: signed full-range literals");
  }
  Check(Envelope::Scale(1000,h[1])==438&&Envelope::Scale(-1000,h[1])==-437&&
     Envelope::Scale(1,h[7])==1&&Envelope::Scale(-1,h[7])==0&&
     Envelope::Scale(2,h[8])==1&&Envelope::Scale(-2,h[8])==0,
     "scale: signed half ties round toward positive infinity");
  Check(Envelope::Scale(-32768,(std::numeric_limits<std::int64_t>::min)())==0&&
     Envelope::Scale(32767,(std::numeric_limits<std::int64_t>::max)())==32767&&
     Envelope::Scale(-32768,1073741825)==-32768,
     "scale: declared native public height clamp0..M, no overflow");
}
// Synthetic SGXD: only region34/wave32 selected.
struct Fixture {
  static constexpr std::uint32_t rg=16,regions=40,sc=2336,group=sc+20,sequence=sc+48;
  static constexpr std::uint32_t wc=sc+96,waves=wc+16,body=waves+38*56;
  static constexpr std::uint32_t region=regions+34*56,wave=waves+32*56,commands=sequence+20;
  std::shared_ptr<Lease> lease;
  PackageView view;
  explicit Fixture(unsigned blocks=3) {
    std::vector<std::uint8_t> b(body+blocks*16,0);
    const auto u16=[&](unsigned at,unsigned x){for(unsigned i=0;i<2;++i)b[at+i]=std::uint8_t(x>>(8*i));};
    const auto u32=[&](unsigned at,unsigned x){for(unsigned i=0;i<4;++i)b[at+i]=std::uint8_t(x>>(8*i));};
    u32(0,0x44584753);u32(8,body);u32(12,blocks*16);
    u32(rg,0x444E4752);u32(rg+4,sc-rg-8);u32(rg+12,1);u32(rg+16,41);u32(rg+20,regions);
    for(unsigned i=0;i<41;++i)u32(regions+i*56+8,56); // source variable record-size field
    u32(region,9);b[region+24]=b[region+25]=66;b[region+28]=78;
    u16(region+30,4096);u16(region+32,2257);u16(region+36,4096);
    u32(region+40,0x80FF);u32(region+44,0x5FC0);b[region+48]=2;u32(region+52,32);
    u32(sc,0x44514553);u32(sc+4,wc-sc-8);u32(sc+12,1);u32(sc+16,group);u32(group+4,5);
    for(unsigned i=0;i<5;++i)u32(group+8+i*4,sequence);
    u16(sequence+10,1000);u16(sequence+12,4096);u16(sequence+14,4096);u32(sequence+16,23);
    const std::uint8_t code[]{0xD0,1,0x66,0,1,0x66,2,0x40,0x96,
                0xC0,1,0x66,0,2,0x35,0x20,0xC0,1,0x66,1,1,0,0xFF};
    std::copy(code,code+23,b.begin()+commands);
    u32(wc,0x45564157);u32(wc+4,body-wc-8);u32(wc+12,38);
    b[wave+8]=3;b[wave+9]=1;u32(wave+12,44100);u16(wave+24,4096);u16(wave+26,4096);
    u32(wave+32,471828);u32(wave+36,UINT32_MAX);u32(wave+40,471828);
    u32(wave+44,blocks*16);u32(wave+52,blocks*16);
    for(unsigned j=0;j<blocks;++j)std::fill(b.begin()+body+j*16+2,b.begin()+body+(j+1)*16,std::uint8_t(0x11));
    lease=std::make_shared<Lease>(std::move(b));view=PackageStorage::Attach(lease,lease->bytes.size());
  }
};
inline void Refuse(const PackageView& bank,Code code,int group=0,int entry=4,
          int volume=128,int gain=3,std::uint64_t limit=4096) {
  PcmClip pcm;pcm.sample_rate=12345;pcm.channels=2;pcm.samples={300,-301,302,-303};
  const auto saved=pcm;auto trace=Sentinel();const auto before=trace;
  const auto r=audio::RenderSelectedGame73(bank,group,entry,volume,gain,limit,pcm,trace);
  Check(!r&&r.code==code,"render: typed finite refusal");
  Check(Same(pcm,saved)&&TraceKey(trace)==TraceKey(before),"render: PCM and every trace field transactional");
}
inline PcmClip Render(Fixture& f,Trace& trace,int gain=3,int volume=128,std::uint64_t limit=4096) {
  PcmClip pcm;const auto r=audio::RenderSelectedGame73(f.view,0,4,volume,gain,limit,pcm,trace);
  if(!r)throw std::runtime_error("render: synthetic selected bank: "+r.detail);return pcm;
}
inline void Stereo(const PcmClip& pcm,std::size_t frame,int expected,const char* why) {
  Check(frame<pcm.FrameCount()&&pcm.channels==2&&pcm.samples[2*frame]==expected&&pcm.samples[2*frame+1]==expected,why);
}
inline void GainAndSelectedTimeline() {
  using audio::Game73EffectiveParameter;
  const int v[]{128,128,128,128,127,3,-3,-7,4,4,128};
  const int p[]{0,1,3,5,3,3,3,3,INT32_MAX,INT32_MIN,INT32_MAX};
  const int want[]{0,32,96,128,93,0,0,-3,128,-128,-32};
  for(unsigned i=0;i<11;++i)Check(Game73EffectiveParameter(v[i],p[i])==want[i],
    "gain: current level, truncation, low32 and clamp");
  Fixture f;Trace t;const auto pcm=Render(f,t);
  Check(t.group==0&&t.entry==4&&t.volume==128&&t.profile_gain==3&&t.effective_parameter==96&&
     t.region_index==34&&t.wave_index==32&&t.sequence_offset==Fixture::sequence&&t.command_bytes==23&&t.events==4&&
     t.wave_offset==Fixture::body&&t.wave_bytes==48&&t.pitch4096==2048&&t.envelope1==0x80FF&&t.envelope2==0x5FC0,
     "render: BCD, region34/wave32, pitch/envelope");
  Check(t.combined_volume==3520&&t.base_gain==1939&&t.dry_left==1028&&t.dry_right==1028&&
     t.master_left==128&&t.master_right==128,
     "gain: composed1028, not final1370 scale1027");
  Check(pcm.sample_rate==44100&&pcm.channels==2&&pcm.FrameCount()==168&&
     t.storage_frames==84&&t.input_frames==84&&t.output_frames==168&&t.first_end_block==UINT32_MAX&&
     t.sequence_ended&&!t.key_off_at_ff&&t.reference_flag_policy&&
     t.height_before_natural_end==1073725440&&t.phase_before_natural_end==Phase::Sustain,
     "render: extents, FF gate and natural end");
  for(unsigned n=0;n<33;++n)Stereo(pcm,n,0,"render: named startup silence");
  const int onset[]{449,899,1011,1028,1028};
  for(unsigned n=0;n<5;++n)Stereo(pcm,33+n,onset[n],"render: output-Hz envelope then dry floor");
  Stereo(pcm,167,1028,"render: last source sample held through odd output");
  const int gains[]{0,1,4,3};const unsigned dry[]{0,342,1370,1028};
  for(unsigned i=0;i<4;++i){Trace s;const auto a=Render(f,s,gains[i]);
    Check(s.profile_gain==gains[i]&&s.dry_left==dry[i]&&s.dry_right==dry[i]&&a.FrameCount()==168,
       "render: current gain changes, same timeline");
    Stereo(a,37,int(dry[i]),"render: curve2 stays linear at quantized sustain");}
  Trace low;Render(f,low,1,4);
  Check(low.effective_parameter==1&&low.combined_volume==3520&&low.base_gain==1939&&
     low.dry_left==10&&low.dry_right==10,"gain: channel32 AFTER base gives15 then dry10, not9");
  PcmClip asymmetric;Trace master;
  Check(bool(audio::RenderSelectedGame73(f.view,0,4,128,3,4096,asymmetric,master,64,128))&&
     master.master_left==64&&master.master_right==128&&master.dry_left==514&&master.dry_right==1028&&
     asymmetric.samples.size()>75&&asymmetric.samples[74]==514&&asymmetric.samples[75]==1028,"gain: captured masters scale channels independently");
  f.view.WriteU8(Fixture::body+17,1);f.view.WriteU8(Fixture::body+33,4);
  Trace flags;Check(Same(Render(f,flags),pcm)&&flags.input_frames==84,
                "selected flags: exact0/1/4 decode once");
  f.view.WriteU8(Fixture::body+17,7);Trace ended;const auto short_pcm=Render(f,ended,3,128,56);
  Check(ended.storage_frames==84&&ended.input_frames==28&&ended.output_frames==56&&ended.first_end_block==1&&
     short_pcm.FrameCount()==56&&std::equal(short_pcm.samples.begin(),short_pcm.samples.end(),pcm.samples.begin()),
     "flags: exact7 excludes block output");
  Refuse(f.view,Code::Limit,0,4,128,3,55);
  Fixture interp;for(unsigned j=0;j<3;++j)for(unsigned i=2;i<16;++i)interp.view.WriteU8(Fixture::body+j*16+i,0xF0);
  Trace it;const auto ip=Render(interp,it,4);
  Stereo(ip,37,-685,"selected interpolation: odd(-4096+0)/2 before envelope/dry");
  Stereo(ip,38,-1370,"selected interpolation: even input uses source exactly");
  Stereo(ip,39,-685,"selected interpolation: following odd transition");
  for(unsigned j=0;j<3;++j)interp.view.WriteU8(Fixture::body+j*16,0x0C);
  const auto tiny=Render(interp,it,4);
  Stereo(tiny,37,-1,"interpolation: floor odd -1/2, then dry -1");
}
inline void RefusalsAndOwnership() {
  Fixture f;Refuse(f.view,Code::Unsupported,1);Refuse(f.view,Code::Unsupported,0,3);
  Refuse(f.view,Code::Unsupported,0,4,-1);Refuse(f.view,Code::Unsupported,0,4,129);
  Refuse(f.view,Code::Unsupported,0,4,128,-1);
  Refuse(f.view,Code::Limit,0,4,128,3,0);Refuse(f.view,Code::Limit,0,4,128,3,167);
  const std::pair<unsigned,unsigned> malformed[]{
    {0,0},{8,15},{8,unsigned(f.lease->bytes.size()+1)},
    {Fixture::rg+4,UINT32_MAX},{Fixture::sc+16,UINT32_MAX},
    {Fixture::group+8+16,UINT32_MAX},{Fixture::sequence+16,UINT32_MAX},
    {Fixture::wave+44,47},{Fixture::wave+48,UINT32_MAX},{Fixture::wave+52,UINT32_MAX}};
  for(const auto m:malformed){Fixture x;x.view.WriteLE32(m.first,m.second);Refuse(x.view,Code::Malformed);}
  Fixture truncated;truncated.view.WriteLE32(Fixture::sequence+16,8);Refuse(truncated.view,Code::Malformed);
  for(const unsigned op:{0xA0u,0xD1u,0xF1u}){Fixture x;x.view.WriteU8(Fixture::commands, std::uint8_t(op));Refuse(x.view,Code::Unsupported);}
  Fixture operand;operand.view.WriteU8(Fixture::commands+1,0x11);Refuse(operand.view,Code::Unsupported);
  Fixture pair;pair.view.WriteLE32(Fixture::region+44,0x9FC3);Refuse(pair.view,Code::Unsupported);
  Fixture curve;curve.view.WriteU8(Fixture::region+48,0);Refuse(curve.view,Code::Unsupported);
  Fixture rate;rate.view.WriteLE32(Fixture::wave+12,22050);Refuse(rate.view,Code::Unsupported);
  Fixture stereo;stereo.view.WriteU8(Fixture::wave+9,2);Refuse(stereo.view,Code::Unsupported);
  for(const unsigned flag:{3u,6u}){Fixture x;x.view.WriteU8(Fixture::body+17,std::uint8_t(flag));Refuse(x.view,Code::Unsupported);}
  Refuse(PackageView{},Code::InvalidLease);
  for(const auto at:{Fixture::commands+22,Fixture::region+44,Fixture::body+47}){
    Fixture x;x.lease->known[at]=0;Refuse(x.view,Code::InvalidLease);
    x.lease->known[at]=2;Refuse(x.view,Code::InvalidLease);}
  using F=Lease::Fault;
  for(const auto fault:{F::Expired,F::Owner,F::Generation,F::Serial,F::Size,F::Base,F::Address,F::Unreadable}){
    Fixture x;x.lease->Arm(fault,1);Refuse(x.view,Code::InvalidLease);}
  Trace trace;const auto before=f.lease->calls;const auto owned=Render(f,trace);
  const auto acquisitions=f.lease->calls-before;Check(acquisitions>4,"selected lease: every parser/PCM read remains borrowed");
  Fixture late;late.lease->Arm(F::Expired,acquisitions/2);Refuse(late.view,Code::InvalidLease);
  const auto saved=owned;const auto trace_saved=trace;
  for(unsigned j=0;j<3;++j)for(unsigned i=2;i<16;++i)f.view.WriteU8(Fixture::body+j*16+i,0x22);
  Trace changed_trace;const auto changed=Render(f,changed_trace);
  Check(Same(owned,saved)&&!Same(changed,saved),"PCM: source mutation cannot alter old result");
  f.lease->live=false;PcmClip retained=owned;
  const auto r=audio::RenderSelectedGame73(f.view,0,4,128,3,4096,retained,trace);
  Check(!r&&r.code==Code::InvalidLease&&Same(retained,saved)&&TraceKey(trace)==TraceKey(trace_saved),
     "PCM: retirement preserves owned PCM and trace");
}
} // namespace dokodemo::game_sound_selected_checks_detail
namespace dokodemo {
inline void GameSoundSelectedRegressionChecks() {
  game_sound_selected_checks_detail::EnvelopeGoldens();
  game_sound_selected_checks_detail::ScaleGoldens();
  game_sound_selected_checks_detail::GainAndSelectedTimeline();
  game_sound_selected_checks_detail::RefusalsAndOwnership();
}
} // namespace dokodemo

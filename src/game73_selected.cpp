#include "game73_selected.h"
#include "ps_adpcm.h"
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

namespace dokodemo::audio {
namespace {
std::int64_t FloorDivide(std::int64_t n,std::int64_t d) noexcept {
    return n>=0?n/d:-((-n+d-1)/d);
}
struct Refusal {SelectedSoundCode code;const char* message;};
void Require(bool ok,const char* message,SelectedSoundCode code=SelectedSoundCode::Unsupported) {
    if(!ok)throw Refusal{code,message};
}
struct BankReader {
    const recovered::PackageView& v;std::uint64_t size;
    void Range(std::uint64_t at,std::uint64_t n) const {Require(at<=size&&n<=size-at,"SGXD bounded range",SelectedSoundCode::Malformed);}
    std::uint8_t B(std::uint64_t at) const {Range(at,1);return v.ReadU8(at);}
    std::uint16_t H(std::uint64_t at) const {Range(at,2);return v.ReadLE16(at);}
    std::uint32_t W(std::uint64_t at) const {Range(at,4);return v.ReadLE32(at);}
};
}
bool SimpleEnvelope80FF5FC0::KeyOn() noexcept {
    if(state_.on)return false;
    state_.on=true;state_.phase=SelectedEnvelopePhase::PendingKeyOn;
    state_.height=std::clamp(state_.height,std::int64_t{0},Maximum);state_.ticks=0;startup_=0;return true;
}
bool SimpleEnvelope80FF5FC0::KeyOff() noexcept {
    if(!state_.on)return false;
    state_.on=false;state_.phase=SelectedEnvelopePhase::Release;return true;
}
void SimpleEnvelope80FF5FC0::NaturalEnd() noexcept {state_.on=false;state_.height=0;state_.phase=SelectedEnvelopePhase::Off;}
void SimpleEnvelope80FF5FC0::Advance() noexcept {
    ++state_.ticks;
    switch(state_.phase) {
    case SelectedEnvelopePhase::Off:break;
    case SelectedEnvelopePhase::PendingKeyOn:state_.height=0;startup_=0;state_.phase=SelectedEnvelopePhase::StartupCounter;break;
    case SelectedEnvelopePhase::StartupCounter:
        if(++startup_==31){state_.height=0;state_.phase=SelectedEnvelopePhase::Attack;}
        else state_.height=startup_;break;
    case SelectedEnvelopePhase::Attack:
        state_.height+=state_.height<=805306368?469762048:117440512;
        if(state_.height>=Maximum){state_.height=Maximum;state_.phase=SelectedEnvelopePhase::Decay;}break;
    case SelectedEnvelopePhase::Decay:
        // This supported pair always enters Decay at Maximum and leaves after
        // one update. No generic exponential-curve implementation is implied.
        state_.height=Maximum-16384;state_.phase=SelectedEnvelopePhase::Sustain;break;
    case SelectedEnvelopePhase::Sustain:break;
    case SelectedEnvelopePhase::Release:
        if(state_.height<=268435456)NaturalEnd();else state_.height-=268435456;break;
    }
}
std::int16_t SimpleEnvelope80FF5FC0::Scale(std::int16_t x,std::int64_t h) noexcept {
    h=std::clamp(h,std::int64_t{0},Maximum);
    const auto gain=(h+16384)/32768;
    return static_cast<std::int16_t>(std::clamp(FloorDivide(std::int64_t(x)*gain+16384,32768),std::int64_t{-32768},std::int64_t{32767}));
}
std::int32_t Game73EffectiveParameter(std::int32_t v,std::int32_t g) noexcept {
    const auto bits=std::uint32_t(v/4)*std::uint32_t(g);
    const std::int64_t product=bits<=0x7FFFFFFFu?std::int64_t(bits):std::int64_t(bits)-0x100000000LL;
    return static_cast<std::int32_t>(std::clamp(product,std::int64_t{-128},std::int64_t{128}));
}
SelectedSoundResult RenderSelectedGame73(const recovered::PackageView& bank,
    std::int32_t group,std::int32_t entry,std::int32_t volume,std::int32_t gain,
    std::uint64_t limit,PcmClip& output,SelectedSoundTrace& trace,std::int32_t masterL,std::int32_t masterR) {
    try {
        Require(group==0&&entry==4&&volume>=0&&volume<=128,"Only selected GAME73 group0/entry4 supported");
        Require(masterL>=0&&masterL<=128&&masterR>=0&&masterR<=128,"Selected nonnegative master domain");
        Require(limit&&limit<=(1u<<22),"Selected PCM frame limit",SelectedSoundCode::Limit);
        BankReader r{bank,bank.Describe().size};Require(r.size<=64u*1024*1024,"SGXD size cap",SelectedSoundCode::Limit);
        Require(r.W(0)==0x44584753,"SGXD signature",SelectedSoundCode::Malformed);
        const std::uint64_t body=r.W(8);Require(body>=16&&body<=r.size,"SGXD metadata extent",SelectedSoundCode::Malformed);
        Require((r.W(12)&0x7FFFFFFFu)==r.size-body,"SGXD body extent",SelectedSoundCode::Malformed);
        std::uint64_t rg=0,seq=0,wv=0,rgEnd=0,seqEnd=0,wvEnd=0;unsigned chunks=0;
        for(std::uint64_t p=16;p<body;){
            Require(++chunks<=32&&body-p>=16,"SGXD chunk header",SelectedSoundCode::Malformed);
            const auto tag=r.W(p),n=r.W(p+4);Require(n>=8&&n<=body-p-8,"SGXD chunk size",SelectedSoundCode::Malformed);
            const auto end=p+8+n;
            if(tag==0x444E4752){Require(!rg,"Duplicate RGND",SelectedSoundCode::Malformed);rg=p;rgEnd=end;}
            if(tag==0x44514553){Require(!seq,"Duplicate SEQD",SelectedSoundCode::Malformed);seq=p;seqEnd=end;}
            if(tag==0x45564157){Require(!wv,"Duplicate WAVE",SelectedSoundCode::Malformed);wv=p;wvEnd=end;}
            p=end;
        }
        Require(rg&&seq&&wv,"Selected SGXD chunks missing",SelectedSoundCode::Malformed);
        Require(r.W(seq+12)>0&&seq+20<=seqEnd,"SEQD group0",SelectedSoundCode::Malformed);
        const std::uint64_t table=r.W(seq+16);Require(table>=seq+16&&table+28<=seqEnd&&r.W(table+4)>4,"SEQD entry4",SelectedSoundCode::Malformed);
        const std::uint64_t rec=r.W(table+8+4*4);Require(rec>=seq+16&&rec+20<=seqEnd,"SESQ record",SelectedSoundCode::Malformed);
        Require(r.H(rec+8)==0&&r.H(rec+10)!=0&&r.H(rec+12)==4096&&r.H(rec+14)==4096,"Selected format0/port unity");
        const std::uint64_t begin=rec+20,end=begin+r.W(rec+16);Require(end<=seqEnd&&end>begin&&end-begin<=128,"SESQ commands",SelectedSoundCode::Malformed);
        auto pc=begin;
        auto argument=[&](int fallback){
            if(pc==end||r.B(pc)>=0xA0)return fallback;
            const auto header=r.B(pc++);Require(header<0x10&&(header&15)<=4,"Finite positive packed-BCD operand");
            int value=0;for(unsigned n=0;n<unsigned(header&15);++n){Require(pc<end,"SESQ truncated BCD",SelectedSoundCode::Malformed);auto b=r.B(pc++);Require((b&15)<10&&(b>>4)<10,"SESQ BCD digit",SelectedSoundCode::Malformed);value=value*100+(b>>4)*10+(b&15);}return value;
        };
        int unitVolume=4096;unsigned event=0;bool ended=false;
        while(pc<end&&!ended){
            const auto op=r.B(pc++);++event;
            if(event==1){Require(op==0xD0,"Selected first D0");const int id=argument(0),program=argument(0),key=argument(0),scale=argument(4096);Require(id==66&&program==0&&key==66&&scale==4096,"Selected D0 domain");}
            else if(event==2||event==3){Require(op==0xC0,"Selected C0 order");const int id=argument(0),selector=argument(0),value=argument(0),time=argument(0),relative=argument(0);Require(id==66&&selector==int(event)-2&&time==0&&relative==0,"Selected immediate C0 domain");if(event==2){Require(value>=0&&value<=4096,"Linear volume range");unitVolume=value;}else Require(value==0,"Selected center pan");}
            else {Require(event==4&&op==0xFF,"Selected FF end");ended=true;}
        }
        Require(ended&&pc==end&&event==4,"Selected sequence completion");
        Require(r.W(rg+12)>0&&rg+24<=rgEnd,"RGND group0",SelectedSoundCode::Malformed);
        const auto count=r.W(rg+16);std::uint64_t at=r.W(rg+20),region=0;std::uint32_t regionIndex=0;unsigned matches=0;
        Require(count<=256&&at>=rg+16,"RGND region table",SelectedSoundCode::Malformed);
        for(std::uint32_t i=0;i<count;++i){Require(at+56<=rgEnd,"RGND record",SelectedSoundCode::Malformed);const auto bytes=r.W(at+8);Require(bytes>=56&&bytes<=rgEnd-at,"RGND record size",SelectedSoundCode::Malformed);if(std::int8_t(r.B(at+24))<=66&&std::int8_t(r.B(at+25))>=66){region=at;regionIndex=i;++matches;}at+=bytes;}
        Require(matches==1,"One selected region required");
        Require(r.W(region)==9&&r.B(region+28)==78&&r.B(region+29)==0&&r.H(region+30)==4096,"Selected region/pitch");
        Require(r.H(region+32)<=4096&&r.H(region+34)==0&&r.H(region+36)==4096&&r.H(region+38)==0&&r.B(region+48)==2&&r.B(region+49)==0,"Selected linear gain/center/dry only");
        Require(r.W(region+40)==0x80FF&&r.W(region+44)==0x5FC0,"Unsupported packed envelope");
        const auto index=r.W(region+52),waveCount=r.W(wv+12);Require(index<waveCount&&waveCount<=256,"WAVE index",SelectedSoundCode::Malformed);
        const auto wave=wv+16+std::uint64_t(index)*56;Require(wave+56<=wvEnd,"WAVE record",SelectedSoundCode::Malformed);
        Require(r.B(wave+8)==3&&r.B(wave+9)==1&&r.W(wave+12)==44100&&r.H(wave+24)==4096&&r.H(wave+26)==4096,"Selected mono PS-ADPCM44100/unity wave");
        const std::uint64_t bytes=r.W(wave+44),relative=r.W(wave+48),finish=r.W(wave+52);Require(bytes&&bytes%16==0&&relative+bytes==finish&&finish<=r.size-body,"WAVE payload",SelectedSoundCode::Malformed);
        Require(bytes/16<=(1u<<22)/28,"Storage decoder capacity",SelectedSoundCode::Limit);
        std::uint64_t input=bytes/16*28;std::uint32_t firstEnd=UINT32_MAX;
        for(std::uint64_t block=0;block<bytes/16;++block)if(r.B(body+relative+block*16+1)==7){input=block*28;firstEnd=std::uint32_t(block);break;}
        Require(input>0&&input<=limit/2,"Selected voice extent",SelectedSoundCode::Limit);
        PcmClip storage;auto decoded=DecodePsAdpcmMono(bank.Subview(body+relative,bytes),44100,1u<<22,storage);
        if(!decoded)return {decoded.code==PsAdpcmCode::InvalidLease?SelectedSoundCode::InvalidLease:SelectedSoundCode::Unsupported,decoded.detail};
        const auto parameter=Game73EffectiveParameter(volume,gain);Require(parameter>=0,"Selected nonnegative request gain");
        // Controller4 is unit volume; source channels2/3 multiply AFTER it.
        const auto base=(std::uint64_t(r.H(region+32))*unitVolume)/4096;
        const auto channelL=(masterL*parameter/128)*32,channelR=(masterR*parameter/128)*32;
        const auto dryL=((base*channelL)/4096)*2896/4096,dryR=((base*channelR)/4096)*2896/4096;
        PcmClip next;next.sample_rate=44100;next.channels=2;next.samples.reserve(static_cast<std::size_t>(input*4));
        SimpleEnvelope80FF5FC0 envelope;envelope.KeyOn();
        for(std::uint64_t i=0;i<input*2;++i){
            const auto a=storage.samples[std::size_t(i/2)],b=storage.samples[std::size_t((std::min)(i/2+1,input-1))];
            const auto value=std::int16_t(i%2?FloorDivide(std::int64_t(a)+b,2):a);
            const auto sample=SimpleEnvelope80FF5FC0::Scale(value,envelope.Snapshot().height);
            for(const auto dry:{dryL,dryR})next.samples.push_back(std::int16_t(std::clamp(FloorDivide(std::int64_t(sample)*std::int64_t(dry),4096),std::int64_t{-32768},std::int64_t{32767})));
            envelope.Advance();
        }
        SelectedSoundTrace t;t.group=group;t.entry=entry;t.volume=volume;t.profile_gain=gain;t.effective_parameter=parameter;t.master_left=masterL;t.master_right=masterR;
        t.sequence_offset=std::uint32_t(rec);t.command_bytes=std::uint32_t(end-begin);t.events=event;t.region_index=regionIndex;t.wave_index=index;
        t.wave_offset=std::uint32_t(body+relative);t.wave_bytes=std::uint32_t(bytes);t.pitch4096=2048;t.combined_volume=unitVolume;t.base_gain=std::uint32_t(base);t.dry_left=std::uint32_t(dryL);t.dry_right=std::uint32_t(dryR);
        t.envelope1=0x80FF;t.envelope2=0x5FC0;t.first_end_block=firstEnd;t.storage_frames=storage.FrameCount();t.input_frames=input;t.output_frames=input*2;
        t.height_before_natural_end=envelope.Snapshot().height;t.phase_before_natural_end=envelope.Snapshot().phase;t.sequence_ended=true;t.reference_flag_policy=true;
        envelope.NaturalEnd();(void)bank.Describe();output=std::move(next);trace=t;return {};
    }catch(const Refusal& e){return {e.code,e.message};}
    catch(const recovered::PackageStorageError& e){return {SelectedSoundCode::InvalidLease,e.what()};}
    catch(const std::bad_alloc&){return {SelectedSoundCode::NoMemory,"Selected PCM allocation failed"};}
}
} // namespace dokodemo::audio

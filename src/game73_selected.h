#pragma once
#include "audio_pcm.h"
#include "recovered/package_storage_native.h"
#include <cstdint>
#include <string>

namespace dokodemo::audio {
// Independently structured finite behavioral adapter. PPSSPP revision
// 1adf3c553d189539c8307ab30d6204ab3ebb1aac is a GPL2+ reference model,
// not linked/copied code or original firmware evidence. See research/functional/game73/SOUND_REFERENCE.md.
enum class SelectedEnvelopePhase { Off, PendingKeyOn, StartupCounter, Attack, Decay, Sustain, Release };
struct SelectedEnvelopeSnapshot {
    SelectedEnvelopePhase phase=SelectedEnvelopePhase::Off;
    std::int64_t height=0;
    std::uint64_t ticks=0;
    bool on=false;
};
class SimpleEnvelope80FF5FC0 final {
public:
    static constexpr std::int64_t Maximum=std::int64_t{1}<<30;
    bool KeyOn() noexcept;
    bool KeyOff() noexcept;
    void Advance() noexcept; // one 44100-Hz OUTPUT sample, independent of pitch
    void NaturalEnd() noexcept;
    SelectedEnvelopeSnapshot Snapshot() const noexcept { return state_; }
    // Native boundary policy: height clamps0..Maximum; ties round toward +inf.
    static std::int16_t Scale(std::int16_t sample,std::int64_t height) noexcept;
private:
    SelectedEnvelopeSnapshot state_;
    std::uint32_t startup_=0;
};
enum class SelectedSoundCode { Ok, Unsupported, Malformed, InvalidLease, Limit, NoMemory };
struct SelectedSoundResult {
    SelectedSoundCode code=SelectedSoundCode::Ok;
    std::string detail;
    explicit operator bool() const noexcept {return code==SelectedSoundCode::Ok;}
};
struct SelectedSoundTrace {
    std::int32_t group=-1,entry=-1,volume=0,profile_gain=0,effective_parameter=0,master_left=128,master_right=128;
    std::uint32_t sequence_offset=0,command_bytes=0,events=0,region_index=0,wave_index=0;
    std::uint32_t wave_offset=0,wave_bytes=0,pitch4096=0,combined_volume=0,base_gain=0,dry_left=0,dry_right=0;
    std::uint32_t envelope1=0,envelope2=0,first_end_block=UINT32_MAX;
    std::uint64_t storage_frames=0,input_frames=0,output_frames=0;
    std::int64_t height_before_natural_end=0;
    SelectedEnvelopePhase phase_before_natural_end=SelectedEnvelopePhase::Off;
    bool sequence_ended=false,key_off_at_ff=false,reference_flag_policy=false;
};
std::int32_t Game73EffectiveParameter(std::int32_t volume,std::int32_t profile_gain) noexcept;
// ONLY source-selected group0/entry4 grammar, one region/mono44100 wave,
// linear curve2, centered pan, pitch2048,80FF/5FC0, no wet/loops/fades.
// Half-rate native resampling: even=source[n], odd=floor((a+b)/2), last held.
// Reference flags0/1/4 decode once; exact7 ends BEFORE its block;3/6 refuse.
// FF ends sequence, NOT voice/keyoff. Natural input exhaustion ends PCM.
// Transactional: PCM AND trace unchanged on any failure, every source read leased.
SelectedSoundResult RenderSelectedGame73(const recovered::PackageView& bank,
    std::int32_t group,std::int32_t entry,std::int32_t volume,std::int32_t profile_gain,
    std::uint64_t frame_limit,PcmClip& output,SelectedSoundTrace& trace,
    std::int32_t master_left=128,std::int32_t master_right=128);
} // namespace dokodemo::audio

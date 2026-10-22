#pragma once
#include "audio_pcm.h"
#include "recovered/package_storage_native.h"
#include <string>

namespace dokodemo::audio {
enum class PsAdpcmCode { Ok, InvalidExtent, UnsupportedFormat, InvalidLease, NoMemory };
struct PsAdpcmResult {
    PsAdpcmCode code = PsAdpcmCode::Ok;
    std::uint64_t block = 0;
    std::string detail;
    explicit operator bool() const noexcept { return code == PsAdpcmCode::Ok; }
};
// Storage decoder, not an SGX sequence/voice renderer. Reached mono PS-ADPCM:
// 16-byte blocks, 28 samples, predictors0..4, shifts0..12, flags0/1/4/7.
// Flag7 yields silence; loop/END interpretation is a separate source consumer.
// Every read validates the live PackageView. Output owns samples and is replaced
// only on complete success. Source sample-count/loop fields are not input bounds.
PsAdpcmResult DecodePsAdpcmMono(const recovered::PackageView& bytes,
    std::uint32_t sample_rate, std::uint64_t frame_limit, PcmClip& output);
} // namespace dokodemo::audio

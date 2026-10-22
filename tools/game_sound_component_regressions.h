#pragma once
#include "../src/ps_adpcm.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dokodemo::game_sound_codec_checks_detail {
using audio::PcmClip;
using audio::PsAdpcmCode;
using recovered::PackageStorage;
using recovered::PackageStorageLease;
using recovered::PackageStorageSpan;
using recovered::PackageView;
inline void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
inline bool Same(const PcmClip& a, const PcmClip& b) {
    return a.sample_rate == b.sample_rate && a.channels == b.channels && a.samples == b.samples;
}
// Synthetic storage; faults count acquisitions from Arm(), including Describe().
struct Lease final : PackageStorageLease {
    enum class Fault { None, Expired, Owner, Generation, Serial, Size, Base, Address, Unreadable };
    std::vector<std::uint8_t> bytes, known, relocated;
    bool live = true;
    mutable std::size_t calls = 0;
    std::size_t trip = (std::numeric_limits<std::size_t>::max)();
    Fault fault = Fault::None;
    explicit Lease(std::vector<std::uint8_t> b)
        : bytes(std::move(b)), known(bytes.size(), 1), relocated(bytes) {}
    void Arm(Fault f, std::size_t after) { fault = f; trip = calls + after; }
    bool Span(PackageStorageSpan& s, std::string& error) const override {
        const bool active = ++calls >= trip;
        const auto is = [&](Fault f) { return active && fault == f; };
        if (!live || is(Fault::Expired)) { error = "codec fixture retired"; return false; }
        const auto& payload = is(Fault::Address) ? relocated : bytes;
        s.bytes = const_cast<std::uint8_t*>(payload.data());
        s.initialized = const_cast<std::uint8_t*>(known.data());
        s.size = bytes.size() - (is(Fault::Size) ? 1u : 0u);
        s.host_address = std::uint64_t(reinterpret_cast<std::uintptr_t>(payload.data()));
        s.owner = 101 + (is(Fault::Owner) ? 1u : 0u);
        s.generation = 7 + (is(Fault::Generation) ? 1u : 0u);
        s.serial = 3 + (is(Fault::Serial) ? 1u : 0u);
        s.source_base = 0x1000 + (is(Fault::Base) ? 1u : 0u);
        s.readable = !is(Fault::Unreadable); s.writable = true;
        return true;
    }
};
inline PackageView View(std::vector<std::uint8_t> b) {
    auto lease = std::make_shared<Lease>(std::move(b));
    return PackageStorage::Attach(lease, lease->bytes.size());
}
inline std::vector<std::uint8_t> Blocks(std::size_t n, std::uint8_t header = 0x0C) {
    std::vector<std::uint8_t> b(n * 16, 0);
    for (std::size_t i = 0; i < n; ++i) b[i * 16] = header;
    return b;
}
inline PcmClip Decode(std::vector<std::uint8_t> b, std::uint32_t rate = 44100,
                      std::uint64_t limit = 112) {
    const auto frames = b.size() / 16 * 28;
    PcmClip out;
    const auto r = audio::DecodePsAdpcmMono(View(std::move(b)), rate, limit, out);
    Check(bool(r) && out.sample_rate == rate && out.channels == 1 &&
          out.samples.size() == frames && out.FrameCount() == frames,
          "codec: complete mono PCM and metadata");
    return out;
}
inline void Prefix(const PcmClip& pcm, std::size_t at, std::initializer_list<int> expected,
                   const char* message) {
    Check(at <= pcm.samples.size() && expected.size() <= pcm.samples.size() - at, message);
    for (const int sample : expected) Check(pcm.samples[at++] == sample, message);
}
inline void Refuse(const PackageView& view, PsAdpcmCode code, std::uint64_t block = 0,
                   std::uint32_t rate = 44100, std::uint64_t limit = 56) {
    PcmClip out; out.sample_rate = 12345; out.channels = 2;
    out.samples = {300, -301, 302, -303};
    const PcmClip before = out;
    const auto r = audio::DecodePsAdpcmMono(view, rate, limit, out);
    Check(!r && r.code == code && r.block == block, "codec: typed refusal and failing block");
    Check(Same(out, before), "codec: refusal preserves every caller PCM field");
}

// 1. Literal arithmetic oracles; derivations in handoff.
inline void GoldenSamples() {
    auto b = Blocks(1);
    const std::uint8_t packed[]{0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE};
    std::copy(packed, packed + 8, b.begin() + 2);
    auto pcm = Decode(b);
    Prefix(pcm, 0, {0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1},
           "codec: signed low nibble first, shift12");
    Check(std::all_of(pcm.samples.begin() + 16, pcm.samples.end(), [](auto x){return x == 0;}),
          "codec: remaining payload nibbles consumed");
    b[0] = 0;
    Prefix(Decode(b), 0, {0,4096,8192,12288,16384,20480,24576,28672,
                         -32768,-28672,-24576,-20480,-16384,-12288,-8192,-4096},
           "codec: signed residual scale at shift0");
    const int scale[]{4096,2048,1024,512,256,128,64,32,16,8,4,2,1};
    for (unsigned shift = 0; shift <= 12; ++shift) {
        b = Blocks(1, std::uint8_t(shift)); b[2] = 0xF1;
        Prefix(Decode(b), 0, {scale[shift], -scale[shift], 0}, "codec: every legal shift");
    }
    // -8 impulse, then zero residual. Negative division truncates toward zero.
    const std::int16_t impulse[5][8] = {
        {-8,0,0,0,0,0,0,0}, {-8,-7,-6,-5,-4,-3,-2,-1},
        {-8,-14,-18,-20,-21,-21,-20,-18},
        {-8,-12,-11,-6,0,5,7,6}, {-8,-15,-21,-25,-27,-28,-28,-27}
    };
    for (unsigned predictor = 0; predictor != 5; ++predictor) {
        b = Blocks(1, std::uint8_t((predictor << 4) | 12)); b[2] = 8;
        pcm = Decode(b);
        Check(std::equal(impulse[predictor], impulse[predictor] + 8, pcm.samples.begin()),
              "codec: all predictor golden prefixes and negative rounding");
    }
    b = Blocks(2); b[15] = 0xE1; b[16] = 0x2C;
    pcm = Decode(b);
    Prefix(pcm, 26, {1,-2,-4,-5,-5,-4,-3,-2,-1,0}, "codec: both history values cross block boundary");
    Check(std::all_of(pcm.samples.begin() + 35, pcm.samples.end(), [](auto x){return x == 0;}),
          "codec: cross-block negative prediction settles at zero");
    pcm = Decode(Blocks(1, 0x2C));
    Check(std::all_of(pcm.samples.begin(), pcm.samples.end(), [](auto x){return x == 0;}),
          "codec: independent decode starts with cold history");
}

// 2. PCM clamps; history retains the unsaturated accumulator.
inline void SaturationAndFlags() {
    auto b = Blocks(1, 0x10); b[2] = 0x77;
    auto pcm = Decode(b);
    Prefix(pcm, 0, {28672,32767,32767}, "codec: positive clip with raw feedback");
    Prefix(pcm, 10, {31075}, "codec: positive raw feedback eventually leaves saturation");
    b[2] = 0x88; pcm = Decode(b);
    Prefix(pcm, 0, {-32768,-32768,-32768}, "codec: negative clip with raw feedback");
    Prefix(pcm, 12, {-31213}, "codec: negative raw feedback truncates toward zero");
    b = Blocks(4); b[1] = 1; b[15] = 0x11; b[17] = 4;
    std::fill(b.begin() + 18, b.begin() + 32, std::uint8_t(0x77));
    b[32] = 0x40; b[33] = 7;
    std::fill(b.begin() + 34, b.begin() + 48, std::uint8_t(0x88));
    b[48] = 0x4C; pcm = Decode(b);
    Prefix(pcm, 26, {1,1,7,7}, "codec: flags1/4 do not truncate storage decode");
    Check(std::all_of(pcm.samples.begin() + 28, pcm.samples.begin() + 56, [](auto x){return x == 7;}),
          "codec: flag4 is decoded exactly once");
    Check(std::all_of(pcm.samples.begin() + 56, pcm.samples.end(), [](auto x){return x == 0;}),
          "codec: flag7 silences nonzero payload and clears history before flag0");
}

// 3. Finite domain and transactional late refusals.
inline void GeometryAndFormat() {
    const auto view = View(Blocks(2));
    for (const auto n : {0u,1u,15u,17u,31u})
        Refuse(view.Subview(0, n), PsAdpcmCode::InvalidExtent);
    for (const auto limit : {std::uint64_t(0),std::uint64_t(27),std::uint64_t(55),
                             std::uint64_t((1u << 22) + 1),
                             (std::numeric_limits<std::uint64_t>::max)()})
        Refuse(view, PsAdpcmCode::InvalidExtent, 0, 44100, limit);
    Refuse(view.Subview(0, 16), PsAdpcmCode::InvalidExtent, 0, 44100, 27);
    auto backing = std::make_shared<Lease>(Blocks(3));
    backing->known[0] = backing->known[47] = 0;
    const auto member = PackageStorage::Attach(backing, 48).Subview(16, 16);
    PcmClip clip;
    Check(bool(audio::DecodePsAdpcmMono(member, 44100, 28, clip)) && clip.FrameCount() == 28,
          "codec: member offset excludes unknown neighboring bytes");
    Decode(Blocks(1), 44100, 28); Decode(Blocks(2), 44100, 56);
    Decode(Blocks(1), 44100, 29); Decode(Blocks(1), 44100, 1u << 22);
    Decode(Blocks(1), 8000, 28); Decode(Blocks(1), 192000, 28);
    for (const auto rate : {0u,7999u,192001u,(std::numeric_limits<std::uint32_t>::max)()})
        Refuse(view, PsAdpcmCode::UnsupportedFormat, 0, rate);
    for (unsigned predictor = 5; predictor < 16; ++predictor) {
        auto b = Blocks(2); b[16] = std::uint8_t(predictor << 4);
        Refuse(View(std::move(b)), PsAdpcmCode::UnsupportedFormat, 1);
    }
    for (unsigned shift = 13; shift < 16; ++shift) {
        auto b = Blocks(2); b[16] = std::uint8_t(shift);
        Refuse(View(std::move(b)), PsAdpcmCode::UnsupportedFormat, 1);
    }
    for (unsigned flag = 0; flag < 256; ++flag) {
        if (flag == 0 || flag == 1 || flag == 4 || flag == 7) continue;
        auto b = Blocks(2); b[17] = std::uint8_t(flag);
        Refuse(View(std::move(b)), PsAdpcmCode::UnsupportedFormat, 1);
    }
    auto b = Blocks(1); b[0] = 0x5C; b[1] = 7;
    Refuse(View(b), PsAdpcmCode::UnsupportedFormat);
    b[0] = 0x0D; Refuse(View(b), PsAdpcmCode::UnsupportedFormat);
}

// 4. Live byte knowledge/lease checks, including otherwise ignored silence data.
inline void LeasesAndOwnership() {
    auto b = Blocks(2); b[17] = 7;
    auto lease = std::make_shared<Lease>(b);
    auto view = PackageStorage::Attach(lease, b.size());
    for (std::size_t at = 0; at < b.size(); ++at) {
        lease->known[at] = 0;
        Refuse(view, PsAdpcmCode::InvalidLease, at / 16);
        lease->known[at] = 2;
        Refuse(view, PsAdpcmCode::InvalidLease, at / 16);
        lease->known[at] = 1;
    }
    Refuse(PackageView{}, PsAdpcmCode::InvalidLease);
    using F = Lease::Fault;
    for (const auto fault : {F::Expired,F::Owner,F::Generation,F::Serial,F::Size,
                             F::Base,F::Address,F::Unreadable}) {
        // Describe, first byte, interior byte, block1 first/last byte.
        for (const auto after : {1u,2u,10u,18u,33u}) {
            lease = std::make_shared<Lease>(b);
            view = PackageStorage::Attach(lease, b.size()); lease->Arm(fault, after);
            Refuse(view, PsAdpcmCode::InvalidLease, after >= 18 ? 1 : 0);
        }
    }
    b = Blocks(1); b[2] = 0xF1;
    lease = std::make_shared<Lease>(b); view = PackageStorage::Attach(lease, b.size());
    PcmClip owned;
    Check(bool(audio::DecodePsAdpcmMono(view, 22050, 28, owned)), "codec: owned PCM seed");
    const PcmClip saved = owned;
    Prefix(saved, 0, {1,-1,0}, "codec: owned PCM literal seed");
    view.WriteU8(2, 0x77);
    Check(Same(owned, saved), "codec: source write cannot mutate old owned PCM");
    PcmClip changed;
    Check(bool(audio::DecodePsAdpcmMono(view, 22050, 28, changed)), "codec: source mutation is actually visible");
    Prefix(changed, 0, {7,7,0}, "codec: new decode sees mutated bytes");
    Check(Same(owned, saved), "codec: subsequent decode cannot mutate prior PCM");
    lease->known[15] = 0;
    auto r = audio::DecodePsAdpcmMono(view, 22050, 28, owned);
    Check(!r && r.code == PsAdpcmCode::InvalidLease && Same(owned, saved),
          "codec: source knowledge loss preserves prior successful PCM");
    lease->live = false;
    r = audio::DecodePsAdpcmMono(view, 22050, 28, owned);
    Check(!r && r.code == PsAdpcmCode::InvalidLease && Same(owned, saved),
          "codec: source retirement preserves prior successful PCM");
}
} // namespace dokodemo::game_sound_codec_checks_detail

namespace dokodemo {
inline void GameSoundCodecRegressionChecks() {
    game_sound_codec_checks_detail::GoldenSamples();
    game_sound_codec_checks_detail::SaturationAndFlags();
    game_sound_codec_checks_detail::GeometryAndFormat();
    game_sound_codec_checks_detail::LeasesAndOwnership();
}
} // namespace dokodemo

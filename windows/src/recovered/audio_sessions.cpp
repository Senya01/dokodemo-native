#include "audio_sessions.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dokodemo::audio {
namespace {
uint16_t U16(const uint8_t* data) { return uint16_t(data[0] | (uint16_t(data[1]) << 8)); }
uint32_t U32(const uint8_t* data) { return uint32_t(U16(data)) | (uint32_t(U16(data + 2)) << 16); }
bool Is(const uint8_t* data, const char* tag) { return std::memcmp(data, tag, 4) == 0; }
struct Riff {
    AtracFormat format{};
    size_t data = 0, size = 0;
    unsigned delay = 0, frames = 0;
};
bool ParseRiff(const std::vector<uint8_t>& bytes, Riff& riff) {
    if (bytes.size() < 12 || !Is(bytes.data(),"RIFF") || !Is(bytes.data()+8,"WAVE") ||
        U32(bytes.data()+4) != bytes.size()-8) return false;
    bool has_fact = false;
    for (size_t pos = 12; pos < bytes.size();) {
        if (bytes.size()-pos < 8) return false;
        const auto* tag = bytes.data()+pos;
        const uint32_t n = U32(tag+4);
        pos += 8;
        if (n > bytes.size()-pos) return false;
        const auto* data = bytes.data()+pos;
        if (Is(tag,"fmt ")) {
            if (riff.format.block_size || n < 18 || U16(data+2) != 2 || U32(data+4) != 44100 ||
                U16(data+16) != n-18 || !U16(data+12)) return false;
            size_t extra = 18;
            if (U16(data) == 0x270) {
                if (n != 32) return false;
                riff.format.codec = AtracCodec::Atrac3;
            } else {
                constexpr uint8_t plus_guid[]{0xBF,0xAA,0x23,0xE9,0x58,0xCB,0x71,0x44,
                                             0xA1,0x19,0xFF,0xFA,0x01,0xE4,0xCE,0x62};
                if (U16(data) != 0xFFFE || n != 52 || std::memcmp(data+24,plus_guid,16)) return false;
                riff.format.codec = AtracCodec::Plus;
                extra = 40;
            }
            riff.format.extra.assign(data+extra,data+n);
            riff.format.sample_rate = U32(data+4);
            riff.format.block_size = U16(data+12);
        } else if (Is(tag,"fact")) {
            if (has_fact || n != 8) return false;
            has_fact = true;
            riff.frames = U32(data);
            riff.delay = U32(data+4);
        } else if (Is(tag,"data")) {
            if (riff.data || !n) return false;
            riff.data = pos;
            riff.size = n;
        }
        pos += n;
        if (n & 1) {
            if (pos == bytes.size()) return false;
            ++pos;
        }
    }
    if (!has_fact || !riff.frames || !riff.data || !riff.format.block_size ||
        riff.size % riff.format.block_size) return false;
    const uint64_t samples_per_block = riff.format.codec == AtracCodec::Plus ? 2048 : 1024;
    return uint64_t(riff.delay)+riff.frames <= (riff.size/riff.format.block_size)*samples_per_block;
}
}
NativeAtracSessions::NativeAtracSessions(AtracDecoderFactory& factory) {
    // 11A0..121C attempts two 1000 and two 1001 records in order. This
    // substitution covers the public ABI of a fully prepared pool.
    for (size_t i = 0; i < slots_.size(); ++i) {
        slots_[i].decoder = factory.Create(i < 2 ? AtracCodec::Plus : AtracCodec::Atrac3);
        if (!slots_[i].decoder) throw std::runtime_error("Could not allocate all four native ATRAC slots");
    }
}
int32_t NativeAtracSessions::Acquire(uint32_t codec) noexcept {
    if (codec != uint32_t(AtracCodec::Plus) && codec != uint32_t(AtracCodec::Atrac3))
        return int32_t(0x80630004u);
    const uint32_t first = codec == uint32_t(AtracCodec::Plus) ? 0 : 2;
    for (uint32_t id = first; id < first + 2; ++id) {
        if (!slots_[id].claimed) {
            slots_[id].claimed = true;
            return static_cast<int32_t>(id);
        }
    }
    return int32_t(0x80630003u);
}
int32_t NativeAtracSessions::Release(uint32_t id) noexcept {
    if (id >= slots_.size() || !slots_[id].claimed) return int32_t(0x80630005u);
    slots_[id].decoder->Reset();
    std::vector<uint8_t>().swap(slots_[id].riff);
    slots_[id].claimed = false;
    return 0;
}
bool NativeAtracSessions::DecodePlusPacket(uint32_t id, const uint8_t* data, size_t size, unsigned sample_rate) {
    return id < slots_.size() && slots_[id].claimed && slots_[id].riff.empty() &&
           slots_[id].decoder->DecodePlusPacket(data, size, sample_rate);
}
bool NativeAtracSessions::OpenRiff(uint32_t id, std::vector<uint8_t> bytes) {
    if (id >= slots_.size() || !slots_[id].claimed) return false;
    auto& slot = slots_[id];
    Riff riff;
    if (!ParseRiff(bytes,riff) || !slot.decoder->Configure(riff.format)) return false;
    slot.riff = std::move(bytes);
    slot.next = riff.data; slot.limit = riff.data+riff.size;
    slot.block = riff.format.block_size; slot.skip = riff.delay;
    slot.remaining = riff.frames; slot.failed = false;
    return true;
}
bool NativeAtracSessions::Decode(uint32_t id, AtracPcmFrame& out) {
    out = {};
    if (id >= slots_.size() || !slots_[id].claimed) return false;
    auto& slot = slots_[id];
    if (slot.riff.empty() || slot.failed) return false;
    while (slot.remaining) {
        AtracPcmFrame packet;
        // A decoder may consume the compressed block before PCM allocation
        // fails. Retrying it on the same decoder would advance history twice.
        slot.failed = true;
        if (slot.limit-slot.next < slot.block ||
            !slot.decoder->DecodePacket(slot.riff.data()+slot.next,slot.block,packet) ||
            packet.channels != 2 || packet.samples.empty() || packet.samples.size()%2) {
            return false;
        }
        slot.next += slot.block;
        slot.failed = false;
        const auto count = static_cast<unsigned>(packet.samples.size()/2);
        const auto skip = std::min(slot.skip,count);
        slot.skip -= skip;
        const auto take = std::min(count-skip,slot.remaining);
        if (!take) continue;
        packet.samples.erase(packet.samples.begin(),packet.samples.begin()+skip*2);
        packet.samples.resize(take*2);
        slot.remaining -= take;
        packet.end = slot.remaining == 0;
        out = std::move(packet);
        return true;
    }
    out.sample_rate = 44100; out.channels = 2; out.end = true;
    return true;
}
}

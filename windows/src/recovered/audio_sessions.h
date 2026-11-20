#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dokodemo::audio {
enum class AtracCodec : uint32_t { Plus = 0x1000, Atrac3 = 0x1001 };
struct AtracPcmFrame {
    std::vector<int16_t> samples;
    unsigned sample_rate = 0, channels = 0;
    bool end = false;
};
struct AtracFormat {
    AtracCodec codec;
    unsigned sample_rate = 0, block_size = 0;
    std::vector<uint8_t> extra;
};

class AtracDecoder {
public:
    virtual ~AtracDecoder() = default;
    virtual void Reset() noexcept = 0;
    virtual bool Configure(const AtracFormat&) = 0;
    virtual bool DecodePacket(const uint8_t*, size_t, AtracPcmFrame&) = 0;
};
class AtracDecoderFactory {
public:
    virtual ~AtracDecoderFactory() = default;
    virtual std::unique_ptr<AtracDecoder> Create(AtracCodec) = 0;
};

struct NativeCodecMemory {
    unsigned input_bytes, pcm_samples;
};

// The host Media Engine replacement owns its input workspace and decoder.
// Required storage describes the native formats passed by the actual caller.
class NativeMediaCodec {
public:
    NativeMediaCodec(AtracCodec codec,std::unique_ptr<AtracDecoder> decoder);
    static std::optional<NativeCodecMemory> CheckNeedMem(const AtracFormat&) noexcept;
    bool Init(const AtracFormat&);
    bool DecodePacket(const uint8_t* data,size_t size,AtracPcmFrame& out);
    void Release() noexcept;
    bool configured() const { return !input_.empty(); }
    bool ready() const { return configured() && !failed_; }
    AtracCodec codec() const { return codec_; }
    unsigned sample_rate() const { return rate_; }
private:
    AtracCodec codec_;
    std::unique_ptr<AtracDecoder> decoder_;
    std::vector<uint8_t> input_;
    unsigned rate_=0, pcm_samples_=0;
    bool failed_=false;
};

// Public sceAtracGetAtracID / sceAtracReleaseAtracID boundary in the game's
// 19524-byte module (VA 0..0x174). The caller serializes access.
class NativeAtracSessions {
public:
    explicit NativeAtracSessions(AtracDecoderFactory&);
    int32_t Acquire(uint32_t codec) noexcept;
    int32_t Release(uint32_t id) noexcept;
    bool DecodePlusPacket(uint32_t id, const uint8_t* data, size_t size, unsigned sample_rate);
    // Full resident RIFF input, consumed one compressed block at a time.
    bool OpenRiff(uint32_t id, std::vector<uint8_t> riff);
    bool Decode(uint32_t id, AtracPcmFrame&);
private:
    struct Slot {
        std::unique_ptr<NativeMediaCodec> workspace;
        bool claimed = false;
        std::vector<uint8_t> riff;
        size_t next = 0, limit = 0;
        unsigned block = 0, skip = 0, remaining = 0;
    };
    std::array<Slot, 4> slots_;
};
}

#pragma once
#include <cstdint>
#include <vector>

namespace dokodemo::audio {
// Owned portable PCM. No output device, borrowed storage or source mixer state.
struct PcmClip {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::vector<std::int16_t> samples;
    std::uint64_t FrameCount() const noexcept {
        return channels ? samples.size() / channels : 0;
    }
    double DurationSeconds() const noexcept {
        return sample_rate ? static_cast<double>(FrameCount()) / sample_rate : 0.0;
    }
};
} // namespace dokodemo::audio

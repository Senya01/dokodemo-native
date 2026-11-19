#pragma once

#include <cstdint>
#include <stdexcept>

namespace dokodemo::gameplay {

// Scalar light track 0x60/0x0b in BG_ROOM/TV_ON.DAK. Input time is in DAK
// units. Each sample seeds its own VFPU random lane with its key index (87BA4);
// this operation does not advance the gameplay RNG.
inline float SampleRoomLightNoise(uint32_t time, uint32_t flags, float spacing, float amplitude) {
    // 7FC48 installs 32767 procedural keys. The scene's 113/2/.1 track
    // spans 96000 units; its successive key times are 0,160,320,....
    if (spacing != 2.f || time > 96000)
        throw std::runtime_error("Unsupported room television noise interval");
    const uint32_t units = static_cast<uint32_t>(80.f * spacing);
    const uint32_t key = time / units;
    const auto sample = [flags, amplitude](uint32_t seed) {
        // VRNDS.S followed by one VRNDF2.S. The seed initializes each of
        // the four reconstructed 32-bit RCX words and the carry to seed.
        const uint32_t linear = seed * 69069u + 1u;
        uint32_t shift = seed;
        shift ^= shift << 13;
        shift ^= shift >> 17;
        shift ^= shift << 5;
        const uint32_t mantissa = (linear + shift + 4u * seed) & 0x007fffffu;
        const float bipolar = float(mantissa) * (1.f / 4194304.f) - 1.f;
        return bipolar * amplitude + ((flags & 0x70u) ? amplitude : 0.f);
    };
    const float start = sample(key);
    if (time % units == 0)
        return start;
    // 88254 gives the incoming and outgoing tangent the same central
    // difference. 88E0C interpolates them between the two actual samples.
    const float end = sample(key + 1);
    const float tangent0 = (end - sample(key - 1)) * .5f;
    const float tangent1 = (sample(key + 2) - start) * .5f;
    const float t = float(time % units) / float(units);
    return (((start * 2 - end * 2) + tangent0 + tangent1) * (t*t*t))
           + ((-3*start + 3*end - 2*tangent0 - tangent1) * (t*t))
           + tangent0*t + start;
}

} // namespace dokodemo::gameplay

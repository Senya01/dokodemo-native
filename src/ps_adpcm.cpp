#include "ps_adpcm.h"
#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace dokodemo::audio {
namespace {
// Explicit floor shift avoids implementation-defined signed right shift.
std::int64_t FloorShift(std::int64_t value, unsigned shift) noexcept {
    const std::int64_t divisor = std::int64_t{1} << shift;
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}
}
PsAdpcmResult DecodePsAdpcmMono(const recovered::PackageView& bytes,
    std::uint32_t sample_rate, std::uint64_t frame_limit, PcmClip& output) {
    std::uint64_t block = 0;
    try {
        const auto size = bytes.Describe().size;
        constexpr std::uint64_t maximum_frames = 1u << 22;
        if (!size || size % 16 || !frame_limit || frame_limit > maximum_frames
            || size / 16 > frame_limit / 28)
            return {PsAdpcmCode::InvalidExtent, 0, "PS-ADPCM aligned extent/frame limit"};
        if (sample_rate < 8000 || sample_rate > 192000)
            return {PsAdpcmCode::UnsupportedFormat, 0, "PS-ADPCM finite sample-rate domain"};
        const auto count = size / 16 * 28;
        PcmClip next; next.sample_rate = sample_rate; next.channels = 1;
        next.samples.reserve(static_cast<std::size_t>(count));
        constexpr int coefficients[5][2] = {{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
        std::int64_t previous = 0, older = 0;
        for (; block != size / 16; ++block) {
            std::array<std::uint8_t,16> data{};
            for (std::size_t i = 0; i != data.size(); ++i) data[i] = bytes.ReadU8(block * 16 + i);
            const unsigned predictor = data[0] >> 4, shift = data[0] & 15, flags = data[1];
            if (predictor >= 5 || shift > 12 || (flags != 0 && flags != 1 && flags != 4 && flags != 7))
                return {PsAdpcmCode::UnsupportedFormat, block, "PS-ADPCM predictor/shift/flags outside reached storage domain"};
            for (unsigned n = 0; n != 28; ++n) {
                int nibble = (data[2 + n / 2] >> (4 * (n % 2))) & 15;
                if (nibble >= 8) nibble -= 16;
                // Division truncates toward zero, as the reference codec does.
                const auto prediction = (previous * coefficients[predictor][0] + older * coefficients[predictor][1]) / 64;
                const auto value = flags == 7 ? 0 : FloorShift(std::int64_t(nibble) * 4096, shift) + prediction;
                if (value < (std::numeric_limits<std::int32_t>::min)() || value > (std::numeric_limits<std::int32_t>::max)())
                    return {PsAdpcmCode::UnsupportedFormat, block, "PS-ADPCM prediction overflow"};
                next.samples.push_back(static_cast<std::int16_t>(std::clamp<std::int64_t>(value,-32768,32767)));
                older = previous; previous = value;
            }
        }
        output = std::move(next);
        return {};
    } catch (const recovered::PackageStorageError& error) {
        return {PsAdpcmCode::InvalidLease, block, error.what()};
    } catch (const std::bad_alloc&) {
        return {PsAdpcmCode::NoMemory, block, "PS-ADPCM PCM allocation failed"};
    }
}
} // namespace dokodemo::audio

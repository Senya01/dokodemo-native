#include "room_word_reading.h"
#include "recovered/service39_native.h"

namespace dokodemo::gameplay {
namespace {
recovered::Service39Normalized Normalize(std::string_view source, bool filter) {
    std::vector<std::uint8_t> bytes(source.begin(),source.end());
    bytes.push_back(0);
    return recovered::Service39Normalize(bytes,filter);
}
}

bool FinishWordReading(std::string_view written, std::string& reading,
                       const RegisterWordReading& register_word) {
    // 20104: empty written text goes straight to the closing animation.
    if(written.empty()) return true;
    auto normalized=Normalize(written,false);
    if(!normalized.all_converted) {
        const auto key=reading.empty()?written:std::string_view(reading);
        if(!register_word || !register_word(key,written)) return false;
        // 20268 filters the original secondary buffer, even when the primary
        // buffer was used in its place for the NJ registration argument.
        normalized=Normalize(reading,true);
    }
    reading.assign(normalized.bytes.begin(),normalized.bytes.end()-1);
    return true;
}
} // namespace dokodemo::gameplay

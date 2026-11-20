#include "gameplay/room_learned_word.h"
#include "recovered/service39_native.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dokodemo::gameplay {

unsigned ScriptCharacterCount(std::string_view text) {
    unsigned count = 0;
    for (std::size_t pos = 0; pos < text.size(); ++count) {
        const auto lead = static_cast<std::uint8_t>(text[pos++]);
        if (!lead) throw std::invalid_argument("Embedded NUL in taught word");
        // 7C000 indexes bit 4 of the E07CC byte-width table.
        if ((lead >= 0x81 && lead <= 0x9f) || (lead >= 0xe0 && lead <= 0xfe)) {
            if (pos == text.size() || text[pos] == 0)
                throw std::invalid_argument("Incomplete CP932 character in taught word");
            ++pos;
        }
    }
    return count;
}

namespace {

// EBCE8's 83 kana pointers, selected by a second byte in [9F,F1].
unsigned IndexKana(unsigned trail) {
    if (trail <= 0xa8) return (trail + 1) & ~1u;
    switch (trail) {
    case 0xc1: return 0;
    case 0xe1: case 0xe3: case 0xe5: case 0xec: return trail + 1;
    case 0xee: return 0xa2;
    case 0xef: return 0xa6;
    case 0xf0: return 0xa8;
    default: return trail;
    }
}

struct ReadingRange {
    unsigned first=0, last=0;
    bool all_kana=true;
};

ReadingRange ReadKana(std::string_view reading) {
    ReadingRange range;
    for (std::size_t at=0;at<reading.size();at+=2) {
        const auto lead=static_cast<std::uint8_t>(reading[at]);
        const auto trail=at+1<reading.size()?static_cast<std::uint8_t>(reading[at+1]):0u;
        if (lead==0x81 && trail==0x5b) continue;
        if (lead!=0x82 || trail<0x9f || trail>0xf1) {
            range.all_kana=false;
            break;
        }
        const auto kana=IndexKana(trail);
        if (kana) {
            if (!range.first) range.first=kana;
            range.last=kana;
        }
    }
    return range;
}

std::vector<std::uint8_t> Normalize(std::string_view text) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    return recovered::Service39Normalize(bytes).bytes;
}

} // namespace

std::optional<std::array<unsigned,2>> ShiritoriKana(std::string_view reading) {
    const auto range=ReadKana(reading);
    if (!range.all_kana || !range.first || reading.size()>30 || range.last==0xf1) return {};
    return std::array<unsigned,2>{range.first,range.last};
}

LearnedWordFields RefreshWordReading(unsigned flags, std::string_view written,
                                     std::optional<std::string_view> reading) {
    LearnedWordFields fields{flags, ScriptCharacterCount(written), std::nullopt};
    if (!reading) return fields;

    const auto [first,last,all_kana]=ReadKana(*reading);

    // 71 stops at the first mapped character even if 70 rejects a later one.
    // 0D takes its first two bytes little-endian, including the static NIL text.
    fields.reading_index = first ? (first << 8) | 0x82u : 0x7383u;
    if (all_kana && first && reading->size() <= 30) {
        fields.flags |= 0x1000;
        if (last != 0xf1) fields.flags |= 0x4000;
    } else fields.flags &= ~0x5000u;
    return fields;
}

std::optional<LearnedWordFields> FinalizeLearnedWord(unsigned current_flags,
                                                     std::string_view written,
                                                     std::optional<std::string_view> reading) {
    if (current_flags != 1) return std::nullopt;
    auto fields = RefreshWordReading(0x24, written, reading);
    if ((fields.flags & 0x1000) && Normalize(*reading) == Normalize(written))
        fields.flags |= 0x2000;
    return fields;
}

} // namespace dokodemo::gameplay

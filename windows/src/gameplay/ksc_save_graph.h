#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dokodemo::gameplay {

// Value consumed by BOOT 7C93C. Strings own their original encoded bytes.
// Tag B has one child, the object reached at consumption. A save copies it
// each time it is referenced. Array headers retain their other original bits.
struct KscSaveValue {
    std::uint32_t word = 0;
    std::string text;
    std::vector<KscSaveValue> children;
};

// Only the VM stream after the profile block, not a save-slot file.
// On failure throw and publish no output. Decode accepts the producing
// game's packed layout and returns the used extent before any slot padding.
std::vector<std::uint8_t> EncodeKscSaveGraph(const KscSaveValue& root);
KscSaveValue DecodeKscSaveGraph(const std::uint8_t* bytes, std::size_t length, std::size_t& used);

} // namespace dokodemo::gameplay

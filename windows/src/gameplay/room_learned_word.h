#pragma once

#include <optional>
#include <string_view>

namespace dokodemo::gameplay {

unsigned ScriptCharacterCount(std::string_view text);

struct LearnedWordFields {
    unsigned flags;
    unsigned character_count;
    std::optional<unsigned> reading_index; // NIL reading leaves field 18 unchanged.
};

// NEKO 1527/1488 refresh the count, kana index and flags on an existing row.
LearnedWordFields RefreshWordReading(unsigned flags, std::string_view written,
                                     std::optional<std::string_view> reading);

// NEKO 8B6C calls 154B(row, 32) for a newly taught word. Flags here are the
// integer payload from field 0; the caller owns the row and its KSC type tag.
std::optional<LearnedWordFields> FinalizeLearnedWord(unsigned current_flags,
                                                     std::string_view written,
                                                     std::optional<std::string_view> reading);

} // namespace dokodemo::gameplay

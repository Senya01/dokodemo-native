#pragma once

#include "room_word_history.h"
#include <string>
#include <string_view>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace dokodemo::gameplay {

class RoomWordConversion {
public:
    using Candidate=RoomNjCandidate;
    bool Load(const std::filesystem::path& directory,std::string& error);
    std::optional<std::vector<Candidate>> Lookup(std::string_view composition) const;
    bool FinishWord(std::string_view written,std::string& reading);
    std::optional<bool> CommitCandidate(const Candidate& candidate,std::string& written,
                                        std::string& reading,std::string& composition,unsigned max_units);
    // BOOT 20D3C maps the editor's native CP932 reading.
    static std::string Reading(std::string_view cp932);

    // BOOT 20850(1) commits unconverted input. The caller owns the cursor
    // and candidate-list reset (202B8), as well as the separate close action.
    static bool CommitRaw(std::string& written, std::string& reading,
                          std::string& composition, unsigned max_units);
private:
    RoomWordHistory history_;
    std::set<unsigned> cold_keys_;
    struct Group {
        std::vector<Candidate> predictions, forms;
        RoomNjSuffix suffix;
    };
    std::map<std::string,Group,std::less<>> dictionary_;
};

} // namespace dokodemo::gameplay

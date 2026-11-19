#pragma once

#include <string>
#include <vector>

namespace dokodemo::recovered { class PackageView; }

namespace dokodemo::gameplay {
struct WordCandidateCell {
    int page, x, row, width;
};

std::vector<WordCandidateCell> MeasureWordCandidates(const std::vector<std::string>& candidates,
                                                     const recovered::PackageView& font);
}

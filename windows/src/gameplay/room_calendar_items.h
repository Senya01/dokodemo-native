#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace dokodemo::gameplay {

// G57A6 is rebuilt from the room calendar before 3B9D. Its ten cells
// retain KSC tags, including the distinct representations of zero and nil.
class RoomCalendarItems {
public:
    using Row = std::array<std::uint32_t, 10>;
    using Rows = std::array<Row, 20>;
    bool Load(const std::filesystem::path& directory, std::string& error);
    unsigned Update(std::uint32_t selection_bcd, std::uint32_t today_bcd,
                    std::uint32_t birthday_bcd);
    Rows& rows() { return rows_; }
    const Rows& rows() const { return rows_; }

private:
    struct Rule {
        std::uint32_t item, first, last, property, spot, layout;
    };
    std::array<Rule,29> rules_{};
    Rows rows_{};
    bool loaded_ = false;
};

} // namespace dokodemo::gameplay

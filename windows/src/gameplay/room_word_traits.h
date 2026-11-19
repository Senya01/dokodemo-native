#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace dokodemo::gameplay {

class RoomWordTraits {
public:
    bool Load(const std::filesystem::path& post_profile, std::string& error);
    // A9C5 admits original indices in order; the caller owns the RNG draw.
    static std::vector<unsigned> Unasked(int row, int field12, int field15);
    // A942 handles an exhausted trait set, remapping one draw from [0,22).
    static unsigned Repeated(int row, int field12, unsigned draw);
    static bool IsTextTable(unsigned table);
    const std::string& Label(unsigned table, unsigned index) const;

private:
    std::array<std::array<std::string,22>,3> forms_{};
};

} // namespace dokodemo::gameplay

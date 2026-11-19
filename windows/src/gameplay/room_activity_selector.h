#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::recovered { class RngNativeOwner; }
namespace dokodemo::gameplay {

struct RoomActivityInputs {
    bool first_after_cleanup; // ctx28[3DF1] passed by 1355C
    int scene;                // 2192
    int actor_field58;        // GAME24
    int actor_field50;        // GAME23
    int period;               // GAME36, current civil minute
    int mood;                 // GAME22, actor +54
    bool game_flag28;         // GAME28, native state word 2A97C
};

struct RoomActivitySelection {
    std::vector<std::uint16_t> rows;
    std::optional<unsigned> selected_row;
    std::optional<unsigned> command;
    int effective_field58 = 0;
};

class RoomActivitySelector {
  public:
    bool Load(const std::filesystem::path& post_profile_directory, std::string& error);
    RoomActivitySelection Select(const RoomActivityInputs& input, recovered::RngNativeOwner& rng) const;

  private:
    std::vector<std::array<std::uint16_t, 7>> rows_;
    unsigned capacity_ = 0;
};
}

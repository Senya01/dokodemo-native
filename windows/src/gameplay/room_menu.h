#pragma once
#include "cb2a_scene.h"
#include <array>
#include <optional>

namespace dokodemo::gameplay {
class RoomMenu {
public:
    bool Load(const std::filesystem::path& post_profile, std::string& error);
    // 236C4 sets the context from the manager's current scene (14 != 0).
    void Open(std::uint32_t buttons, bool away=false, bool other_profiles=false, bool media_mounted=true);
    std::optional<int> Step(std::uint32_t buttons);
    bool Draw(std::vector<std::uint8_t>& vram, std::size_t offset) const;
    bool active() const { return phase_ != Phase::closed; }
    int cursor() const { return row_; }
    int column() const { return column_; }
    int notebook_column() const { return active() && sub_ == 1 ? column_ : -1; }
    int friends_column() const { return active() && sub_ == 2 ? column_ : -1; }
    bool friends_ready() const { return phase_ == Phase::input && sub_ == 2 && frame_ == 21; }
    unsigned boundary_pc() const { return boundary_; }
    const std::vector<unsigned>& effects() const { return effects_; }
private:
    enum class Phase { closed, opening, input, closing };
    Phase phase_=Phase::closed;
    Cb2aScene scene_;
    bool away_=false, others_=false, mounted_=false;
    unsigned previous_=0, clip_=0, frame_=0, idle_=0, boundary_=0;
    int row_=0, column_=0, sub_=0, result_=-1;
    std::array<unsigned,2> holds_{};
    std::array<int,7> highlights_{};
    std::vector<unsigned> effects_;
    void Close(int result);
};
}

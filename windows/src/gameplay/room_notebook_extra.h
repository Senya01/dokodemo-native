#pragma once
#include "cb2a_scene.h"
#include "profile_adoption.h"
#include <array>
#include <string_view>

namespace dokodemo::recovered { class PackageView; class RngNativeOwner; }
namespace dokodemo::gameplay {
// Home profile before a friend exchange. The player's own card is independent
// of the contact rows used by the four sort tabs.
class RoomFriendNotebook {
public:
    bool Load(const std::filesystem::path& dir, std::string& error);
    void Open(std::uint32_t buttons);
    void Step(std::uint32_t buttons, bool fade_complete, recovered::RngNativeOwner* rng);
    bool Draw(const AcceptedProfile& profile, std::string_view saying,
              const recovered::PackageView& font, std::vector<std::uint8_t>& vram,
              std::size_t offset) const;
    bool ready() const { return phase_==Phase::list || detail(); }
    bool closing() const { return phase_==Phase::closing; }
    bool detail() const { return phase_==Phase::detail; }
    unsigned tab() const { return tab_; }
    std::vector<unsigned> TakeEffects();
private:
    enum class Phase { opening,list,detail,closing };
    Phase phase_=Phase::opening;
    Cb2aScene scene_;
    unsigned tab_=0,stamp_=0;
    std::uint32_t previous_=0;
    std::array<unsigned,2> held_{};
    std::vector<unsigned> effects_;
};
}

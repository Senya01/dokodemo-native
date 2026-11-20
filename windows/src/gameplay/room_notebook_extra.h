#pragma once
#include "cb2a_scene.h"
#include "profile_adoption.h"
#include <array>
#include <string_view>

namespace dokodemo::recovered { class PackageView; class RngNativeOwner; }
namespace dokodemo::gameplay {
class RoomVocabulary;

// The player's own card remains independent of the saved contact rows.
class RoomFriendNotebook {
public:
    bool Load(const std::filesystem::path& dir, std::string& error);
    void Open(std::uint32_t buttons,const RoomVocabulary* vocabulary=nullptr);
    void Step(std::uint32_t buttons, bool fade_complete, recovered::RngNativeOwner* rng);
    bool Draw(const AcceptedProfile& profile, std::string_view saying,
              const recovered::PackageView& font, std::vector<std::uint8_t>& vram,
              std::size_t offset) const;
    bool ready() const { return phase_==Phase::list || detail(); }
    bool closing() const { return phase_==Phase::closing; }
    bool detail() const { return phase_==Phase::detail; }
    unsigned tab() const { return tab_; }
    unsigned count() const { return unsigned(contacts_.size()); }
    unsigned selection() const { return selected_; }
    int selected_row() const { return contacts_.empty() ? -1 : int(contacts_.at(selected_)); }
    std::vector<unsigned> TakeEffects();
private:
    enum class Phase { opening,list,detail,closing };
    Phase phase_=Phase::opening;
    Cb2aScene scene_;
    unsigned tab_=0,stamp_=0,selected_=0;
    bool own_card_=true;
    const RoomVocabulary* vocabulary_=nullptr;
    std::vector<unsigned> contacts_;
    void SortContacts();
    std::uint32_t previous_=0;
    std::array<unsigned,2> held_{};
    std::vector<unsigned> effects_;
};
}

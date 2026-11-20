#pragma once
#include "cb2a_scene.h"

namespace dokodemo::gameplay {

// GAME42 selects the title control on SYOUKAI; the Circle navigation prompt
// is a different control and can be shown or hidden independently.
class RoomFriendsBanner {
public:
    bool Load(const std::filesystem::path& room_talk_dir, std::string& error);
    void Set(bool show);
    void SetNavigation(bool show);
    void Step();
    bool Draw(std::vector<std::uint8_t>& vram, std::size_t offset) const;
private:
    Cb2aScene scene_;
    unsigned frame_=0;
    unsigned navigation_frame_=0;
};
}

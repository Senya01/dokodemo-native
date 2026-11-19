#pragma once
#include "cb2a_scene.h"
#include <array>

namespace dokodemo::gameplay {
struct RoomSpeechPlacement {
    float center_x=0,center_y=0,tail_x=0;
};
struct RoomTextLine {
    std::string cp932;
    float y=0;
    float x=0;
};
using DrawRoomLine=std::function<void(const RoomTextLine&,int,int,int,int)>;

class RoomTalkRenderer {
public:
    bool Load(const std::filesystem::path& game_directory,std::string& error);
    bool DrawPrompt(std::vector<std::uint8_t>& vram,std::size_t offset,unsigned frame=8) const;
    // B1F0 / B380 project the actor's scene and speech positions using the
    // current room camera. Coordinates passed to DrawMessage are screen space.
    static bool Project(const std::array<float,16>& camera,float fov,
                        const std::array<float,3>& body,const std::array<float,3>& speaker,
                        RoomSpeechPlacement& placement,unsigned rows=4);
    bool DrawMessage(const RoomSpeechPlacement&,float appearance,const std::vector<RoomTextLine>&,
                     std::vector<std::uint8_t>& vram,std::size_t offset,const DrawRoomLine& draw,unsigned columns=9,unsigned rows=4) const;
private:
    Cb2aScene prompt_,dialog_;
};
}

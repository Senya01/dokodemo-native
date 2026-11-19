#include "room_friends_banner.h"

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia media{
    "SYOUKAI.CB2A",1516,"c3c396f03d500d3628f1e7265d8a9b4878ad3344fb32bea45d85ebf1867eb240",
    "syoukai-textures.bin",95004,"1ac2027a4583ffbb4f4905e315529a6389d7600cb8095cb40613cdc95a9f3d16"};
}

bool RoomFriendsBanner::Load(const std::filesystem::path& room_talk_dir,std::string& error) {
    return scene_.Load(room_talk_dir,media,error);
}

void RoomFriendsBanner::Set(bool show) {
    // 1C85C: GAME42(1) -> 3E8BC(control 1), playing clip 2 from frame 1.
    // Other values -> 3E978(control 1), sampling transparent frame 1.
    frame_=show?1u:0u;
}

void RoomFriendsBanner::Step() {
    if (frame_ && frame_<9) ++frame_;
}

bool RoomFriendsBanner::Draw(std::vector<std::uint8_t>& vram,std::size_t offset) const {
    return !frame_ || scene_.Draw(2,{{2,int(frame_)}},vram,offset);
}
}
